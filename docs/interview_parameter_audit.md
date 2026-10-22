# STM32H757 Industrial Controller 面试级工程参数审计

> 审计范围：当前仓库源码、链接脚本、Makefile、FreeRTOS 配置及现有 OTA Host 源码。  
> 审计原则：只记录当前源码可追溯的配置；未从源码确认的内容明确标注“未确认”。  
> 本文档只做分析，本次未修改任何 `.c`、`.h`、`.s`、`.ld`、Makefile 或工程源文件。

## 一、硬件平台参数

### 1.1 MCU 与编译目标

| 项目 | 当前源码结论 | 依据 |
|---|---|---|
| MCU 型号 | STM32H757 系列目标，构建宏为 `STM32H757xx` | `app/Makefile/CM7/Makefile`、`app/Makefile/CM4/Makefile` |
| CM7 | Cortex-M7，硬浮点，`fpv5-d16` | `app/Makefile/CM7/Makefile` |
| CM4 | Cortex-M4，硬浮点，`fpv4-sp-d16` | `app/Makefile/CM4/Makefile` |
| CM7 目标频率 | 时钟配置推导为 HCLK 约 480 MHz；未进行示波器或运行时寄存器实测 | `app/CM7/Core/Src/main.c:220-275` |
| HSE | 25 MHz | `app/Common/Src/system_stm32h7xx_dualcore_boot_cm4_cm7.c:53-54` |
| FPU | CM7、CM4 均启用 FreeRTOS 浮点支持 | 两核 `FreeRTOSConfig.h` |
| Flash 容量 | 当前链接布局使用 Bank1/Bank2 的地址空间；芯片完整标称容量未由工程源码单独声明 | 两核链接脚本、`boot_slot.h` |
| 外部 SDRAM | 工程使用 `0xD0000000` 作为 SDRAM/FrameBuffer 基址；按地址和备份区注释推断为 8 MB 级空间，容量应以硬件和 FMC 配置最终确认 | `app/BSP/SDRAM/sdram.h`、`app/BSP/OTA/bsp_ota_conf.h` |

### 1.2 时钟配置

CM7 的 `SystemClock_Config()` 使用：

| 参数 | 值 |
|---|---:|
| HSE | 25 MHz，Bypass |
| PLLM | 5 |
| PLLN | 192 |
| PLLP | 2 |
| PLLQ | 15 |
| PLLR | 1 |
| PLLFRACN | 0 |
| SYSCLK 来源 | PLLCLK |
| SYSCLK Divider | 1 |
| AHB Divider | 2 |
| APB1/APB2/APB3/APB4 | 均为 `/2` |
| LSI | 开启，供 IWDG |
| HSI48 | 开启 |

计算上，PLL 输入为 `25 MHz / 5 = 5 MHz`，VCO 为 `5 MHz * 192 = 960 MHz`。工程配置再通过 H7 的域时钟分频得到 CM7/总线运行时钟。面试时应区分“PLL 输出计算值”和“CPU 实际 HCLK”，不要直接把 960 MHz 说成 CM7 主频。源码没有提供运行时寄存器打印，因此本文将约 480 MHz 视为配置推导值，而非板级实测值。

依据：`app/CM7/Core/Src/main.c:220-275`。

### 1.3 外设资源

当前源码能确认的资源包括：

| 资源 | 用途 | 依据 |
|---|---|---|
| USART6 | UART OTA，PG9 RX、PG14 TX，AF7 | `app/CM7/Core/Src/usart.c:187-228,346-416` |
| USART1 | Bootloader 调试/启动阶段串口初始化 | `bootloader/CM7/Core/Src/main.c:94-121` |
| DMA1 Stream2 | USART6 RX | `app/CM7/Core/Src/usart.c:346-416` |
| DMA1 Stream3 | USART6 TX | `app/CM7/Core/Src/usart.c:346-416` |
| LTDC | LCD 图像输出 | `app/CM7/Core/Src/ltdc.c` |
| FMC SDRAM | 外部帧缓存及 OTA 临时区域 | `app/BSP/SDRAM/sdram.h`、`app/BSP/OTA/bsp_ota_conf.h` |
| HSEM | CM7/CM4 启动握手、共享数据保护、跨核通知 | `app/Common/Src/sensor_shared.c`、两核 `main.c` |
| IWDG1 | CM7 硬件看门狗 | `app/CM7/Core/Src/iwdg.c` |
| ADC | CM7 ADC 采样任务 | `app/BSP/ADC/bsp_adc.c` |
| Touch 控制器 | GT9xxx 优先，兼容 FT5206/FT5426 | `app/BSP/TOUCH/touch.c:41-77` |

W5500、Modbus 等代码和任务也存在于当前构建路径，但本次审计不把它们作为项目核心面试亮点；这里只记录其确实占用的任务和资源。

## 二、Flash 空间布局

### 2.1 当前地址表

| 区域 | 地址 | 大小 | 用途 | 依据 |
|---|---:|---:|---|---|
| Bootloader | `0x08000000` | 128 KiB | CM7 Bootloader | `bootloader/Makefile/CM7/stm32h747xx_flash_CM7.ld` |
| CM7 APP Slot A | `0x08020000` | 448 KiB | 当前实际执行镜像 | `app/Makefile/CM7/stm32h747xx_flash_CM7.ld` |
| CM7 APP Slot B | `0x08090000` | 448 KiB | OTA 下载/暂存镜像 | `app/Common/Inc/boot_slot.h` |
| 共用/预留区 | `0x08080000` | 128 KiB | OTA 配置中的 shared sector base；与 Slot B 地址范围存在重叠，需以当前链接和写入流程为准 | `app/BSP/OTA/bsp_ota_conf.h` |
| CM4 firmware | `0x08100000` | 1024 KiB | Bank2 CM4 镜像 | `app/Makefile/CM4/stm32h747xx_flash_CM4.ld` |
| Metadata | `0x38800000` | 由结构体决定 | BKPSRAM 中的启动元数据 | `app/Common/Src/boot_meta.c`、`bootloader/Common/Src/boot_meta.c` |

Boot Slot 常量还定义了 32 字节 footer，magic 为 `0x534F4F54`（ASCII 形式的 slot footer magic）。来源：`app/Common/Inc/boot_slot.h`、`bootloader/Common/Inc/boot_slot.h`。

### 2.2 分区意图

- Bootloader 独占 Bank1 起始的 128 KiB，保证上电时先进入启动决策逻辑。
- CM7 Slot A 从 `0x08020000` 开始，和 Bootloader 分离。
- Slot B 用作下载和暂存区域，避免 OTA 接收过程中覆盖正在运行的 Slot A。
- CM4 镜像放在 Bank2 的 `0x08100000`，由 CM4 链接脚本固定。
- Metadata 放在 BKPSRAM，避免普通 Flash 擦写影响启动状态记录。

### 2.3 是否真正支持 A/B rollback

当前实现**不是完整的双镜像直接回滚系统**。源码表现为：

1. OTA/Bootloader 使用 Slot B 作为下载或暂存位置。
2. Bootloader 检测 pending 状态后把 Slot B 拷贝到 Slot A。
3. Bootloader 最终固定从 Slot A 跳转。
4. `boot_meta_select_boot_slot()` 当前固定选择 Slot A，见 `bootloader/Common/Src/boot_meta.c:115-120`。

因此，Slot B 更接近 staging/download slot，而不是运行时可直接切换的备用执行 slot。metadata 中有 trial 次数和 pending 标志，但从当前启动选择逻辑不能证明已经实现了真正意义上的“失败后自动回滚到旧镜像”。

## 三、RAM 布局

### 3.1 链接脚本区域

