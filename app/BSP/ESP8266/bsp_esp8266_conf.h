/**
 * @file bsp_esp8266_conf.h
 * @brief ESP8266 WiFi 模块配置（USART6、STA 凭据与 OTA 主机）
 */
#ifndef BSP_ESP8266_CONF_H
#define BSP_ESP8266_CONF_H

#include "usart.h"

#define BSP_ESP8266_UART_HANDLE          huart6

/**
 * WiFi / OTA 主机网络配置切换：
 *   0 = Mate80 热点，PC 192.168.43.4
 *   1 = ASUS 手机热点，PC 192.168.71.63（当前）
 */
#define BSP_ESP8266_NETWORK_PROFILE      1

#if (BSP_ESP8266_NETWORK_PROFILE == 0)
#define BSP_ESP8266_WIFI_SSID            "Mate80"
#define BSP_ESP8266_WIFI_PASSWORD        "123456789"
#define BSP_ESP8266_OTA_HOST             "192.168.43.4"
#elif (BSP_ESP8266_NETWORK_PROFILE == 1)
#define BSP_ESP8266_WIFI_SSID            "ASUS"
#define BSP_ESP8266_WIFI_PASSWORD        "123456789"
#define BSP_ESP8266_OTA_HOST             "192.168.71.63"
#else
#error "Invalid BSP_ESP8266_NETWORK_PROFILE"
#endif

#define BSP_ESP8266_RX_BUF_SIZE          1024U
#define BSP_ESP8266_AT_TX_BUF_SIZE       128U
#define BSP_ESP8266_BOOT_DELAY_MS        2000U
#define BSP_ESP8266_AT_TIMEOUT_MS        5000U
/** OTA 小帧 ACK 经 ESP8266 回传，链路忙时 5s 易超时 */
#define BSP_ESP8266_OTA_CIPSEND_TIMEOUT_MS 15000U
/** DATA ACK 单次 CIPSEND 尝试超时（poll 内快速重试，避免长时间阻塞收包） */
#define BSP_ESP8266_OTA_ACK_TRY_MS          1200U
/** 连续 ACK 发送失败达此次数后触发 TCP 重连（不中断 OTA Session） */
#define BSP_ESP8266_OTA_ACK_RECOVER_FAILS   12U
#define BSP_ESP8266_CIPSEND_RETRIES      3U
#define BSP_ESP8266_AT_CLOSE_TIMEOUT_MS  1000U
#define BSP_ESP8266_WIFI_JOIN_TIMEOUT_MS 30000U
#define BSP_ESP8266_WIFI_STABLE_MS       2000U

#define BSP_ESP8266_TASK_STACK_WORDS     1024U
#define BSP_ESP8266_LINK_CHECK_MS        60000U
/** TCP 刚连上后暂缓 CIPSTATUS，避免 AT 抢占串口丢掉 +IPD/SOIP 数据 */
#define BSP_ESP8266_LINK_CHECK_GRACE_MS  120000U
#define BSP_ESP8266_RETRY_MS             5000U

/** OTA 主机 PC（TCP Server）；MCU 作为 TCP Client 连接 */
#define BSP_ESP8266_OTA_PORT             5000U
#define BSP_ESP8266_TCP_CONNECT_TIMEOUT_MS 15000U
#define BSP_ESP8266_TCP_RETRY_MS         5000U

/** 1=打印 TCP/+IPD 收包内容，联调完可改 0 */
#define BSP_ESP8266_TCP_RX_DEBUG         0U
#define BSP_ESP8266_TCP_RX_DUMP_MAX      128U

#endif /* BSP_ESP8266_CONF_H */
