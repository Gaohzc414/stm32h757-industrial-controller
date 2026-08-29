/**
 * @file    bsp_adc.h
 * @brief   ADC 采样应用层（FreeRTOS 周期任务）
 */
#ifndef BSP_ADC_H
#define BSP_ADC_H

/**
 * @brief  创建并启动 ADC 采样 FreeRTOS 任务
 */
void bsp_adc_app_start(void);

#endif
