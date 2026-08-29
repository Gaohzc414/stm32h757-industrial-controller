/**
 * @file bsp_esp8266_at.c
 * @brief ESP8266 USART6：中断逐字节 RX + 阻塞 TX（无 DMA）
 *
 * 115200bps 下单字节中断足够；避免 DMA/ReceiveToIdle 与 CIPSEND 状态机冲突。
 */
#include "bsp_esp8266_at.h"
#include "bsp_esp8266_tcp.h"
#include "bsp_esp8266_conf.h"
#include "bsp_log.h"
#include "bsp_watchdog.h"

#include <stdio.h>
#include <string.h>

#include "cmsis_os.h"

#define ESP8266_AT_TAG           "ESP-AT"
#define ESP8266_AT_TX_MAX        1280U

static UART_HandleTypeDef *s_uart;

static uint8_t s_rx_line[BSP_ESP8266_RX_BUF_SIZE];
static volatile uint16_t s_rx_len;
static volatile uint8_t s_rx_done;

static osMutexId_t s_at_mutex;
static volatile uint8_t s_at_busy;
/** CIPSEND 全程（含 payload 发送与等 SEND OK）：+IPD 必须进 TCP，不能进 AT 行缓冲 */
static volatile uint8_t s_cipsend_session;

/**
 * @brief  清除 UART 错误标志（ORE/FE/NE/PE）
 * @retval 无
 */
static void esp8266_clear_uart_errors(void)
{
  s_uart->ErrorCode = HAL_UART_ERROR_NONE;

  if (__HAL_UART_GET_FLAG(s_uart, UART_FLAG_ORE) != RESET) {
    __HAL_UART_CLEAR_OREFLAG(s_uart);
  }
  if (__HAL_UART_GET_FLAG(s_uart, UART_FLAG_FE) != RESET) {
    __HAL_UART_CLEAR_FEFLAG(s_uart);
  }
  if (__HAL_UART_GET_FLAG(s_uart, UART_FLAG_NE) != RESET) {
    __HAL_UART_CLEAR_NEFLAG(s_uart);
  }
  if (__HAL_UART_GET_FLAG(s_uart, UART_FLAG_PE) != RESET) {
    __HAL_UART_CLEAR_PEFLAG(s_uart);
  }
}

/**
 * @brief  排空 UART RX FIFO 并清错误
 * @retval 无
 */
static void esp8266_drain_uart_fifo(void)
{
  while (__HAL_UART_GET_FLAG(s_uart, UART_FLAG_RXNE) != RESET) {
    (void)s_uart->Instance->RDR;
  }
  esp8266_clear_uart_errors();
}

/**
 * @brief  向 AT 响应行缓冲追加 1 字节
 * @param  ch  接收字节
 * @retval 无
 */
static void esp8266_append_line_byte(uint8_t ch)
{
  if (s_rx_len < (BSP_ESP8266_RX_BUF_SIZE - 1U)) {
    s_rx_line[s_rx_len++] = ch;
    s_rx_line[s_rx_len] = '\0';
    s_rx_done = 1U;
  }
}

/**
 * @brief  路由接收字节到 TCP 解析或 AT 行缓冲
 * @param  ch  接收字节
 * @retval 无
 */
static void esp8266_route_rx_byte(uint8_t ch)
{
  if ((bsp_esp8266_tcp_wants_background_rx() != 0U) &&
      ((s_at_busy == 0U) || (s_cipsend_session != 0U) ||
       (bsp_esp8266_tcp_cipsend_active() != 0U))) {
    bsp_esp8266_tcp_feed_uart(&ch, 1U);
  } else {
    esp8266_append_line_byte(ch);
  }
}

/**
 * @brief  在中断上下文中排空 RXNE 并逐字节路由
 * @retval 无
 */
