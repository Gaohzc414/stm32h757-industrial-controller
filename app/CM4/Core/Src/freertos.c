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
    sensor_ipc_init(NULL);
    dht11_app_start();
}

