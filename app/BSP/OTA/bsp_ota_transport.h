/**
 * @file    bsp_ota_transport.h
 * @brief   OTA 底层传输抽象（UART）
 */
#ifndef BSP_OTA_TRANSPORT_H
#define BSP_OTA_TRANSPORT_H

#include <stdint.h>

/**
 * @brief  初始化 OTA 传输层
 */
void bsp_ota_transport_init(void);

/**
 * @brief  启动接收：绑定通知任务并武装 DMA RX
 * @param  notify_task 被 RX 中断唤醒的任务句柄（osThreadId_t）
 * @retval 0 成功
 * @retval -1 失败
 */
int bsp_ota_transport_start(void *notify_task);

/**
 * @brief  轮询 UART 接收
 */
void bsp_ota_transport_poll(void);

/**
 * @brief  阻塞等待 UART RX
 * @param  timeout_ms 超时时间（毫秒）
 * @retval 0 等待结束
 */
int bsp_ota_transport_wait_rx(uint32_t timeout_ms);

/**
 * @brief  非阻塞读取 1 字节
 * @param  byte 输出字节指针
 * @retval 0 成功
 * @retval -1 无数据或参数无效
 */
int bsp_ota_transport_try_read(uint8_t *byte);

/**
 * @brief  查询 UART 通道是否就绪
 * @retval 1 已就绪
 * @retval 0 未就绪
 */
uint8_t bsp_ota_transport_ready(void);

/**
 * @brief  阻塞读取 1 字节
 * @param  byte       输出字节指针
 * @param  timeout_ms 超时时间（毫秒）
 * @retval 0 成功
 * @retval -1 失败
 */
int bsp_ota_transport_read(uint8_t *byte, uint32_t timeout_ms);

/**
 * @brief  发送数据（带内部重试）
 * @param  data 待发送缓冲区
 * @param  len  字节数
 * @retval 0 成功
 * @retval -1 失败
 */
int bsp_ota_transport_write(const uint8_t *data, uint16_t len);

/**
 * @brief  单次发送数据（指定超时）
 * @param  data       待发送缓冲区
 * @param  len        字节数
 * @param  timeout_ms 单次发送超时（毫秒）
 * @retval 0 成功
 * @retval -1 失败
 */
int bsp_ota_transport_write_once(const uint8_t *data, uint16_t len, uint32_t timeout_ms);

/**
 * @brief  UART 无连接恢复动作
 * @retval 0 无需恢复
 */
int bsp_ota_transport_recover_link(void);

#endif /* BSP_OTA_TRANSPORT_H */
