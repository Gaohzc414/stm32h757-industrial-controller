# STM32H757 工程整体架构审计

## 结论范围

本文只描述当前仓库中能够由源码和构建结果确认的内容。

- CM7 和 CM4 应用工程均已使用独立的 `build_audit` 目录完成编译、链接。
- 当前没有板级运行日志、示波器或调试器记录，因此双核通信、LCD、触摸和 OTA 的“硬件实际效果”不能仅凭源码认定为已验证。
- `W5500`、Modbus、ESP8266 等代码仍存在于仓库或构建配置中，但不作为本项目面试版成果展开。

## 工程目录树

下面是过滤 `build`、编译中间文件和大体量第三方库内部文件后的功能目录树。`Drivers/CMSIS`、`Middlewares` 和 Bootloader 自带 HAL/CMSIS 的内部文件没有逐个展开，但目录职责和实际构建入口均保留。

```text
project/
├── app/
│   ├── CM7/
│   │   ├── Core/
│   │   │   ├── Inc/                 # CM7 头文件、外设句柄、FreeRTOS 配置
│   │   │   └── Src/                 # CM7 main、时钟、外设、任务和中断
│   │   ├── FATFS/
│   │   │   ├── App/                 # FatFs 应用适配
│   │   │   └── Target/              # SD 卡块设备和 diskio
│   │   └── USB_DEVICE/
│   │       ├── App/                 # USB Device、CDC
│   │       └── Target/              # USB 底层适配
│   ├── CM4/
│   │   ├── Core/
│   │   │   ├── Inc/                 # CM4 头文件、FreeRTOS 配置
│   │   │   └── Src/                 # CM4 main、FreeRTOS、中断、DMA
│   │   └── BSP/
│   │       └── Sensor/              # CM4 DHT11 驱动和采集任务
│   ├── Common/
│   │   ├── Inc/                     # 双核共享头文件、boot 公共接口
│   │   └── Src/                     # SystemInit、sensor_shared、公共 boot 代码
│   ├── BSP/
│   │   ├── ADC/                     # ADC 采集 BSP
│   │   ├── DHT11/                   # 保留的历史/独立 DHT11 BSP
│   │   ├── LCD/                     # LCD 控制和 LTDC 适配
│   │   ├── LVGL/                    # LVGL 显示/输入移植
│   │   ├── Log/                     # 日志输出
│   │   ├── MPU/                     # MPU 和共享区属性配置
│   │   ├── Modbus/                  # USB/Modbus 相关代码，非本复盘主成果
│   │   ├── OTA/                     # OTA BSP、传输、Flash、CRC、协作
│   │   ├── SDRAM/                   # 外部 SDRAM 初始化
│   │   ├── TOUCH/                   # GT9xxx、FT5206、I2C 触摸
│   │   ├── W5500/                   # W5500 网络 BSP，仓库保留但不纳入面试成果
│   │   └── bsp_*.c/.h               # 应用数据、延时、系统、看门狗
│   ├── Drivers/
│   │   ├── STM32H7xx_HAL_Driver/    # STM32H7 HAL/LL 驱动
│   │   └── CMSIS/                   # CMSIS Core、Device、RTOS 等
│   ├── Middlewares/
│   │   ├── Third_Party/
│   │   │   ├── FreeRTOS/            # RTOS 内核和 CMSIS-RTOS V2 适配
│   │   │   ├── FatFs/               # 文件系统
│   │   │   └── libmodbus/           # Modbus 第三方库
│   │   ├── LVGL/                    # LVGL 内核和 GUI 资源
│   │   ├── ST/                      # STM32 USB Device Library
│   │   └── GUI/GUI_APP/             # GUI 相关资源/代码
│   └── Makefile/
│       ├── CM7/                     # CM7 Makefile、startup、linker
│       └── CM4/                     # CM4 Makefile、startup、linker
├── bootloader/
│   ├── CM7/
│   │   ├── Core/Inc                 # Bootloader 配置和头文件
│   │   └── Core/Src                 # Bootloader main、跳转、外设
│   ├── CM4/
│   │   ├── Core/Inc                 # Bootloader CM4 工程文件
│   │   └── Core/Src                 # CM4 启动工程文件
│   ├── Common/
│   │   ├── Inc                     # metadata、slot、Flash RAM 接口
│   │   └── Src                     # metadata、slot 校验、B->A 拷贝
│   ├── Drivers/                    # Bootloader 独立 HAL/CMSIS 副本
│   └── Makefile/
│       ├── CM7/                    # Bootloader CM7 构建、startup、linker
│       └── CM4/                    # Bootloader CM4 构建文件
├── tools/
│   └── ota-host/
│       ├── engine/                 # Qt OTA 更新流程
│       ├── protocol/               # Qt Host 协议和 CRC
│       ├── transport/              # SerialManager、NetworkManager、抽象传输
│       └── docs/                   # Host 协议和设计说明
├── docs/                           # 原理图、调试器、显示屏资料和工程审计文档
├── scripts/                        # 当前仓库中的辅助脚本目录；无活动源码构建入口
├── .claude/                        # 开发辅助配置
├── .gitattributes
└── .gitignore
```

