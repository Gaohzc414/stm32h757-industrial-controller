/**
 * @file bsp_esp8266_app.c
 * @brief ESP8266 启动流程：WiFi STA + OTA TCP 客户端
 */
#include "bsp_esp8266_app.h"
#include "bsp_esp8266_at.h"
#include "bsp_esp8266_conf.h"
#include "bsp_esp8266_tcp.h"
#include "bsp_esp8266_wifi.h"
#include "bsp_log.h"
#include "bsp_ota_app.h"
#include "bsp_watchdog.h"

#include <stdio.h>
#include <string.h>

#include "cmsis_os.h"

#define ESP8266_APP_TAG   "ESP8266"

static volatile bsp_esp8266_state_t s_state = BSP_ESP8266_STATE_OFF;
static char s_ip[20];
static int s_rssi;
static uint8_t s_link_fail_count;
static uint32_t s_tcp_up_tick;

static uint8_t esp8266_try_connect(void);
static uint8_t esp8266_try_tcp(void);

/**
 * @brief  上电后清理可能残留的旧 TCP 会话
 * @retval 无
 */
static void esp8266_startup_cleanup(void)
{
  LOG_I(ESP8266_APP_TAG, "startup cleanup: close stale TCP if any");
  (void)bsp_esp8266_tcp_close();
  (void)bsp_esp8266_at_send_and_wait("AT+CIPMUX=0", "OK", BSP_ESP8266_AT_TIMEOUT_MS);
}

/**
 * @brief  带重试查询 STA IP 地址
 * @param  ip      输出 IP 缓冲区
 * @param  ip_len  缓冲区长度
 * @retval 1 成功；0 失败
 */
static uint8_t esp8266_query_ip_retry(char *ip, size_t ip_len)
{
  for (uint32_t i = 0U; i < 3U; i++) {
    if (bsp_esp8266_wifi_query_ip(ip, ip_len)) {
      return 1U;
    }
    osDelay(500U);
  }

  return 0U;
}

/**
 * @brief  更新 ESP8266 应用状态机状态
 * @param  state  新状态
 * @retval 无
 */
static void esp8266_set_state(bsp_esp8266_state_t state)
{
  s_state = state;
}

/**
 * @brief  记录 TCP 连接建立时刻
 * @param  last_check  可选输出上次检查 tick 指针
 * @retval 无
 */
static void esp8266_mark_tcp_up(uint32_t *last_check)
{
  uint32_t now = osKernelGetTickCount();

  s_tcp_up_tick = now;
  if (last_check != NULL) {
    *last_check = now;
  }
}

/**
 * @brief  周期性链路健康检查（WiFi 丢失/TCP 断开时触发重连）
 * @param  last_check  上次检查 tick 指针
 * @retval 无
 */
static void esp8266_run_link_check(uint32_t *last_check)
{
  int link_status;

  link_status = bsp_esp8266_tcp_query_link_status();
  if (link_status < 0) {
    if (++s_link_fail_count < 5U) {
      LOG_W(ESP8266_APP_TAG, "link check AT error (%u/5), keep connection",
            (unsigned)s_link_fail_count);
      return;
    }
    s_link_fail_count = 0U;
    LOG_W(ESP8266_APP_TAG, "link check failed repeatedly, reconnecting host");
    esp8266_set_state(BSP_ESP8266_STATE_TCP_ERROR);
    return;
  }

  s_link_fail_count = 0U;

  if (link_status == BSP_ESP8266_STATUS_WIFI_DISCONNECTED) {
    LOG_W(ESP8266_APP_TAG, "WiFi lost (STATUS:%d), reconnecting", link_status);
    (void)bsp_esp8266_tcp_close();
    s_ip[0] = '\0';
    s_rssi = 0;
    if (esp8266_try_connect() != 0U) {
      if (esp8266_try_tcp() != 0U) {
        esp8266_mark_tcp_up(last_check);
      }
    }
    return;
  }

  if (link_status != BSP_ESP8266_STATUS_TCP_CONNECTED) {
    LOG_W(ESP8266_APP_TAG, "TCP lost (STATUS:%d), reconnecting host", link_status);
    (void)bsp_esp8266_tcp_close();
    esp8266_set_state(BSP_ESP8266_STATE_TCP_ERROR);
    return;
  }

  s_rssi = bsp_esp8266_wifi_query_rssi();
  LOG_I(ESP8266_APP_TAG, "online IP=%s RSSI=%d host=%s:%u",
        s_ip, s_rssi, BSP_ESP8266_OTA_HOST, (unsigned)BSP_ESP8266_OTA_PORT);
}