#### CM7

| 区域 | 地址 | 大小 | 主要用途 |
|---|---:|---:|---|
| DTCM/主 RAM 区 | `0x20000000` | 128 KiB | 普通变量、栈等，具体分配由链接脚本决定 |
| AXI SRAM | `0x24000000` | 512 KiB | 普通高速 RAM、DMA buffer |
| Shared SRAM | `0x38000000` | 64 KiB | `.shared_data`，CM7/CM4 共享 |
| ITCMRAM | `0x00000000` | 64 KiB | 链接脚本声明的 ITCM 区 |

#### CM4

| 区域 | 地址 | 大小 | 主要用途 |
|---|---:|---:|---|
| CM4 RAM | `0x10000000` | 288 KiB | CM4 普通变量、栈、FreeRTOS heap |
| Shared SRAM | `0x38000000` | 64 KiB | `.shared_data`，与 CM7 使用同一区域 |

依据：`app/Makefile/CM7/stm32h747xx_flash_CM7.ld`、`app/Makefile/CM4/stm32h747xx_flash_CM4.ld`。

### 3.2 专用段和缓存属性

| 段/用途 | 地址或区域 | 说明 |
|---|---|---|
| `.dma_buffer` | CM7 AXI SRAM | OTA UART DMA buffer 等 DMA 数据 |
| `.shared_data` | `0x38000000` Shared SRAM | 跨核共享对象，按 32 字节对齐 |
| FrameBuffer | `0xD0000000` | LTDC/LCD 的 RGB565 framebuffer |
| SDRAM OTA scratch | `0xD07F0000`，64 KiB | OTA 备份/临时区域配置 |

CM7 MPU 配置：

| 基址 | 大小 | 属性 |
|---:|---:|---|
| `0x20000000` | 128 KiB | Cacheable/Bufferable |
| `0x24000000` | 512 KiB | Cacheable/Bufferable |
| `0x30000000` | 512 KiB | Cacheable/Bufferable |
| `0x38000000` | 64 KiB | Non-cacheable/Non-bufferable |
| `0x60000000` | 64 MiB | Non-cacheable/Non-bufferable |
| `0xD0000000` | 32 MiB | Cacheable/Bufferable |
| `0x80000000` | 256 MiB | Execute-never、Non-cacheable、Non-bufferable |

依据：`app/BSP/MPU/mpu.c:47-76`。

面试重点：Shared SRAM 被设置为 non-cacheable，降低了 CM7 D-Cache 与 CM4 共享数据之间的一致性风险；UART DMA buffer 仍需配合 D-Cache invalidate/clean，不能因为共享区 non-cacheable 就推断所有 DMA buffer 都天然一致。

## 四、双核通信参数

### 4.1 共享数据结构

共享对象定义为：

```c
typedef struct
{
    uint32_t magic;
    uint8_t  valid;
    float    temperature;
    float    humidity;
    uint32_t timestamp;
} sensor_shared_data_t;
```

依据：`app/Common/Inc/sensor_shared.h:16-29`。

共享对象：

- 放置在 `.shared_data`；
- 按 32 字节对齐；
- magic 为 `0x53484D31`；
- 共享内存基址为 `0x38000000`；
- 实际对象大小由结构体布局决定，工程没有把整个 64 KiB 区域都当作数据包。

实现位置：`app/Common/Src/sensor_shared.c:18`。

### 4.2 HSEM 分配

| HSEM | 当前用途 | 方向/说明 | 依据 |
|---|---|---|---|
| HSEM0 | CM7/CM4 启动握手、唤醒 CM4 | 启动同步 | `app/CM7/Core/Src/main.c:124-138`、`app/CM4/Core/Src/main.c:76-90` |
| HSEM1 | 共享传感器数据保护 | 访问共享对象时 take/release | `app/Common/Src/sensor_shared.c:58-84` |
| HSEM2 | CM4 通知 CM7 有新数据 | CM4 release，CM7 HSEM IRQ/callback 接收 | `app/Common/Src/sensor_shared.c:65-68,99-116` |

这种分配把“启动控制”“数据互斥”“事件通知”分开，避免把一个 semaphore 同时承担三种语义。HSEM2 是事件通知，不等同于数据锁；数据锁仍由 HSEM1 保护。

### 4.3 CM4 到 CM7 路径

1. CM4 DHT11 任务周期约 2000 ms 读取传感器。
2. CM4 调用共享层更新温湿度、时间戳和 valid/magic。
3. 共享层通过 HSEM1 保护写入。
4. 更新后释放 HSEM2，触发 CM7 HSEM 中断通知。
5. CM7 的 HSEM 回调重新 armed notification，唤醒 `sensor_ipc` 任务。
6. CM7 读取共享数据，并将跨核 heartbeat 置为活跃。

依据：`app/CM4/BSP/Sensor/dht11.c:172-212`、`app/Common/Src/sensor_shared.c`、`app/CM7/Core/Src/freertos.c:174-204`。

### 4.4 当前审计风险

`sensor_ipc_init()` 在 CM4 和 CM7 两侧都会调用，并且初始化函数包含共享数据清零/初始化逻辑，存在一侧初始化覆盖另一侧已写入数据的风险。这个结论是代码审计风险，不代表当前必然复现。依据：`app/Common/Src/sensor_shared.c:39-49`。

## 五、FreeRTOS 参数审计

### 5.1 全局配置

| 参数 | CM7 | CM4 | 依据 |
|---|---:|---:|---|
| Tick rate | 1000 Hz | 1000 Hz | 两核 `FreeRTOSConfig.h` |
| 最大优先级数 | 56 | 未见同样的 56 配置，按 CM4 配置文件为准 | `app/CM7/Core/Inc/FreeRTOSConfig.h`、`app/CM4/Core/Inc/FreeRTOSConfig.h` |
| FreeRTOS heap | 96 KiB override | 24 KiB | 两核 `FreeRTOSConfig.h` |
| 最大 syscall IRQ priority | 5 | 5 | 两核 `FreeRTOSConfig.h` |
| FPU 支持 | 开启 | 开启 | 两核 `FreeRTOSConfig.h` |

### 5.2 任务清单

> CMSIS-RTOS 的 `osThreadNew()` 栈参数在当前代码中通常按 `字节数 = 配置值 * 4` 使用。下表同时给出源码表达式和换算后的字节数。