活动构建入口：

- CM7 APP：`app/Makefile/CM7/Makefile`
- CM4 APP：`app/Makefile/CM4/Makefile`
- CM7 Bootloader：`bootloader/Makefile/CM7/Makefile`
- Qt Host：`tools/ota-host/OTA.pro`

## 软件分层架构

```text
┌─────────────────────────────────────────────────────────────┐
│ 应用层                                                      │
│ CM7: LVGL GUI / LCD 应用 / OTA / 应用数据                   │
│ CM4: DHT11 周期采集任务                                     │
│ Host: Qt OTA 更新流程                                        │
├─────────────────────────────────────────────────────────────┤
│ 系统层                                                      │
│ FreeRTOS + CMSIS-RTOS V2                                    │
│ task / notification / HSEM ISR callback / watchdog task      │
├─────────────────────────────────────────────────────────────┤
│ BSP 层                                                       │
│ LCD/LTDC、SDRAM、Touch、ADC、UART OTA、watchdog、MPU、IPC   │
├─────────────────────────────────────────────────────────────┤
│ 驱动层                                                       │
│ STM32 HAL/LL、CMSIS、DMA、FMC、USART、LTDC、HSEM            │
├─────────────────────────────────────────────────────────────┤
│ 硬件层                                                       │
│ STM32H757 CM7 + CM4、内部 Flash、D1/D2/D3 SRAM、HSEM、     │
│ 外部 SDRAM、LCD、UART/USB-TTL、DHT11                         │
└─────────────────────────────────────────────────────────────┘
```

从构建清单看，CM7 是应用主核：它链接 LVGL、LTDC、FMC/SDRAM、SDMMC/FatFs、USART6 OTA、USB 和应用任务；CM4 是精简传感核：它只链接 FreeRTOS、DHT11、共享 IPC 和必要 HAL。证据分别见 `app/Makefile/CM7/Makefile` 和 `app/Makefile/CM4/Makefile` 的 `C_SOURCES`。

## 从上电到应用的启动流程