/**
 * @brief  尝试连接 WiFi 并获取 IP
 * @retval 1 成功；0 失败
 */
static uint8_t esp8266_try_connect(void)
{
  esp8266_set_state(BSP_ESP8266_STATE_WIFI_CONNECTING);
  BSP_Watchdog_Kick(BSP_WDG_ESP8266);
  BSP_Watchdog_SetLongOp(1U);

  if (!bsp_esp8266_wifi_station_connect(BSP_ESP8266_WIFI_SSID, BSP_ESP8266_WIFI_PASSWORD)) {
    BSP_Watchdog_SetLongOp(0U);
    esp8266_set_state(BSP_ESP8266_STATE_WIFI_ERROR);
    return 0U;
  }

  if (!esp8266_query_ip_retry(s_ip, sizeof(s_ip))) {
    BSP_Watchdog_SetLongOp(0U);
    esp8266_set_state(BSP_ESP8266_STATE_WIFI_ERROR);
    return 0U;
  }

  s_rssi = bsp_esp8266_wifi_query_rssi();
  BSP_Watchdog_SetLongOp(0U);
  esp8266_set_state(BSP_ESP8266_STATE_WIFI_CONNECTED);
  LOG_I(ESP8266_APP_TAG, "connected SSID=%s IP=%s RSSI=%d",
        BSP_ESP8266_WIFI_SSID, s_ip, s_rssi);
  osDelay(BSP_ESP8266_WIFI_STABLE_MS);
  return 1U;
}

/**
 * @brief  尝试建立 OTA TCP 连接
 * @retval 1 成功；0 失败
 */
static uint8_t esp8266_try_tcp(void)
{
  esp8266_set_state(BSP_ESP8266_STATE_TCP_CONNECTING);
  BSP_Watchdog_Kick(BSP_WDG_ESP8266);
  BSP_Watchdog_SetLongOp(1U);

  if (!bsp_esp8266_tcp_connect(BSP_ESP8266_OTA_HOST, BSP_ESP8266_OTA_PORT)) {
    BSP_Watchdog_SetLongOp(0U);
    if (bsp_esp8266_tcp_query_link_status() == BSP_ESP8266_STATUS_TCP_CONNECTED) {
      esp8266_set_state(BSP_ESP8266_STATE_TCP_CONNECTED);
      LOG_W(ESP8266_APP_TAG, "TCP connect timeout but link is up");
      return 1U;
    }
    esp8266_set_state(BSP_ESP8266_STATE_TCP_ERROR);
    LOG_W(ESP8266_APP_TAG, "TCP connect failed, host=%s:%u",
          BSP_ESP8266_OTA_HOST, (unsigned)BSP_ESP8266_OTA_PORT);
    return 0U;
  }

  BSP_Watchdog_SetLongOp(0U);
  esp8266_set_state(BSP_ESP8266_STATE_TCP_CONNECTED);
  LOG_I(ESP8266_APP_TAG, "OTA link up %s:%u",
        BSP_ESP8266_OTA_HOST, (unsigned)BSP_ESP8266_OTA_PORT);
  return 1U;
}

/**
 * @brief  ESP8266 后台任务主循环
 * @param  argument  线程参数（未使用）
 * @retval 无
 */