static void esp8266_rx_irq_drain(void)
{
  while (__HAL_UART_GET_FLAG(s_uart, UART_FLAG_RXNE) != RESET) {
    uint8_t ch = (uint8_t)(s_uart->Instance->RDR & 0xFFU);
    esp8266_route_rx_byte(ch);
  }
  esp8266_clear_uart_errors();
}

/**
 * @brief  启动 UART RXNE 中断接收
 * @retval 无
 */
static void esp8266_start_rx_irq(void)
{
  if (s_uart == NULL) {
    return;
  }

  esp8266_drain_uart_fifo();
  __HAL_UART_ENABLE_IT(s_uart, UART_IT_RXNE);
}

/**
 * @brief  停止 UART RXNE 中断并排空 FIFO
 * @retval 无
 */
static void esp8266_stop_rx_irq(void)
{
  if (s_uart == NULL) {
    return;
  }

  __HAL_UART_DISABLE_IT(s_uart, UART_IT_RXNE);
  esp8266_drain_uart_fifo();
}

/**
 * @brief  阻塞发送 UART 数据
 * @param  data         数据指针
 * @param  len          字节数
 * @param  timeout_ms   超时毫秒
 * @retval HAL_OK 成功；HAL_ERROR 失败
 */
static HAL_StatusTypeDef esp8266_uart_transmit(const uint8_t *data, uint16_t len,
                                               uint32_t timeout_ms)
{
  if ((data == NULL) || (len == 0U) || (s_uart == NULL)) {
    return HAL_ERROR;
  }

  return HAL_UART_Transmit(s_uart, (uint8_t *)data, len, timeout_ms);
}

/**
 * @brief  标记 AT 命令开始并清空响应缓冲
 * @retval 无
 */
static void esp8266_at_begin_cmd(void)
{
  s_at_busy = 1U;
  s_rx_len = 0U;
  s_rx_line[0] = '\0';
  s_rx_done = 0U;
}

/**
 * @brief  标记 AT 命令结束并通知 TCP 层
 * @retval 无
 */
static void esp8266_at_end_cmd(void)
{
  s_at_busy = 0U;
  bsp_esp8266_tcp_on_at_transaction_end();
}

/**
 * @brief  USART6 中断处理中调用，逐字节接收数据
 * @retval 无
 */
void bsp_esp8266_at_uart_irq_handler(void)
{
  if (s_uart == NULL) {
    return;
  }

  esp8266_rx_irq_drain();
}

/**
 * @brief  初始化 AT 层 RTOS 互斥锁与 TCP 接收通知
 * @retval 无
 */
void bsp_esp8266_at_rtos_init(void)
{
  if (s_at_mutex == NULL) {
    s_at_mutex = osMutexNew(NULL);
  }
  bsp_esp8266_tcp_rx_notify_init();
}

/**
 * @brief  初始化 ESP8266 AT 串口与接收中断
 * @retval 无
 */
void bsp_esp8266_at_init(void)
{
  s_uart = &BSP_ESP8266_UART_HANDLE;

  (void)HAL_UART_AbortReceive(s_uart);
  (void)HAL_UART_AbortTransmit(s_uart);
  s_uart->RxState = HAL_UART_STATE_READY;
  s_uart->gState = HAL_UART_STATE_READY;

  bsp_esp8266_at_flush();
  esp8266_start_rx_irq();
  LOG_I(ESP8266_AT_TAG, "USART6 IRQ byte-RX + blocking TX");
}

/**
 * @brief  清空 AT 接收缓冲并重启 RX 中断
 * @retval 无
 */
void bsp_esp8266_at_flush(void)
{
  esp8266_stop_rx_irq();
  s_rx_len = 0U;
  s_rx_line[0] = '\0';
  s_rx_done = 0U;
  esp8266_start_rx_irq();
}

/**
 * @brief  获取最近一次 AT 响应行缓冲
 * @retval 响应 C 字符串指针
 */
const char *bsp_esp8266_at_last_response(void)
{
  return (const char *)s_rx_line;
}

