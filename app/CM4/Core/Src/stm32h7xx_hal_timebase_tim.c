/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    stm32h7xx_hal_timebase_tim.c
  * @brief   CM4 HAL time base based on the hardware TIM7.
  *
  *          CM4 core runs FreeRTOS, which owns SysTick. To keep HAL_GetTick()
  *          working independently, the HAL time base is routed to TIM7
  *          (TIM6 is used by the CM7 core for the same purpose).
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "stm32h7xx_hal.h"
#include "stm32h7xx_hal_tim.h"

/* Private variables ---------------------------------------------------------*/
TIM_HandleTypeDef        htim7;

/**
  * @brief  This function configures the TIM7 as a time base source.
  *         The time source is configured to have 1ms time base with a dedicated
  *         Tick interrupt priority.
  * @param  TickPriority: Tick interrupt priority.
  * @retval HAL status
  */
HAL_StatusTypeDef HAL_InitTick(uint32_t TickPriority)
{
  RCC_ClkInitTypeDef    clkconfig;
  uint32_t              uwTimclock, uwAPB1Prescaler;
  uint32_t              uwPrescalerValue;
  uint32_t              pFLatency;

  /* Configure the TIM7 IRQ priority */
  if (TickPriority < (1UL << __NVIC_PRIO_BITS))
  {
    HAL_NVIC_SetPriority(TIM7_IRQn, TickPriority, 0);
    HAL_NVIC_EnableIRQ(TIM7_IRQn);
    uwTickPrio = TickPriority;
  }
  else
  {
    return HAL_ERROR;
  }

  /* Enable TIM7 clock */
  __HAL_RCC_TIM7_CLK_ENABLE();

  /* Get clock configuration */
  HAL_RCC_GetClockConfig(&clkconfig, &pFLatency);

  /* Get APB1 prescaler */
  uwAPB1Prescaler = clkconfig.APB1CLKDivider;

  /* Compute TIM7 clock */
  if (uwAPB1Prescaler == RCC_HCLK_DIV1)
  {
    uwTimclock = HAL_RCC_GetPCLK1Freq();
  }
  else
  {
    uwTimclock = 2UL * HAL_RCC_GetPCLK1Freq();
  }

  /* Compute the prescaler value to have TIM7 counter clock equal to 1MHz */
  uwPrescalerValue = (uint32_t) ((uwTimclock / 1000000U) - 1U);

  /* Initialize TIM7 */
  htim7.Instance = TIM7;
  htim7.Init.Period = (1000000U / 1000U) - 1U;
  htim7.Init.Prescaler = uwPrescalerValue;
  htim7.Init.ClockDivision = 0;
  htim7.Init.CounterMode = TIM_COUNTERMODE_UP;

  if (HAL_TIM_Base_Init(&htim7) == HAL_OK)
  {
    return HAL_TIM_Base_Start_IT(&htim7);
  }

  return HAL_ERROR;
}

/**
  * @brief  Suspend Tick increment.
  */
void HAL_SuspendTick(void)
{
  __HAL_TIM_DISABLE_IT(&htim7, TIM_IT_UPDATE);
}

/**
  * @brief  Resume Tick increment.
  */
void HAL_ResumeTick(void)
{
  __HAL_TIM_ENABLE_IT(&htim7, TIM_IT_UPDATE);
}

/**
  * @brief  Period elapsed callback in non blocking mode.
  * @param  htim : TIM handle
  */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  if (htim->Instance == TIM7)
  {
    HAL_IncTick();
  }
}