static void Esp8266Task(void *argument)
{
  uint32_t last_check = 0U;

  (void)argument;

  esp8266_set_state(BSP_ESP8266_STATE_BOOT);
  bsp_esp8266_at_init();

  LOG_I(ESP8266_APP_TAG, "waiting module boot %lu ms",
        (unsigned long)BSP_ESP8266_BOOT_DELAY_MS);
  for (uint32_t i = 0U; i < (BSP_ESP8266_BOOT_DELAY_MS / 200U); i++) {
    BSP_Watchdog_Kick(BSP_WDG_ESP8266);
    osDelay(200);
  }

  BSP_Watchdog_SetLongOp(1U);
  if (!bsp_esp8266_wifi_at_probe()) {
    BSP_Watchdog_SetLongOp(0U);
    LOG_E(ESP8266_APP_TAG, "AT probe failed");
    esp8266_set_state(BSP_ESP8266_STATE_WIFI_ERROR);
  } else {
    BSP_Watchdog_SetLongOp(0U);
    esp8266_set_state(BSP_ESP8266_STATE_AT_READY);
    esp8266_startup_cleanup();
    if (esp8266_try_connect() != 0U) {
      if (esp8266_try_tcp() != 0U) {
        esp8266_mark_tcp_up(&last_check);
      }
    }
  }

  for (;;) {
    BSP_Watchdog_Kick(BSP_WDG_ESP8266);

    if (BSP_Ota_IsActive() != 0U) {
      /*
       * OTA 会话进行中：ESP 任务几乎空转，仅喂狗。
       * TCP 收发与 SOIP 解析由 OTA 任务 + poll 驱动，避免本任务发 CIPSTATUS 打断 +IPD。
       */
      osDelay(50);
      continue;
    }

    /* TCP 已连上后不做 CIPSTATUS 轮询：会 stop DMA 丢掉 +IPD/SOIP，依赖 +IPD CLOSED 感知断链 */
    if (s_state == BSP_ESP8266_STATE_TCP_CONNECTED) {
      osDelay(1000);
      continue;
    }

    if (s_state == BSP_ESP8266_STATE_WIFI_ERROR) {
      /* WiFi 断开：先连 AP 再连 OTA 主机 TCP */
      osDelay(BSP_ESP8266_RETRY_MS);
      if (esp8266_try_connect() != 0U) {
        if (esp8266_try_tcp() != 0U) {
          esp8266_mark_tcp_up(&last_check);
        }
      }
    } else if (s_state == BSP_ESP8266_STATE_TCP_ERROR) {
      /* WiFi 正常但 TCP 断：仅重连 OTA 主机 */
      osDelay(BSP_ESP8266_TCP_RETRY_MS);
      if (esp8266_try_tcp() != 0U) {
        esp8266_mark_tcp_up(&last_check);
      }
    } else {
      /* BOOT / AT_READY / CONNECTING 等中间态：低频率空转 */
      osDelay(1000);
    }
  }
}

/**
 * @brief  创建 ESP8266 后台任务（WiFi + OTA TCP）
 * @retval 无
 */
void bsp_esp8266_app_start(void)
{
  const osThreadAttr_t attr = {
    .name = "esp8266",
    .stack_size = BSP_ESP8266_TASK_STACK_WORDS * 4U,
    .priority = (osPriority_t)osPriorityNormal,
  };

  bsp_esp8266_at_rtos_init();
  (void)osThreadNew(Esp8266Task, NULL, &attr);
}

/**
 * @brief  获取 ESP8266 应用状态机当前状态
 * @retval 当前状态枚举值
 */
bsp_esp8266_state_t bsp_esp8266_get_state(void)
{
  return s_state;
}

/**
 * @brief  获取已分配的 STA IP 地址
 * @param  buf  输出缓冲区
 * @param  len  缓冲区长度
 * @retval 0 成功；-1 参数无效或尚无 IP
 */
int bsp_esp8266_get_ip(char *buf, size_t len)
{
  if ((buf == NULL) || (len == 0U)) {
    return -1;
  }

  if (s_ip[0] == '\0') {
    buf[0] = '\0';
    return -1;
  }

  (void)snprintf(buf, len, "%s", s_ip);
  return 0;
}

/**
 * @brief  获取当前 WiFi RSSI
 * @retval RSSI（dBm）
 */
int bsp_esp8266_get_rssi(void)
{
  return s_rssi;
}

/**
 * @brief  判断 OTA TCP 链路是否已建立
 * @retval 1 已连接；0 未连接
 */
uint8_t bsp_esp8266_is_tcp_connected(void)
{
  return (s_state == BSP_ESP8266_STATE_TCP_CONNECTED) ? 1U : 0U;
}

/**
 * @brief  通知 TCP 连接被对端或模块关闭
 * @retval 无
 */
void bsp_esp8266_notify_tcp_closed(void)
{
  if (s_state == BSP_ESP8266_STATE_TCP_CONNECTED) {
    LOG_W(ESP8266_APP_TAG, "TCP closed by peer/module");
    esp8266_set_state(BSP_ESP8266_STATE_TCP_ERROR);
  }
}
