/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    iwdg.c
  * @brief   This file provides code for the configuration
  *          of the IWDG instances.
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
#include "iwdg.h"

/* USER CODE BEGIN 0 */
#include "bsp_log.h"
/* USER CODE END 0 */

IWDG_HandleTypeDef hiwdg1;

/* IWDG1 init function */
void MX_IWDG1_Init(void)
{

  /* USER CODE BEGIN IWDG1_Init 0 */

  /* USER CODE END IWDG1_Init 0 */

  /* USER CODE BEGIN IWDG1_Init 1 */

  /* USER CODE END IWDG1_Init 1 */
  hiwdg1.Instance = IWDG1;
  hiwdg1.Init.Prescaler = IWDG_PRESCALER_128;
  /* Match working OTA_BUG config: WIN>=RL disables early-refresh window,
   * RL=2000 => ~8s timeout @ LSI 32k / PR128; feed every 2s is safe. */
  hiwdg1.Init.Window = 4095;
  hiwdg1.Init.Reload = 2000;
  if (HAL_IWDG_Init(&hiwdg1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN IWDG1_Init 2 */
  LOG_I("IWDG", "started PR=128 RL=%lu WIN=%lu (~%lu ms timeout @32k)",
        (unsigned long)hiwdg1.Init.Reload,
        (unsigned long)hiwdg1.Init.Window,
        (unsigned long)((128UL * hiwdg1.Init.Reload * 1000UL) / 32000UL));

  /* === IWDG 排障诊断：确认 LSI 是否起振、IWDG 是否已写入（排查“停止喂狗却不复位”）=== */
  {
    uint32_t csr = RCC->CSR;
    LOG_I("IWDG", "DIAG: RCC->CSR=0x%08lX  LSION=%lu  LSIRDY=%lu",
          (unsigned long)csr,
          (unsigned long)((csr & RCC_CSR_LSION) ? 1UL : 0UL),
          (unsigned long)((csr & RCC_CSR_LSIRDY) ? 1UL : 0UL));
    LOG_I("IWDG", "DIAG: IWDG SR=0x%08lX (PVU/RVU/WVU=%lu/%lu/%lu, 全 0 = 写入完成)",
          (unsigned long)IWDG1->SR,
          (unsigned long)((IWDG1->SR & IWDG_SR_PVU) ? 1UL : 0UL),
          (unsigned long)((IWDG1->SR & IWDG_SR_RVU) ? 1UL : 0UL),
          (unsigned long)((IWDG1->SR & IWDG_SR_WVU) ? 1UL : 0UL));
    LOG_I("IWDG", "DIAG: DBGMCU->APB4FZ1=0x%08lX  DBG_IWDG1_STOP=%lu (1=调试模式冻结 IWDG)",
          (unsigned long)DBGMCU->APB4FZ1,
          (unsigned long)((DBGMCU->APB4FZ1 & DBGMCU_APB4FZ1_DBG_IWDG1) ? 1UL : 0UL));
    {
      /* Cortex-M7 DHCSR：判断核心当前是否被调试器暂停（S_HALT=1 时 DBG 冻结才真正生效）*/
      uint32_t dhcsr = *(volatile uint32_t *)0xE000EDF0UL;
      LOG_I("IWDG", "DIAG: DHCSR=0x%08lX  C_DEBUGEN=%lu  S_HALT=%lu (S_HALT=1=核心被调试器暂停, IWDG 被冻结)",
            (unsigned long)dhcsr,
            (unsigned long)(dhcsr & 1UL),
            (unsigned long)((dhcsr >> 17U) & 1UL));
    }
  }
  /* USER CODE END IWDG1_Init 2 */

}

/* USER CODE BEGIN 1 */

/* USER CODE END 1 */