```text
Power ON
  ↓
芯片复位，按当前启动配置进入 CM7 Bootloader
  ↓
Bootloader Reset_Handler
  ├─ 设置 MSP
  ├─ SystemInit：RCC 基础复位、HSEM EXTI、VTOR
  ├─ 拷贝 .data、清零 .bss、拷贝 .RamFunc
  └─ __libc_init_array → main
  ↓
Bootloader 初始化时钟、GPIO、USART1、BKPSRAM metadata
  ↓
检查 Slot A/Slot B footer；若 pending Slot B 有效，则 B→A 拷贝
  ↓
校验 Slot A 向量和镜像边界
  ↓
Boot_JumpToApp()
  ├─ hold CM4
  ├─ 关闭中断/SysTick，清 NVIC
  ├─ 写入 Boot jump magic
  ├─ 设置 APP MSP
  └─ bx 到 APP Reset_Handler
  ↓
CM7 APP Reset_Handler → SystemInit → main
  ├─ 配置系统时钟
  ├─ EnableBootCore(RCC_BOOT_C2)
  ├─ 等待 CM4 进入 STOP，释放 HSEM0 唤醒 CM4
  ├─ 初始化 GPIO/DMA/FMC/LTDC/SD/USART/SPI/IWDG
  ├─ 初始化 OTA、MPU、SDRAM、LCD
  └─ 创建任务并启动 FreeRTOS
  ↓
CM4 被唤醒后从 STOP 返回
  ├─ HAL_Init、SystemCoreClockUpdate、DMA
  ├─ 创建 FreeRTOS
  └─ DHT11 task 采集并通过 Shared SRAM/HSEM 上报
```

### BCM4、BOOT_C2 和 CM4 STOP

源码没有读取或写入 option byte 的 `BCM4` 配置，因此无法从仓库证明实际芯片 option byte 当前值。CM7 `main.c` 的启动序列按“BCM4 为 0 时由 `HAL_RCCEx_EnableBootCore(RCC_BOOT_C2)` 释放 CM4”的路径编写；CM4 启动后立即进入 D2 STOP，并等待 HSEM0 notification。CM7 先等待 `RCC_FLAG_D2CKRDY` 清除，再释放 HSEM0，避免 CM4 尚未进入 STOP 时丢失唤醒事件。

证据：

- CM7：`app/CM7/Core/Src/main.c:108-142`
- CM4：`app/CM4/Core/Src/main.c:76-90`
- option byte 定义：`app/Drivers/STM32H7xx_HAL_Driver/Inc/stm32h7xx_hal_flash_ex.h`

### Bootloader 跳转与 VTOR

Bootloader 从 `CM7_APP_SLOT_A_BASE` 读取向量表第 0 项 MSP 和第 1 项 Reset_Handler，然后执行 `Boot_JumpToAppAt()`。跳转函数本身没有直接写 `SCB->VTOR`；APP 的 `Reset_Handler` 进入公共 `SystemInit()` 后设置：

- CM7：`FLASH_BANK1_BASE | 0x00020000`，即 `0x08020000`
- CM4：`FLASH_BANK2_BASE | 0x00000000`，即 `0x08100000`

这与两个 APP linker script 的 `FLASH ORIGIN` 一致：

- CM7：`app/Makefile/CM7/stm32h747xx_flash_CM7.ld`
- CM4：`app/Makefile/CM4/stm32h747xx_flash_CM4.ld`

Bootloader 的 `SystemInit()` 使用 Bootloader 自身的向量表地址配置。跳转函数负责运行时上下文清理和 MSP/PC 切换，向量表重定位由目标 APP 的 `SystemInit()` 完成。

## 关键启动文件

| 镜像 | 启动文件 | 启动入口 | `main` 前动作 |
| --- | --- | --- | --- |
| CM7 APP | `app/Makefile/CM7/startup_stm32h757xx_CM7.s` | `g_pfnVectors[1] -> Reset_Handler` | MSP、`SystemInit`、`.data`、`.bss`、`.RamFunc`、`__libc_init_array` |
| CM4 APP | `app/Makefile/CM4/startup_stm32h757xx_CM4.s` | `g_pfnVectors[1] -> Reset_Handler` | MSP、`SystemInit`、`.data`、`.bss`、`__libc_init_array` |
| CM7 Bootloader | `bootloader/Makefile/CM7/startup_stm32h757xx_CM7.s` | `g_pfnVectors[1] -> Reset_Handler` | MSP、`SystemInit`、`.data`、`.bss`、`.RamFunc`、`__libc_init_array` |

启动汇编中的关键指令位置：