/**
 * @brief  获取 AT 互斥锁
 * @retval 无
 */
void bsp_esp8266_at_lock(void)
{
  if (s_at_mutex != NULL) {
    (void)osMutexAcquire(s_at_mutex, osWaitForever);
  }
}

/**
 * @brief  释放 AT 互斥锁
 * @retval 无
 */
void bsp_esp8266_at_unlock(void)
{
  if (s_at_mutex != NULL) {
    (void)osMutexRelease(s_at_mutex);
  }
}

/**
 * @brief  开始 AT 事务（加锁并清响应）
 * @retval 无
 */
void bsp_esp8266_at_begin_transaction(void)
{
  bsp_esp8266_at_lock();
  esp8266_at_begin_cmd();
}

/**
 * @brief  开始 AT 轮询事务（加锁并清响应）
 * @retval 无
 */
void bsp_esp8266_at_begin_poll_transaction(void)
{
  bsp_esp8266_at_lock();
  esp8266_at_begin_cmd();
}

/**
 * @brief  开始 CIPSEND 事务（保持 RX，+IPD 仍路由到 TCP）
 * @retval 无
 */
void bsp_esp8266_at_begin_cipsend_keep_dma(void)
{
  s_at_busy = 1U;
  s_cipsend_session = 1U;
}

/**
 * @brief  结束 CIPSEND 事务并释放锁
 * @retval 无
 */
void bsp_esp8266_at_end_cipsend_transaction(void)
{
  s_cipsend_session = 0U;
  s_at_busy = 0U;
  bsp_esp8266_at_unlock();
}

/**
 * @brief  查询是否处于 CIPSEND 会话中
 * @retval 1 是；0 否
 */
uint8_t bsp_esp8266_at_in_cipsend_session(void)
{
  return s_cipsend_session;
}

/**
 * @brief  清空 AT 响应缓冲（不释放锁）
 * @retval 无
 */
void bsp_esp8266_at_clear_response(void)
{
  s_rx_len = 0U;
  s_rx_line[0] = '\0';
  s_rx_done = 0U;
}

/**
 * @brief  结束 AT 事务并释放锁
 * @retval 无
 */
void bsp_esp8266_at_end_transaction(void)
{
  esp8266_at_end_cmd();
  bsp_esp8266_at_unlock();
}

/**
 * @brief  查询 AT 层是否处于忙状态
 * @retval 1 忙；0 空闲
 */
uint8_t bsp_esp8266_at_is_busy(void)
{
  return s_at_busy;
}

/**
 * @brief  恢复后台 UART 接收中断
 * @retval 无
 */
void bsp_esp8266_at_resume_background_rx(void)
{
  esp8266_start_rx_irq();
}

/**
 * @brief  非阻塞从 UART RX FIFO 读取 1 字节
 * @param  byte  输出字节指针
 * @retval 1 读到数据；0 无数据
 */
uint8_t bsp_esp8266_at_uart_try_read(uint8_t *byte)
{
  if ((byte == NULL) || (s_uart == NULL)) {
    return 0U;
  }

  if (__HAL_UART_GET_FLAG(s_uart, UART_FLAG_RXNE) == RESET) {
    return 0U;
  }

  *byte = (uint8_t)(s_uart->Instance->RDR & 0xFFU);
  return 1U;
}

/**
 * @brief  阻塞从 UART 读取 1 字节
 * @param  byte         输出字节指针
 * @param  timeout_ms   超时毫秒
 * @retval 1 成功；0 超时
 */
uint8_t bsp_esp8266_at_uart_read(uint8_t *byte, uint32_t timeout_ms)
{
  uint32_t start = HAL_GetTick();

  if (byte == NULL) {
    return 0U;
  }

  while ((HAL_GetTick() - start) < timeout_ms) {
    if (bsp_esp8266_at_uart_try_read(byte) != 0U) {
      return 1U;
    }
    BSP_Watchdog_Kick(BSP_WDG_ESP8266);
    osDelay(1U);
  }

  return 0U;
}

