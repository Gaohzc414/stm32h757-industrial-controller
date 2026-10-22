# STM32H757 双核工业控制项目面试版总结

## 项目背景

项目基于 STM32H757 双核 MCU，目标是构建一个带 LCD 图形界面、传感器采集、可靠启动和串口固件升级能力的工业控制应用。

本项目当前可由源码确认的重点是：

- CM7/CM4 双核应用分工；
- CM4 DHT11 采集和 CM7 图形应用解耦；
- Shared SRAM + HSEM 的轻量级跨核通信；
- Bootloader + Slot B 下载缓冲 + Slot A 执行；
- USART6 UART OTA 接收优化；
- FreeRTOS 任务健康监控和 IWDG 保护；
- LTDC RGB565 LCD、LVGL 和电容触摸。

## 系统架构

CM7 是应用主核，负责系统时钟、外部 SDRAM、LCD/LTDC、LVGL、OTA 和应用任务。CM4 使用精简工程，负责 DHT11 GPIO 时序和周期采样。

CM7 启动 CM4 并通过 HSEM 0 完成唤醒：

- `app/CM7/Core/Src/main.c:121-142`

CM4 通过 `app/CM4/BSP/Sensor/dht11.c` 采集 DHT11，将结果写入 `0x38000000` 的共享 SRAM。CM7 接收 HSEM 事件后读取共享结构，再更新应用数据和 LVGL：

- 共享数据：`app/Common/Inc/sensor_shared.h:16-35`
- 共享地址：`app/Makefile/CM7/stm32h747xx_flash_CM7.ld:46-50,176-182`
- CM4 发布：`app/Common/Src/sensor_shared.c:52-70`
- CM7 接收：`app/Common/Src/sensor_shared.c:99-116`
- UI 更新：`app/CM7/Core/Src/lvgl_demo.c:250-288`

## 关键设计

### 双核任务分工

DHT11 的单总线读取包含微秒级忙等和临界时序。当前实现将 GPIO 访问和采集任务放到 CM4，CM7 不再直接初始化 DHT11，而是读取共享数据：

- CM4 采集：`app/CM4/BSP/Sensor/dht11.c:98-199`
- CM7 读取：`app/CM7/Core/Src/freertos.c:174-204`

### 轻量级 IPC

共享结构包含 magic、valid、temperature、humidity 和 timestamp。HSEM 1 保护读写，HSEM 2 负责事件通知，CM7 在 IRQ 回调中重新 arm notification 并唤醒 FreeRTOS 任务：

- `app/Common/Inc/sensor_shared.h:22-35`
- `app/Common/Src/sensor_shared.c:30-36,52-116`

方案没有引入 OpenAMP、rpmsg 或自定义复杂 IPC 框架。

### OTA 存储策略

OTA 将镜像写入 Slot B，完成 CRC32 后写 footer 和 pending metadata。下次 Bootloader 检测到 pending Slot B 后，将镜像复制到 Slot A，最后仍从 Slot A 启动：

- `app/BSP/OTA/bsp_ota_manager.c:614-651`
- `bootloader/CM7/Core/Src/main.c:141-180`
- `bootloader/Common/Src/boot_meta.c:115-120`

因此项目中应准确说“Slot B 下载缓冲和 Bootloader 拷贝到 Slot A”，不能说成两个镜像直接切换启动。

### UART OTA

设备 OTA 使用 USART6。接收采用 FIFO、DMA ReceiveToIdle、SPSC ring buffer 和 task notification：

- `app/CM7/Core/Src/usart.c:185-223,376-415`
- `app/BSP/OTA/bsp_ota_uart.c:97-109,187-210`

这样避免逐字节 `HAL_UART_Receive` 加 `osDelay` 带来的接收空窗。当前发送仍是阻塞式 `HAL_UART_Transmit()`：

- `app/BSP/OTA/bsp_ota_uart.c:166-177`

Qt Host 已支持 `QSerialPort`，并通过 UI 在 TCP 和 UART 模式之间选择：

- `tools/ota-host/transport/SerialManager.cpp:22-53,87-105`
- `tools/ota-host/mainwindow.cpp:136-164,341-370,477-517`

### 故障监控