| 任务名 | 核 | 优先级 | 栈配置 | 周期/阻塞 | 通信或职责 | 依据 |
|---|---|---|---|---|---|---|
| `defaultTask` | CM7 | Normal | `512*4` = 2048 B | 主循环约 1000 ms | 启动 ADC、W5500、OTA、USB Modbus 等 | `app/CM7/Core/Src/freertos.c:68-74,143-166` |
| `sensor_ipc` | CM7 | AboveNormal | `512*4` = 2048 B | 等待 HSEM 回调 task notification | 读取共享传感器数据 | `app/CM7/Core/Src/freertos.c:60-65,174-204` |
| `lvgl_task` | CM7 | AboveNormal | `4096*4` = 16384 B | `osDelay(1)`；页面刷新 500 ms | LVGL handler、UI 数据更新 | `app/CM7/Core/Src/lvgl_demo.c:17-23,84-117` |
| `ota` | CM7 | Realtime | `2048*4` = 8192 B | 接收等待约 100/20/5 ms，随 OTA 状态变化 | OTA 状态机和协议处理 | `app/BSP/OTA/bsp_ota_app.c:15-20,36-55,99-124` |
| `watchdog` | CM7 | Realtime | `384*4` = 1536 B | 每 2000 ms 喂狗 | 软件心跳检查、IWDG feed | `app/BSP/bsp_watchdog.c:12-16,295-305` |
| `adc` | CM7 | Normal | `512*4` = 2048 B | 每 500 ms，16 点采样 | ADC 采样和 EMA | `app/BSP/ADC/bsp_adc.c:14-25,130-165` |
| `w5500` | CM7 | Normal | `1024*4` = 4096 B | 由默认任务启动 | 网络通信 | `app/BSP/W5500/bsp_w5500_conf.h:42-58`、`bsp_w5500_app.c:154-163` |
| `modbusSrv` | CM7 | Normal | `2048*4` = 8192 B | 无数据时约 1 ms 延时 | Modbus 服务 | `app/BSP/Modbus/bsp_modbus_app.c:396-410` |
| `mbCoilStore` | CM7 | BelowNormal | `1024*4` = 4096 B | 按任务逻辑执行 | Modbus coil 保存 | `app/BSP/Modbus/bsp_modbus_app.c:22-25` |
| `mbCoilRestore` | CM7 | BelowNormal | `1024*4` = 4096 B | 恢复流程约 500 ms 延时 | Modbus coil 恢复 | `app/BSP/Modbus/bsp_modbus_app.c:22-25,290-320` |
| `dht11` | CM4 | Normal | `DHT11_TASK_STACK_WORDS*4` | 每 2000 ms | DHT11 采集并更新 IPC | `app/CM4/BSP/Sensor/dht11.c:172-212`、`dht11_conf.h` |

### 5.3 信号量、通知、队列和栈审计

- IPC 事件使用 HSEM IRQ 加 FreeRTOS task notification，不使用复杂队列协议。
- 共享数据保护使用 HSEM1。
- 当前已审计路径没有证据表明 DHT11 数据通过 FreeRTOS queue 传输；实际路径是 Shared SRAM + HSEM。
- OTA 主要通过 transport/ring buffer 和任务状态机处理，不应把 UART 接收 buffer 误称为 FreeRTOS queue。
- CM7 的大栈任务是 `lvgl_task`，为 16 KiB；OTA 为 8 KiB；Modbus server 为 8 KiB。
- heap 参数是 FreeRTOS 可用动态内存上限，不等价于所有任务栈总和。

## 六、UART OTA 参数

### 6.1 UART 配置

| 参数 | 当前值 |
|---|---|
| 外设 | USART6 |
| RX/TX 引脚 | PG9 RX、PG14 TX |
| Alternate Function | AF7 |
| 波特率 | 115200 |
| 数据格式 | 8 数据位、无校验、1 停止位（8N1） |
| 流控 | 无硬件流控 |
| Oversampling | 16 |
| FIFO | 开启，TX/RX threshold 为 1/8 |
| 时钟源 | D2PCLK2 |
| RX DMA | DMA1 Stream2，USART6_RX request，Normal，Low priority |
| TX DMA | DMA1 Stream3，USART6_TX request |
| USART IRQ | 优先级 5 |

依据：`app/CM7/Core/Src/usart.c:187-228,346-416`。

### 6.2 接收路径

当前接收链路为：

```text
USART6
  -> DMA ReceiveToIdle
  -> 768 B DMA buffer
  -> D-Cache invalidate
  -> 2048 B ring buffer
  -> OTA transport
  -> SOIP protocol parser
  -> OTA manager
```

依据：`app/BSP/OTA/bsp_ota_uart.c`。

| 参数 | 当前值 | 依据 |
|---|---:|---|
| DMA 接收 buffer | 768 B | `BSP_OTA_RX_BUF_SIZE`，`.dma_buffer`，32 字节对齐 |
| Ring buffer | 2048 B | `bsp_ota_uart.c:21-27` |
| Write timeout | 200 ms | `BSP_OTA_UART_TIMEOUT_MS` |
| 接收 API | `HAL_UARTEx_ReceiveToIdle_DMA` | `bsp_ota_uart.c:97-109` |
| transport ready | 恒定返回 `1U` | `bsp_ota_transport.c:65-72` |
| transport wait | UART 没有 TCP connected/semaphore 语义，当前按 UART 模式处理 | `bsp_ota_transport.c` |

### 6.3 SOIP 帧格式

```text
SOF(2) | Version(1) | Command(1) | Sequence(2 LE) |
PayloadLen(2 LE) | Payload(N) | CRC16(2 LE)
```

| 项目 | 当前值 |
|---|---:|
| SOF | `AA 55` |
| Version | `0x02` |
| 固定 header | 8 B |
| CRC | CRC16，2 B |
| Payload 理论上限 | 1024 B |
| 当前 OTA chunk | 256 B |
| OTA RX/TX buffer 配置 | 768 B |
| Host 默认 chunk | 256 B |

依据：`app/BSP/OTA/bsp_ota_protocol.h`、`bsp_ota_protocol.c`、`app/BSP/OTA/bsp_ota_conf.h`、`tools/ota-host/protocol/OtaProtocol.h`。

CRC16：覆盖 Version 到 Payload，初值 `0xFFFF`，多项式 `0x1021`。  
CRC32：镜像校验使用 reflected polynomial `0xEDB88320`，初值 `0xFFFFFFFF`，最终取反。

依据：`app/BSP/OTA/bsp_ota_crc.c:14-78`。

### 6.4 帧长和一次发送量

DATA payload 为：

```text
token(4) | offset(4) | chunk_data(256)
```

当前最大 DATA 帧：

```text
固定 header 8 + token/offset 8 + 数据 256 + CRC16 2 = 274 字节
```

Host 的协议理论最大帧为：

```text
8 + 1024 + 2 = 1034 字节
```

但当前 OTA 实际 buffer 为 768 B，且 DATA chunk 为 256 B，因此当前正常 DATA 帧 274 B，可以放入接收 buffer。面试时应明确区分“协议解析器上限 1024 payload”和“当前 OTA 业务 chunk 256”。

chunk 选择 256 B 的工程理由可以从源码确认到：Host 与 MCU 配置一致，并且 274 B DATA 帧明显小于 768 B 接收 buffer。源码没有记录更深入的吞吐 benchmark，因此不要宣称 256 B 是经过完整性能最优实验得出。

### 6.5 “115200 丢字节”问题的参数解释

旧问题是阻塞式 `HAL_UART_Receive` 加 `osDelay` 使 CPU 没有及时取走数据。当前实现改为：

```text
USART FIFO
  -> DMA ReceiveToIdle
  -> Ring Buffer
  -> OTA Task
```

这样把“实时搬运字节”和“协议处理”分开。DMA buffer 是 768 B，软件 ring buffer 是 2048 B，可以吸收 OTA task 短时间未运行的输入。DMA 回调中进行 D-Cache invalidate 并重新 arm，见 `app/BSP/OTA/bsp_ota_uart.c:187-210`。

## 七、Bootloader 参数

### 7.1 跳转流程

当前启动路径：

```text
Reset
  -> Bootloader Reset_Handler/main
  -> HAL/SystemClock/USART1/Metadata 初始化
  -> 读取 BKPSRAM metadata
  -> 检查 Slot A / Slot B 有效性
  -> pending 时 Slot B 拷贝到 Slot A
  -> 读取 Slot A 向量表
  -> 设置 MSP
  -> 跳转 Reset_Handler
  -> APP SystemInit 设置 VTOR
  -> APP main
```

依据：`bootloader/CM7/Core/Src/main.c:74-201`、`bootloader/CM7/Core/Src/boot_jump.c:29-63`。

### 7.2 跳转关键参数

