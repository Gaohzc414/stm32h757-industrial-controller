/**
 * @file    lv_port_disp.c
 * @brief   LVGL 显示移植层（LTDC RGB LCD，部分刷新模式）
 */
#include "lv_port_disp.h"

#include "lvgl.h"

#include "lcd.h"

#include "lcd_ltdc.h"

#include <stdbool.h>

#include <stdint.h>



#define MY_DISP_HOR_RES    800

#define MY_DISP_VER_RES    480



static void disp_init(void);

/**
 * @brief  LVGL 刷新回调：将渲染区域写入 LCD/LTDC
 * @param  disp    显示设备指针
 * @param  area    脏矩形区域
 * @param  px_map  像素数据
 */
static void disp_flush(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map);



/**
 * @brief  初始化 LVGL 显示驱动与帧缓冲
 */
void lv_port_disp_init(void)

{

    disp_init();



    lv_display_t *disp = lv_display_create(lcddev.width, lcddev.height);

    lv_display_set_flush_cb(disp, disp_flush);



    static LV_ATTRIBUTE_MEM_ALIGN uint8_t buf_1[MY_DISP_HOR_RES * 400];

    lv_display_set_buffers(disp, buf_1, NULL, sizeof(buf_1), LV_DISPLAY_RENDER_MODE_PARTIAL);

}



/**
 * @brief  显示硬件初始化（设置横屏方向）
 */
static void disp_init(void)

{

    /* Reference: lcd_init() is called here; main may call it earlier — both are OK */

    lcd_display_dir(1);

}



/**
 * @brief  LVGL 刷新回调：将渲染区域写入 LCD/LTDC
 * @param  disp_drv  显示设备指针
 * @param  area      脏矩形区域
 * @param  px_map    像素数据（格式与 LTDC_PIXFORMAT 一致）
 */
static void disp_flush(lv_display_t *disp_drv, const lv_area_t *area, uint8_t *px_map)

{

    if (lcddev.dir)

    {

        lcd_color_fill(area->x1, area->y1, area->x2, area->y2, (uint16_t *)px_map);

    }

    else

    {

#if LTDC_PIXFORMAT == LTDC_PIXFORMAT_ARGB8888

        uint32_t *buf = (uint32_t *)px_map;

#else

        uint16_t *buf = (uint16_t *)px_map;

#endif

        for (int y = area->y1; y <= area->y2; y++)

        {

            for (int x = area->x1; x <= area->x2; x++)

            {

#if LTDC_PIXFORMAT == LTDC_PIXFORMAT_ARGB8888

                uint32_t color = *buf++;

#elif LTDC_PIXFORMAT == LTDC_PIXFORMAT_RGB565

                uint16_t color = *buf++;

#else

                uint16_t color = *buf++;

#endif

                ltdc_draw_point((uint16_t)x, (uint16_t)y, color);

            }

        }

    }



    lv_display_flush_ready(disp_drv);

}