- CM7 APP：`startup_stm32h757xx_CM7.s:61-121`
- CM4 APP：`startup_stm32h757xx_CM4.s:57-100`
- Bootloader：`bootloader/Makefile/CM7/startup_stm32h757xx_CM7.s:61-121`

公共 `SystemInit()` 位于 `app/Common/Src/system_stm32h7xx_dualcore_boot_cm4_cm7.c:177-294`，其中 CM7 和 CM4 分支分别设置 `SCB->VTOR`，并处理双核启动所需的 RCC/HSEM 基础状态。

## 硬件与核间分工

### STM32H757 双核结构

工程分别生成 CM7 与 CM4 镜像。CM7 使用 Cortex-M7 目标选项，CM4 使用 Cortex-M4 目标选项，见：

- `app/Makefile/CM7/Makefile:170-180`
- `app/Makefile/CM4/Makefile:97-100`

CM7 的启动代码在系统时钟配置后显式启动 CM4，并通过 HSEM 0 完成从 STOP 状态唤醒握手：

- `app/CM7/Core/Src/main.c:121-142`
- `app/CM4/Core/Src/main.c:76-90`

### CM7 负责的功能

CM7 工程实际包含以下应用入口和资源初始化：

- 外部 SDRAM、LTDC、DMA2D、ADC、SDMMC/FATFS、USART1/3/6、SPI、IWDG；
- LVGL 初始化与显示任务；
- OTA 初始化与 OTA 任务；
- CM4 共享传感器数据接收任务；
- 应用数据缓存与 LCD 显示。

证据：

- 外设初始化：`app/CM7/Core/Src/main.c:149-177`
- RTOS 任务创建：`app/CM7/Core/Src/freertos.c:92-124`
- LVGL 任务：`app/CM7/Core/Src/lvgl_demo.c:74-117`
- OTA 任务：`app/BSP/OTA/bsp_ota_app.c:27-55`

CM7 适合承担应用层的原因是：当前应用镜像、图形框架、外部存储和 OTA 逻辑都链接在 CM7 工程中，且 CM7 是系统时钟、外部内存和应用调度的主控制核。这个结论来自现有工程布局，不是对芯片架构的泛化假设。

### CM4 负责的功能

CM4 工程被有意保持为精简工程，仅包含：

- CM4 启动与 FreeRTOS；
- `app/CM4/BSP/Sensor/dht11.c`；
- 共享传感器通信模块；
- 运行 DHT11 采集任务。

证据：

- CM4 源文件清单：`app/Makefile/CM4/Makefile:29-69`
- CM4 RTOS 初始化：`app/CM4/Core/Src/freertos.c:17-21`
- DHT11 任务创建：`app/CM4/BSP/Sensor/dht11.c:172-212`

DHT11 数据引脚为 `GPIOD/GPIO_PIN_12`，采样周期为 `2000 ms`：

- `app/CM4/BSP/Sensor/dht11_conf.h:8-14`

因此当前双核分工是：CM4 处理对时序敏感的传感器采样，CM7 处理图形、外设集成和应用业务。这样可以降低 DHT11 微秒级忙等对 CM7 图形任务和应用任务的干扰；该设计意图也与 CM4 工程的最小化源文件集合一致。

## Flash 布局

| 区域 | 地址 | 大小 | 用途 | 来源 |
| --- | ---: | ---: | --- | --- |
| Bootloader CM7 | `0x08000000` | `128K` | 启动检查、拷贝和跳转 | `bootloader/Makefile/CM7/stm32h747xx_flash_CM7.ld:43-49` |
| CM7 Slot A | `0x08020000` | `448K` | 当前执行镜像 | `bootloader/Common/Inc/boot_slot.h:9-19` |
| CM7 Slot B | `0x08090000` | `448K` | OTA 下载缓冲槽 | `bootloader/Common/Inc/boot_slot.h:9-19` |
| CM4 应用 | `0x08100000` | `1024K` | CM4 镜像 | `app/Makefile/CM4/stm32h747xx_flash_CM4.ld:43-49` |

