/**
 * @file    dht11.c
 * @brief   DHT11 单总线温湿度驱动与 FreeRTOS 采样任务
 */
#include "dht11_conf.h"
#include "dht11.h"

#include "main.h"
#include "bsp_log_conf.h"
#include "bsp_log.h"
#include "bsp_app_data.h"
#include "bsp_modbus_app.h"
#include "bsp_watchdog.h"
#include "bsp_ota_coop.h"
#include "cmsis_os.h"

#define DHT11_TAG                    "DHT11"

#define DHT11_TASK_STACK_WORDS       512U

/**
 * @brief  微秒级 busy-wait 延时（基于 DWT 周期计数）
 * @param  us  延时微秒数
 */
static void dht11_delay_us(uint32_t us)
{
    uint32_t start;
    uint32_t ticks;

    start = DWT->CYCCNT;
    ticks = us * (SystemCoreClock / 1000000U);

    while ((DWT->CYCCNT - start) < ticks) {
    }
}

/**
 * @brief  将数据线配置为开漏输出
 */
static void dht11_pin_output(void)
{
    GPIO_InitTypeDef gpio = {0};

    gpio.Pin = DHT11_GPIO_PIN;
    gpio.Mode = GPIO_MODE_OUTPUT_OD;
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(DHT11_GPIO_PORT, &gpio);
}

/**
 * @brief  将数据线配置为输入
 */
static void dht11_pin_input(void)
{
    GPIO_InitTypeDef gpio = {0};

    gpio.Pin = DHT11_GPIO_PIN;
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(DHT11_GPIO_PORT, &gpio);
}

/**
 * @brief  数据线拉高
 */
static void dht11_line_high(void)
{
    HAL_GPIO_WritePin(DHT11_GPIO_PORT, DHT11_GPIO_PIN, GPIO_PIN_SET);
}

/**
 * @brief  数据线拉低
 */
static void dht11_line_low(void)
{
    HAL_GPIO_WritePin(DHT11_GPIO_PORT, DHT11_GPIO_PIN, GPIO_PIN_RESET);
}

/**
 * @brief  读取数据线电平
 * @retval GPIO 引脚状态
 */
static GPIO_PinState dht11_line_read(void)
{
    return HAL_GPIO_ReadPin(DHT11_GPIO_PORT, DHT11_GPIO_PIN);
}

/**
 * @brief  等待数据线达到指定电平
 * @param  level       目标电平
 * @param  timeout_us  超时微秒数
 * @retval 0 成功；-1 超时
 */
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

/**
 * @brief  从 DHT11 读取一个字节
 * @retval 0~255 字节值；-1 失败
 */
static int dht11_read_byte(void)
{
    uint8_t value = 0U;
    uint32_t bit;

    for (bit = 0U; bit < 8U; bit++) {
        /* 每位：等待上升沿 -> 延时 40us 判 0/1 -> 等待下降沿 */
        if (dht11_wait_level(GPIO_PIN_SET, 100U) != 0) {
            return -1;
        }

        /* 数据 0 约 26~28us 高电平，数据 1 约 70us 高电平，40us 采样点可区分 */
        dht11_delay_us(40U);

        if (dht11_line_read() == GPIO_PIN_SET) {
            value = (uint8_t)((value << 1) | 1U);
        } else {
            value = (uint8_t)(value << 1);
        }

        if (dht11_wait_level(GPIO_PIN_RESET, 100U) != 0) {
            return -1;
        }
    }

    return (int)value;
}

/**
 * @brief  初始化 DHT11 GPIO 与 DWT 微秒延时
 */
void dht11_init(void)
{
    DHT11_GPIO_CLK_ENABLE();

    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0U;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

    dht11_pin_output();
    dht11_line_high();
}

/**
 * @brief  读取一次温湿度数据
 * @param  data  输出数据结构指针
 * @retval 0 成功；-1 失败（超时或校验错误）
 */
int dht11_read(dht11_data_t *data)
{
    uint8_t raw[5];
    uint8_t checksum;
    uint32_t irq_mask;
    int byte;
    int i;

    if (data == NULL) {
        return -1;
    }

    irq_mask = __get_PRIMASK();
    __disable_irq();  /* 单总线时序严格，禁止中断打断微秒级采样 */

    /* 主机起始信号：拉低 ≥18ms，拉高 20~40us 后切换为输入 */
    dht11_pin_output();
    dht11_line_low();
    dht11_delay_us(20000U);
    dht11_line_high();
    dht11_delay_us(30U);
    dht11_pin_input();

    /* DHT11 应答：80us 低 + 80us 高 + 进入 40 位数据 */
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

    for (i = 0; i < 5; i++) {
        byte = dht11_read_byte();
        if (byte < 0) {
            __set_PRIMASK(irq_mask);
            return -1;
        }

        raw[i] = (uint8_t)byte;
    }

    __set_PRIMASK(irq_mask);

    checksum = (uint8_t)(raw[0] + raw[1] + raw[2] + raw[3]);
    if (checksum != raw[4]) {
        /* 5 字节校验和不符，丢弃本次采样 */
        return -1;
    }

    /* raw[0..1] 湿度整数/小数，raw[2..3] 温度整数/小数（本驱动仅用整数部分） */
    data->humidity = raw[0];
    data->humidity_dec = raw[1];
    data->temperature = raw[2];
    data->temperature_dec = raw[3];

    dht11_pin_output();
    dht11_line_high();

    return 0;
}

/**
 * @brief  DHT11 周期采样 FreeRTOS 任务
 * @param  argument  未使用
 */
static void DHT11Task(void *argument)
{
    dht11_data_t sample;
    uint16_t humidity;
    uint16_t temperature;

    (void)argument;

    osDelay(1000U);

    for (;;) {
        BSP_Watchdog_Kick(BSP_WDG_DHT11);
        if (dht11_read(&sample) == 0) {
            humidity = sample.humidity;
            temperature = sample.temperature;

            BSP_AppData_SetHumidity(humidity);
            BSP_AppData_SetTemperature(temperature);
            BSP_Modbus_SetHoldingRegister(MODBUS_REG_HUMIDITY, humidity);
            BSP_Modbus_SetHoldingRegister(MODBUS_REG_TEMPERATURE, temperature);

            LOG_I(DHT11_TAG, "humidity=%u.%u%%, temperature=%u.%uC",
                  sample.humidity, sample.humidity_dec,
                  sample.temperature, sample.temperature_dec);
        } else {
            LOG_W(DHT11_TAG, "read failed");
        }

        osDelay(DHT11_SAMPLE_INTERVAL_MS);
    }
}

/**
 * @brief  创建并启动 DHT11 采样 FreeRTOS 任务
 */
void dht11_app_start(void)
{
    osThreadId_t task;
    const osThreadAttr_t attr = {
        .name = "dht11",
        .stack_size = DHT11_TASK_STACK_WORDS * 4U,
        .priority = (osPriority_t)osPriorityNormal,
    };

    task = osThreadNew(DHT11Task, NULL, &attr);
    bsp_ota_coop_register(task);
}