- APP MSP：读取 APP 基地址处的第一个 word。
- APP Reset_Handler：读取 APP 基地址处的第二个 word。
- 跳转前关闭 IRQ、清 SysTick、清 NVIC enable/pending。
- 设置 `MSP` 为 APP 向量表中的初始栈顶。
- `CONTROL` 清零，恢复特权线程/主栈语义。
- 写入 boot magic 后执行 `bx app_entry`。
- Bootloader 跳转函数本身不直接写 `SCB->VTOR`；APP 的 `SystemInit()` 设置 VTOR。

依据：`bootloader/CM7/Core/Src/boot_jump.c`、`app/Common/Src/system_stm32h7xx_dualcore_boot_cm4_cm7.c:278,291`。

VTOR：

| 核 | 当前 APP 向量表目标 |
|---|---:|
| CM7 | `FLASH_BANK1_BASE + 0x20000 = 0x08020000` |
| CM4 | `FLASH_BANK2_BASE + 0 = 0x08100000` |

### 7.3 Metadata 和校验

Metadata：

- 地址：`0x38800000` BKPSRAM；
- magic：`0xB007B007`；
- version：1；
- trial max：3；
- pending：bit0；
- 结构体定义：`app/Common/Inc/boot_meta.h:17-40`。

镜像校验：

- 传输帧使用 CRC16；
- 固件镜像使用 CRC32；
- slot footer 使用 32 B，magic 为 `0x534F4F54`；
- Bootloader 通过 metadata 和 slot validity 决定是否复制/跳转。

本文没有把“有 trial 字段”扩展解释成“已验证 rollback”，因为当前 slot 选择仍固定为 Slot A。

## 八、LCD/LVGL 参数

### 8.1 LCD/LTDC

| 参数 | 当前值 |
|---|---:|
| 分辨率 | 当前 LTDC CubeMX 配置为 800 x 480 |
| 像素格式 | RGB565 |
| Layer | Layer 0 |
| Framebuffer | `0xD0000000` |
| Alpha | 255 |
| HSync | 0 |
| VSync | 0 |
| Accumulated HBP | 46 |
| Accumulated VBP | 23 |
| Active Width | 846 |
| Active Height | 503 |
| Total Width | 1056 |
| Total Height | 525 |

依据：`app/CM7/Core/Src/ltdc.c:42-77`、`app/BSP/LCD/lcd_ltdc.h:72-75`。

BSP LCD 代码还包含 480x272 与 800x480 面板分支，并且会读取 panel ID；因此“当前编译配置”和“运行时面板分支”应分开描述。本文采用 CubeMX/LTDC 当前 800x480 配置作为主参数，未把所有兼容分支都宣称为当前硬件已验证。

### 8.2 Framebuffer 计算

RGB565 每像素 2 字节：

```text
800 * 480 * 2 = 768000 bytes
                 = 750 KiB
```

如果按 480x272 计算：

```text
480 * 272 * 2 = 261120 bytes
```

实际 framebuffer 地址和 LTDC 配置来自 `0xD0000000`；是否启用双 framebuffer、是否完整使用 SDRAM 全容量，需要以当前 LCD 初始化路径和运行时配置继续确认，源码当前明确的是单一主 framebuffer 地址。

### 8.3 LVGL 任务参数

- `lv_init()`、`lv_port_disp_init()`、`lv_port_indev_init()` 在 `app/CM7/Core/Src/lvgl_demo.c:74-79`。
- `lvgl_task` 优先级为 AboveNormal，栈为 `4096*4` 字节。
- 任务每 1 ms 调用 `lv_timer_handler()`。
- 仪表盘数据显示刷新周期为 500 ms。
- Touch 初始化优先尝试 GT9xxx，并兼容 FT5206/FT5426。

## 九、Watchdog 参数

### 9.1 IWDG

| 参数 | 当前值 |
|---|---:|
| 实例 | IWDG1 |
| 时钟源 | LSI |
| Prescaler | 128 |
| Window | 4095 |
| Reload | 2000 |
| 由日志/公式推导的 timeout | 约 8 s |

公式：

```text
timeout ≈ 128 * 2000 / 32000 = 8 s
```

LSI 实际频率会有误差，因此 8 s 是按工程假定 32 kHz 的估算值，不是精密实测值。依据：`app/CM7/Core/Src/iwdg.c:40-54`、`app/CM7/Core/Src/main.c:238-242`。

### 9.2 软件监控参数

| 监控项 | 超时 |
|---|---:|
| LVGL | 5000 ms |
| default task | 8000 ms |
| Modbus | 10000 ms |
| ADC | 5000 ms |
| CM4 heartbeat | 10000 ms |
| W5500 | 60000 ms |

看门狗任务：

- 优先级 Realtime；
- 栈 `384*4` = 1536 B；
- feed 周期 2000 ms；
- 启动宽限期 45000 ms；
- 状态日志间隔参数为 5。

依据：`app/BSP/bsp_watchdog.c:12-16,28-35,295-305`。

CM4 heartbeat 由 CM7 `SensorIpcTask` 在收到 HSEM 通知时更新。依据：`app/CM7/Core/Src/freertos.c:180-195`。

### 9.3 OTA 长操作

OTA 长操作入口调用 `BSP_Watchdog_SetLongOp(1)`，并在相应流程结束时解除。实现使用 reference counter，而不是简单 boolean 覆盖，见 `app/BSP/OTA/bsp_ota_port.c:31-45` 和 `app/BSP/bsp_watchdog.c:48-49`。

当前长操作模式在 watchdog 检查路径中主要保留 W5500 slot 检查，见 `bsp_watchdog.c:200-204`。这属于当前实现细节；由于当前 OTA transport 已切换到 UART，面试时应主动说明这是需要持续复盘的耦合点，而不是声称长操作逻辑已经完全按 UART 语义重构。

## 十、调试案例与参数

### 10.1 HSEM notification 失效

**现象：** CM7 的 `sensor_ipc` 任务没有按预期被唤醒，或共享数据更新表现为偶发不及时。  

**定位：**

1. 检查 HSEM2 是否由 CM4 release。
2. 检查 CM7 HSEM IRQ 是否使能、优先级是否落在 FreeRTOS 可调用范围。
3. 检查回调是否重新 armed notification。
4. 检查 HSEM1 的数据保护和共享对象 magic/valid。
5. 检查 CM7/CM4 是否重复初始化共享对象。

**涉及参数/寄存器：** HSEM channel 2、HSEM IRQ、FreeRTOS syscall priority 5、Shared SRAM `0x38000000`。  

**代码依据：** `sensor_shared.c:65-68,99-116`、两核 `main.c`、两核 `FreeRTOSConfig.h`。  

**根因/修复结论：** 当前源码能确认的修复设计是 HSEM2 作为事件通知、回调重新 armed、任务使用 notification 阻塞；如果现场案例具体根因是 NVIC 配置或初始化顺序，应以调试记录和寄存器现场为准，不能仅从静态源码强行归因。

### 10.2 UART OTA 丢包

**现象：** 115200 下使用阻塞式接收加 `osDelay` 时出现丢字节、帧 CRC 错误或协议不同步。  

**定位：**

- 检查 USART6 FIFO 是否开启；
- 检查 DMA1 Stream2 是否正确连接 USART6_RX；
- 检查 `HAL_UARTEx_ReceiveToIdle_DMA` 是否重新 arm；
- 检查 DMA buffer 是否在 D-Cache 下 invalidate；
- 检查 768 B DMA buffer 与 2048 B ring buffer 的写入边界；
- 检查 Host chunk 256 与 MCU chunk 256 是否一致。

**最终设计参数：** USART6、115200 8N1、DMA ReceiveToIdle、768 B DMA buffer、2048 B ring buffer、SOIP DATA 最大当前帧 274 B。  

