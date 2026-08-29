/**
 * @file    bsp_adc.c
 * @brief   ADC 采样应用层实现，周期性读取电压并通过 Modbus/共享数据上报
 */
#include "bsp_adc.h"

#include "adc.h"
#include "bsp_app_data.h"
#include "bsp_modbus_app.h"
#include "bsp_watchdog.h"
#include "bsp_ota_coop.h"
#include "cmsis_os.h"

/** ADC 任务栈大小（字） */
#define BSP_ADC_TASK_STACK_WORDS 512U
/** ADC 采样周期（毫秒） */
#define BSP_ADC_SAMPLE_MS        500U
/** ADC 参考电压（毫伏） */
#define BSP_ADC_VREF_MV          3300U
/** ADC 满量程计数值（16 位） */
#define BSP_ADC_FULL_SCALE       65535U
/** 单次上报前的平均采样次数 */
#define BSP_ADC_AVG_SAMPLES      16U
/** EMA 滤波权重移位：新值权重 = 1 / (1 << SHIFT)，此处为 1/8 */
#define BSP_ADC_EMA_SHIFT        3U

/** ADC 是否已完成校准 */
static int bsp_adc_calibrated;

/**
 * @brief  执行一次 ADC 校准（仅首次调用时生效）
 */
static void bsp_adc_calibrate_once(void)
{
    if (bsp_adc_calibrated != 0) {
        return;
    }

    /* H7 ADC 上电后需做一次 offset 校准，消除内部偏置误差 */
    if (HAL_ADCEx_Calibration_Start(&hadc1, ADC_CALIB_OFFSET,
                                    ADC_SINGLE_ENDED) == HAL_OK) {
        bsp_adc_calibrated = 1;
    }
}

/**
 * @brief  读取单次 ADC 原始采样值
 * @retval 原始 ADC 计数值，失败返回 0
 */
static uint16_t bsp_adc_read_raw(void)
{
    uint32_t raw;

    /* 单次转换：Start -> Poll -> GetValue -> Stop */
    if (HAL_ADC_Start(&hadc1) != HAL_OK) {
        return 0U;
    }

    if (HAL_ADC_PollForConversion(&hadc1, 50U) != HAL_OK) {
        (void)HAL_ADC_Stop(&hadc1);
        return 0U;
    }

    raw = HAL_ADC_GetValue(&hadc1);
    (void)HAL_ADC_Stop(&hadc1);

    return (uint16_t)raw;
}

/**
 * @brief  多次采样取平均，降低噪声
 * @retval 平均后的原始 ADC 计数值
 */
static uint16_t bsp_adc_read_raw_avg(void)
{
    uint32_t sum = 0U;
    uint32_t i;

    for (i = 0U; i < BSP_ADC_AVG_SAMPLES; i++) {
        sum += bsp_adc_read_raw();
        /* 16 次采样耗时较长，每 4 次喂一次狗防止 ADC 任务被判死 */
        if ((i & 3U) == 3U) {
            BSP_Watchdog_Kick(BSP_WDG_ADC);
        }
    }

    return (uint16_t)(sum / BSP_ADC_AVG_SAMPLES);
}

/**
 * @brief  将原始 ADC 计数值转换为毫伏
 * @param  raw 原始计数值
 * @retval 电压值（毫伏）
 */
static uint16_t bsp_adc_raw_to_mv(uint16_t raw)
{
    /* 线性换算：mV = raw * Vref / 65535，+半量程做四舍五入 */
    return (uint16_t)(((uint32_t)raw * BSP_ADC_VREF_MV + (BSP_ADC_FULL_SCALE / 2U)) /
                      BSP_ADC_FULL_SCALE);
}

/**
 * @brief  对毫伏值做指数移动平均滤波
 * @param  mv 当前采样毫伏值
 * @retval 滤波后的毫伏值
 */
static uint16_t bsp_adc_filter_mv(uint16_t mv)
{
    static uint16_t ema_mv;
    static uint8_t ema_ready;
    uint32_t blend;

    if (ema_ready == 0U) {
        /* 首样本直接作为 EMA 初值，避免启动瞬态 */
        ema_mv = mv;
        ema_ready = 1U;
        return mv;
    }

    /* EMA: new = (7/8)*old + (1/8)*sample，SHIFT=3 即权重 1/8 */
    blend = ((uint32_t)ema_mv * ((1U << BSP_ADC_EMA_SHIFT) - 1U)) + (uint32_t)mv;
    ema_mv = (uint16_t)(blend >> BSP_ADC_EMA_SHIFT);
    return ema_mv;
}

/**
 * @brief  ADC 采样 FreeRTOS 任务入口
 * @param  argument 任务参数（未使用）
 */
static void BspAdcTask(void *argument)
{
    uint16_t raw;
    uint16_t mv;

    (void)argument;

    osDelay(200U);  /* 等待 CubeMX ADC 与外设稳定 */
    bsp_adc_calibrate_once();

    for (;;) {
        BSP_Watchdog_Kick(BSP_WDG_ADC);
        raw = bsp_adc_read_raw_avg();
        mv = bsp_adc_filter_mv(bsp_adc_raw_to_mv(raw));
        /* 双写：共享内存供 LVGL 读，Modbus 映射供上位机读 */
        BSP_AppData_SetAdcMv(mv);
        BSP_Modbus_SetHoldingRegister(MODBUS_REG_ADC_MV, mv);
        osDelay(BSP_ADC_SAMPLE_MS);
    }
}

/**
 * @brief  创建并启动 ADC 采样 FreeRTOS 任务
 */
void bsp_adc_app_start(void)
{
    osThreadId_t task;
    const osThreadAttr_t attr = {
        .name = "adc",
        .stack_size = BSP_ADC_TASK_STACK_WORDS * 4U,
        .priority = (osPriority_t)osPriorityNormal,
    };

    task = osThreadNew(BspAdcTask, NULL, &attr);
    /* OTA 期间该任务会被 osThreadSuspend，避免与 Flash 擦写争 CPU */
    bsp_ota_coop_register(task);
}
