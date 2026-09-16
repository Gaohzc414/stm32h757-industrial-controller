/**
 * @file    sensor_shared.h
 * @brief   CM4/CM7 共享传感器数据与 HSEM 通知接口
 */
#ifndef SENSOR_SHARED_H
#define SENSOR_SHARED_H

#include <stdint.h>

#include "cmsis_os2.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SENSOR_SHARED_MAGIC            (0x53484D31UL)
#define SENSOR_SHARED_DATA_HSEM_ID     (1U)
#define SENSOR_SHARED_EVENT_HSEM_ID    (2U)
#define SENSOR_SHARED_DATA_HSEM_MASK    (1UL << SENSOR_SHARED_DATA_HSEM_ID)
#define SENSOR_SHARED_EVENT_HSEM_MASK   (1UL << SENSOR_SHARED_EVENT_HSEM_ID)

typedef struct
{
    uint32_t magic;
    uint8_t valid;
    float temperature;
    float humidity;
    uint32_t timestamp;
} sensor_shared_data_t;

/**
 * @brief  初始化共享数据块并武装 CM4->CM7 事件通知
 *
 * 必须在放行对端内核之前调用：CM7 在 HAL_RCCEx_EnableBootCore() 之前调，
 * 此时 CM4 仍停在 STOP，因此这次 memset 可证明早于 CM4 的任何发布。
 * 放到 MX_FREERTOS_Init() 里做就晚了 —— CM4 的首次发布已经落盘，会被这里抹掉。
 */
void sensor_ipc_boot(void);

/**
 * @brief  挂上接收通知的任务句柄（仅 CM7 需要，且只赋值、不碰共享数据）
 * @param  notify_task  等待 CM4 事件的任务
 */
void sensor_ipc_attach(osThreadId_t notify_task);
int sensor_shared_publish(const sensor_shared_data_t *data);
int sensor_shared_read(sensor_shared_data_t *data);
int sensor_ipc_read(sensor_shared_data_t *data);
int Sensor_IPC_Get(sensor_shared_data_t *data);

#ifdef __cplusplus
}
#endif

#endif /* SENSOR_SHARED_H */