/**
 * @brief  发送 AT 命令（自动追加 CRLF）
 * @param  cmd  命令字符串（不含换行）
 * @retval HAL_OK 成功；HAL_ERROR 失败
 */
HAL_StatusTypeDef bsp_esp8266_at_send(const char *cmd)
{
  char tx[BSP_ESP8266_AT_TX_BUF_SIZE];
  int n;

  if (cmd == NULL) {
    return HAL_ERROR;
  }

  n = snprintf(tx, sizeof(tx), "%s\r\n", cmd);
  if (n <= 0 || n >= (int)sizeof(tx)) {
    return HAL_ERROR;
  }

  return esp8266_uart_transmit((const uint8_t *)tx, (uint16_t)n, 3000U);
}

/**
 * @brief  发送原始字节（用于 CIPSEND payload）
 * @param  data  数据指针
 * @param  len   字节数
 * @retval HAL_OK 成功；HAL_ERROR 失败
 */
HAL_StatusTypeDef bsp_esp8266_at_send_bytes(const uint8_t *data, uint16_t len)
{
  if ((data == NULL) || (len == 0U) || (len > ESP8266_AT_TX_MAX)) {
    return HAL_ERROR;
  }

  return esp8266_uart_transmit(data, len, BSP_ESP8266_AT_TIMEOUT_MS);
}

/**
 * @brief  检查响应行是否包含 marker_a 或 marker_b
 * @param  marker_a  标记 A
 * @param  marker_b  标记 B（可为 NULL）
 * @retval 1 匹配；0 不匹配
 */
static uint8_t esp8266_response_has_marker(const char *marker_a, const char *marker_b)
{
  if ((marker_a != NULL) && (marker_a[0] != '\0') &&
      (strstr((char *)s_rx_line, marker_a) != NULL)) {
    return 1U;
  }

  if ((marker_b != NULL) && (marker_b[0] != '\0') &&
      (strstr((char *)s_rx_line, marker_b) != NULL)) {
    return 1U;
  }

  return 0U;
}

/**
 * @brief  等待响应中出现 marker_a 或 marker_b
 * @param  marker_a     标记 A
 * @param  marker_b     标记 B（可为 NULL）
 * @param  timeout_ms   超时毫秒
 * @retval 1 匹配成功；0 超时或 FAIL
 */
uint8_t bsp_esp8266_at_wait_markers(const char *marker_a, const char *marker_b,
                                    uint32_t timeout_ms)
{
  uint32_t start = HAL_GetTick();

  if (esp8266_response_has_marker(marker_a, marker_b) != 0U) {
    return 1U;
  }

  while ((HAL_GetTick() - start) < timeout_ms) {
    BSP_Watchdog_Kick(BSP_WDG_ESP8266);
    osDelay(1U);

    if (s_rx_done != 0U) {
      s_rx_done = 0U;

      if (esp8266_response_has_marker(marker_a, marker_b) != 0U) {
        return 1U;
      }

      if (strstr((char *)s_rx_line, "FAIL") != NULL) {
        return 0U;
      }
    }
  }

  return esp8266_response_has_marker(marker_a, marker_b);
}

/**
 * @brief  等待响应中出现期望子串
 * @param  expect       期望标记字符串
 * @param  timeout_ms   超时毫秒
 * @retval 1 匹配成功；0 超时或 FAIL
 */
uint8_t bsp_esp8266_at_wait(const char *expect, uint32_t timeout_ms)
{
  return bsp_esp8266_at_wait_markers(expect, NULL, timeout_ms);
}

/**
 * @brief  轮询模式下等待期望响应（直接读 UART）
 * @param  expect       期望标记
 * @param  timeout_ms   超时毫秒
 * @retval 1 成功；0 超时
 */
