/**
 * @file bsp_esp8266_app.h
 * @brief ESP8266 FreeRTOS 任务与应用状态
 */
#ifndef BSP_ESP8266_APP_H
#define BSP_ESP8266_APP_H

#include <stddef.h>
#include <stdint.h>

typedef enum {
  BSP_ESP8266_STATE_OFF = 0,
  BSP_ESP8266_STATE_BOOT,
  BSP_ESP8266_STATE_AT_READY,
  BSP_ESP8266_STATE_WIFI_CONNECTING,
  BSP_ESP8266_STATE_WIFI_CONNECTED,
  BSP_ESP8266_STATE_WIFI_ERROR,
  BSP_ESP8266_STATE_TCP_CONNECTING,
  BSP_ESP8266_STATE_TCP_CONNECTED,
  BSP_ESP8266_STATE_TCP_ERROR,
} bsp_esp8266_state_t;

/**
 * @brief  创建 ESP8266 后台任务（WiFi + OTA TCP）
 * @retval 无
 */
void bsp_esp8266_app_start(void);

/**
 * @brief  获取 ESP8266 应用状态机当前状态
 * @retval 当前状态枚举值
 */
bsp_esp8266_state_t bsp_esp8266_get_state(void);

/**
 * @brief  获取已分配的 STA IP 地址
 * @param  buf  输出缓冲区
 * @param  len  缓冲区长度
 * @retval 0 成功；-1 参数无效或尚无 IP
 */
int bsp_esp8266_get_ip(char *buf, size_t len);

/**
 * @brief  获取当前 WiFi RSSI
 * @retval RSSI（dBm）
 */
int bsp_esp8266_get_rssi(void);

/**
 * @brief  判断 OTA TCP 链路是否已建立
 * @retval 1 已连接；0 未连接
 */
uint8_t bsp_esp8266_is_tcp_connected(void);

/**
 * @brief  通知 TCP 连接被对端或模块关闭
 * @retval 无
 */
void bsp_esp8266_notify_tcp_closed(void);

#endif /* BSP_ESP8266_APP_H */
