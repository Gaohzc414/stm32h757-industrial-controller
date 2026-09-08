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

void sensor_ipc_init(osThreadId_t notify_task);
int sensor_shared_publish(const sensor_shared_data_t *data);
int sensor_shared_read(sensor_shared_data_t *data);
int sensor_ipc_read(sensor_shared_data_t *data);
int Sensor_IPC_Get(sensor_shared_data_t *data);

#ifdef __cplusplus
}
#endif

#endif /* SENSOR_SHARED_H */
