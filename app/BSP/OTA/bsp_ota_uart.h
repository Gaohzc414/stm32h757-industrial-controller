/**
 * @file    bsp_ota_uart.h
 * @brief   OTA USART6 DMA + 环形缓冲收发接口
 */
#ifndef BSP_OTA_UART_H
#define BSP_OTA_UART_H

#include <stdint.h>

/**
 * @brief  初始化 OTA UART（依赖 CubeMX 初始化）
 */
void bsp_ota_uart_init(void);

/**
 * @brief  绑定 RX 通知目标任务并武装 DMA 循环接收
 * @param  notify_task 被 RX 中断唤醒的 FreeRTOS 任务句柄（osThreadId_t 可强转）
 * @retval 0 成功
 * @retval -1 DMA 武装失败
 */
int bsp_ota_uart_start_rx(void *notify_task);

/**
 * @brief  停止 DMA 接收（复位/退出 OTA 时使用）
 */
void bsp_ota_uart_stop_rx(void);

/**
 * @brief  非阻塞从环形缓冲读取数据
 * @param  buf 输出缓冲
 * @param  cap 期望读取字节数
 * @retval 实际读取字节数，0 表示当前无数据
 */
int bsp_ota_uart_try_read(uint8_t *buf, uint16_t cap);

/**
 * @brief  阻塞等待 RX 数据（有数据或超时后返回）
 * @param  timeout_ms 超时时间（毫秒）
 * @retval 0 等待结束（超时或被 RX 中断唤醒）
 */
int bsp_ota_uart_wait_rx(uint32_t timeout_ms);

/**
 * @brief  发送 UART 数据
 * @param  buf 发送缓冲区
 * @param  len 字节数
 * @retval 0 成功
 * @retval -1 失败
 */
int bsp_ota_uart_write(const uint8_t *buf, uint16_t len);

#endif /* BSP_OTA_UART_H */
