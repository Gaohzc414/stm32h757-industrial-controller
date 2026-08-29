/**
 * @file    bsp_ota_app.h
 * @brief   OTA 应用层入口：初始化、启动任务与启动确认
 *
 * 调用顺序建议：
 *   1. BSP_Ota_Init()      —— main 早期，初始化 boot_meta / transport
 *   2. BSP_Ota_OnBoot()    —— 处理 pending 试启动确认（commit）
 *   3. BSP_Ota_Start()     —— RTOS 启动后创建 OTA 任务（需 TCP RX 通知已就绪）
 */
#ifndef BSP_OTA_APP_H
#define BSP_OTA_APP_H

#include <stdint.h>

/**
 * @brief  初始化 boot meta 与 OTA 传输层
 */
void BSP_Ota_Init(void);

/**
 * @brief  创建 OTA RTOS 任务并初始化 TCP 接收通知
 *
 * OTA 任务负责 poll 收包、驱动状态机；与 W5500 任务配合完成链路维护。
 */
void BSP_Ota_Start(void);

/**
 * @brief  查询 OTA 是否处于活动会话
 *
 * W5500 应用层据此降低重连频率，避免打断 DATA 停等。
 *
 * @retval 1 活动
 * @retval 0 空闲
 */
uint8_t BSP_Ota_IsActive(void);

/**
 * @brief  上电时处理 pending 槽位的 trial boot 确认
 *
 * 若 meta 带 PENDING 且试启动成功，调用 boot_meta_commit_pending。
 */
void BSP_Ota_OnBoot(void);

#endif /* BSP_OTA_APP_H */