依据：`usart.c`、`bsp_ota_uart.c`、`bsp_ota_conf.h`、Qt Host `SerialManager.cpp`。

### 10.3 Watchdog long operation bug

**现象：** OTA 或其他长操作持续期间，某个业务模块的 heartbeat 暂时不更新，IWDG 被误判为系统失活。  

**定位：**

- 检查 watchdog 是否使用 boolean 记录长操作；
- 检查嵌套调用是否会提前清除长操作标志；
- 检查 IWDG reload 和软件超时是否混淆；
- 检查长操作检查分支是否仍绑定 W5500。

**修复：** 使用 reference counter，进入长操作递增，退出递减，避免一个调用者提前覆盖另一个调用者状态。  

**参数：** IWDG 约 8 s、watchdog feed 2000 ms、OTA 长操作计数器。  

依据：`bsp_watchdog.c:48-49`、`bsp_ota_port.c:31-45`。

### 10.4 CM4 heartbeat 缺失

**现象：** DHT11 迁移到 CM4 后，CM7 原有 watchdog 仍按 CM7 本地任务思路监控，无法发现 CM4 已停止。  

**定位：**

- 检查 watchdog slot 是否存在 CM4；
- 检查 CM4 是否在每次有效采集后触发 HSEM2；
- 检查 CM7 `sensor_ipc` 是否在 HSEM notification 后更新 CM4 heartbeat；
- 检查 10000 ms CM4 timeout 与 DHT11 2000 ms 周期是否匹配。

**修复：** 采用跨核 heartbeat：CM4 采集/通知，CM7 Sensor IPC task 收到事件后更新 CM4 watchdog slot。  

**参数：** DHT11 周期 2000 ms，CM4 heartbeat timeout 10000 ms，HSEM2 事件通知。  

## 十一、审计边界与面试表达注意事项

1. 代码存在不等于硬件已验证。本文没有在本次审计中进行示波器、逻辑分析仪、实板温湿度、LCD 触摸或 OTA 长时间压力测试。
2. “CM7 约 480 MHz”是基于时钟配置和 H7 分频的推导，不是本次实测。
3. “SDRAM 8 MB”来自地址空间/注释线索，最终应由 FMC 初始化参数、BOM 或芯片型号确认。
4. Slot B 当前是下载/暂存意义上的第二槽，不能直接说成完整 A/B rollback。
5. W5500、Modbus 代码在当前工程中存在并可能被任务启动，但不应在项目核心经历中包装成已完成的主线功能，除非面试官专门追问。
6. OTA 配置注释中曾出现 chunk 512 的描述，但当前配置和 Qt Host 默认 chunk 为 256；面试时以实际宏和 Host 实现为准，并指出注释与实现存在不一致。
7. `bsp_ota_app.h` 等历史注释仍可能出现 TCP 字样，当前 transport 实现已经是 UART 语义；应以调用链和实际代码为准。

## 十二、模拟面试官问题

以下问题按“问题 / 当前源码回答 / 追问方向”组织。回答只覆盖当前工程能支撑的结论。

### 硬件和时钟

#### 1
Q: 这个项目使用什么 MCU？

A: 构建宏是 `STM32H757xx`，工程分成 Cortex-M7 和 Cortex-M4 两个目标。

追问: 如何从构建系统证明不是单核工程？  

#### 2
Q: 为什么使用 H757 双核？

A: CM7 承担 GUI、FreeRTOS、OTA 和主应用，CM4 承担 DHT11 周期采集，利用双核隔离采集和应用负载。

追问: 双核通信采用什么机制？

#### 3
Q: CM7 和 CM4 的 FPU 配置是什么？

A: CM7 使用 `fpv5-d16`，CM4 使用 `fpv4-sp-d16`，两者均为硬浮点构建。

追问: 浮点上下文会带来什么 RTOS 成本？

#### 4
Q: 工程时钟树的 PLL 参数是什么？

A: HSE 25 MHz，PLLM=5、PLLN=192、PLLR=1，PLL VCO 推导为 960 MHz，AHB divider 为 2。

追问: PLL VCO 和 CPU 实际 HCLK 有什么区别？

#### 5
Q: 你如何确认 CM7 主频？

A: 先看 `SystemClock_Config()` 的 PLL 和域分频，再通过 RCC 寄存器或示波器/运行时时钟测量验证；源码本身只能给出配置推导。

追问: 为什么不能直接把 PLL 输出称为 CPU 主频？

#### 6
Q: HSE 的频率是多少？

A: 工程定义为 25 MHz。

追问: 如果板上晶振变成 24 MHz，哪些参数会受影响？

#### 7
Q: IWDG 使用什么时钟？

A: 使用 LSI。

追问: LSI 不精确会怎样影响 timeout？

#### 8
Q: 当前工程有哪些主要外设？

A: USART6 OTA、LTDC LCD、FMC SDRAM、HSEM、IWDG、ADC、Touch，以及代码中的 W5500/Modbus 模块。

追问: 哪些是本项目面试主线？

#### 9
Q: USART6 的引脚是什么？

A: PG9 为 RX，PG14 为 TX，AF7。

追问: 如何验证 AF 复用和板级连接一致？

#### 10
Q: 为什么 CM7 负责 GUI 和 OTA？

A: 这类功能对内存、实时调度和外设协同要求更高，CM7 性能更强；CM4 用于相对独立的低频传感采集。

追问: 如果 GUI 卡顿，CM4 能否替 CM7 分担？

#### 11
Q: SDRAM 在系统中做什么？

A: 主要用于 LCD framebuffer，并预留 OTA scratch 地址。

追问: SDRAM 是否适合放任意 FreeRTOS 栈？

#### 12
Q: DTCM、AXI SRAM 和 Shared SRAM 有什么区别？

A: DTCM/主 RAM 用于普通变量和栈，AXI SRAM 放高速数据和 DMA buffer，Shared SRAM 专门用于双核共享并配置为非缓存。

追问: 为什么不把 DMA buffer 放 DTCM？

#### 13
Q: 当前 Framebuffer 的地址是多少？

A: `0xD0000000`。

追问: 这个地址对应哪个存储器？

#### 14
Q: STM32H7 为什么要关注 D-Cache？

A: CM7 有数据缓存，DMA 或跨核共享数据可能看到旧 cache line，所以要通过 MPU 属性或 clean/invalidate 维护一致性。

追问: Shared SRAM 和 UART DMA buffer 的 cache 处理是否相同？

#### 15
Q: 你如何证明 Shared SRAM 是 64 KiB？

A: 两核链接脚本都声明 `0x38000000`、长度 `64K` 的 `SHARED_RAM`。

追问: 共享对象是否占满整个区域？

### Flash 和启动

#### 16
Q: Bootloader 放在哪里？

A: `0x08000000`，链接区域 128 KiB。

追问: APP 为什么不能从 `0x08000000` 启动？

#### 17
Q: CM7 APP Slot A 的地址和大小？

A: `0x08020000`，448 KiB。

追问: 向量表偏移是多少？

#### 18
Q: Slot B 的作用是什么？

A: 当前主要是 OTA 下载/暂存区域，Bootloader pending 时会把它复制到 Slot A。

追问: 这是不是完整 A/B rollback？

#### 19
Q: CM4 固件放在哪里？

A: CM4 链接脚本从 `0x08100000` 开始，区域 1024 KiB。

追问: CM4 的 VTOR 怎么设置？

#### 20
Q: Metadata 放在哪里？

A: `0x38800000` 的 BKPSRAM。

追问: 为什么不用普通 Flash？

#### 21
Q: 当前是否支持真正 rollback？

A: 不能从当前源码确认支持完整 rollback；启动选择函数固定选择 Slot A，Slot B 是 staging。

