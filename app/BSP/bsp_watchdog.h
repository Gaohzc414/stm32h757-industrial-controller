/**
 * @file bsp_watchdog.h
 * @brief 独立看门狗（IWDG）任务健康监控接口
 */
#ifndef BSP_WATCHDOG_H
#define BSP_WATCHDOG_H

#include <stdint.h>

typedef enum {
    BSP_WDG_LVGL = 0,
    BSP_WDG_DEFAULT,
    BSP_WDG_MODBUS,
    BSP_WDG_ADC,
    BSP_WDG_CM4,
    BSP_WDG_W5500,
    BSP_WDG_TASK_COUNT
} bsp_wdg_task_t;

/**
 * @brief  记录并打印 MCU 复位原因
 * @retval 无
 */
void BSP_Watchdog_LogResetCause(void);

/**
 * @brief  创建看门狗喂狗任务
 * @retval 无
 */
void BSP_Watchdog_Start(void);

/**
 * @brief  任务喂狗（递增心跳计数）
 * @param  task  任务槽位编号
 * @retval 无
 */
void BSP_Watchdog_Kick(bsp_wdg_task_t task);

/**
 * @brief  进入/退出长时间操作（内部为引用计数）
 * @param  active  非 0 表示进入长操作，0 表示退出
 *
 * OTA / W5500 / ESP8266 等长操作期间用 active!=0 进入、active==0 退出。
 * 内部采用引用计数：多个子系统并发持有时互不覆盖，只要计数 > 0，
 * 看门狗就仅监控 W5500 槽位、忽略其余被挂起的任务。
 */
void BSP_Watchdog_SetLongOp(uint8_t active);

#endif /* BSP_WATCHDOG_H */
