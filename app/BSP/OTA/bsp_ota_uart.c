/**
 * @file    bsp_ota_uart.c
 * @brief   OTA 调试/辅助 UART 读写实现
 */
#include "bsp_ota_uart.h"

#include "bsp_ota_conf.h"
#include "usart.h"

/**
 * @brief  初始化 OTA UART（当前为空实现，依赖 CubeMX 初始化）
 */
void bsp_ota_uart_init(void)
{
}

/**
 * @brief  阻塞接收 UART 数据
 * @param  buf        接收缓冲区
 * @param  cap        期望接收字节数
 * @param  timeout_ms 超时时间（毫秒）
 * @retval 实际接收字节数，0 表示失败或超时
 */
int bsp_ota_uart_read(uint8_t *buf, uint16_t cap, uint32_t timeout_ms)
{
    if ((buf == NULL) || (cap == 0U)) {
        return 0;
    }

    if (HAL_UART_Receive(&huart1, buf, cap, timeout_ms) != HAL_OK) {
        return 0;
    }

    return (int)cap;
}

/**
 * @brief  发送 UART 数据
 * @param  buf 发送缓冲区
 * @param  len 字节数
 * @retval 0 成功
 * @retval -1 失败
 */
int bsp_ota_uart_write(const uint8_t *buf, uint16_t len)
{
    if ((buf == NULL) || (len == 0U)) {
        return -1;
    }

    if (HAL_UART_Transmit(&huart1, (uint8_t *)buf, len, BSP_OTA_UART_TIMEOUT_MS) != HAL_OK) {
        return -1;
    }

    return 0;
}
