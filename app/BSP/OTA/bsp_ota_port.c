/**
 * @file    bsp_ota_port.c
 * @brief   OTA 双核与看门狗端口控制实现
 *
 * STM32H747 双核共享 Bank1 Flash。OTA 擦写期间若 CM4 同时访问 Flash 可能总线冲突，
 * 因此在 ERASE 前 hold CM4，并放宽看门狗对 W5500 长网络/Flash 操作的超时判定。
 */
#include "bsp_ota_port.h"

#include "bsp_watchdog.h"
#include "stm32h7xx.h"

/**
 * @brief  阻止 CM4 从 Flash 启动（保持 CM7 独占 Flash 总线）
 *
 * 清除 RCC_GCR 的 BOOT_C2 位后，CM4 不会从 Flash 取指运行，
 * 避免 OTA 擦写 Bank1 时 CM4 代码访问与 program/erase 硬件操作冲突。
 */
void bsp_ota_port_hold_cm4(void)
{
    CLEAR_BIT(RCC->GCR, RCC_GCR_BOOT_C2);
}

/**
 * @brief  进入 OTA 端口模式（挂起 CM4、启用长操作看门狗）
 *
 * 在 ERASE 命令处理前调用：
 * 1. hold CM4，防止双核 Flash 争用
 * 2. SetLongOp(1)，看门狗任务只监控 W5500 槽位，忽略已挂起的 LVGL/Modbus 等
 */
void bsp_ota_port_enter(void)
{
    bsp_ota_port_hold_cm4();
    BSP_Watchdog_SetLongOp(1U);
}

/**
 * @brief  退出 OTA 端口模式（恢复正常看门狗策略）
 *
 * VERIFY 成功、ABORT 或 Flash 失败 abort 时调用，恢复全任务心跳监控。
 * 注意：CM4 hold 状态保持至下次复位或 Boot 配置，本函数不恢复 CM4 启动。
 */
void bsp_ota_port_exit(void)
{
    BSP_Watchdog_SetLongOp(0U);
}

