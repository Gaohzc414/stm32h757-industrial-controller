/**
 * @file    bsp_ota_transport.c
 * @brief   OTA 传输层实现，封装 USART6 UART 收发
 *
 * 本层将 OTA 状态机与具体传输解耦：上层只依赖本文件 API，
 * 底层当前映射到 bsp_ota_uart（DMA + 环形缓冲）。
 */
#include "bsp_ota_transport.h"

#include <stddef.h>

#include "bsp_ota_uart.h"

/**
 * @brief  初始化 OTA 传输层
 */
void bsp_ota_transport_init(void)
{
    bsp_ota_uart_init();
}

/**
 * @brief  启动接收：绑定通知任务并武装 DMA RX
 * @param  notify_task 被 RX 中断唤醒的任务句柄
 * @retval 0 成功，-1 失败
 */
int bsp_ota_transport_start(void *notify_task)
{
    return bsp_ota_uart_start_rx(notify_task);
}

/**
 * @brief  轮询 UART 接收
 */
void bsp_ota_transport_poll(void)
{
    /* DMA 在中断里持续把数据搬进环形缓冲，此处无需再主动轮询 USART6 */
}

/**
 * @brief  等待 UART RX
 * @param  timeout_ms 超时时间（毫秒）
 * @retval 0 等待结束
 */
int bsp_ota_transport_wait_rx(uint32_t timeout_ms)
{
    return bsp_ota_uart_wait_rx(timeout_ms);
}

/**
 * @brief  非阻塞读取 1 字节 SOIP 载荷
 * @param  byte 输出字节指针
 * @retval 0 成功
 * @retval -1 无数据或参数无效
 */
int bsp_ota_transport_try_read(uint8_t *byte)
{
    if (byte == NULL) {
        return -1;
    }

    return (bsp_ota_uart_try_read(byte, 1U) == 1) ? 0 : -1;
}

/**
 * @brief  查询 UART 通道是否就绪
 * @retval 1 UART 无连接状态，初始化后视为就绪
 */
uint8_t bsp_ota_transport_ready(void)
{
    return 1U;
}

/**
 * @brief  阻塞读取 1 字节
 * @param  byte       输出字节指针
 * @param  timeout_ms 超时时间（毫秒）
 * @retval 0 成功
 * @retval -1 失败（空指针或超时）
 */
int bsp_ota_transport_read(uint8_t *byte, uint32_t timeout_ms)
{
    if (byte == NULL) {
        return -1;
    }

    /* 先试环形缓冲，无数据则等通知/超时后再试一次 */
    if (bsp_ota_uart_try_read(byte, 1U) == 1) {
        return 0;
    }

    (void)bsp_ota_uart_wait_rx(timeout_ms);

    return (bsp_ota_uart_try_read(byte, 1U) == 1) ? 0 : -1;
}

/**
 * @brief  发送数据
 * @param  data 待发送缓冲区
 * @param  len  字节数
 * @retval 0 成功
 * @retval -1 失败（参数无效或发送失败）
 */
int bsp_ota_transport_write(const uint8_t *data, uint16_t len)
{
    if ((data == NULL) || (len == 0U)) {
        return -1;
    }

    return bsp_ota_uart_write(data, len);
}

/**
 * @brief  单次发送数据
 * @param  data       待发送缓冲区
 * @param  len        字节数
 * @param  timeout_ms 单次发送超时（毫秒，UART 阻塞发送接口内部使用默认超时）
 * @retval 0 成功
 * @retval -1 失败
 */
int bsp_ota_transport_write_once(const uint8_t *data, uint16_t len, uint32_t timeout_ms)
{
    (void)timeout_ms;

    if ((data == NULL) || (len == 0U)) {
        return -1;
    }

    return bsp_ota_uart_write(data, len);
}

/**
 * @brief  UART 无连接恢复动作
 * @retval 0 无需恢复
 */
int bsp_ota_transport_recover_link(void)
{
    return 0;
}