追问: 真正 rollback 还需要哪些状态机？

#### 22
Q: Bootloader 如何判断 APP 是否有效？

A: 读取 metadata 和 slot/footer 等有效性信息，再决定复制或跳转。

追问: 仅检查 magic 是否足够？

#### 23
Q: Bootloader 为什么要复制 Slot B 到 Slot A？

A: 当前执行地址固定在 Slot A，Slot B 接收完成后需要迁移到执行槽。

追问: 复制期间掉电怎么办？

#### 24
Q: APP 跳转前如何设置 MSP？

A: 从 APP 基地址第一个 word 读取初始栈顶，调用 `__set_MSP()`。

追问: 第二个 word 是什么？

#### 25
Q: Reset_Handler 地址从哪里来？

A: 从 APP 向量表第二个 word 读取并跳转。

追问: 为什么需要清理 NVIC pending？

#### 26
Q: Bootloader 跳转函数是否设置 VTOR？

A: 跳转函数主要设置 MSP、清中断和执行跳转；APP 的 `SystemInit()` 负责设置 VTOR。

追问: 如果 APP 不设置 VTOR 会发生什么？

#### 27
Q: CM7 VTOR 目标地址是什么？

A: `0x08020000`。

追问: 如何避免中断仍然指向 Bootloader 向量表？

#### 28
Q: CM4 VTOR 目标地址是什么？

A: `0x08100000`。

追问: CM4 是由谁释放或唤醒的？

#### 29
Q: Bootloader 启动阶段为什么关注 CM4 STOP？

A: 启动流程需要在修改/检查关键启动状态时避免 CM4 并行运行造成干扰，代码有等待 CM4 stop 的超时逻辑。

追问: 超时后系统如何处理？

#### 30
Q: metadata 中有哪些关键字段？

A: magic、version、pending 标志、trial 相关字段等。

追问: trial max=3 是否等于已实现三次回滚？

#### 31
Q: Bootloader 是否修改已有 Flash/RAM 布局？

A: 它使用独立链接脚本；本文只按源码现有地址审计，没有改动布局。

追问: 链接地址和向量表地址不一致会怎样？

#### 32
Q: 为什么要在跳转前关闭 SysTick？

A: 防止 Bootloader 的系统 tick 在 APP 尚未完成初始化时继续产生中断。

追问: 还要清哪些状态？

#### 33
Q: 你如何解释 `0x08080000` 的 shared sector？

A: OTA 配置声明了该地址和 128 KiB 大小，但它与 Slot B 地址范围存在重叠，需结合实际擦写流程确认，不能在面试中直接宣称是独立安全分区。

追问: 如何用 map 文件验证？

#### 34
Q: 为什么 metadata 放 BKPSRAM 仍要考虑掉电？

A: BKPSRAM 是否保持取决于电源域、备份电源和复位类型，不能把它当作永久非易失存储。

追问: 量产设计会怎么增强？

#### 35
Q: OTA 的 CRC16 和镜像 CRC32 分别解决什么问题？

A: CRC16 保护传输帧，CRC32 保护完整镜像内容。

追问: 为什么不只使用一种 CRC？

### 双核 IPC

#### 36
Q: CM4 采集数据如何到 CM7？

A: CM4 写 `0x38000000` 的共享结构，释放 HSEM2，CM7 中断回调唤醒 IPC 任务后读取。

追问: 数据结构有哪些字段？

#### 37
Q: 共享结构的 magic 是多少？

A: `0x53484D31`。

追问: magic 和 valid 各自解决什么问题？

#### 38
Q: Shared SRAM 为什么配置为 non-cacheable？

A: 避免 CM7 cache 中存在跨核共享数据的陈旧副本，简化一致性。

追问: 这样是否意味着完全不需要内存屏障？

#### 39
Q: HSEM0、HSEM1、HSEM2 分别做什么？

A: HSEM0 启动握手，HSEM1 数据访问保护，HSEM2 事件通知。

追问: 为什么不只用一个 semaphore？

#### 40
Q: HSEM2 是锁还是通知？

A: 主要是通知；共享数据互斥由 HSEM1 负责。

追问: 通知丢失会如何处理？

#### 41
Q: CM7 如何等待 CM4 数据？

A: `sensor_ipc` 任务阻塞在 FreeRTOS task notification 上，由 HSEM 回调唤醒。

追问: 为什么不轮询？

#### 42
Q: CM4 采集周期是多少？

A: DHT11 任务约 2000 ms。

追问: CM4 heartbeat timeout 为什么是 10000 ms？

#### 43
Q: CM4 的 DHT11 驱动是否还在 CM7？

A: 当前架构要求 CM4 负责采集，CM7 通过 IPC 读共享数据；源码审计时应区分驱动文件保留与调用关系迁移。

追问: 如何证明 CM7 没有直接访问 GPIO？

#### 44
Q: IPC 是否使用 OpenAMP 或 RPMsg？

A: 当前审计路径使用 Shared SRAM + HSEM，没有把 OpenAMP/RPMsg 作为通信框架。

追问: 这种方案的边界是什么？

#### 45
Q: 共享数据对象为什么 32 字节对齐？

A: 便于按 cache line 对齐管理，降低跨 cache line 的一致性复杂度。

追问: 对齐是否自动保证原子性？

#### 46
Q: float 温湿度字段是否可能撕裂？

A: 不能仅靠对齐保证跨核一致读取，所以代码使用 HSEM1 保护访问。

追问: 如果要无锁读取，如何设计版本号？

#### 47
Q: `sensor_ipc_init()` 双核都调用有什么风险？

A: 如果初始化包含清零共享数据，可能发生一核覆盖另一核已写数据的竞态。

追问: 如何改成单边初始化？

#### 48
Q: HSEM IRQ 的优先级为什么重要？

A: 回调需要和 FreeRTOS notification 配合，必须满足 FreeRTOS 可调用中断优先级限制。

追问: 当前最大 syscall interrupt priority 是多少？

#### 49
Q: CM7 怎么知道 CM4 活着？

A: 每次收到 CM4 的 HSEM2 数据通知时，`sensor_ipc` 任务更新 CM4 heartbeat。

追问: 如果传感器读失败但 CM4 仍运行，heartbeat 是否应更新？

#### 50
Q: Shared SRAM 的大小是否等于传感器数据包大小？

A: 不是，区域 64 KiB，当前只放一个对齐的共享对象和相关共享数据。

追问: 未来扩展多个传感器要注意什么？

### FreeRTOS

#### 51
Q: CM7 的 tick rate 是多少？

A: 1000 Hz。

追问: 1 ms tick 对功耗和调度开销有什么影响？

#### 52
Q: CM7 heap 配置是多少？

A: 配置文件中有 96 KiB heap override。

追问: heap 和链接脚本 RAM 是什么关系？

#### 53
Q: CM4 heap 配置是多少？

A: 24 KiB。

追问: 为什么 CM4 可以使用更小 heap？

#### 54
Q: 哪个任务栈最大？

A: `lvgl_task`，`4096*4`，约 16 KiB。

追问: 如何验证栈没有溢出？

#### 55
Q: OTA 任务优先级是多少？

A: CMSIS-RTOS 配置为 Realtime。

追问: 高优先级 OTA 会不会饿死 GUI？

#### 56
Q: watchdog 任务优先级是多少？

A: Realtime。

追问: 为什么 watchdog 需要高优先级？

#### 57
Q: Sensor IPC 任务如何阻塞？

A: 等待 HSEM 回调发出的 task notification。

追问: notification 和 queue 的差异是什么？

#### 58
Q: LVGL handler 周期是多少？

