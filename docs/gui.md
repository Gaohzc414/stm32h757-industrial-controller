# GUI、LCD 与 Touch 审计

## 范围

本文只描述源码中实际存在的 LCD、LVGL、framebuffer 和 Touch 路径，不展开 Modbus、W5500 或 TCP/IP。

## LCD 接口

LCD 是 RGB LTDC 类型。LCD 初始化负责：

1. 打开 LCD 电源；
2. 读取面板 ID；
3. 初始化 LTDC；
4. 设置横屏；
5. 打开背光并清屏。

来源：`app/BSP/LCD/lcd.c:176-215`

LTDC 使用的硬件引脚和 LCD 控制接口定义在：

- `app/BSP/LCD/lcd_ltdc.h:48-84`

## 显示参数

当前活动配置为：

- 分辨率：`800x480`；
- 像素格式：RGB565；
- framebuffer：`0xD0000000`；
- LTDC layer 的 framebuffer 宽高：800x480。

来源：

- `app/CM7/Core/Src/ltdc.c:66-73`
- `app/BSP/LCD/lcd_ltdc.h:39-74`

framebuffer 位于外部 SDRAM 地址空间。CM7 MPU 将 `0xD0000000` 区域设置为可缓存、可缓冲：

- `app/BSP/MPU/mpu.c:69-71`

这与共享 SRAM 的非缓存属性不同。framebuffer 的缓存策略服务于显示访问，IPC 共享区则强调核间一致性。

## LTDC 与 DMA2D

LTDC 驱动维护 layer、方向、宽高、像素大小和 pixel format：

- `app/BSP/LCD/lcd_ltdc.c:11-12`
- `app/BSP/LCD/lcd_ltdc.h:19-37`

纯色填充和颜色块填充使用 DMA2D：

- `app/BSP/LCD/lcd_ltdc.c:172-237`
- `app/BSP/LCD/lcd_ltdc.c:305-362`

这说明当前 LCD BSP 不只是写寄存器启动 LTDC，还提供了绘点、区域填充、颜色块拷贝和清屏接口。

## LVGL 显示结构

LVGL 初始化顺序为：

```text
lv_init()
 -> lv_port_disp_init()
 -> lv_port_indev_init()
```

来源：`app/CM7/Core/Src/lvgl_demo.c:71-79`

显示移植层创建 `800x480` display，绑定 flush 回调，并使用 `800*400` 的部分渲染 buffer：

- `app/BSP/LVGL/lv_port_disp.c:19-21,40-58`

渲染模式为 `LV_DISPLAY_RENDER_MODE_PARTIAL`。flush 回调将脏矩形区域写入 LCD/LTDC，并在完成后调用 `lv_display_flush_ready()`：

- `app/BSP/LVGL/lv_port_disp.c:78-143`

CM7 LVGL 任务每 1ms 调用 `lv_timer_handler()`：

- `app/CM7/Core/Src/lvgl_demo.c:100-117`

## 当前 GUI 内容

实际构建的界面是机器仪表盘，包含温度、湿度、电压和 DHT11 在线状态显示：

- 控件定义：`app/CM7/Core/Src/lvgl_demo.c:32-44`
- 界面构建：`app/CM7/Core/Src/lvgl_demo.c:159-244`
- 数据刷新：`app/CM7/Core/Src/lvgl_demo.c:246-288`

DHT11 数值由 CM7 应用数据缓存提供，而不是 LVGL 直接访问 DHT11 GPIO：

- `app/CM7/Core/Src/lvgl_demo.c:260-277`
- `app/BSP/bsp_app_data.c:60-83,141-160`

## Touch 驱动

Touch BSP 对 GT9xxx 和 FT5206 做自动探测：

1. 初始化时先尝试 GT9xxx；
2. 失败后尝试 FT5206/FT5426；
3. 成功后绑定对应的 scan 函数；
4. 都失败则返回未检测到支持芯片。

来源：`app/BSP/TOUCH/touch.c:41-78`

电容触摸控制器通过软件 I2C 与 GPIO bit-bang 实现：

- `app/BSP/TOUCH/ctiic.c:18-42`
- `app/BSP/TOUCH/ctiic.c:44-74`

GT9xxx 和 FT5206 均有独立寄存器读写与扫描实现：

- GT9xxx：`app/BSP/TOUCH/gt9xxx.c:19-79,81-151,159-180`
- FT5206：`app/BSP/TOUCH/ft5206.c:14-69,71-127,133-158`

## LVGL 输入路径

LVGL 输入移植层注册 pointer 类型输入设备，但不在注册时强制初始化触摸硬件：

- `app/BSP/LVGL/lv_port_indev.c:26-40`

LVGL 任务启动时尝试初始化触摸。失败时 `touch_ready` 为 false，UI 仍可继续显示：

- `app/BSP/LVGL/lv_port_indev.c:42-65`
- `app/CM7/Core/Src/lvgl_demo.c:104-109`

输入回调采用轮询扫描：

- `app/BSP/LVGL/lv_port_indev.c:68-123`

准确地说，当前触摸是“软件 I2C + 控制器轮询”，不是中断驱动输入。

## 已实现与未验证边界

源码和构建可确认：

- LTDC/RGB565/800x480 配置存在；
- framebuffer 地址存在；
- LVGL display、partial render buffer、flush 回调存在；
- GT9xxx/FT5206 自动探测和轮询路径存在。

当前仓库不能仅凭源码确认：

- 实际面板 ID 一定匹配；
- framebuffer 在目标硬件上长期稳定显示；
- 触摸控制器一定被探测成功；
- 所有 UI 交互已完成板级回归。
