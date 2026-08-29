/**
 * @file    dht11.h
 * @brief   DHT11 温湿度传感器驱动接口
 */
#ifndef DHT11_H
#define DHT11_H

#include <stdint.h>

/** @brief DHT11 一次读数结果 */
typedef struct {
    uint8_t humidity;
    uint8_t humidity_dec;
    uint8_t temperature;
    uint8_t temperature_dec;
} dht11_data_t;

/**
 * @brief  初始化 DHT11 GPIO 与 DWT 微秒延时
 */
void dht11_init(void);

/**
 * @brief  读取一次温湿度数据
 * @param  data  输出数据结构指针
 * @retval 0 成功；-1 失败（超时或校验错误）
 */
int dht11_read(dht11_data_t *data);

/**
 * @brief  创建并启动 DHT11 采样 FreeRTOS 任务
 */
void dht11_app_start(void);

#endif /* DHT11_H */
