/**
 * @file    dht11_conf.h
 * @brief   CM4 DHT11 硬件引脚与采样周期配置
 */
#ifndef CM4_DHT11_CONF_H
#define CM4_DHT11_CONF_H

#include "main.h"

#define DHT11_GPIO_PORT              GPIOD
#define DHT11_GPIO_PIN               GPIO_PIN_12
#define DHT11_GPIO_CLK_ENABLE()      __HAL_RCC_GPIOD_CLK_ENABLE()

#define DHT11_SAMPLE_INTERVAL_MS     2000U

#endif /* CM4_DHT11_CONF_H */
