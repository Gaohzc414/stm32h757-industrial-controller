/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : freertos.c
  * Description        : Code for freertos applications
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "FreeRTOS.h"
#include "cmsis_os2.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "bsp_log_conf.h"
#include "bsp_log.h"
#include "lvgl_demo.h"
#include "bsp_modbus_app.h"
#include "bsp_usb_modbus.h"
#include "dht11.h"
#include "bsp_adc.h"
#include "bsp_watchdog.h"
#include "bsp_app_data.h"
#include "bsp_w5500_app.h"
#include "bsp_ota_app.h"
#include "bsp_ota_coop.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN Variables */

/* USER CODE END Variables */
/* Definitions for defaultTask */
osThreadId_t defaultTaskHandle;
const osThreadAttr_t defaultTask_attributes = {
  .name = "defaultTask",
  .stack_size = 512 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */

/* USER CODE END FunctionPrototypes */

void StartDefaultTask(void *argument);

extern void MX_USB_DEVICE_Init(void);
void MX_FREERTOS_Init(void); /* (MISRA C 2004 rule 8.1) */

/**
  * @brief  FreeRTOS initialization
  * @param  None
  * @retval None
  */
void MX_FREERTOS_Init(void) {
  /* USER CODE BEGIN Init */
  bsp_log_rtos_init();
  BSP_AppData_RtosInit();
  lvgl_app_init();
  lvgl_start_task();
  /* USER CODE END Init */

  /* USER CODE BEGIN RTOS_MUTEX */
  /* add mutexes, ... */
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* add semaphores, ... */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* USER CODE BEGIN RTOS_QUEUES */
  /* add queues, ... */
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* creation of defaultTask */
  defaultTaskHandle = osThreadNew(StartDefaultTask, NULL, &defaultTask_attributes);

  /* USER CODE BEGIN RTOS_THREADS */
  BSP_Watchdog_Start();
  /* USER CODE END RTOS_THREADS */

  /* USER CODE BEGIN RTOS_EVENTS */
  /* add events, ... */
  /* USER CODE END RTOS_EVENTS */

}

/* USER CODE BEGIN Header_StartDefaultTask */
/**
 * @brief  默认任务：启动 USB/Modbus、传感器与 OTA 子系统并周期性喂狗
 * @param  argument  未使用
 *
 * 启动顺序要点：
 *   - 先起 W5500（建以太网 + OTA TCP），再 BSP_Ota_Start（SOIP 收发任务）
 *   - bsp_ota_coop_register：OTA 擦写 Flash 时挂起本任务等 peer，避免并发访问
 *   - 主循环仅喂狗；业务在各自任务中运行
 */
/* USER CODE END Header_StartDefaultTask */
void StartDefaultTask(void *argument)
{
  /* init code for USB_DEVICE */
  MX_USB_DEVICE_Init();
  /* USER CODE BEGIN StartDefaultTask */
  BSP_Modbus_Data_Init();
  dht11_app_start();
  bsp_adc_app_start();
  /* 网络与 OTA：W5500 维护链路，OTA 任务解析 SOIP */
  bsp_w5500_app_start();
  BSP_Ota_Start();

  MX_USB_DEVICE_Init();
  osDelay(500);
  BSP_USB_Modbus_Init();
  BSP_Modbus_App_Start();

  /* 注册为 OTA 协作 peer：擦写期间可被挂起 */
  bsp_ota_coop_register(defaultTaskHandle);

  for (;;)
  {
    BSP_Watchdog_Kick(BSP_WDG_DEFAULT);
    osDelay(1000);
  }
  /* USER CODE END StartDefaultTask */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */

/* USER CODE END Application */

