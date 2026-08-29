/**
 * @file bsp_esp8266_wifi.c
 * @brief ESP8266 WiFi STA AT 命令实现
 */
#include "bsp_esp8266_wifi.h"
#include "bsp_esp8266_at.h"
#include "bsp_esp8266_conf.h"
#include "bsp_log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ESP8266_WIFI_TAG   "ESP-WiFi"

/**
 * @brief  AT 通信探测（回显关闭、AT、版本查询）
 * @retval 1 探测通过（至少 2/3 成功）；0 失败
 */
uint8_t bsp_esp8266_wifi_at_probe(void)
{
  uint8_t pass = 0U;

  if (bsp_esp8266_at_send_and_wait("ATE0", "OK", BSP_ESP8266_AT_TIMEOUT_MS)) {
    pass++;
  }
  if (bsp_esp8266_at_send_and_wait("AT", "OK", BSP_ESP8266_AT_TIMEOUT_MS)) {
    pass++;
  }
  if (bsp_esp8266_at_send_and_wait("AT+GMR", "OK", 5000U)) {
    pass++;
  }

  LOG_I(ESP8266_WIFI_TAG, "AT probe %u/3", (unsigned)pass);
  return (pass >= 2U) ? 1U : 0U;
}

/**
 * @brief  以 STA 模式连接指定 WiFi
 * @param  ssid      WiFi 名称
 * @param  password  WiFi 密码
 * @retval 1 成功；0 失败
 */
uint8_t bsp_esp8266_wifi_station_connect(const char *ssid, const char *password)
{
  char cmd[96];
  char ip[20];

  if (!bsp_esp8266_at_send_and_wait("AT+CWMODE=1", "OK", BSP_ESP8266_AT_TIMEOUT_MS)) {
    LOG_E(ESP8266_WIFI_TAG, "CWMODE failed");
    return 0U;
  }

  if (bsp_esp8266_wifi_query_ip(ip, sizeof(ip))) {
    LOG_I(ESP8266_WIFI_TAG, "already joined SSID=%s IP=%s", ssid, ip);
    return 1U;
  }

  (void)snprintf(cmd, sizeof(cmd), "AT+CWJAP=\"%s\",\"%s\"", ssid, password);
  if (!bsp_esp8266_at_send_and_wait_markers(cmd, BSP_ESP8266_WIFI_JOIN_TIMEOUT_MS,
                                            "WIFI GOT IP", "OK")) {
    if (!bsp_esp8266_wifi_query_ip(ip, sizeof(ip))) {
      LOG_E(ESP8266_WIFI_TAG, "CWJAP failed");
      return 0U;
    }
    LOG_W(ESP8266_WIFI_TAG, "CWJAP timeout but IP is ready: %s", ip);
    return 1U;
  }

  return 1U;
}

/**
 * @brief  查询 STA 已分配 IP 地址
 * @param  ip_buf  输出 IP 字符串缓冲区
 * @param  ip_len  缓冲区长度
 * @retval 1 成功；0 失败或未获取 IP
 */
uint8_t bsp_esp8266_wifi_query_ip(char *ip_buf, size_t ip_len)
{
  const char *resp;
  const char *tag = "+CIFSR:STAIP,\"";
  const char *end;
  size_t n;

  if ((ip_buf == NULL) || (ip_len == 0U)) {
    return 0U;
  }

  ip_buf[0] = '\0';

  if (!bsp_esp8266_at_send_and_wait("AT+CIFSR", "OK", BSP_ESP8266_AT_TIMEOUT_MS)) {
    return 0U;
  }

  resp = bsp_esp8266_at_last_response();
  tag = strstr(resp, "+CIFSR:STAIP,\"");
  if (tag == NULL) {
    return 0U;
  }

  tag += strlen("+CIFSR:STAIP,\"");
  end = strchr(tag, '\"');
  if (end == NULL) {
    return 0U;
  }

  n = (size_t)(end - tag);
  if (n >= ip_len) {
    n = ip_len - 1U;
  }

  memcpy(ip_buf, tag, n);
  ip_buf[n] = '\0';
  return (n > 0U) ? 1U : 0U;
}

/**
 * @brief  查询当前 WiFi 信号强度
 * @retval RSSI（dBm）；失败时返回 0
 */
int bsp_esp8266_wifi_query_rssi(void)
{
  const char *resp;
  const char *p;
  int rssi = 0;

  if (!bsp_esp8266_at_send_and_wait("AT+CWJAP?", "OK", BSP_ESP8266_AT_TIMEOUT_MS)) {
    return 0;
  }

  resp = bsp_esp8266_at_last_response();
  p = strrchr(resp, ',');
  if (p == NULL) {
    return 0;
  }

  rssi = (int)strtol(p + 1, NULL, 10);
  return rssi;
}

/**
 * @brief  判断 WiFi 是否已获取 IP
 * @retval 1 已连接；0 未连接
 */
uint8_t bsp_esp8266_wifi_is_connected(void)
{
  char ip[20];

  return bsp_esp8266_wifi_query_ip(ip, sizeof(ip));
}
