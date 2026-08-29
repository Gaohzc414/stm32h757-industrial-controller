/**
 * @file    lvgl_demo.c
 * @brief   LVGL 机器仪表盘应用（800x480 + 触摸）
 */
#include "lvgl_demo.h"
#include "lvgl.h"
#include "lv_port_disp.h"
#include "lv_port_indev.h"
#include "bsp_app_data.h"
#include "bsp_modbus_app.h"
#include "FreeRTOS.h"
#include "task.h"
#include "cmsis_os.h"
#include "bsp_watchdog.h"
#include "bsp_ota_coop.h"

/** LVGL 任务优先级 */
#define LVGL_TASK_PRIO      (osPriorityAboveNormal)
/** LVGL 任务栈大小（字节） */
#define LVGL_TASK_STACK     (4096U * 4U)

/** 仪表盘数据刷新周期（毫秒） */
#define DASH_REFRESH_MS     500U

/** 温度圆弧最大值（摄氏度） */
#define TEMP_ARC_MAX_C      50U
/** 湿度进度条最大值（%） */
#define HUMI_BAR_MAX        100U
/** 电压进度条最大值（毫伏） */
#define VOLT_BAR_MAX_MV     3300U

/** 仪表盘 UI 控件集合 */
typedef struct
{
    lv_obj_t *label_temp;   /**< 温度数值标签 */
    lv_obj_t *label_humi;   /**< 湿度数值标签 */
    lv_obj_t *label_volt;   /**< 电压数值标签 */
    lv_obj_t *arc_temp;     /**< 温度圆弧指示器 */
    lv_obj_t *bar_humi;     /**< 湿度进度条 */
    lv_obj_t *bar_volt;     /**< 电压进度条 */
    lv_obj_t *switch_led;   /**< LED 开关控件 */
    lv_obj_t *led_status;   /**< LED 状态指示灯 */
} dashboard_ui_t;

/** LVGL 任务句柄 */
static osThreadId_t lvglTaskHandle;
/** 仪表盘 UI 实例 */
static dashboard_ui_t dashboard;
/** 标题字体 */
static const lv_font_t *font_title;
/** 数值字体 */
static const lv_font_t *font_value;
/** 标签字体 */
static const lv_font_t *font_label;
/** 是否正在同步 Modbus 线圈到 UI（防止回调递归） */
static bool dashboard_syncing;

/** @brief LVGL 主任务入口 */
static void lvgl_task(void *argument);
/** @brief 构建仪表盘 UI */
static void dashboard_build(void);
/** @brief 定时刷新仪表盘数据 */
static void dashboard_refresh(lv_timer_t *timer);
/** @brief LED 开关触摸回调 */
static void dashboard_led_switch_cb(lv_event_t *e);
/** @brief 创建仪表盘卡片控件 */
static lv_obj_t *dashboard_add_card(lv_obj_t *parent, int32_t x, int32_t y,
                                    const char *title, lv_obj_t **value_label);

/**
 * @brief  初始化 LVGL 内核与显示/输入移植层
 */
void lvgl_app_init(void)
{
    lv_init();
    lv_port_disp_init();
    lv_port_indev_init();
}

/**
 * @brief  创建并启动 LVGL 主任务
 */
void lvgl_start_task(void)
{
    const osThreadAttr_t lvgl_task_attributes = {
        .name = "lvgl_task",
        .stack_size = LVGL_TASK_STACK,
        .priority = LVGL_TASK_PRIO,
    };

    lvglTaskHandle = osThreadNew(lvgl_task, NULL, &lvgl_task_attributes);
    bsp_ota_coop_register(lvglTaskHandle);
}

/**
 * @brief  LVGL 主任务入口，周期性调用 lv_timer_handler
 * @param  argument 任务参数（未使用）
 */
static void lvgl_task(void *argument)
{
    (void)argument;

    BSP_Watchdog_Kick(BSP_WDG_LVGL);
    /* GT9xxx/FT5206 初始化可能失败，失败时触摸不可用但 UI 仍可显示 */
    (void)lv_port_indev_touch_init();
    dashboard_build();
    /* 500ms 定时器刷新传感器读数，与 DHT11/ADC 采样周期接近 */
    lv_timer_create(dashboard_refresh, DASH_REFRESH_MS, NULL);

    for (;;)
    {
        BSP_Watchdog_Kick(BSP_WDG_LVGL);
        /* lv_timer_handler 处理刷新、输入与动画；需高频调用（约 1ms 一次） */
        lv_timer_handler();
        osDelay(1);
    }
}