软件 watchdog 用递增 counter 监测任务进展，CM4 通过跨核发布维护 `BSP_WDG_CM4` 心跳；IWDG1 在健康检查失败后停止刷新，等待硬件复位：

- `app/BSP/bsp_watchdog.c:18-35,119-143,194-287`
- `app/CM7/Core/Src/freertos.c:180-203`

OTA 等长操作使用引用计数，避免多个子系统相互覆盖 watchdog 豁免状态：

- `app/BSP/bsp_watchdog.c:48-49,105-117`

## 遇到的问题

### DHT11 对 CM7 实时任务的影响

DHT11 读取需要关闭中断并进行微秒级轮询，和图形刷新、网络或 OTA 任务放在同一核会增加调度干扰。解决方式是把传感器采集任务迁移到 CM4，通过共享 SRAM 上报结果。

### HSEM notification 只触发一次

HAL HSEM 中断处理后需要重新启用 notification。否则第一次事件能唤醒 CM7，后续事件可能丢失。当前修复位于：

- `app/Common/Src/sensor_shared.c:104-112`

### OTA 串口丢字节

逐字节轮询配合任务延时无法稳定承受连续串口字节流。当前通过 DMA ReceiveToIdle、ring buffer 和 task notification 将 ISR 接收和协议解析解耦：

- `app/BSP/OTA/bsp_ota_uart.c:1-8,187-210`

### OTA 期间 watchdog 误判

OTA 会挂起部分任务，watchdog 需要识别这是受控长操作，而不是任务故障。当前通过长操作引用计数和调整监控范围处理：

- `app/BSP/OTA/bsp_ota_manager.c:199-221`
- `app/BSP/bsp_watchdog.c:194-205`

### CM4 迁移后 heartbeat 失效

DHT11 迁移后，CM7 本地任务 heartbeat 不再代表 CM4 采集任务。当前由 CM4 发布 HSEM 事件，CM7 收到通知后 kick CM4 槽位：

- `app/CM7/Core/Src/freertos.c:191-203`

## 解决方案

最终形成以下稳定的代码分层：

```text
CM4 Sensor BSP
  -> Common sensor_shared
  -> HSEM IRQ/FreeRTOS notification
  -> CM7 application data
  -> LVGL/LTDC
```

```text
Qt SerialManager
  -> USART6
  -> DMA ReceiveToIdle
  -> ring buffer
  -> OTA manager
  -> Slot B
  -> CRC/footer/metadata
  -> Bootloader B->A copy
```

## 项目亮点

1. 在 STM32H757 双核上实现了清晰的 CM7 应用核、CM4 传感核分工。
2. 使用官方硬件资源 Shared SRAM 和 HSEM 完成轻量级 IPC，没有引入复杂通信框架。
3. 对 HSEM notification 的一次性触发问题进行了源码级修复。
4. 采用 USART6 DMA ReceiveToIdle + ring buffer，解决连续串口 OTA 接收路径的结构性风险。
5. OTA 具备 Slot B 下载、CRC32 校验、footer 和 pending metadata 流程。
6. watchdog 同时覆盖本核任务、跨核 CM4 heartbeat 和硬件 IWDG。
7. LCD/LVGL 使用 RGB565、800x480、外部 SDRAM framebuffer，并集成 GT9xxx/FT5206 触摸探测。

## 事实边界

- CM7 和 CM4 应用已完成 `build_audit` 编译和链接。
- 当前没有板级完整升级、连续 HSEM 通知、触摸或 watchdog 故障注入记录，因此这些部分只能称为“源码实现并通过构建”，不能称为已完成硬件验证。
- `app/BSP/DHT11/dht11.c` 仍保留，但不是当前 CM7 活动构建路径；活动 DHT11 路径是 `app/CM4/BSP/Sensor/dht11.c`。
- Modbus、W5500、TCP/IP、ESP8266 等代码在仓库中存在，但不作为本项目已实现成果描述。
- 共享 IPC 初始化两侧都会调用 `sensor_shared_reset()`，存在启动时序导致共享数据被清零的审计风险：`app/Common/Src/sensor_shared.c:24-49`。该问题应在后续板级验证和启动策略收敛时单独处理。
