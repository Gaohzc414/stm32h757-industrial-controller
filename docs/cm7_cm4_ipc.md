# CM7/CM4 IPC 审计

## 状态说明

共享数据模块已经进入 CM7、CM4 两侧 Makefile，并完成两侧应用构建。当前没有板级运行证据，因此本文描述的是“源码实现完整、构建通过，尚未硬件验证”的状态。

## 共享内存

共享对象定义在公共通信模块中：

```c
static sensor_shared_data_t s_sensor_data
    __attribute__((section(".shared_data"), aligned(32)));
```

来源：`app/Common/Src/sensor_shared.c:18`

两个链接脚本都把 `.shared_data` 放入 `0x38000000` 起始、64KB 大小的 `SHARED_RAM`：

- CM7：`app/Makefile/CM7/stm32h747xx_flash_CM7.ld:43-50,176-182`
- CM4：`app/Makefile/CM4/stm32h747xx_flash_CM4.ld:43-49,152-158`

实际结构为：

```c
typedef struct
{
    uint32_t magic;
    uint8_t valid;
    float temperature;
    float humidity;
    uint32_t timestamp;
} sensor_shared_data_t;
```

来源：`app/Common/Inc/sensor_shared.h:22-29`

magic 为 `0x53484D31`。数据 HSEM 为 1，事件 HSEM 为 2：

- `app/Common/Inc/sensor_shared.h:16-20`

## CM4 路径：DHT11 到共享数据

### 1. CM4 启动并创建任务

CM4 从 STOP 模式被 CM7 通过 HSEM 0 唤醒，之后初始化 HAL、FreeRTOS，并调用：

- `app/CM4/Core/Src/main.c:69-112`
- `app/CM4/Core/Src/freertos.c:17-21`

`MX_FREERTOS_Init()` 先调用 `sensor_ipc_init(NULL)`，再启动 `dht11_app_start()`：

- `app/CM4/Core/Src/freertos.c:17-21`

DHT11 任务栈为 `512 * 4` 字节，优先级为 `osPriorityNormal`：

- `app/CM4/BSP/Sensor/dht11.c:203-212`

### 2. DHT11 采集

任务首次初始化 DHT11，然后每次循环读取一次：

- `app/CM4/BSP/Sensor/dht11.c:183-199`

采集协议使用 GPIOD Pin 12、DWT 周期计数器和微秒级等待：

- `app/CM4/BSP/Sensor/dht11.c:15-42`
- `app/CM4/BSP/Sensor/dht11.c:98-108`
- `app/CM4/BSP/Sensor/dht11.c:120-169`

采样成功时写入整数温度、整数湿度、`HAL_GetTick()` 时间戳并置 `valid=1`；失败时置 `valid=0`，仍更新时间戳：

- `app/CM4/BSP/Sensor/dht11.c:175-198`

任务每 `2000 ms` 延时：

- `app/CM4/BSP/Sensor/dht11.c:187-200`
- `app/CM4/BSP/Sensor/dht11_conf.h:14`

### 3. 共享内存写入与 HSEM Release

`sensor_shared_publish()` 的顺序是：

1. 获取数据 HSEM 1；
2. 拷贝结构体到共享对象；
3. 修正 magic；
4. Release 数据 HSEM 1；
5. CM4 Release 事件 HSEM 2；
6. 重新获取事件 HSEM 2，等待 CM7 下一次处理。

来源：`app/Common/Src/sensor_shared.c:52-70`

事件 HSEM 2 的 release 是 CM4 发给 CM7 的通知触发点。CM4 不直接操作 CM7 任务句柄，也不引入 OpenAMP、rpmsg 或自定义消息队列。

## CM7 路径：HSEM IRQ 到 LCD 更新

### 1. HSEM IRQ

CM7 的 HSEM1、HSEM2 中断都进入 HAL：

- `app/CM7/Core/Src/stm32h7xx_it.c:326-339`

HSEM 中断优先级和 NVIC 使能在 MSP 初始化中配置：

- `app/CM7/Core/Src/stm32h7xx_hal_msp.c:72-76`

### 2. HAL 回调与 FreeRTOS notification

CM7 调用 `sensor_ipc_init(sensorIpcTaskHandle)` 时保存任务句柄并激活事件 HSEM 通知：

- `app/CM7/Core/Src/freertos.c:116-120`
- `app/Common/Src/sensor_shared.c:39-49`

收到 HSEM 释放后，`HAL_HSEM_FreeCallback()`：

1. 检查事件 HSEM 2 mask；
2. 重新调用 `HAL_HSEM_ActivateNotification()`；
3. 使用 `vTaskNotifyGiveFromISR()` 唤醒 `SensorIpcTask`；
4. 根据 `higher_priority_task_woken` 请求任务切换。

来源：`app/Common/Src/sensor_shared.c:99-116`

重新激活 notification 是关键修复。HAL 处理一次中断时会清理 IER，如果不重新 arm，后续 CM4 发布可能不会再次触发回调。