应用 CM7 链接脚本从 `0x08020000` 开始，长度为 `448K`：

- `app/Makefile/CM7/stm32h747xx_flash_CM7.ld:43-51`

应用 CM4 链接脚本从 `0x08100000` 开始：

- `app/Makefile/CM4/stm32h747xx_flash_CM4.ld:43-49`

## SRAM 布局

### CM7

- DTCM/主 RAM 区：`0x20000000`，`128K`；
- AXI SRAM：`0x24000000`，`512K`；
- 共享 SRAM：`0x38000000`，`64K`；
- 外部 SDRAM 作为 LCD framebuffer：`0xD0000000`。

来源：

- `app/Makefile/CM7/stm32h747xx_flash_CM7.ld:43-51`
- `app/BSP/LCD/lcd_ltdc.h:72-74`

### CM4

- CM4 RAM：`0x10000000`，`288K`；
- 共享 SRAM：`0x38000000`，`64K`。

来源：

- `app/Makefile/CM4/stm32h747xx_flash_CM4.ld:43-49`

两个应用链接脚本都把 `.shared_data` 放入 `SHARED_RAM`，且 32 字节对齐：

- `app/Makefile/CM7/stm32h747xx_flash_CM7.ld:176-182`
- `app/Makefile/CM4/stm32h747xx_flash_CM4.ld:152-158`
- `app/Common/Src/sensor_shared.c:18`

CM7 MPU 将 `0x38000000` 的 64KB 区域配置为不可缓存、不可缓冲：

- `app/BSP/MPU/mpu.c:61-63`

这避免了 CM7 D-Cache 直接缓存共享数据。CM4 没有单独的 MPU 缓存配置，当前共享区通过链接地址、HSEM 和 CM7 非缓存配置协作。

## 外设资源

### UART

CM7 初始化 USART1、USART3、USART6：

- `app/CM7/Core/Src/main.c:158-160`

USART6 配置为 115200、8N1、无硬件流控，并启用 FIFO：

- `app/CM7/Core/Src/usart.c:185-228`

USART6 使用 DMA1 Stream2 接收、Stream3 发送，GPIO 为 PG9/PG14：

- `app/CM7/Core/Src/usart.c:346-415`

### LCD 与外部 SDRAM

LTDC 使用 RGB565、800x480、外部 framebuffer：

- `app/CM7/Core/Src/ltdc.c:66-73`
- `app/BSP/LCD/lcd_ltdc.h:72-74`

### 传感器

DHT11 仅在 CM4 活动路径中初始化和采集：

- `app/CM4/BSP/Sensor/dht11.c:98-108`
- `app/CM4/BSP/Sensor/dht11.c:172-199`

仓库中的 `app/BSP/DHT11/dht11.c` 是历史/独立 BSP 文件；当前 CM7 Makefile 未将该源文件列入活动 CM7 源文件清单，不能把它当作当前 CM7 正在运行的 DHT11 任务。

## 为什么采用双核

从源码可确认的工程性理由有三点：

1. CM7 已承载 LVGL、LCD、外部内存、OTA 和多外设调度，任务负载较重。
2. DHT11 驱动在读取 40 bit 数据时关闭中断并使用 DWT 微秒级忙等，见 `app/CM4/BSP/Sensor/dht11.c:120-154`。
3. CM4 工程可以只保留传感器和 RTOS，通过共享 SRAM/HSEM 向 CM7 上报结果，减少跨层耦合。

## 构建审计

本次审计使用非破坏性的 `build_audit` 输出目录验证：

- CM7：`app/Makefile/CM7/build_audit/GPIO_CM7.elf`
- CM4：`app/Makefile/CM4/build_audit/GPIO_CM4.elf`

两侧均完成编译和链接。CM7 仅有标准库 `_getpid/_kill` 未实现及 RWX segment 类警告，没有发现编译失败或 undefined reference。构建产物按用户要求保留，未删除任何文件或文件夹。