A: 任务每 1 ms 调用 `lv_timer_handler()`，页面数据刷新逻辑约 500 ms。

追问: handler 周期和 UI 数据周期为什么不同？

#### 59
Q: ADC 任务参数是什么？

A: 500 ms 采样周期、16 点采样、EMA 系数 1/8。

追问: 16 点采样对响应速度和噪声有什么影响？

#### 60
Q: DHT11 任务栈大小如何确定？

A: 由 `DHT11_TASK_STACK_WORDS*4` 决定，具体宏值应以 `dht11_conf.h` 为准。

追问: 为什么不能只看任务名猜栈大小？

#### 61
Q: 当前任务之间如何通信？

A: IPC 通过 HSEM + Shared SRAM；CM7 内部还使用 task notification、驱动自身的 buffer/状态机。

追问: 哪些地方使用了 queue？

#### 62
Q: `defaultTask` 做什么？

A: 负责启动 ADC、W5500、OTA、USB Modbus 等应用模块。

追问: 在 `defaultTask` 里启动太多模块有什么风险？

#### 63
Q: 为什么 Modbus server 无数据时要 delay？

A: 避免空循环持续占用 CPU；代码使用约 1 ms 延时。

追问: 更好的阻塞式接收方案是什么？

#### 64
Q: `osThreadNew` 的 stack 参数单位是什么？

A: 当前代码按 `配置值*4` 传入，审计时应同时记录源码表达式和换算字节数。

追问: CMSIS-RTOS2 的 stack_size 语义要如何确认？

#### 65
Q: FreeRTOS 最大 syscall IRQ priority 是多少？

A: 5。

追问: HSEM ISR 调用 RTOS API 时要检查什么？

#### 66
Q: 任务周期和 deadline 有什么不同？

A: 周期是代码里的 delay/调度间隔，deadline 是系统必须完成的时间；源码当前主要给出了周期，没有完整 deadline 表。

追问: 如何给 OTA 任务定义 deadline？

#### 67
Q: 栈大小是否包含 heap？

A: 不包含，任务栈和 FreeRTOS heap 是不同的内存资源。

追问: 如何做 high-water mark 审计？

#### 68
Q: 为什么 LVGL 栈比 Sensor IPC 大？

A: GUI 调用链更深、局部对象和绘制处理更复杂；最终仍应以栈水位和运行时 profiling 验证。

追问: GUI 栈放 DTCM 是否合适？

#### 69
Q: Realtime 优先级是硬实时保证吗？

A: 不是。它只是 RTOS 优先级枚举映射，不能替代 WCET、IRQ 延迟和调度分析。

追问: 如何建立硬实时证据？

#### 70
Q: 任务创建在哪里发生？

A: 主要在 `freertos.c`、LVGL、OTA、watchdog 和各 BSP app 的初始化函数中通过 `osThreadNew()` 创建。

追问: 任务创建顺序会影响哪些启动问题？

### UART OTA

#### 71
Q: OTA 使用哪个串口？

A: USART6。

追问: 为什么不再用 W5500 TCP transport？

#### 72
Q: UART 参数是什么？

A: 115200、8N1、无硬件流控、FIFO 开启。

追问: PC Qt Host 是否支持串口？

#### 73
Q: Qt Host 如何配置串口？

A: `QSerialPort` 使用 Data8、NoParity、OneStop、NoFlowControl。

追问: MCU 和 Qt 的参数是否完全匹配？

#### 74
Q: USART6 RX DMA 是哪个 stream？

A: DMA1 Stream2。

追问: 如何验证 request mapping？

#### 75
Q: TX DMA 是哪个 stream？

A: DMA1 Stream3。

追问: TX 为什么当前写路径仍有阻塞 HAL 发送？

#### 76
Q: DMA buffer 多大？

A: 768 字节。

追问: 为什么不是 256 字节？

#### 77
Q: ring buffer 多大？

A: 2048 字节。

追问: 如何证明 2048 足以吸收任务抖动？

#### 78
Q: ReceiveToIdle 解决什么问题？

A: DMA 持续接收数据，并以空闲线或 DMA 事件通知本次收到的数据长度，减少逐字节轮询。

追问: 回调中为什么要重新 arm？

#### 79
Q: D-Cache 对 UART DMA 有什么影响？

A: DMA 写内存后，CPU 可能仍读 cache 中旧内容，因此回调中需要 invalidate 对应区域。

追问: invalidate 前后要注意 cache line 对齐吗？

#### 80
Q: OTA 的 SOF 是什么？

A: `AA 55`。

追问: SOF 丢失后解析器如何重新同步？

#### 81
Q: SOIP header 多大？

A: 固定 header 8 字节。

追问: header 中有哪些字段？

#### 82
Q: 当前 chunk 多大？

A: 256 字节。

追问: payload 上限为什么是 1024？

#### 83
Q: DATA 帧的 payload 有哪些部分？

A: 4 字节 token、4 字节 offset、再加 chunk data。

追问: offset 为什么需要 4 字节？

#### 84
Q: 当前 DATA 帧最大多大？

A: 8 字节 header + 8 字节 token/offset + 256 字节数据 + 2 字节 CRC，即 274 字节。

追问: 这和 768 字节 buffer 的关系是什么？

#### 85
Q: CRC16 的参数是什么？

A: 初值 `0xFFFF`，多项式 `0x1021`，覆盖 Version 到 Payload。

追问: 是否包含 SOF？

#### 86
Q: 镜像 CRC32 的参数是什么？

A: reflected polynomial `0xEDB88320`，初值 `0xFFFFFFFF`，最终取反。

追问: CRC32 失败后设备做什么？

#### 87
Q: Host 重试次数是多少？

A: 5 次。

追问: ACK timeout 和 retry delay 是多少？

#### 88
Q: Host window size 是多少？

A: 1，当前是停等式/单窗口传输倾向。

追问: 这对吞吐有什么影响？

#### 89
Q: Host 包间延时是多少？

A: 默认 2 ms。

追问: 115200 下 2 ms 是否足够？

#### 90
Q: 为什么不直接把 chunk 调到 1024？

A: 当前协议上限允许 1024，但业务 buffer、协议栈、超时和丢包重传代价需要一起评估；当前实际配置是 MCU/Host 一致的 256。

追问: 你会如何做 chunk benchmark？

#### 91
Q: transport ready 为什么直接返回 1？

A: UART 没有 TCP connected 状态，串口打开后不需要网络连接握手，因此当前 transport 层把 ready 视为可用。

追问: 串口拔出/重连如何感知？

#### 92
Q: UART wait_rx 是否有 semaphore？

A: 当前 UART 模式没有 W5500 TCP RX semaphore 语义，等待策略不能机械复用网络实现。

追问: 未来如何引入 notification？

#### 93
Q: OTA 发送路径是 DMA 还是阻塞？

A: 当前 `bsp_ota_uart_write()` 使用 `HAL_UART_Transmit`，接收路径是 DMA ReceiveToIdle；收发路径并不完全对称。

追问: 发送阻塞会不会影响 GUI？

#### 94
Q: Qt Host 的最大 payload 是多少？

A: `kMaxPayload=1024`，但默认 chunk 是 256。

追问: Host 与 MCU 两个上限不一致时如何避免越界？

#### 95
Q: OTA 使用哪些地址配置？

A: shared sector base `0x08080000`、size 128 KiB，SDRAM backup `0xD07F0000`、size 64 KiB。

追问: shared sector 与 Slot B 是否有地址重叠？

#### 96
Q: UART FIFO threshold 是多少？

A: TX/RX threshold 均为 1/8。

追问: FIFO threshold 对中断/DMA触发有什么影响？

