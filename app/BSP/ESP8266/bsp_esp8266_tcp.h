/**
 * @file bsp_esp8266_tcp.h
 * @brief ESP8266 TCP 客户端（连接 OTA 主机 PC）
 */
#ifndef BSP_ESP8266_TCP_H
#define BSP_ESP8266_TCP_H

#include <stdint.h>

#define BSP_ESP8266_STATUS_GOT_IP           2
#define BSP_ESP8266_STATUS_TCP_CONNECTED    3
#define BSP_ESP8266_STATUS_TCP_DISCONNECTED 4
#define BSP_ESP8266_STATUS_WIFI_DISCONNECTED 5

/**
 * @brief  建立 TCP 连接到指定主机与端口
 * @param  host  目标 IP 或域名
 * @param  port  目标端口
 * @retval 1 成功；0 失败
 */
uint8_t bsp_esp8266_tcp_connect(const char *host, uint16_t port);

/**
 * @brief  关闭当前 TCP 连接
 * @retval 1 成功或本就未连接；0 AT 失败
 */
uint8_t bsp_esp8266_tcp_close(void);

/**
 * @brief  查询 ESP-AT 链路状态码
 * @retval STATUS 码（2..5）；-1 表示 AT 超时或错误
 */
int bsp_esp8266_tcp_query_link_status(void);

/**
 * @brief  判断 TCP 是否处于已连接状态
 * @retval 1 已连接；0 未连接或查询失败
 */
uint8_t bsp_esp8266_tcp_is_connected(void);

/**
 * @brief  通过 CIPSEND 发送 TCP 数据（含重试）
 * @param  data  待发送数据
 * @param  len   字节数
 * @retval 1 成功；0 失败
 */
uint8_t bsp_esp8266_tcp_send(const uint8_t *data, uint16_t len);

/**
 * @brief  单次 CIPSEND 发送（供 OTA poll 非阻塞重试）
 * @param  data         待发送数据
 * @param  len          字节数
 * @param  timeout_ms   超时毫秒
 * @retval 1 成功；0 失败
 */
uint8_t bsp_esp8266_tcp_send_once(const uint8_t *data, uint16_t len, uint32_t timeout_ms);

/**
 * @brief  OTA 期间 CIPSEND 连续失败时关闭并重连 PC Server
 * @retval 1 重连成功；0 失败
 */
uint8_t bsp_esp8266_tcp_recover_ota_link(void);

/**
 * @brief  阻塞接收 1 字节 TCP payload
 * @param  byte         输出字节指针
 * @param  timeout_ms   超时毫秒
 * @retval 0 成功；-1 超时或参数错误
 */
int bsp_esp8266_tcp_recv_byte(uint8_t *byte, uint32_t timeout_ms);

/**
 * @brief  非阻塞取 1 字节 TCP payload
 * @param  byte  输出字节指针
 * @retval 0 成功；-1 无数据或参数错误
 */
int bsp_esp8266_tcp_try_recv_byte(uint8_t *byte);

/**
 * @brief  阻塞等待 TCP 接收数据或超时
 * @param  timeout_ms  超时毫秒
 * @retval 0 有数据就绪；-1 超时
 */
int bsp_esp8266_tcp_wait_rx(uint32_t timeout_ms);

/**
 * @brief  初始化 TCP 接收通知信号量
 * @retval 无
 */
void bsp_esp8266_tcp_rx_notify_init(void);

/**
 * @brief  从中断/AT 层喂入 UART 原始字节（ISR 入队）
 * @param  data  数据指针
 * @param  len   字节数
 * @retval 无
 */
void bsp_esp8266_tcp_feed_uart(const uint8_t *data, uint16_t len);

/**
 * @brief  AT 轮询读 UART 时旁路 +IPD，避免 CIPSEND 期间丢包
 * @param  data  数据指针
 * @param  len   字节数
 * @retval 无
 */
void bsp_esp8266_tcp_feed_raw_uart(const uint8_t *data, uint16_t len);

/**
 * @brief  轮询处理 ISR 环形缓冲与 UART FIFO 中的 +IPD 数据
 * @retval 无
 */
void bsp_esp8266_tcp_poll_rx(void);

/**
 * @brief  判断 CIPSEND 事务是否进行中
 * @retval 1 进行中；0 空闲
 */
uint8_t bsp_esp8266_tcp_cipsend_active(void);

/**
 * @brief  判断是否应启用后台 +IPD 接收路由
 * @retval 1 启用；0 禁用
 */
uint8_t bsp_esp8266_tcp_wants_background_rx(void);

/**
 * @brief  AT 事务结束时恢复后台接收
 * @retval 无
 */
void bsp_esp8266_tcp_on_at_transaction_end(void);

#endif /* BSP_ESP8266_TCP_H */
