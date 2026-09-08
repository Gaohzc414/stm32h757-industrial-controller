/**
 * @file    dht11.c
 * @brief   CM4 DHT11 单总线驱动与采样任务
 */
#include "dht11_conf.h"
#include "dht11.h"

#include "main.h"
#include "cmsis_os2.h"

#include "sensor_shared.h"

#define DHT11_TASK_STACK_WORDS       512U

static void dht11_delay_us(uint32_t us)
{
    uint32_t start = DWT->CYCCNT;
    uint32_t ticks = us * (SystemCoreClock / 1000000U);

    while ((DWT->CYCCNT - start) < ticks) {
    }
}

static void dht11_pin_output(void)
{
    GPIO_InitTypeDef gpio = {0};

    gpio.Pin = DHT11_GPIO_PIN;
    gpio.Mode = GPIO_MODE_OUTPUT_OD;
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(DHT11_GPIO_PORT, &gpio);
}

static void dht11_pin_input(void)
{
    GPIO_InitTypeDef gpio = {0};

    gpio.Pin = DHT11_GPIO_PIN;
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(DHT11_GPIO_PORT, &gpio);
}

static void dht11_line_high(void)
{
    HAL_GPIO_WritePin(DHT11_GPIO_PORT, DHT11_GPIO_PIN, GPIO_PIN_SET);
}

static void dht11_line_low(void)
{
    HAL_GPIO_WritePin(DHT11_GPIO_PORT, DHT11_GPIO_PIN, GPIO_PIN_RESET);
}

static GPIO_PinState dht11_line_read(void)
{
    return HAL_GPIO_ReadPin(DHT11_GPIO_PORT, DHT11_GPIO_PIN);
}

static int dht11_wait_level(GPIO_PinState level, uint32_t timeout_us)
{
    while (timeout_us > 0U) {
        if (dht11_line_read() == level) {
            return 0;
        }

        dht11_delay_us(1U);
        timeout_us--;
    }

    return -1;
}

static int dht11_read_byte(void)
{
    uint8_t value = 0U;

    for (uint32_t bit = 0U; bit < 8U; bit++) {
        if (dht11_wait_level(GPIO_PIN_SET, 100U) != 0) {
            return -1;
        }

        dht11_delay_us(40U);

        value = (uint8_t)(value << 1);
        if (dht11_line_read() == GPIO_PIN_SET) {
            value |= 1U;
        }

        if (dht11_wait_level(GPIO_PIN_RESET, 100U) != 0) {
            return -1;
        }
    }

    return (int)value;
}

void dht11_init(void)
{
    DHT11_GPIO_CLK_ENABLE();

    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0U;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

    dht11_pin_output();
    dht11_line_high();
}

int dht11_read(dht11_data_t *data)
{
    uint8_t raw[5];
    uint8_t checksum;
    uint32_t irq_mask;

    if (data == NULL) {
        return -1;
    }

    irq_mask = __get_PRIMASK();
    __disable_irq();

    dht11_pin_output();
    dht11_line_low();
    dht11_delay_us(20000U);
    dht11_line_high();
    dht11_delay_us(30U);
    dht11_pin_input();

    if (dht11_wait_level(GPIO_PIN_RESET, 100U) != 0) {
        __set_PRIMASK(irq_mask);
        return -1;
    }

    if (dht11_wait_level(GPIO_PIN_SET, 100U) != 0) {
        __set_PRIMASK(irq_mask);
        return -1;
    }

    if (dht11_wait_level(GPIO_PIN_RESET, 100U) != 0) {
        __set_PRIMASK(irq_mask);
        return -1;
    }

    for (uint32_t i = 0U; i < 5U; i++) {
        int byte = dht11_read_byte();
        if (byte < 0) {
            __set_PRIMASK(irq_mask);
            return -1;
        }
        raw[i] = (uint8_t)byte;
    }

    __set_PRIMASK(irq_mask);

    checksum = (uint8_t)(raw[0] + raw[1] + raw[2] + raw[3]);
    if (checksum != raw[4]) {
        return -1;
    }

    data->humidity = raw[0];
    data->humidity_dec = raw[1];
    data->temperature = raw[2];
    data->temperature_dec = raw[3];

    dht11_pin_output();
    dht11_line_high();

    return 0;
}

static void DHT11Task(void *argument)
{
    dht11_data_t sample;
    sensor_shared_data_t shared = {
        .magic = SENSOR_SHARED_MAGIC,
        .valid = 0U,
        .temperature = 0.0f,
        .humidity = 0.0f,
        .timestamp = 0U,
    };

    (void)argument;

    dht11_init();

    for (;;) {
        if (dht11_read(&sample) == 0) {
            shared.valid = 1U;
            shared.temperature = (float)sample.temperature;
            shared.humidity = (float)sample.humidity;
            shared.timestamp = HAL_GetTick();
        } else {
            shared.valid = 0U;
            shared.timestamp = HAL_GetTick();
        }

        (void)sensor_shared_publish(&shared);
        osDelay(DHT11_SAMPLE_INTERVAL_MS);
    }
}

void dht11_app_start(void)
{
    const osThreadAttr_t attr = {
        .name = "dht11",
        .stack_size = DHT11_TASK_STACK_WORDS * 4U,
        .priority = (osPriority_t)osPriorityNormal,
    };

    (void)osThreadNew(DHT11Task, NULL, &attr);
}