#### 97
Q: USART6 中断优先级是多少？

A: 5。

追问: 它和 FreeRTOS syscall priority 的关系是什么？

#### 98
Q: UART 丢包问题能否只靠提高波特率解决？

A: 不能。核心问题是接收搬运和处理解耦、DMA re-arm、cache 一致性和 ring buffer 容量。

追问: 提高到 921600 还需要重新审计什么？

#### 99
Q: 为什么保留 W5500 文件？

A: 本次 UART OTA 切换只改变 OTA BSP transport，不能删除其他网络模块文件；是否启用由调用和配置决定。

追问: 如何证明 transport 已经不直接依赖 W5500？

#### 100
Q: OTA 协议是否被改了？

A: 当前审计对象是 transport 和 UART 收发路径；SOIP 帧格式、Qt Host 协议字段和 CRC 规则保持现有实现。

追问: 如何做 Host/Device 互操作测试？

### LCD、MPU 和 SDRAM

#### 101
Q: LCD 分辨率是多少？

A: 当前 LTDC 配置为 800x480，同时 BSP 代码保留 480x272/800x480 分支。

追问: 如何确认运行时实际面板分支？

#### 102
Q: RGB565 每像素多少字节？

A: 2 字节。

追问: 800x480 单 framebuffer 占多少 RAM？

#### 103
Q: Framebuffer 大小怎么算？

A: `800*480*2=768000` 字节，约 750 KiB。

追问: 如果做双 framebuffer 需要多少？

#### 104
Q: LTDC timing 中 TotalWidth 和 TotalHeight 是多少？

A: 1056 和 525。

追问: 为什么 ActiveWidth 不是 800？

#### 105
Q: LTDC 使用哪个 layer？

A: Layer 0。

追问: Alpha 值是多少？

#### 106
Q: LVGL 的显示和输入初始化在哪里？

A: `lvgl_demo.c` 中调用 `lv_init`、`lv_port_disp_init`、`lv_port_indev_init`。

追问: Touch 支持哪些控制器？

#### 107
Q: MPU 为什么配置 SDRAM 为 cacheable？

A: 当前 `0xD0000000` 区域配置为 cacheable/bufferable，以提高 CPU 访问效率；DMA/LTDC 一致性仍需结合实际刷新策略验证。

追问: Framebuffer 更新后何时 clean cache？

#### 108
Q: SDRAM OTA scratch 地址是多少？

A: `0xD07F0000`，大小 64 KiB。

追问: 这个地址如何确保不覆盖 framebuffer？

#### 109
Q: Touch 初始化的探测顺序是什么？

A: 优先尝试 GT9xxx，再兼容 FT5206/FT5426。

追问: I2C 总线错误如何恢复？

#### 110
Q: 为什么 GUI 任务需要 16 KiB 栈？

A: LVGL 调用链和绘制路径比简单传感任务复杂，当前源码配置为 `4096*4`；最终应结合 high-water mark 验证。

追问: 如何降低 LVGL 栈占用？

### Watchdog 和可靠性

#### 111
Q: IWDG reload 是多少？

A: 2000。

追问: 按 32 kHz LSI 估算 timeout 是多少？

#### 112
Q: 软件 watchdog 和硬件 IWDG 的区别是什么？

A: 软件 watchdog 判断各任务 heartbeat，硬件 IWDG 在系统整体未及时喂狗时复位 MCU。

追问: 为什么两层都需要？

#### 113
Q: CM4 timeout 配置是多少？

A: 10000 ms。

追问: DHT11 周期是 2000 ms，为什么不是 3000 ms？

#### 114
Q: OTA 期间如何避免 watchdog 误复位？

A: 长操作使用 reference counter，进入/退出成对计数，并让 watchdog 依据长操作状态调整检查策略。

追问: 如果 OTA 任务真的死锁，如何保证最终仍能复位？

#### 115
Q: 为什么 boolean 长操作标志有 bug？

A: 多个调用者嵌套时，一个调用者清零会覆盖另一个仍在进行的长操作；reference counter 能保留嵌套状态。

追问: counter 溢出如何防护？

#### 116
Q: W5500 watchdog timeout 是多少？

A: 60000 ms。

追问: 当前 OTA 已经使用 UART，为什么代码仍有 W5500 长操作分支？

#### 117
Q: watchdog feed 周期是多少？

A: 2000 ms。

追问: 和约 8 s IWDG timeout 的裕量是多少？

#### 118
Q: 启动宽限期是多少？

A: 45000 ms。

追问: 为什么启动阶段需要 grace period？

#### 119
Q: 心跳更新点在哪里？

A: CM7 侧 Sensor IPC task 收到 HSEM 通知后更新 CM4 heartbeat。

追问: 如果 HSEM 中断丢失，软件 watchdog 能否区分 CM4 死机和通信故障？

#### 120
Q: 如何验证 watchdog 参数？

A: 通过故障注入停止任务、阻塞中断、模拟 CM4 无通知，并用复位原因寄存器和日志确认实际复位路径。

追问: 当前源码是否给出完整复位原因记录？

### 综合设计

#### 121
Q: 这个工程最大的实时性风险是什么？

A: UART/OTA接收搬运、GUI 高频 handler、DMA cache 一致性和高优先级长操作之间的相互影响。

追问: 你会先测哪一项？

#### 122
Q: 为什么不用 OpenAMP？

A: 第一阶段需求是简单传感数据通道，Shared SRAM + HSEM 足够，复杂框架会增加依赖和调试面。

追问: 什么时候值得引入 OpenAMP/RPMsg？

#### 123
Q: 你如何证明没有修改 Qt Host 协议？

A: 对照 `tools/ota-host/protocol/OtaProtocol.h` 的帧字段、chunk、CRC 和 ACK 参数，并检查 MCU parser 对应实现。

追问: Host 的 SerialManager 使用什么 Qt 类？

#### 124
Q: 为什么不把 DHT11 采集留在 CM7？

A: 迁移后可把低频、时序敏感的传感器采集与 UI/OTA 负载隔离，并展示双核任务分工。

追问: DHT11 本身是否适合高实时采样？

#### 125
Q: 这个系统的主要内存压力来自哪里？

A: LCD framebuffer、LVGL 栈/对象、OTA buffer 和 SDRAM/AXI SRAM 数据区。

追问: 如何查内存碎片和峰值？

#### 126
Q: 如何做一次完整 OTA 压力测试？

A: 重复发送不同长度镜像，覆盖丢包、重传、掉电、CRC错误、边界 chunk 和长时间 GUI 并发运行。

追问: 当前源码能直接证明掉电恢复吗？

#### 127
Q: Slot A/B 设计的主要缺陷是什么？

A: 当前 Slot B 不是独立运行槽，启动选择固定 Slot A，真正 rollback 证据不足。

追问: 如何改成真正 A/B？

#### 128
Q: 如果 CM7 Cache 开启，最容易漏掉哪类 bug？

A: DMA 写入后 CPU 读到旧 cache、共享结构更新不可见、framebuffer 刷新内容不一致。

追问: 哪些区域当前被 MPU 设置为 non-cacheable？

#### 129
Q: 你如何做启动时间分析？

A: 对 Reset_Handler、Bootloader metadata/CRC、CM7 SystemInit、CM4 唤醒、FreeRTOS scheduler start 分段打点。

追问: 代码当前是否已有完整启动时间戳？

#### 130
Q: 面对一个“参数很多”的工程，你如何避免背错？

A: 以链接脚本、配置头、CubeMX 初始化源文件和 Host 常量为一手证据；对公式推导和未实测项明确标注。

追问: 你会把哪些参数放进量产校准表？

