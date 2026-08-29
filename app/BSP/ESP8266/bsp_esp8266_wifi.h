/**
 * @file bsp_esp8266_wifi.h
 * @brief ESP8266 WiFi STA 连接与状态查询
 */
#ifndef BSP_ESP8266_WIFI_H
#define BSP_ESP8266_WIFI_H

#include <stddef.h>
#include <stdint.h>

/**
 * @brief  AT 通信探测（回显关闭、AT、版本查询）
 * @retval 1 探测通过（至少 2/3 成功）；0 失败
 */
uint8_t bsp_esp8266_wifi_at_probe(void);

/**
 * @brief  以 STA 模式连接指定 WiFi
 * @param  ssid      WiFi 名称
 * @param  password  WiFi 密码
 * @retval 1 成功；0 失败
 */
uint8_t bsp_esp8266_wifi_station_connect(const char *ssid, const char *password);

/**
 * @brief  查询 STA 已分配 IP 地址
 * @param  ip_buf  输出 IP 字符串缓冲区
 * @param  ip_len  缓冲区长度
 * @retval 1 成功；0 失败或未获取 IP
 */
uint8_t bsp_esp8266_wifi_query_ip(char *ip_buf, size_t ip_len);

/**
 * @brief  查询当前 WiFi 信号强度
 * @retval RSSI（dBm）；失败时返回 0
 */
int bsp_esp8266_wifi_query_rssi(void);

/**
 * @brief  判断 WiFi 是否已获取 IP
 * @retval 1 已连接；0 未连接
 */
uint8_t bsp_esp8266_wifi_is_connected(void);

#endif /* BSP_ESP8266_WIFI_H */
