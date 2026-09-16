/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : freertos.c
  * Description        : Code for freertos applications
  ******************************************************************************
  */
/* USER CODE END Header */

#include "FreeRTOS.h"
#include "task.h"
#include "cmsis_os2.h"

#include "dht11.h"
#include "sensor_shared.h"

void MX_FREERTOS_Init(void)
{
    /* 只武装 CM4->CM7 事件边沿；共享数据块由 CM7 在放行本核之前初始化 */
    sensor_ipc_boot();
    dht11_app_start();
}

