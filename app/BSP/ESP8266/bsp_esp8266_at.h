/**
 * @file bsp_esp8266_at.h
 * @brief ESP8266 AT 命令传输层（USART6 中断逐字节 RX + 阻塞 TX）
 */
#ifndef BSP_ESP8266_AT_H
#define BSP_ESP8266_AT_H

#include <stdint.h>
#include "stm32h7xx_hal.h"

/**
 * @brief  初始化 ESP8266 AT 串口与接收中断
 * @retval 无
 */
void bsp_esp8266_at_init(void);

/**
 * @brief  初始化 AT 层 RTOS 互斥锁与 TCP 接收通知
 * @retval 无
 */
void bsp_esp8266_at_rtos_init(void);

/**
 * @brief  发送 AT 命令（自动追加 CRLF）
 * @param  cmd  命令字符串（不含换行）
 * @retval HAL_OK 成功；HAL_ERROR 失败
 */
HAL_StatusTypeDef bsp_esp8266_at_send(const char *cmd);

/**
 * @brief  发送原始字节（用于 CIPSEND payload）
 * @param  data  数据指针
 * @param  len   字节数
 * @retval HAL_OK 成功；HAL_ERROR 失败
 */
HAL_StatusTypeDef bsp_esp8266_at_send_bytes(const uint8_t *data, uint16_t len);

/**
 * @brief  等待响应中出现期望子串
 * @param  expect       期望标记字符串
 * @param  timeout_ms   超时毫秒
 * @retval 1 匹配成功；0 超时或 FAIL
 */
uint8_t bsp_esp8266_at_wait(const char *expect, uint32_t timeout_ms);

/**
 * @brief  发送 AT 命令并等待期望响应
 * @param  cmd          命令字符串
 * @param  expect       期望标记
 * @param  timeout_ms   超时毫秒
 * @retval 1 成功；0 失败
 */
uint8_t bsp_esp8266_at_send_and_wait(const char *cmd, const char *expect, uint32_t timeout_ms);

/**
 * @brief  等待响应中出现 marker_a 或 marker_b
 * @param  marker_a     标记 A
 * @param  marker_b     标记 B（可为 NULL）
 * @param  timeout_ms   超时毫秒
 * @retval 1 匹配成功；0 超时或 FAIL
 */
uint8_t bsp_esp8266_at_wait_markers(const char *marker_a, const char *marker_b,
                                    uint32_t timeout_ms);

/**
 * @brief  发送 AT 命令并等待双标记之一
 * @param  cmd          命令字符串
 * @param  timeout_ms   超时毫秒
 * @param  marker_a     标记 A
 * @param  marker_b     标记 B
 * @retval 1 成功；0 失败
 */
uint8_t bsp_esp8266_at_send_and_wait_markers(const char *cmd, uint32_t timeout_ms,
                                             const char *marker_a, const char *marker_b);

/**
 * @brief  获取最近一次 AT 响应行缓冲
 * @retval 响应 C 字符串指针
 */
const char *bsp_esp8266_at_last_response(void);

/**
 * @brief  清空 AT 接收缓冲并重启 RX 中断
 * @retval 无
 */
void bsp_esp8266_at_flush(void);

/**
 * @brief  获取 AT 互斥锁
 * @retval 无
 */
void bsp_esp8266_at_lock(void);

/**
 * @brief  释放 AT 互斥锁
 * @retval 无
 */
void bsp_esp8266_at_unlock(void);

/**
 * @brief  开始 AT 事务（加锁并清响应）
 * @retval 无
 */
void bsp_esp8266_at_begin_transaction(void);

/**
 * @brief  开始 AT 轮询事务（加锁并清响应）
 * @retval 无
 */
void bsp_esp8266_at_begin_poll_transaction(void);

/**
 * @brief  开始 CIPSEND 事务（保持 DMA/中断 RX，+IPD 仍路由到 TCP）
 * @retval 无
 */
void bsp_esp8266_at_begin_cipsend_keep_dma(void);

/**
 * @brief  结束 CIPSEND 事务并释放锁
 * @retval 无
 */
void bsp_esp8266_at_end_cipsend_transaction(void);

/**
 * @brief  清空 AT 响应缓冲（不释放锁）
 * @retval 无
 */
void bsp_esp8266_at_clear_response(void);

/**
 * @brief  结束 AT 事务并释放锁
 * @retval 无
 */
void bsp_esp8266_at_end_transaction(void);

/**
 * @brief  轮询模式下等待期望响应（直接读 UART）
 * @param  expect       期望标记
 * @param  timeout_ms   超时毫秒
 * @retval 1 成功；0 超时
 */
uint8_t bsp_esp8266_at_wait_poll(const char *expect, uint32_t timeout_ms);

/**
 * @brief  阻塞从 UART 读取 1 字节
 * @param  byte         输出字节指针
 * @param  timeout_ms   超时毫秒
 * @retval 1 成功；0 超时
 */
uint8_t bsp_esp8266_at_uart_read(uint8_t *byte, uint32_t timeout_ms);

/**
 * @brief  非阻塞从 UART RX FIFO 读取 1 字节
 * @param  byte  输出字节指针
 * @retval 1 读到数据；0 无数据
 */
uint8_t bsp_esp8266_at_uart_try_read(uint8_t *byte);

/**
 * @brief  查询 AT 层是否处于忙状态
 * @retval 1 忙；0 空闲
 */
uint8_t bsp_esp8266_at_is_busy(void);

/**
 * @brief  查询是否处于 CIPSEND 会话中
 * @retval 1 是；0 否
 */
uint8_t bsp_esp8266_at_in_cipsend_session(void);

/**
 * @brief  恢复后台 UART 接收中断
 * @retval 无
 */
void bsp_esp8266_at_resume_background_rx(void);

/**
 * @brief  USART6 中断处理中调用，逐字节接收数据
 * @retval 无
 */
void bsp_esp8266_at_uart_irq_handler(void);

#endif /* BSP_ESP8266_AT_H */
