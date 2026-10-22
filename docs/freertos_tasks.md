# FreeRTOS 任务与同步审计

## 范围说明

下表只突出当前架构中的 CM7/CM4 核心任务和本项目相关同步机制。仓库中还存在 W5500、Modbus、ESP8266 等任务代码，但这些模块不作为本项目已验证成果描述。

## 任务表

| 任务 | 核 | 优先级 | 周期/等待 | 栈大小 | 主要功能 | 来源 |
| --- | --- | --- | --- | ---: | --- | --- |
| `lvgl_task` | CM7 | `AboveNormal` | `osDelay(1)`，约 1ms 调度 | `4096*4` 字节 | LVGL tick、输入、刷新 | `app/CM7/Core/Src/lvgl_demo.c:17-23,84-117` |
| `defaultTask` | CM7 | `Normal` | `osDelay(1000)` | `512*4` 字节 | 启动应用子系统、周期心跳 | `app/CM7/Core/Src/freertos.c:68-74,143-166` |
| `sensor_ipc` | CM7 | `AboveNormal` | `ulTaskNotifyTake(..., portMAX_DELAY)` | `512*4` 字节 | 等待 CM4 数据通知并更新应用缓存 | `app/CM7/Core/Src/freertos.c:60-65,78-80,174-204` |
| `ota` | CM7 | `Realtime` | notification/短超时等待 | `BSP_OTA_TASK_STACK_WORDS*4`，即 8192 字节 | OTA 传输和协议轮询 | `app/BSP/OTA/bsp_ota_app.c:36-55,105-124`; `app/BSP/OTA/bsp_ota_conf.h:54-57` |
| `adc` | CM7 | `Normal` | `500 ms` | `512*4` 字节 | ADC 平均采样、滤波、应用数据更新 | `app/BSP/ADC/bsp_adc.c:14-25,130-165` |
| `watchdog` | CM7 | `Realtime` | `2000 ms` | `384*4` 字节 | 检查任务心跳并刷新 IWDG | `app/BSP/bsp_watchdog.c:12-16,238-307` |
| `dht11` | CM4 | `Normal` | `2000 ms` | `512*4` 字节 | DHT11 采样并发布共享数据 | `app/CM4/BSP/Sensor/dht11.c:13,172-212`; `app/CM4/BSP/Sensor/dht11_conf.h:14` |

## CM7 初始化顺序

CM7 的 `MX_FREERTOS_Init()` 先初始化日志和应用数据互斥锁，再初始化 LVGL、创建默认任务和 IPC 任务，随后注册 watchdog：

- `app/CM7/Core/Src/freertos.c:92-124`

`SensorIpcTask` 的任务句柄会传给 `sensor_ipc_init()`，从而供 HSEM ISR 回调发送 task notification：

- `app/CM7/Core/Src/freertos.c:116-120`
- `app/Common/Src/sensor_shared.c:43-46,99-112`

`defaultTask` 启动 ADC、OTA 等子系统：

- `app/CM7/Core/Src/freertos.c:143-160`

## CM4 初始化顺序

CM4 从 STOP 唤醒后初始化 DMA、FreeRTOS，并调用：

- `app/CM4/Core/Src/main.c:76-112`

CM4 的 RTOS 初始化只执行：

```c
sensor_ipc_init(NULL);
dht11_app_start();
```

来源：`app/CM4/Core/Src/freertos.c:17-21`

## 同步机制

### HSEM

- HSEM 0：启动/唤醒 CM4；
- HSEM 1：保护共享数据结构；
- HSEM 2：发布事件并触发 CM7 HSEM IRQ。

来源：`app/Common/Inc/sensor_shared.h:16-20`；`app/CM7/Core/Src/main.c:124-138`。

### FreeRTOS task notification

用于两个中断到任务的路径：

1. CM4 HSEM 事件到达 CM7 后，`vTaskNotifyGiveFromISR()` 唤醒 `SensorIpcTask`；
2. USART6 DMA 接收回调后，`vTaskNotifyGiveFromISR()` 唤醒 OTA 任务。

来源：

- IPC：`app/Common/Src/sensor_shared.c:100-115`
- OTA UART：`app/BSP/OTA/bsp_ota_uart.c:187-210`

### Mutex

CM7 应用数据缓存使用 CMSIS-RTOS mutex 保护：

- `app/BSP/bsp_app_data.c:16-18,24-39,46-54`

### Queue

CM7 的 USB Modbus 源码使用 FreeRTOS queue，但该模块不纳入本项目面试版成果：

- `app/BSP/Modbus/bsp_usb_modbus.c:55-75,145-167`

## 栈和堆配置

CM7 FreeRTOS 配置：

- 动态堆 `96KB`；
- 最大优先级数 `56`；
- tick `1000Hz`；
- mutex、计数信号量和 timer API 已启用。

来源：`app/CM7/Core/Inc/FreeRTOSConfig.h:61-105,168-175`

CM4 配置：

- 动态堆 `24KB`；
- tick `1000Hz`；
- 最大优先级数 `56`；
- mutex、计数信号量和 timer API 已启用。

来源：`app/CM4/Core/Inc/FreeRTOSConfig.h:21-69,90-91`

## OTA 对任务的影响

OTA 会挂起部分 peer 任务以避免 Flash 擦写期间发生资源争用：

- `app/BSP/OTA/bsp_ota_manager.c:199-221`

OTA 协作注册点可见于默认任务、LVGL 和 ADC：

- `app/CM7/Core/Src/freertos.c:159-160`
- `app/CM7/Core/Src/lvgl_demo.c:92-94`
- `app/BSP/ADC/bsp_adc.c:163-165`

OTA 端口模式会 hold CM4，避免 CM4 在共享 Flash 上取指或访问：

- `app/BSP/OTA/bsp_ota_port.c:14-46`

## 审计边界

源码中存在的任务、信号量或队列不等于本次项目已经完成并验证。尤其是 W5500/Modbus/ESP8266 相关任务，应按项目范围单独审计；本文件只将其作为“源码存在”处理。