uint8_t bsp_esp8266_at_wait_poll(const char *expect, uint32_t timeout_ms)
{
  uint32_t start = HAL_GetTick();

  if ((expect != NULL) && (expect[0] != '\0') &&
      (strstr((char *)s_rx_line, expect) != NULL)) {
    return 1U;
  }

  while ((HAL_GetTick() - start) < timeout_ms) {
    uint8_t ch;

    if (bsp_esp8266_at_uart_try_read(&ch) != 0U) {
      esp8266_append_line_byte(ch);

      if ((expect != NULL) && (expect[0] != '\0') &&
          (strstr((char *)s_rx_line, expect) != NULL)) {
        return 1U;
      }

      if (strstr((char *)s_rx_line, "FAIL") != NULL) {
        return 0U;
      }
    } else {
      BSP_Watchdog_Kick(BSP_WDG_ESP8266);
      osDelay(1U);
    }
  }

  return 0U;
}

/**
 * @brief  发送 AT 命令并等待期望响应
 * @param  cmd          命令字符串
 * @param  expect       期望标记
 * @param  timeout_ms   超时毫秒
 * @retval 1 成功；0 失败
 */
uint8_t bsp_esp8266_at_send_and_wait(const char *cmd, const char *expect, uint32_t timeout_ms)
{
  uint8_t ok = 0U;

  if (s_at_mutex != NULL) {
    (void)osMutexAcquire(s_at_mutex, osWaitForever);
  }

  esp8266_at_begin_cmd();
  LOG_D(ESP8266_AT_TAG, ">>> %s", cmd);

  if (bsp_esp8266_at_send(cmd) != HAL_OK) {
    LOG_E(ESP8266_AT_TAG, "TX failed");
    esp8266_at_end_cmd();
    if (s_at_mutex != NULL) {
      (void)osMutexRelease(s_at_mutex);
    }
    return 0U;
  }

  if (bsp_esp8266_at_wait(expect, timeout_ms) != 0U) {
    ok = 1U;
    LOG_D(ESP8266_AT_TAG, "<< %s", s_rx_line);
  } else {
    LOG_W(ESP8266_AT_TAG, "timeout, last: %s", s_rx_line);
  }

  esp8266_at_end_cmd();

  if (s_at_mutex != NULL) {
    (void)osMutexRelease(s_at_mutex);
  }

  return ok;
}

/**
 * @brief  发送 AT 命令并等待双标记之一
 * @param  cmd          命令字符串
 * @param  timeout_ms   超时毫秒
 * @param  marker_a     标记 A
 * @param  marker_b     标记 B
 * @retval 1 成功；0 失败
 */
uint8_t bsp_esp8266_at_send_and_wait_markers(const char *cmd, uint32_t timeout_ms,
                                             const char *marker_a, const char *marker_b)
{
  uint8_t ok = 0U;

  if (s_at_mutex != NULL) {
    (void)osMutexAcquire(s_at_mutex, osWaitForever);
  }

  esp8266_at_begin_cmd();
  LOG_D(ESP8266_AT_TAG, ">>> %s", cmd);

  if (bsp_esp8266_at_send(cmd) != HAL_OK) {
    LOG_E(ESP8266_AT_TAG, "TX failed");
    esp8266_at_end_cmd();
    if (s_at_mutex != NULL) {
      (void)osMutexRelease(s_at_mutex);
    }
    return 0U;
  }

  if (bsp_esp8266_at_wait_markers(marker_a, marker_b, timeout_ms) != 0U) {
    ok = 1U;
    LOG_D(ESP8266_AT_TAG, "<< %s", s_rx_line);
  } else {
    LOG_W(ESP8266_AT_TAG, "timeout, last: %s", s_rx_line);
  }

  esp8266_at_end_cmd();

  if (s_at_mutex != NULL) {
    (void)osMutexRelease(s_at_mutex);
  }

  return ok;
}
