/**
 * @file    dht11_conf.h
 * @brief   DHT11 硬件引脚与采样周期配置
 */
#ifndef DHT11_CONF_H
#define DHT11_CONF_H

/* Single-wire data pin: PD12. */
#define DHT11_GPIO_PORT              GPIOD
#define DHT11_GPIO_PIN               GPIO_PIN_12
#define DHT11_GPIO_CLK_ENABLE()      __HAL_RCC_GPIOD_CLK_ENABLE()

/* Minimum interval between two reads. */
#define DHT11_SAMPLE_INTERVAL_MS     2000U

#endif /* DHT11_CONF_H */
