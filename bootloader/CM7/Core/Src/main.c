/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
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
#include "main.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdio.h>
#include "boot_config.h"
#include "boot_jump.h"
#include "boot_meta.h"
#include "boot_slot.h"
#include "boot_slot_copy.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

#ifndef HSEM_ID_0
#define HSEM_ID_0 (0U) /* HW semaphore 0*/
#endif

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{
  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */
/* USER CODE BEGIN Boot_Mode_Sequence_0 */
  int32_t timeout;
/* USER CODE END Boot_Mode_Sequence_0 */

/* USER CODE BEGIN Boot_Mode_Sequence_1 */
  /* 等待 CM4 进入 STOP；超时仍继续，避免未烧 CM4 时 Bootloader 卡死 */
  timeout = 0xFFFF;
  while((__HAL_RCC_GET_FLAG(RCC_FLAG_D2CKRDY) != RESET) && (timeout-- > 0));
  if ( timeout < 0 )
  {
    /* CM4 未进 STOP，IAP 跳转前由 Boot_HoldCm4() 拉住 CM4 */
  }
/* USER CODE END Boot_Mode_Sequence_1 */
  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();
/* USER CODE BEGIN Boot_Mode_Sequence_2 */
  /* Bootloader 不在此处唤醒 CM4；由 APP �? Boot_Mode_Sequence_2 负责 */
/* USER CODE END Boot_Mode_Sequence_2 */

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_USART1_UART_Init();
  /* USER CODE BEGIN 2 */
  boot_meta_t meta;

  boot_meta_hw_init();

  printf("\r\n=== STM32H7 Bootloader (CM7) ===\r\n");
  printf("Tag: %s\r\n", BOOT_FW_TAG);
  printf("USART1 115200, PA9/PA10\r\n");

  {
    boot_meta_t meta_dbg;
    uint8_t slot_a_ok = boot_slot_is_valid(CM7_APP_SLOT_A_INDEX);
    uint8_t slot_b_ok = boot_slot_is_valid(CM7_APP_SLOT_B_INDEX);

    (void)boot_meta_load(&meta_dbg);
    printf("[BOOT] meta: active=%u pending=%u trial=%u flags=0x%02X\r\n",
           (unsigned int)meta_dbg.active_slot,
           (unsigned int)meta_dbg.pending_slot,
           (unsigned int)meta_dbg.boot_trial,
           (unsigned int)meta_dbg.flags);
    printf("[BOOT] slot A(0x%08lX) valid=%u, slot B(0x%08lX) valid=%u\r\n",
           (unsigned long)CM7_APP_SLOT_A_BASE,
           (unsigned int)slot_a_ok,
           (unsigned long)CM7_APP_SLOT_B_BASE,
           (unsigned int)slot_b_ok);
  }

  (void)boot_meta_load(&meta);

  if (((meta.flags & BOOT_META_FLAG_PENDING) != 0U) &&
      (meta.pending_slot == CM7_APP_SLOT_B_INDEX) &&
      (boot_slot_is_valid(CM7_APP_SLOT_B_INDEX) != 0)) {
    printf("[BOOT] pending OTA in slot B, copying to slot A...\r\n");
    if (boot_slot_copy_to_run_slot(CM7_APP_SLOT_B_INDEX) != 0) {
      printf("[BOOT] copy B->A failed, clear pending\r\n");
      meta.flags &= (uint8_t)~BOOT_META_FLAG_PENDING;
      meta.boot_trial = 0U;
      (void)boot_meta_save(&meta);
    } else {
      printf("[BOOT] copy B->A OK\r\n");
    }
  }

  if (Boot_IsAppValid())
  {
    uint32_t app_base = CM7_APP_SLOT_A_BASE;
    uint32_t sp = *(__IO uint32_t *)app_base;
    uint32_t pc = *(__IO uint32_t *)(app_base + 4U);

    if ((meta.flags & BOOT_META_FLAG_PENDING) != 0U) {
      if (meta.boot_trial > 0U) {
        meta.boot_trial--;
      }
      if (meta.boot_trial == 0U) {
        meta.flags &= (uint8_t)~BOOT_META_FLAG_PENDING;
      }
      (void)boot_meta_save(&meta);
      printf("[BOOT] trial left=%u\r\n", (unsigned int)meta.boot_trial);
    }

    printf("Valid APP @ 0x%08lX, jumping...\r\n", (unsigned long)app_base);
    printf("[BOOT] vec SP=0x%08lX Reset=0x%08lX\r\n", (unsigned long)sp, (unsigned long)pc);
    printf("[BOOT] D2CKRDY=%u (1=CM4 domain active)\r\n",
           (unsigned int)(__HAL_RCC_GET_FLAG(RCC_FLAG_D2CKRDY) != RESET));
    printf("[BOOT] hold CM4 reset, then jump...\r\n");
    Boot_JumpToApp();
    printf("[BOOT] ERROR: Jump returned (should never happen)\r\n");
  }
  else
  {
    uint32_t sp = *(__IO uint32_t *)CM7_APP_SLOT_A_BASE;
    uint32_t pc = *(__IO uint32_t *)(CM7_APP_SLOT_A_BASE + 4U);
    printf("No valid APP @ 0x%08lX, stay in Boot.\r\n", (unsigned long)CM7_APP_SLOT_A_BASE);
    printf("  vec: SP=0x%08lX Reset=0x%08lX\r\n", (unsigned long)sp, (unsigned long)pc);
  }
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    HAL_Delay(1000);
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Supply configuration update enable
  */
  HAL_PWREx_ConfigSupply(PWR_LDO_SUPPLY);

  /** Configure the main internal regulator output voltage
  */
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  while(!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY)) {}

  __HAL_RCC_SYSCFG_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE0);

  while(!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY)) {}

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_DIV1;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL.PLLM = 4;
  RCC_OscInitStruct.PLL.PLLN = 60;
  RCC_OscInitStruct.PLL.PLLP = 2;
  RCC_OscInitStruct.PLL.PLLQ = 2;
  RCC_OscInitStruct.PLL.PLLR = 2;
  RCC_OscInitStruct.PLL.PLLRGE = RCC_PLL1VCIRANGE_3;
  RCC_OscInitStruct.PLL.PLLVCOSEL = RCC_PLL1VCOWIDE;
  RCC_OscInitStruct.PLL.PLLFRACN = 0;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2
                              |RCC_CLOCKTYPE_D3PCLK1|RCC_CLOCKTYPE_D1PCLK1;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.SYSCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB3CLKDivider = RCC_APB3_DIV2;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_APB1_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_APB2_DIV2;
  RCC_ClkInitStruct.APB4CLKDivider = RCC_APB4_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}

#ifdef  USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
