/**
 * @file    sensor_shared.c
 * @brief   CM4/CM7 共享传感器数据与 HSEM 通知
 */
#include "sensor_shared.h"

#include <string.h>

#include "main.h"

#include "cmsis_os.h"

#if defined(CORE_CM7)
#include "FreeRTOS.h"
#include "task.h"
#endif

static sensor_shared_data_t s_sensor_data __attribute__((section(".shared_data"), aligned(32)));

#if defined(CORE_CM7)
static TaskHandle_t s_notify_task;
#endif

static void sensor_shared_reset(void)
{
    memset((void *)&s_sensor_data, 0, sizeof(s_sensor_data));
    s_sensor_data.magic = SENSOR_SHARED_MAGIC;
}

static int sensor_shared_take_lock(uint32_t sem_id)
{
    while (HAL_HSEM_Take(sem_id, 0U) != HAL_OK) {
        osDelay(1U);
    }

    return 0;
}

void sensor_ipc_init(osThreadId_t notify_task)
{
    sensor_shared_reset();

#if defined(CORE_CM7)
    s_notify_task = (TaskHandle_t)notify_task;
    HAL_HSEM_ActivateNotification(SENSOR_SHARED_EVENT_HSEM_MASK);
#else
    (void)notify_task;
    sensor_shared_take_lock(SENSOR_SHARED_EVENT_HSEM_ID);
#endif
}

int sensor_shared_publish(const sensor_shared_data_t *data)
{
    if (data == NULL) {
        return -1;
    }

    sensor_shared_take_lock(SENSOR_SHARED_DATA_HSEM_ID);
    s_sensor_data = *data;
    if (s_sensor_data.magic != SENSOR_SHARED_MAGIC) {
        s_sensor_data.magic = SENSOR_SHARED_MAGIC;
    }
    HAL_HSEM_Release(SENSOR_SHARED_DATA_HSEM_ID, 0U);

#if defined(CORE_CM4)
    HAL_HSEM_Release(SENSOR_SHARED_EVENT_HSEM_ID, 0U);
    sensor_shared_take_lock(SENSOR_SHARED_EVENT_HSEM_ID);
#endif

    return 0;
}

int sensor_shared_read(sensor_shared_data_t *data)
{
    if (data == NULL) {
        return -1;
    }

    if (HAL_HSEM_Take(SENSOR_SHARED_DATA_HSEM_ID, 0U) != HAL_OK) {
        return -1;
    }

    *data = s_sensor_data;
    HAL_HSEM_Release(SENSOR_SHARED_DATA_HSEM_ID, 0U);

    return 0;
}

int sensor_ipc_read(sensor_shared_data_t *data)
{
    return sensor_shared_read(data);
}

int Sensor_IPC_Get(sensor_shared_data_t *data)
{
    return sensor_ipc_read(data);
}

#if defined(CORE_CM7)
void HAL_HSEM_FreeCallback(uint32_t SemMask)
{
    BaseType_t higher_priority_task_woken = pdFALSE;

    if ((SemMask & SENSOR_SHARED_EVENT_HSEM_MASK) != 0U) {
        /* Re-arm the notification. HAL_HSEM_IRQHandler() clears the IER bit
         * before invoking this callback, so without re-arming the CM4->CM7
         * event notification fires only once and every later publish is lost. */
        HAL_HSEM_ActivateNotification(SENSOR_SHARED_EVENT_HSEM_MASK);

        if (s_notify_task != NULL) {
            vTaskNotifyGiveFromISR(s_notify_task, &higher_priority_task_woken);
        }
    }

    portYIELD_FROM_ISR(higher_priority_task_woken);
}
#endif
