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

#if defined(CORE_CM7)
/* Only CM7 initialises the shared block (it boots first and owns the reset);
 * CM4 is a pure producer and must never clear data the consumer may still want. */
static void sensor_shared_reset(void)
{
    memset((void *)&s_sensor_data, 0, sizeof(s_sensor_data));
    s_sensor_data.magic = SENSOR_SHARED_MAGIC;
}
#endif

static int sensor_shared_take_lock(uint32_t sem_id)
{
    while (HAL_HSEM_Take(sem_id, 0U) != HAL_OK) {
        osDelay(1U);
    }

    return 0;
}

void sensor_ipc_boot(void)
{
#if defined(CORE_CM7)
    /* CM7 boots first and releases CM4 only afterwards, so resetting the shared
     * block here is provably ordered before CM4 can publish anything.  Doing it
     * later (from MX_FREERTOS_Init) let CM4's first publish land first and then
     * be wiped by this memset, which is what made the display show zeros. */
    sensor_shared_reset();

    /* Drop any release flag left over from the previous run: arming below must
     * not immediately re-raise a stale CM4->CM7 event.  Without this, CM7 would
     * consume one bogus notification whose payload is the just-cleared block. */
    __HAL_HSEM_CLEAR_FLAG(SENSOR_SHARED_EVENT_HSEM_MASK);
    HAL_HSEM_ActivateNotification(SENSOR_SHARED_EVENT_HSEM_MASK);
#else
    /* CM4 only arms the event edge.  The shared block belongs to CM7, which
     * initialises it before releasing this core. */
    sensor_shared_take_lock(SENSOR_SHARED_EVENT_HSEM_ID);
#endif
}

void sensor_ipc_attach(osThreadId_t notify_task)
{
#if defined(CORE_CM7)
    s_notify_task = (TaskHandle_t)notify_task;
#else
    (void)notify_task;
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