/**
 * @brief  创建仪表盘卡片控件
 * @param  parent      父对象
 * @param  x           X 坐标
 * @param  y           Y 坐标
 * @param  title       卡片标题
 * @param  value_label 输出数值标签指针
 * @retval 创建的卡片对象
 */
static lv_obj_t *dashboard_add_card(lv_obj_t *parent, int32_t x, int32_t y,
                                    const char *title, lv_obj_t **value_label)
{
    lv_obj_t *card;
    lv_obj_t *title_label;
    lv_obj_t *val;

    card = lv_obj_create(parent);
    lv_obj_set_size(card, 360, 96);
    lv_obj_set_pos(card, x, y);
    lv_obj_set_style_radius(card, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_all(card, 12, LV_PART_MAIN);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    title_label = lv_label_create(card);
    lv_label_set_text(title_label, title);
    lv_obj_set_style_text_font(title_label, font_label, LV_PART_MAIN);
    lv_obj_set_style_text_color(title_label, lv_color_hex(0x888888), LV_PART_MAIN);
    lv_obj_align(title_label, LV_ALIGN_TOP_LEFT, 0, 0);

    val = lv_label_create(card);
    lv_label_set_text(val, "--");
    lv_obj_set_style_text_font(val, font_value, LV_PART_MAIN);
    lv_obj_align(val, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    *value_label = val;
    return card;
}

/**
 * @brief  构建仪表盘全部 UI 控件
 */
static void dashboard_build(void)
{
    lv_obj_t *scr;
    lv_obj_t *title;
    lv_obj_t *footer;

    font_title = &lv_font_montserrat_24;
    font_value = &lv_font_montserrat_28;
    font_label = &lv_font_montserrat_14;

    scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x1A1A2E), LV_PART_MAIN);

    /* ---- 顶部标题 ---- */
    title = lv_label_create(scr);
    lv_label_set_text(title, "Machine Dashboard");
    lv_obj_set_style_text_font(title, font_title, LV_PART_MAIN);
    lv_obj_set_style_text_color(title, lv_color_hex(0xEAEAEA), LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 16);

    /* ---- 左侧三行卡片：温度 / 湿度 / 电压数值 ---- */
    dashboard_add_card(scr, 20, 64, "Temperature", &dashboard.label_temp);
    dashboard_add_card(scr, 20, 176, "Humidity", &dashboard.label_humi);
    dashboard_add_card(scr, 20, 288, "Voltage", &dashboard.label_volt);

    /* ---- 右侧可视化：温度圆弧 + 湿度/电压进度条 ---- */
    dashboard.arc_temp = lv_arc_create(scr);
    lv_obj_set_size(dashboard.arc_temp, 120, 120);
    lv_arc_set_range(dashboard.arc_temp, 0, (int32_t)TEMP_ARC_MAX_C);
    lv_arc_set_bg_angles(dashboard.arc_temp, 135, 45);
    lv_obj_remove_flag(dashboard.arc_temp, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(dashboard.arc_temp, LV_ALIGN_TOP_RIGHT, -40, 72);

    dashboard.bar_humi = lv_bar_create(scr);
    lv_obj_set_size(dashboard.bar_humi, 280, 18);
    lv_bar_set_range(dashboard.bar_humi, 0, (int32_t)HUMI_BAR_MAX);
    lv_obj_align(dashboard.bar_humi, LV_ALIGN_RIGHT_MID, -40, -20);

    dashboard.bar_volt = lv_bar_create(scr);
    lv_obj_set_size(dashboard.bar_volt, 280, 18);
    lv_bar_set_range(dashboard.bar_volt, 0, (int32_t)VOLT_BAR_MAX_MV);
    lv_obj_align(dashboard.bar_volt, LV_ALIGN_RIGHT_MID, -40, 60);

    /* ---- 底部栏：PA3 LED 开关，与 Modbus 线圈 00000 双向同步 ---- */
    footer = lv_obj_create(scr);
    lv_obj_set_size(footer, 760, 72);
    lv_obj_align(footer, LV_ALIGN_BOTTOM_MID, 0, -12);
    lv_obj_set_style_radius(footer, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_all(footer, 12, LV_PART_MAIN);
    lv_obj_clear_flag(footer, LV_OBJ_FLAG_SCROLLABLE);

    {
        lv_obj_t *led_title = lv_label_create(footer);
        lv_label_set_text(led_title, "PA3 LED");
        lv_obj_set_style_text_font(led_title, font_label, LV_PART_MAIN);
        lv_obj_align(led_title, LV_ALIGN_LEFT_MID, 0, 0);

        dashboard.switch_led = lv_switch_create(footer);
        lv_obj_align(dashboard.switch_led, LV_ALIGN_LEFT_MID, 90, 0);
        lv_obj_add_event_cb(dashboard.switch_led, dashboard_led_switch_cb,
                            LV_EVENT_VALUE_CHANGED, NULL);

        dashboard.led_status = lv_led_create(footer);
        lv_led_set_color(dashboard.led_status, lv_color_hex(0x00C853));
        lv_obj_align(dashboard.led_status, LV_ALIGN_LEFT_MID, 180, 0);
        lv_led_off(dashboard.led_status);

        {
            lv_obj_t *hint = lv_label_create(footer);
            lv_label_set_text(hint, "Modbus coil 00000 / touch switch");
            lv_obj_set_style_text_font(hint, font_label, LV_PART_MAIN);
            lv_obj_set_style_text_color(hint, lv_color_hex(0x888888), LV_PART_MAIN);
            lv_obj_align(hint, LV_ALIGN_RIGHT_MID, 0, 0);
        }
    }

    dashboard_refresh(NULL);
}

/**
 * @brief  定时刷新仪表盘传感器数据与 LED 状态
 * @param  timer LVGL 定时器（未使用）
 */
static void dashboard_refresh(lv_timer_t *timer)
{
    uint16_t temp_c;
    uint16_t humi;
    uint16_t mv;
    uint8_t led_on;

    (void)timer;

    temp_c = BSP_AppData_GetTemperature();
    humi = BSP_AppData_GetHumidity();
    mv = BSP_AppData_GetAdcMv();
    led_on = BSP_Modbus_GetCoil(MODBUS_COIL_LED_PA3);

    /* 左侧卡片：显示 DHT11/ADC 任务写入的共享数据 */
    lv_label_set_text_fmt(dashboard.label_temp, "%u C", (unsigned)temp_c);
    lv_label_set_text_fmt(dashboard.label_humi, "%u %%", (unsigned)humi);
    lv_label_set_text_fmt(dashboard.label_volt, "%u mV", (unsigned)mv);

    if (temp_c > TEMP_ARC_MAX_C) {
        temp_c = TEMP_ARC_MAX_C;
    }

    lv_arc_set_value(dashboard.arc_temp, (int32_t)temp_c);
    lv_bar_set_value(dashboard.bar_humi, (int32_t)humi, LV_ANIM_OFF);
    lv_bar_set_value(dashboard.bar_volt, (int32_t)mv, LV_ANIM_OFF);

    dashboard_syncing = true;
    /* 从 Modbus 同步 LED 状态到 UI，期间屏蔽 switch 回调防止循环写线圈 */
    if (led_on != 0U) {
        lv_obj_add_state(dashboard.switch_led, LV_STATE_CHECKED);
        lv_led_on(dashboard.led_status);
    } else {
        lv_obj_remove_state(dashboard.switch_led, LV_STATE_CHECKED);
        lv_led_off(dashboard.led_status);
    }
    dashboard_syncing = false;
}

/**
 * @brief  LED 开关触摸回调，同步 Modbus 线圈状态
 * @param  e LVGL 事件对象
 */
static void dashboard_led_switch_cb(lv_event_t *e)
{
    lv_obj_t *sw;
    bool on;

    if (dashboard_syncing) {
        /* refresh 正在写 switch 状态，忽略此次 VALUE_CHANGED 避免回写 Modbus */
        return;
    }

    sw = lv_event_get_target(e);
    on = lv_obj_has_state(sw, LV_STATE_CHECKED);

    /* 用户触摸开关 -> 写 Modbus 线圈 -> 驱动 PA3 GPIO（在 Modbus 层 ApplyCoilToLedPa3） */
    BSP_Modbus_SetCoil(MODBUS_COIL_LED_PA3, on ? 1U : 0U);

    if (on) {
        lv_led_on(dashboard.led_status);
    } else {
        lv_led_off(dashboard.led_status);
    }
}

/**
 * @brief  FreeRTOS Tick Hook，驱动 LVGL 内部 tick 计数
 */
void vApplicationTickHook(void)
{
    lv_tick_inc(1);
}
