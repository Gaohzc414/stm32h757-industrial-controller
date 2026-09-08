/**
 * @file    dht11.h
 * @brief   CM4 DHT11 温湿度传感器接口
 */
#ifndef CM4_DHT11_H
#define CM4_DHT11_H

#include <stdint.h>

typedef struct
{
    uint8_t humidity;
    uint8_t humidity_dec;
    uint8_t temperature;
    uint8_t temperature_dec;
} dht11_data_t;

void dht11_init(void);
int dht11_read(dht11_data_t *data);
void dht11_app_start(void);

#endif /* CM4_DHT11_H */