### 3. CM7 读取共享结构

`SensorIpcTask` 首次读取一次共享数据，之后阻塞等待 task notification：

- `app/CM7/Core/Src/freertos.c:174-204`

收到通知后，任务调用 `sensor_ipc_read()`，最终通过 HSEM 1 保护共享数据的复制：

- `app/Common/Src/sensor_shared.c:73-97`

读取过程是：

1. 非阻塞尝试获取数据 HSEM 1；
2. 复制共享结构到 CM7 栈上的局部对象；
3. Release 数据 HSEM 1。

### 4. 应用数据与 LCD

CM7 将 `valid` 转换为 DHT11 在线状态，并将温度、湿度写入线程安全的应用缓存：

- `app/CM7/Core/Src/freertos.c:183-203`
- `app/BSP/bsp_app_data.c:46-54,106-160`

LVGL 任务每 1ms 调用 `lv_timer_handler()`，并创建 500ms 数据刷新定时器：

- `app/CM7/Core/Src/lvgl_demo.c:100-117`

刷新回调从应用缓存读取温度、湿度和在线状态，更新 LVGL 控件：

- `app/CM7/Core/Src/lvgl_demo.c:250-288`

因此完整链路是：

```text
CM4 DHT11
  -> sensor_shared_publish()
  -> HSEM 2 release
  -> CM7 HSEM IRQ
  -> HAL_HSEM_FreeCallback()
  -> FreeRTOS task notification
  -> SensorIpcTask
  -> BSP_AppData
  -> LVGL timer refresh
  -> LTDC framebuffer / LCD
```

## Cache 处理

共享结构所在区域通过 CM7 MPU 配置为不可缓存、不可缓冲：

- `app/BSP/MPU/mpu.c:61-63`

共享对象本身 32 字节对齐：

- `app/Common/Src/sensor_shared.c:18`

当前 IPC 数据不是通过 DMA 搬运，不存在像 UART DMA 那样的显式 `SCB_InvalidateDCache_by_Addr()`。同步依赖 MPU 属性和 HSEM 临界区。

## HSEM 的两个作用

| HSEM | 用途 | 代码 |
| --- | --- | --- |
| 0 | CM7 启动/唤醒 CM4 | `app/CM7/Core/Src/main.c:124-138` |
| 1 | 保护共享数据读写 | `app/Common/Inc/sensor_shared.h:17-19`、`sensor_shared.c:30-36,58-84` |
| 2 | CM4 发布事件，触发 CM7 IRQ | `app/Common/Inc/sensor_shared.h:18-20`、`sensor_shared.c:65-68,99-116` |

## Debug 案例：HSEM notification 失效

### 现象：

CM4 第一次发布后 CM7 能收到数据，但后续发布不再唤醒 `SensorIpcTask`。

### 排查：

检查 CM7 的 HSEM IRQ 入口、HAL 回调、事件 HSEM mask 以及 FreeRTOS task notification：

- IRQ：`app/CM7/Core/Src/stm32h7xx_it.c:326-339`
- notification 激活：`app/Common/Src/sensor_shared.c:43-46`
- 回调：`app/Common/Src/sensor_shared.c:99-116`
- 任务等待：`app/CM7/Core/Src/freertos.c:191-203`

### 关键寄存器：

- HSEM 的中断使能/通知状态，由 `HAL_HSEM_ActivateNotification()` 管理；
- HSEM IRQ 由 `HSEM1_IRQHandler()`、`HSEM2_IRQHandler()` 进入 `HAL_HSEM_IRQHandler()`；
- CM7 启动握手另使用 `RCC->GCR` 的 `BOOT_C2` 和 `RCC_FLAG_D2CKRDY`，见 `app/CM7/Core/Src/main.c:124-142`。

### 根因：

`HAL_HSEM_IRQHandler()` 处理事件后会清除对应 IER。若回调不重新调用 `HAL_HSEM_ActivateNotification()`，通知只会成功一次。

### 修复：

在 `HAL_HSEM_FreeCallback()` 中检测事件 HSEM 2 后立即重新 arm，再使用 `vTaskNotifyGiveFromISR()` 唤醒任务：

- `app/Common/Src/sensor_shared.c:104-112`

### 验证：

源码层面已完成回调修复，CM7/CM4 构建通过。当前仓库没有提供板级连续多次发布的日志或调试器截图，因此不能声明硬件验证已完成。

## 审计发现：初始化竞态

`sensor_ipc_init()` 在 CM4 与 CM7 两侧都会调用 `sensor_shared_reset()`：

- `app/Common/Src/sensor_shared.c:24-28,39-49`
- `app/CM4/Core/Src/freertos.c:19`
- `app/CM7/Core/Src/freertos.c:118-120`

如果两核启动时序发生交叉，后启动的一侧可能清零先前已发布的数据。当前设计依赖启动顺序和首次读取时机，但源码没有版本号、所有者标志或只允许一侧 reset 的机制。该项应视为源码审计风险，不能写成已验证的健壮性保证。
