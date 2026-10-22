# STM32H757 工业控制终端 面试题库 V1.0

> 面向：芯片原厂、大厂嵌入式软件岗位面试准备。
> 事实原则：每道题都来自当前工程真实实现；未落地/未验证的设计一律标注 `【当前未实现】` 或 `【设计讨论】`。
> 难度标签：`[S级]` 必须掌握（面试高概率）｜`[A级]` 可能深挖｜`[B级]` 了解即可。
> 本文只做整理，不修改任何源码。

---

## H1 项目整体架构

### Q001 [S级]

**问题：** 介绍一下这个项目，用的是什么 MCU？

**面试官关注点：** 是否能用一句话讲清项目定位，是否清楚自己在双核上做了什么。

**标准回答：**
这是一个基于 STM32H757 双核 MCU 的工业控制终端。CM7（Cortex-M7）作为应用主核，负责 LCD/LTDC 图形界面、LVGL、FreeRTOS 应用调度、USART6 UART OTA 升级和各类外设集成；CM4（Cortex-M4）作为精简传感核，只运行 FreeRTOS + DHT11 采集任务，通过 Shared SRAM + HSEM 把温湿度上报给 CM7。构建宏是 `STM32H757xx`，工程拆成 CM7/CM4 两个独立目标。

**关键参数：** MCU `STM32H757`；构建宏 `STM32H757xx`；CM7 `fpv5-d16`，CM4 `fpv4-sp-d16`（均硬浮点）。

**源码位置：** `app/Makefile/CM7/Makefile`、`app/Makefile/CM4/Makefile`。

**可能追问：** 如何从构建系统证明它不是单核工程？

**追问回答：** 存在两个独立 Makefile 与两个链接脚本、两个启动文件（`startup_stm32h757xx_CM7.s` / `_CM4.s`），分别产出 `GPIO_CM7.elf` 和 `GPIO_CM4.elf`，CM7 使用 Cortex-M7 目标、CM4 使用 Cortex-M4 目标。

---

### Q002 [S级]

**问题：** 为什么用 H757 双核，CM7 和 CM4 各承担什么？

**面试官关注点：** 是否有清晰的"为什么拆核"的工程理由，而不是为了双核而双核。

**标准回答：**
三点工程理由（源码可确认）：
1. CM7 已经承载 LVGL、LCD、外部 SDRAM、OTA 和多外设调度，负载较重；
2. DHT11 单总线读取需要关中断 + 微秒级忙等，放在 CM7 会干扰图形刷新和 OTA 等实时任务；
3. CM4 工程刻意精简，只保留传感器 + RTOS，通过共享内存上报结果，降低跨层耦合。

**关键参数：** DHT11 读取 40 bit 数据时关闭中断并用 DWT 微秒级忙等。

**源码位置：** `app/CM4/BSP/Sensor/dht11.c:120-154`、`app/Makefile/CM4/Makefile:29-69`。

**可能追问：** 如果 GUI 卡顿，CM4 能替 CM7 分担吗？

**追问回答：** CM4 分担的是"低频、时序敏感"的传感器采集，不承担图形渲染（LTDC/LVGL 都在 CM7 链接）。要分担 GUI 需要把显示数据源搬到 CM4 并引入更大带宽的跨核通道，当前架构没有这么做。

---

### Q003 [S级]

**问题：** 这个工程的软件分层是怎么组织的？

**面试官关注点：** 是否有分层意识，能否画出调用链。

**标准回答：**
从上到下：应用层（CM7 的 LVGL GUI/OTA/应用数据，CM4 的 DHT11 采集，Host 的 Qt OTA）→ 系统层（FreeRTOS + CMSIS-RTOS V2，task/notification/HSEM ISR callback/watchdog task）→ BSP 层（LCD/LTDC、SDRAM、Touch、ADC、UART OTA、watchdog、MPU、IPC）→ 驱动层（STM32 HAL/LL、CMSIS、DMA、FMC、USART、LTDC、HSEM）→ 硬件层（H757 CM7+CM4、内部 Flash、D1/D2/D3 SRAM、HSEM、外部 SDRAM、LCD、USART/USB-TTL、DHT11）。

**关键参数：** 活动构建入口四个：CM7 APP、CM4 APP、CM7 Bootloader、Qt Host。

**源码位置：** `docs/project_architecture.md`（目录树）、`app/Makefile/CM7/Makefile`、`app/Makefile/CM4/Makefile`。

**可能追问：** Bootloader 和应用是同一个工程吗？

**追问回答：** 不是。`bootloader/` 是独立工程，有独立的 HAL/CMSIS 副本、链接脚本和 Makefile，与应用 `app/` 分离。

---

### Q004 [A级]

**问题：** 工程里有哪些外设资源，各自用途是什么？

**面试官关注点：** 对外设资源是否心里有数。

**标准回答：**
- USART6：UART OTA（PG9 RX / PG14 TX，AF7，DMA1 Stream2/3）；
- USART1：Bootloader/应用日志 `printf` 输出；
- LTDC + FMC SDRAM：LCD 图像输出 + 外部 framebuffer；
- HSEM：双核启动握手、共享数据保护、跨核通知；
- IWDG1：CM7 硬件看门狗；
- ADC：CM7 采样（500ms、16 点、EMA）；
- Touch（软件 I2C）：GT9xxx 优先，兼容 FT5206/FT5426；
- SPI：W5500（仓库保留，非主线成果）。

**关键参数：** 见各外设。

**源码位置：** `app/CM7/Core/Src/main.c`、`app/CM7/Core/Src/usart.c`、`app/BSP/TOUCH/touch.c`。

**可能追问：** 哪些是本项目面试主线？

**追问回答：** 双核 IPC、Bootloader/OTA、UART DMA 接收、watchdog、LCD/LVGL。W5500/Modbus 代码存在但不作为已实现主线成果。

---

### Q005 [S级]

**问题：** 你觉得这个项目最大的亮点是什么？

**面试官关注点：** 自我总结能力，是否能把"做了什么"和"为什么有价值"讲清楚。

**标准回答：**
1. 在 H757 上实现清晰的 CM7 应用核 + CM4 传感核分工；
2. 用官方硬件资源 Shared SRAM + HSEM 做轻量 IPC，没引入 OpenAMP/RPMsg 等重框架；
3. 修掉 HSEM notification 一次性触发的问题（回调重新 arm）；
4. 用 USART6 DMA ReceiveToIdle + ring buffer 解决连续串口 OTA 丢字节；
5. OTA 具备 Slot B 下载 + CRC32 + footer + pending metadata 流程；
6. watchdog 覆盖本核任务 + 跨核 CM4 heartbeat + 硬件 IWDG 三层。

**关键参数：** 见对应章节。

**源码位置：** `app/Common/Src/sensor_shared.c`、`app/BSP/OTA/bsp_ota_uart.c`、`app/BSP/bsp_watchdog.c`。

**可能追问：** 这些"亮点"里，哪些是硬件实测过的？

**追问回答：** 诚实说：双核 IPC、OTA、watchdog 都已通过构建，但只有 IWDG 复位（selftest）、DHT11 离线状态等少数项做过实机验证；完整 OTA 长时间压力、掉电恢复、跨核故障注入还没有板级记录。

---

### Q006 [A级]

**问题：** 这个工程的时钟树是怎么配的？CM7 主频是多少？

**面试官关注点：** 是否理解 PLL 链和"配置推导值 vs 实测值"的区别。

**标准回答：**
HSE 25MHz（Bypass），PLLM=5、PLLN=192、PLLP=2、PLLQ=15、PLLR=1、PLLFRACN=0，SYSCLK 来自 PLLCLK，SYSCLK Divider=1，AHB Divider=2，APB1/2/3/4 都 `/2`。VCO = 25/5×192 = 960MHz，经过 H7 域分频得到 CM7 运行时钟约 480MHz——这是配置推导值，不是示波器实测。

**关键参数：** HSE 25MHz；VCO 960MHz；AHB `/2`。

**源码位置：** `app/CM7/Core/Src/main.c:220-275`。

**可能追问：** 为什么不能直接把 960MHz 说成 CM7 主频？

**追问回答：** 960MHz 是 PLL VCO 输出，还要经过 `sys_pllclk`、`sys_d1cpre_ck`、`hclk` 等分频后才到 CPU；必须看 RCC 域时钟寄存器才能确认真实 HCLK，源码当前没有运行时寄存器打印。

---

## H2 STM32H757 双核启动流程

### Q007 [S级]

**问题：** 从上电到应用跑起来，完整启动流程是怎样的？

**面试官关注点：** 能否讲清 Bootloader → CM7 APP → CM4 的完整链条。

**标准回答：**
```
Power ON → CM7 Bootloader Reset_Handler（MSP、SystemInit、.data/.bss/.RamFunc、__libc_init_array）
  → 初始化时钟/GPIO/USART1/BKPSRAM metadata
  → 检查 Slot A/Slot B footer，若 pending Slot B 有效则 B→A 拷贝
  → 校验 Slot A 向量 → Boot_JumpToApp()（hold CM4、关中断/SysTick、清 NVIC、写 jump magic、设 MSP、bx 到 APP Reset_Handler）
  → CM7 APP SystemInit 设 VTOR → main 配置时钟 → EnableBootCore(RCC_BOOT_C2) → 等 D2CKRDY 清 → 释放 HSEM0 唤醒 CM4
  → 初始化外设/IWDG/OTA/MPU/SDRAM/LCD → 创建任务启动 FreeRTOS
  → CM4 从 STOP 返回 → HAL_Init/SystemCoreClockUpdate/DMA → FreeRTOS → DHT11 任务
```

**关键参数：** CM7 APP VTOR `0x08020000`，CM4 VTOR `0x08100000`。

**源码位置：** `bootloader/CM7/Core/Src/main.c:74-201`、`bootloader/CM7/Core/Src/boot_jump.c`、`app/CM7/Core/Src/main.c:108-142`。

**可能追问：** Bootloader 和 APP 的 SystemInit 各自设置 VTOR 吗？

**追问回答：** Bootloader 用自身向量表；跳转函数不写 VTOR，由目标 APP 的 `SystemInit()` 设置（CM7 `0x08020000`、CM4 `0x08100000`）。

---

### Q008 [S级]

**问题：** CM7 是怎么把 CM4 唤醒的？

**面试官关注点：** 双核启动握手机制。

**标准回答：**
按 BCM4 为 0 的路径：CM7 配置完时钟后调用 `HAL_RCCEx_EnableBootCore(RCC_BOOT_C2)` 释放 CM4；CM4 启动后立即进入 D2 STOP 并等待 HSEM0 通知；CM7 先等待 `RCC_FLAG_D2CKRDY` 清除（确保 CM4 已进入 STOP），再释放 HSEM0，避免 CM4 尚未就绪时丢失唤醒事件。

**关键参数：** HSEM0 用于启动/唤醒握手；`RCC_FLAG_D2CKRDY`。

**源码位置：** `app/CM7/Core/Src/main.c:124-142`、`app/CM4/Core/Src/main.c:76-90`。

**可能追问：** 为什么要先等 D2CKRDY 清再释放 HSEM0？

**追问回答：** 因为 CM4 释放唤醒前必须先进入 STOP 等待态，否则 HSEM0 通知发出时 CM4 还没在等，事件就丢了。

---

### Q009 [A级]

**问题：** 为什么跳转 APP 前要关闭中断、清 SysTick、清 NVIC？

**面试官关注点：** 是否理解"运行时上下文清理"的细节。

**标准回答：**
防止 Bootloader 的系统 tick 和已使能的外设中断在 APP 尚未完成初始化时继续触发，导致向量表还没重定位时中断跳到错误地址。跳转前还要设置 MSP 为 APP 向量表第一个 word、清 NVIC enable/pending、`CONTROL` 清零恢复特权线程/主栈语义。

**关键参数：** APP 向量表第 0 项 = 初始 MSP，第 1 项 = Reset_Handler。

**源码位置：** `bootloader/CM7/Core/Src/boot_jump.c:29-63`。

**可能追问：** 如果 APP 不设置 VTOR 会发生什么？

**追问回答：** 中断会仍然指向 Bootloader 向量表，一旦 APP 运行期发生中断就会跳到错误入口，通常表现为异常/复位。

---

### Q010 [A级]

**问题：** CM4 的 HAL tick 是怎么处理的？为什么不能直接用 SysTick？

**面试官关注点：** FreeRTOS 与 HAL tick 的冲突，timebase 外设选择。

**标准回答：**
CM4 的 HAL tick 不用 SysTick（SysTick 被 FreeRTOS 抢占），改用 TIM7 做 HAL timebase，`HAL_InitTick()` 使能 TIM7 并启动中断，`HAL_TIM_PeriodElapsedCallback` 里 `HAL_IncTick()`。CM7 同理用 TIM6。CM4 的 `SystemCoreClock` 在 `HAL_Init()` 后调用 `SystemCoreClockUpdate()`，从初始 64MHz 更新为 CM7 配好的 D2 真实频率。

**关键参数：** CM7 用 TIM6，CM4 用 TIM7。

**源码位置：** `app/CM7/Core/Src/stm32h7xx_hal_timebase_tim.c`、`app/CM4/Core/Src/stm32h7xx_hal_timebase_tim.c`、`app/CM4/Core/Src/main.c`。

**可能追问：** 为什么 CM4 要 `SystemCoreClockUpdate()`？

**追问回答：** `SystemCoreClock` 初值是 64MHz，而实际 D2 时钟由 CM7 配置，不更新会导致 DWT 延时和 FreeRTOS tick 算错。

---

### Q011 [B级]

**问题：** BCM4 option byte 和 `EnableBootCore` 是什么关系？

**面试官关注点：** 是否区分"芯片配置"和"软件启动"两条路径。

**标准回答：**
源码没有读写 option byte 的 BCM4 配置，因此无法从仓库证明实际芯片 option byte 值。当前代码按"BCM4=0 时由 `HAL_RCCEx_EnableBootCore(RCC_BOOT_C2)` 释放 CM4"的路径编写。

**关键参数：** option byte 定义在 `stm32h7xx_hal_flash_ex.h`。

**源码位置：** `app/CM7/Core/Src/main.c:108-142`。

**可能追问：** 如果 BCM4 被烧成 1 会怎样？

**追问回答：** CM4 会自行启动而不是等 CM7 释放，启动时序会改变，需要按新路径重新核对握手逻辑。

---

## H3 CM7/CM4 任务划分与 IPC 通信

### Q012 [S级]

**问题：** DHT11 为什么放到 CM4，而不是留在 CM7？

**面试官关注点：** 任务划分的工程动机。

**标准回答：**
DHT11 单总线读取需要关闭中断并进行微秒级忙等（DWT 周期计数），和图形刷新、OTA 等任务同核会引入调度干扰。放到 CM4 后，把"时序敏感的采集"和"负载重的应用"隔离；CM4 工程只保留传感器 + RTOS，通过共享内存上报结果。

**关键参数：** 数据引脚 GPIOD Pin 12，采样周期 2000ms。

**源码位置：** `app/CM4/BSP/Sensor/dht11.c:120-169`、`app/CM4/BSP/Sensor/dht11_conf.h`。

**可能追问：** 如何证明 CM7 不再直接访问 DHT11 GPIO？

**追问回答：** CM7 的 Makefile 不含 `bsp_dht11.mk`，CM7 不链接 DHT11 驱动；`app/BSP/DHT11/dht11.c` 是历史残留，当前活动路径是 `app/CM4/BSP/Sensor/dht11.c`。

---

### Q013 [S级]

**问题：** CM4 采集到的数据是怎么到 CM7 的？画出链路。

**面试官关注点：** 跨核数据流的完整理解。

**标准回答：**
```
CM4 DHT11 任务（2000ms）
  → sensor_shared_publish()（HSEM1 保护写共享结构）
  → 释放 HSEM2（事件通知）
  → CM7 HSEM2_IRQHandler → HAL_HSEM_IRQHandler → HAL_HSEM_FreeCallback()
  → 重新 arm notification + vTaskNotifyGiveFromISR() 唤醒 SensorIpcTask
  → sensor_ipc_read()（HSEM1 保护读）
  → BSP_AppData_Set*（互斥锁保护的应用缓存）
  → LVGL 500ms 定时刷新 → LTDC framebuffer → LCD
```

**关键参数：** 共享结构 `sensor_shared_data_t`（magic/valid/temperature/humidity/timestamp）。

**源码位置：** `app/Common/Src/sensor_shared.c`、`app/CM7/Core/Src/freertos.c:174-204`、`app/CM7/Core/Src/lvgl_demo.c:250-288`。

**可能追问：** 为什么不轮询共享内存，而用 HSEM 通知？

**追问回答：** 轮询要么空耗 CPU 要么延迟大；HSEM 事件 + task notification 让"数据到达"成为唤醒条件，CM7 侧任务阻塞等待，低延迟低开销。

---

### Q014 [A级]

**问题：** DHT11 采集失败时，CM4 上报什么？

**面试官关注点：** 是否理解 `valid` 标志的设计。

**标准回答：**
读取成功写整数温湿度、`HAL_GetTick()` 时间戳并置 `valid=1`；失败时置 `valid=0`，但**仍更新时间戳并照常发布**。这样 CM7 能区分两种故障：单次读失败（valid=0，可显示离线）和 CM4 长时间不发布（heartbeat 超时）。

**关键参数：** valid=0/1；timestamp 总是更新。

**源码位置：** `app/CM4/BSP/Sensor/dht11.c:175-198`。

**可能追问：** 为什么失败也要发？

**追问回答：** 否则 CM7 无法区分"DHT11 离线"和"CM4 死了"——失败也发，valid 标志承担"在线/离线"，发布频率承担"CM4 是否存活"。

---

### Q015 [S级]

**问题：** DHT11 的"离线"状态是怎么设计的？（非致命，不复位）

**面试官关注点：** 故障分级：什么该复位、什么只上报。

**标准回答：**
CM7 把 `sample.valid` 映射成应用层的 `dht11_online` 标志（互斥锁保护），三处消费：
1. LCD 右上角显示 "DHT11: ONLINE"（绿）或 "DHT11: OFFLINE"（红）；
2. Modbus 保持寄存器 3 = 在线状态（1/0）；
3. 仅作状态展示，**不触发复位**。
而 CM4 失联走的是另一条路（watchdog CM4 槽超时 → IWDG 复位），两者分开。

**关键参数：** `dht11_online`；Modbus 寄存器 3。

**源码位置：** `app/BSP/bsp_app_data.c`、`app/CM7/Core/Src/lvgl_demo.c:264-280`、`app/BSP/Modbus/bsp_modbus_app.c`。

**可能追问：** 为什么"读失败"不直接复位？

**追问回答：** 单次读失败可能是传感器本身问题（非致命），复位解决不了且影响可用性；只有 CM4 整个停止发布才说明核级故障，才需要复位。

---

### Q016 [A级]

**问题：** 共享结构有哪些字段，各解决什么问题？

**面试官关注点：** 数据结构设计与字段语义。

**标准回答：**
```c
typedef struct {
    uint32_t magic;       // 0x53484D31，标记共享结构已初始化
    uint8_t  valid;       // 本次读数是否有效
    float    temperature; // 温度
    float    humidity;    // 湿度
    uint32_t timestamp;   // 发布时间戳（HAL_GetTick）
} sensor_shared_data_t;
```

**关键参数：** magic `0x53484D31`。

**源码位置：** `app/Common/Inc/sensor_shared.h:16-29`。

**可能追问：** magic 和 valid 的区别？

**追问回答：** magic 标记"共享对象是否已初始化"（一次性），valid 标记"本次数据是否有效"（每周期变化）。

---

## H4 HSEM 机制与 Shared SRAM 设计

### Q017 [S级]

**问题：** 为什么采用 Shared SRAM + HSEM 实现 CM7/CM4 通信，而不是 OpenAMP/RPMsg？

**面试官关注点：** 通信方案选型的能力。

**标准回答：**
第一阶段需求只是简单的传感器数据通道，Shared SRAM + HSEM 足够，且更轻量、无额外依赖；OpenAMP/RPMsg 会引入更重的框架和更大的调试面。Shared SRAM 负责数据，HSEM 负责互斥和事件通知，三者角色清晰。

**关键参数：** Shared SRAM `0x38000000`（64KB）；HSEM1 数据锁，HSEM2 事件通知。

**源码位置：** `app/Common/Src/sensor_shared.c`、两核链接脚本 `.shared_data` 段。

**可能追问：** 什么时候才值得引入 OpenAMP？

**追问回答：** 当跨核数据量大、消息种类多、需要标准协议（virtio/RPMsg）、或要复用多核框架生态时；当前单传感器上报不需要。

---

### Q018 [S级]

**问题：** 三个 HSEM 分别做什么？为什么不只用一个？

**面试官关注点：** 是否理解"启动控制/数据互斥/事件通知"三种语义要分离。

**标准回答：**
| HSEM | 用途 |
|---|---|
| HSEM0 | CM7 启动/唤醒 CM4（启动握手） |
| HSEM1 | 保护共享数据读写（互斥锁） |
| HSEM2 | CM4 发布事件，触发 CM7 IRQ（通知） |

分开避免一个 semaphore 同时承担三种语义——HSEM2 是事件通知，不等同于数据锁；数据锁仍由 HSEM1 保护。

**关键参数：** 见上表。

**源码位置：** `app/Common/Inc/sensor_shared.h:16-20`、`app/CM7/Core/Src/main.c:124-138`。

**可能追问：** HSEM2 是"锁"还是"通知"？

**追问回答：** 主要是通知。CM4 release HSEM2 触发 CM7 中断，CM7 回调重新 arm；数据互斥由 HSEM1 单独负责。

---

### Q019 [S级]

**问题：** Shared SRAM 为什么配置为 non-cacheable？

**面试官关注点：** D-Cache 一致性问题。

**标准回答：**
CM7 有数据缓存，若共享区可缓存，CM7 可能读到旧 cache line 而看不到 CM4 刚写入的数据。CM7 MPU 把 `0x38000000`（64KB）配置为 non-cacheable/non-bufferable，从根上降低跨核共享数据的一致性风险。

**关键参数：** MPU 区域 `0x38000000` 64KB non-cacheable。

**源码位置：** `app/BSP/MPU/mpu.c:61-63`。

**可能追问：** 这样是不是完全不需要内存屏障了？

**追问回答：** non-cacheable 规避了 cache 一致性问题，但跨核可见性仍依赖 HSEM 临界区提供同步；不能因此推断"所有 DMA buffer 都天然一致"（UART DMA buffer 在可缓存区，仍需显式 invalidate）。

---

### Q020 [A级]

**问题：** 共享对象 32 字节对齐的意义是什么？对齐能保证原子性吗？

**面试官关注点：** 是否混淆"对齐"和"原子性"。

**标准回答：**
32 字节对齐便于按 cache line 对齐管理，降低跨 cache line 的一致性复杂度。但对齐**不保证原子性**——float 温湿度等字段仍可能被撕裂，所以代码用 HSEM1 保护读写。

**关键参数：** `.shared_data` 32 字节对齐。

**源码位置：** `app/Common/Src/sensor_shared.c:18`、链接脚本 `:176-182`。

**可能追问：** 如果要无锁读取，怎么设计？

**追问回答：** 用版本号/序号（seqlock）思路：写前递增奇数序号、写后递增偶数序号，读者读前后比对序号判断是否有并发写。

---

### Q021 [A级]

**问题：** `sensor_ipc_init()` 双核都调用，有什么风险？

**面试官关注点：** 是否发现初始化竞态。

**标准回答：**
`sensor_ipc_init()` 在 CM4 和 CM7 两侧都会调用 `sensor_shared_reset()`，如果两核启动时序交叉，后启动的一侧可能清零先前已发布的数据。当前依赖启动顺序和首次读取时机，没有版本号/所有者标志/单侧 reset 机制——这是源码审计风险，不是已验证的健壮性保证。

**关键参数：** 无（审计风险项）。

**源码位置：** `app/Common/Src/sensor_shared.c:39-49`、`app/CM4/Core/Src/freertos.c:19`、`app/CM7/Core/Src/freertos.c:118-120`。

**可能追问：** 如何改成单边初始化？

**追问回答：** 只让 CM7（或只让先启动的一方）执行 reset，另一方只 attach；或用 magic 判断"是否已初始化过"再决定是否清零。

---

### Q022 [B级]

**问题：** 共享 SRAM 大小是不是等于传感器数据包大小？

**面试官关注点：** 是否混淆区域大小和对象大小。

**标准回答：**
不是。Shared SRAM 区域 64KB，当前只放一个 32 字节对齐的共享对象，实际对象大小由结构体布局决定，没有占满整个区域。

**关键参数：** 区域 64KB；对象 `sensor_shared_data_t`（约 20 字节 + 对齐）。

**源码位置：** `app/Common/Src/sensor_shared.c:18`。

**可能追问：** 未来扩展多个传感器要注意什么？

**追问回答：** 要规划共享区布局、每个对象的对齐与 HSEM 分配，避免多对象共用一把锁造成粒度问题。

---

## H5 FreeRTOS 系统设计

### Q023 [S级]

**问题：** 这个工程有哪些任务，优先级和栈是怎么配的？

**面试官关注点：** 任务清单是否背熟，是否理解优先级/栈的合理性。

**标准回答：**
核心任务：
| 任务 | 核 | 优先级 | 栈 | 周期/等待 |
|---|---|---|---|---|
| lvgl_task | CM7 | AboveNormal | 16KB | 1ms handler / 500ms 刷新 |
| defaultTask | CM7 | Normal | 2KB | 1000ms |
| sensor_ipc | CM7 | AboveNormal | 2KB | 阻塞等 notification |
| ota | CM7 | Realtime | 8KB | 通知/短超时 |
| watchdog | CM7 | Realtime | 1.5KB | 2000ms |
| adc | CM7 | Normal | 2KB | 500ms |
| dht11 | CM4 | Normal | 2KB | 2000ms |

**关键参数：** tick 1000Hz；CM7 heap 96KB、CM4 heap 24KB；最大优先级 56。

**源码位置：** `app/CM7/Core/Src/freertos.c`、`app/BSP/bsp_watchdog.c`、`app/BSP/ADC/bsp_adc.c`、`app/CM4/BSP/Sensor/dht11.c`。

**可能追问：** `osThreadNew` 的 stack 参数单位是什么？

**追问回答：** 当前代码按 `配置值*4` 传入，即"字"换算成"字节"；面试时要说清楚源码表达式和换算字节数。

---

### Q024 [A级]

**问题：** 跨核和本核事件通知，为什么用 task notification 而不是 queue？

**面试官关注点：** 同步原语选型。

**标准回答：**
本工程两个中断到任务的路径都用 task notification：
1. CM4 HSEM 事件 → `vTaskNotifyGiveFromISR()` 唤醒 SensorIpcTask；
2. USART6 DMA 回调 → `vTaskNotifyGiveFromISR()` 唤醒 OTA 任务。
task notification 比 queue 更轻量（无需预先创建对象、无拷贝），适合"单一事件唤醒单任务"的场景。

**关键参数：** 两处 `vTaskNotifyGiveFromISR`。

**源码位置：** `app/Common/Src/sensor_shared.c:100-115`、`app/BSP/OTA/bsp_ota_uart.c:187-210`。

**可能追问：** 什么场景下应该改用 queue？

**追问回答：** 需要缓冲多条数据、多生产者、或需要传递 payload 时；单纯二值唤醒用 notification 更省。

---

### Q025 [A级]

**问题：** CM7 heap 96KB、CM4 heap 24KB，为什么差这么多？

**面试官关注点：** 是否按核的负载分配内存。

**标准回答：**
CM7 承载 LVGL（对象、字体缓存、绘制缓冲）、OTA、Modbus、USB 等大量动态内存需求，所以 heap 大；CM4 只跑 FreeRTOS + DHT11，动态分配很少，24KB 足够。

**关键参数：** CM7 96KB、CM4 24KB。

**源码位置：** 两核 `FreeRTOSConfig.h`。

**可能追问：** heap 和链接脚本 RAM 是什么关系？

**追问回答：** heap 是 FreeRTOS 可动态分配的池，来自链接脚本划出的 RAM 区域；它不等于任务栈总和，任务栈在创建时从 heap 里分配。

---

### Q026 [A级]

**问题：** 为什么 watchdog 和 OTA 任务都是 Realtime 优先级？

**面试官关注点：** 是否理解高优先级的理由和代价。

**标准回答：**
watchdog 需要及时判断系统健康并喂 IWDG，延迟判断会误复位；OTA 接收需要及时搬运字节避免溢出。但"Realtime"只是 RTOS 优先级枚举映射，不等于硬实时保证——真正的硬实时还要 WCET、IRQ 延迟和调度分析。

**关键参数：** 两个 Realtime 任务。

**源码位置：** `app/BSP/bsp_watchdog.c`、`app/BSP/OTA/bsp_ota_app.c`。

**可能追问：** 高优先级 OTA 会不会饿死 GUI？

**追问回答：** 会带来风险。OTA 任务在非 DATA 阶段用 `wait_rx` 阻塞/短超时，不空转；DATA 阶段是 Realtime 可能挤压 LVGL，需要评估。这正是需要持续复盘的点。

---

### Q027 [A级]

**问题：** 为什么最大 syscall interrupt priority 是 5，和 HSEM ISR 有什么关系？

**面试官关注点：** FreeRTOS 中断优先级约束。

**标准回答：**
FreeRTOS 要求"可调用 API 的中断"优先级数值 ≥ `configMAX_SYSCALL_INTERRUPT_PRIORITY`（=5）。HSEM ISR 会调用 `vTaskNotifyGiveFromISR`，所以它的优先级必须落在允许范围内，否则会触发断言或行为未定义。

**关键参数：** `configMAX_SYSCALL_INTERRUPT_PRIORITY = 5`；USART6 IRQ 优先级也是 5。

**源码位置：** 两核 `FreeRTOSConfig.h`、`app/CM7/Core/Src/stm32h7xx_hal_msp.c`。

**可能追问：** 中断优先级数值和硬件优先级是什么关系？

**追问回答：** Cortex-M 里数值越小优先级越高；FreeRTOS 的 `configMAX_SYSCALL_INTERRUPT_PRIORITY` 用的是"逻辑数值"，需结合 `__NVIC_PRIO_BITS` 和 shift 理解实际抢占关系。

---

### Q028 [B级]

**问题：** tick 1000Hz 有什么利弊？

**面试官关注点：** 对调度开销和精度的权衡。

**标准回答：**
1000Hz 让 `osDelay(1)` = 1ms，方便 LVGL 高频调度和喂狗周期表达；代价是每秒 1000 次 tick 中断，功耗和调度开销增加。

**关键参数：** tick 1000Hz。

**源码位置：** 两核 `FreeRTOSConfig.h`。

**可能追问：** 低功耗场景会怎么改？

**追问回答：** 降到 100Hz 或 200Hz，或用 tickless idle；但要重新核对所有 `osDelay` 的周期表达。

---

## H6 Bootloader 设计

### Q029 [S级]

**问题：** 整个 Flash 是怎么划分的？

**面试官关注点：** 内存映射是否背熟。

**标准回答：**
| 区域 | 地址 | 大小 | 用途 |
|---|---|---|---|
| Bootloader | `0x08000000` | 128K | CM7 Bootloader |
| CM7 Slot A | `0x08020000` | 448K | 当前执行镜像 |
| CM7 Slot B | `0x08090000` | 448K | OTA 下载/暂存 |
| CM4 firmware | `0x08100000` | 1024K | CM4 镜像（Bank2） |
| Metadata | `0x38800000` | 结构体定 | BKPSRAM |

Bank1（`0x08000000`-`0x080FFFFF`）放 Bootloader + Slot A + Slot B，Bank2（`0x08100000`-`0x081FFFFF`）放 CM4。

**关键参数：** 见上表；Slot footer 32 字节，magic `0x534F4F54`。

**源码位置：** `bootloader/Common/Inc/boot_slot.h`、`app/Makefile/CM7/stm32h747xx_flash_CM7.ld`、`app/Makefile/CM4/stm32h747xx_flash_CM4.ld`。

**可能追问：** 为什么 APP 不能从 `0x08000000` 启动？

**追问回答：** 那是 Bootloader 独占区，保证上电先进入启动决策逻辑；APP 从 `0x08020000` 起，与 Bootloader 分离。

---

### Q030 [S级]

**问题：** Slot A/B 是什么关系？这是真正的 A/B rollback 吗？

**面试官关注点：** 是否诚实区分"下载缓冲"和"可切换执行"。

**标准回答：**
不是真正的 A/B 直接切换。当前 Slot B 是 OTA 下载/暂存槽，Bootloader 检测到 pending Slot B 有效后把它**拷贝到 Slot A**，最终固定从 Slot A 启动。`boot_meta_select_boot_slot()` 固定返回 Slot A，`Boot_IsAppValid()`/`Boot_JumpToApp()` 也固定用 Slot A。所以准确说法是"Slot B 下载 + Bootloader B→A 拷贝 + Slot A 执行"，不能说成"两镜像直接切换/成熟 rollback"。

**关键参数：** Slot A/B 各 448K；启动固定 Slot A。

**源码位置：** `bootloader/Common/Src/boot_meta.c:115-120`、`bootloader/CM7/Core/Src/boot_jump.c:24-27`。

**可能追问：** 真正 rollback 还需要哪些状态机？

**追问回答：** 需要 Bootloader 能直接跳 Slot B 执行、启动失败计数 + 自动切回旧槽、`boot_meta_on_boot_failure()` 被主流程真正调用、双槽各自完整校验。

---

### Q031 [S级]

**问题：** Bootloader 是怎么把 Slot B 拷贝到 Slot A 的？

**面试官关注点：** 启动决策和 B→A 拷贝流程。

**标准回答：**
Bootloader 先读 Slot A、Slot B 的有效性；当 metadata 标记 pending Slot B 且 Slot B footer 有效（magic 正确、size 非零、不超槽容量）时，执行 B→A 拷贝，然后仍从 Slot A 跳转。

**关键参数：** Slot B 有效性核心条件：footer magic + 非零 size + 不超槽容量。

**源码位置：** `bootloader/CM7/Core/Src/main.c:123-155`、`bootloader/Common/Src/boot_slot.c:78-90`。

**可能追问：** `boot_slot_is_valid()` 会重算镜像 CRC 吗？

**追问回答：** 不会。它主要验证向量和 footer 元数据，整镜像 CRC32 在 OTA VERIFY 阶段算，Bootloader 不做整镜像 CRC 重算。

---

### Q032 [A级]

**问题：** boot metadata 放在哪里，有哪些字段？

**面试官关注点：** 元数据设计。

**标准回答：**
放在 BKPSRAM 地址 `0x38800000`，包含 magic（`0xB007B007`）、version（1）、active_slot、pending_slot、boot_trial（trial max 3）、flags、slot_size[]、slot_crc[] 和自身 crc32。`boot_meta_load()` 检查 magic/version/结构 CRC，无效时填默认值。

**关键参数：** magic `0xB007B007`；version 1；trial max 3。

**源码位置：** `bootloader/Common/Inc/boot_meta.h`、`bootloader/Common/Src/boot_meta.c:73-95`。

**可能追问：** 为什么不用普通 Flash 存 metadata？

**追问回答：** BKPSRAM 避免普通 Flash 擦写影响启动状态记录，写入粒度小、擦写次数无限制。

---

### Q033 [A级]

**问题：** 为什么说"trial 字段存在"不等于"已实现 rollback"？

**面试官关注点：** 是否识别"代码存在"和"调用链完整"的区别。

**标准回答：**
源码有 `boot_trial` 字段和 `boot_meta_on_boot_failure()`（会递减 trial），但当前 Bootloader 主流程**没有调用** `boot_meta_on_boot_failure()`，且 `boot_meta_commit_pending()` 把 active slot 固定为 A。所以只能评"代码存在、完整失败回滚调用链证据不足"。

**关键参数：** `boot_meta_on_boot_failure()` 存在于 `boot_meta.c:154-168`，但主流程未调用。

**源码位置：** `bootloader/Common/Src/boot_meta.c`。

**可能追问：** trial max=3 是否等于三次回滚？

**追问回答：** 不等于，只能说明字段上限是 3，实际回滚调用链未闭环。

---

### Q034 [A级]

**问题：** 掉电时 B→A 拷贝有什么风险？

**面试官关注点：** 掉电窗口/原子性意识。

**标准回答：**
逻辑 Slot A/B 共享 Bank1 Sector 4，代码通过备份另一侧 64KB → 整扇区擦除 → 恢复来处理，存在掉电窗口：掉电可能损坏当前 APP 或两个逻辑槽。这是 OTA 审计报告 R2 风险。

**关键参数：** 共享 Sector 4；备份区 64KB。

**源码位置：** `app/BSP/OTA/bsp_ota_flash.c:142-174`、`bootloader/Common/Src/boot_slot_copy.c:23-39`。

**可能追问：** 量产设计怎么增强？

**追问回答：** 掉电原子写入（写前备份、写标记、完成标记）、双备份 metadata、或改用不共享物理扇区的槽位布局。

---

### Q035 [A级]

**问题：** 为什么跳转前要设置 MSP，值从哪来？

**面试官关注点：** 跳转细节。

**标准回答：**
MSP 从 APP 基地址第一个 word 读取（初始栈顶），调用 `__set_MSP()`；第二个 word 是 Reset_Handler 地址，跳转到它。跳转前还要清 NVIC、关 SysTick。

**关键参数：** 向量表第 0 项 MSP，第 1 项 Reset_Handler。

**源码位置：** `bootloader/CM7/Core/Src/boot_jump.c:29-63`。

**可能追问：** `CONTROL` 清零是为什么？

**追问回答：** 恢复特权线程 + 主栈（MSP）语义，避免带着 Handler 模式或 PSP 状态进入 APP。

---

## H7 UART OTA 升级

### Q036 [S级]

**问题：** 一次完整的 OTA 流程（设备侧状态机和命令序列）是怎样的？

**面试官关注点：** 是否掌握 OTA 状态机。

**标准回答：**
状态机：`IDLE → SESSION → ERASING → RECEIVING → READY_VERIFY → IDLE`。
命令序列：
```
GET_INFO → BEGIN_SESSION → ERASE → START_TRANSFER → DATA* → END_TRANSFER → VERIFY → RESET
```
- BEGIN_SESSION：选 inactive 槽（Slot B），生成 session token；
- ERASE：擦目标槽；
- START_TRANSFER：下发 token/addr/image_size/image_crc32；
- DATA：按 offset 写 chunk（最大 256B），逐包 ACK；
- END_TRANSFER：校验写入总量 == image_size；
- VERIFY：CRC32 校验 → 写 footer → set pending metadata；
- RESET：ACK 后延时 50ms → `NVIC_SystemReset()`。

**关键参数：** 状态机 6 态；chunk 256B。

**源码位置：** `app/BSP/OTA/bsp_ota_manager.c`（各 `ota_handle_*`）。

**可能追问：** END_TRANSFER 进入 READY_VERIFY 的条件？

**追问回答：** 只有 `s_bytes_written == s_image_size` 才进入 READY_VERIFY，防止写入不完整就进入校验。

---

### Q037 [S级]

**问题：** OTA 为什么改到 USART6 UART？原来的 transport 是什么？

**面试官关注点：** 传输层演进的理解。

**标准回答：**
原 transport 固定映射 W5500 TCP。后来按板级接线和调试需求切换到 USART6 UART（115200 8N1），复用现有 SOIP 协议，只替换 transport 后端。USART6 选型理由：USART1 已被 `printf`/日志占用，USART3 板级引出不如 USART6 清晰；USART6 的 PG9(RX)/PG14(TX) 引出明确。

**关键参数：** USART6；`bsp_ota_transport.c` 直接映射 UART BSP，不再依赖 W5500。

**源码位置：** `app/BSP/OTA/bsp_ota_transport.c`、`docs/UART_OTA_transport审计.md`。

**可能追问：** 切换 transport 要不要改协议？

**追问回答：** 不用。`bsp_ota_protocol.c` 是字节流解析器，不关心底层 TCP 还是 UART；Qt Host 也复用 `tryParseFrame`，帧格式不变。

---

### Q038 [A级]

**问题：** OTA 写入哪个槽？为什么是 Slot B？

**面试官关注点：** 双槽写策略。

**标准回答：**
写 inactive 槽（正常场景是 Slot B），避免 OTA 接收过程中覆盖正在运行的 Slot A。BEGIN_SESSION 时选择当前运行槽的 inactive 槽。

**关键参数：** `boot_slot_base(target_slot) + offset`。

**源码位置：** `app/BSP/OTA/bsp_ota_manager.c:360-371`。

**可能追问：** 为什么不能直接覆盖正在运行的 Slot A？

**追问回答：** 覆盖运行中的代码会立即崩溃；用独立下载槽 + 重启后 B→A 拷贝，接收过程不影响当前运行。

---

### Q039 [A级]

**问题：** CRC16 和 CRC32 分别解决什么问题？为什么不只用一种？

**面试官关注点：** 分层校验意识。

**标准回答：**
CRC16 保护每一帧传输（覆盖 version 到 payload），用于发现单帧误码并重传；CRC32 保护完整镜像内容，用于确认整个镜像下载正确。两者粒度不同：帧级校验及时止损，镜像级校验确保最终可运行。

**关键参数：** CRC16 初值 `0xFFFF`、多项式 `0x1021`；CRC32 reflected 多项式 `0xEDB88320`、初值 `0xFFFFFFFF`、最终取反。

**源码位置：** `app/BSP/OTA/bsp_ota_crc.c`。

**可能追问：** CRC32 失败后设备做什么？

**追问回答：** VERIFY 失败不会写 footer/置 pending，镜像不会被 Bootloader 采用，需主机重试。

---

### Q040 [S级]

**问题：** SOIP 帧格式是什么？

**面试官关注点：** 协议字段是否背熟。

**标准回答：**
```
SOF(2) AA55 | Version(1) 0x02 | Command(1) | Sequence(2 LE)
  | PayloadLen(2 LE) | Payload(N) | CRC16(2 LE)
```
固定 header 8 字节；payload 理论上限 1024B；当前 DATA chunk 256B。

**关键参数：** SOF `AA55`；version `0x02`；header 8B；CRC16 2B。

**源码位置：** `app/BSP/OTA/bsp_ota_protocol.h`。

**可能追问：** CRC16 是否包含 SOF？

**追问回答：** 不包含，覆盖范围是 version 到 payload。

---

### Q041 [A级]

**问题：** chunk 为什么选 256 字节，而不是 1024？

**面试官关注点：** 参数选择的工程理由和诚实边界。

**标准回答：**
协议解析器上限是 1024 payload，但当前业务 buffer 和 Host 默认 chunk 都是 256。工程上 274B 的 DATA 帧（8 header + 8 token/offset + 256 data + 2 CRC）明显小于 768B 接收 buffer，能放入缓冲。源码没有记录更深入的吞吐 benchmark，所以不能宣称 256 是完整性能最优实验得出的。

**关键参数：** DATA 帧最大 274B；接收 buffer 768B。

**源码位置：** `app/BSP/OTA/bsp_ota_conf.h`、`tools/ota-host/protocol/OtaProtocol.h`。

**可能追问：** 你会怎么确定最优 chunk？

**追问回答：** 做吞吐/重传率 benchmark，扫不同 chunk 和包间延时，结合 Flash 写页大小和接收缓冲容量取舍。

---

### Q042 [A级]

**问题：** 当前 OTA 支持签名/加密/防回滚吗？

**面试官关注点：** 是否清楚企业级 OTA 的缺失项。

**标准回答：**
`【当前未实现】` 数字签名、公钥/证书链验证、镜像加密、密钥管理、防回滚版本比较。版本字段存在于 footer 和 metadata，但没有按版本号拒绝旧镜像的比较逻辑。当前只有 CRC 校验 + session token + 双槽意图，属于具备基本完整性的工程 OTA 原型。

**关键参数：** footer.version、BOOT_META_VERSION 存在但未做版本比较。

**源码位置：** `STM32H757_OTA模块代码审计报告.md`（完整性章节）。

**可能追问：** 如果要做安全启动，先补什么？

**追问回答：** 先加镜像签名验证（公钥验签），再做防回滚版本比较，最后才是加密和密钥管理。

---

### Q043 [A级]

**问题：** OTA 擦写期间怎么避免和 CM4/其他任务冲突？

**面试官关注点：** 跨核 + 多任务的 Flash 并发控制。

**标准回答：**
两件事：1) peer task 通过 `bsp_ota_coop_register()` 注册，OTA 激活后 `bsp_ota_coop_suspend_peers()` 挂起，结束后恢复；2) OTA port 模式会 hold CM4，避免 CM4 在共享 Flash 上取指或访问。

**关键参数：** peer 挂起 + CM4 hold。

**源码位置：** `app/BSP/OTA/bsp_ota_manager.c:199-221`、`app/BSP/OTA/bsp_ota_port.c:14-46`。

**可能追问：** 为什么要 hold CM4？

**追问回答：** CM4 从 Flash 取指，擦写期间 Flash 不可读，不 hold 会导致 CM4 取指失败/跑飞。

---

### Q044 [B级]

**问题：** 你知道这个 OTA 实现有哪些已知风险吗？

**面试官关注点：** 是否做过自我代码审计。

**标准回答：**
审计报告列了 R1-R8：R1 GET_INFO 上报 Flash 容量 1MiB（实际 H757XIH6 是 2MiB）；R2 Slot A/B 共享 Bank1 Sector 4 的掉电窗口；R3 Flash 操作固定 Bank1；R4 rollback 调用链不足；R5 RESET 不检查 session/READY_VERIFY 状态；R6 DATA 未强制连续 offset；R7（transport 固定 W5500，已随 UART 切换解决）；R8 依赖任务初始化时序。

**关键参数：** R1 容量硬编码 1MiB vs 实际 2MiB。

**源码位置：** `STM32H757_OTA模块代码审计报告.md`（关键风险章节）、`bsp_ota_manager.c:295-300`。

**可能追问：** 哪个风险最危险？

**追问回答：** R2 共享扇区掉电窗口最危险——它可能直接损坏当前运行的 APP 或两个槽。

---

### Q045 [A级]

**问题：** RESET 命令处理有什么注意点？

**面试官关注点：** 复位前时序。

**标准回答：**
`ota_handle_reset()` 发送应答后延时 50ms（保证 ACK 发出去），再调用 `NVIC_SystemReset()`。审计指出 R5：当前不检查 session 或 READY_VERIFY 状态。

**关键参数：** 延时 50ms。

**源码位置：** `app/BSP/OTA/bsp_ota_manager.c:654-663`。

**可能追问：** 为什么 ACK 后要延时？

**追问回答：** 给阻塞式 TX 时间把 ACK 完整发出，否则立即复位可能让主机收不到应答。

---

## H8 DMA / Ring Buffer / 串口协议

### Q046 [S级]

**问题：** 115200 下原来的 UART OTA 为什么丢包？

**面试官关注点：** 是否理解接收路径的根本缺陷。

**标准回答：**
原设计是逐字节 `HAL_UART_Receive` 轮询加 `osDelay`，CPU 在接收间隙无法及时取走数据，115200 连续字节流下出现接收溢出/丢字节。更本质的是：协议解析任务的调度延迟被直接暴露给 UART 接收路径，而 OTA 帧是连续字节流，不能假设每次任务运行时恰好只到达一个字节。

**关键参数：** 115200 ≈ 每字节约 87µs。

**源码位置：** `app/BSP/OTA/bsp_ota_uart.c:5-8`。

**可能追问：** 只提高波特率能解决吗？

**追问回答：** 不能。核心是接收搬运和协议处理要解耦 + DMA re-arm + cache 一致 + 足够 ring 容量；提高波特率只是把阈值抬高，根本问题还在。

---

### Q047 [S级]

**问题：** 现在 UART 接收是怎么设计的？

**面试官关注点：** DMA + ring buffer 的完整理解。

**标准回答：**
```
USART6 FIFO → DMA ReceiveToIdle → 768B DMA buffer
  → D-Cache invalidate → 2048B SPSC ring buffer
  → vTaskNotifyGiveFromISR 唤醒 OTA 任务 → 协议解析
```
DMA 回调先失效 D-Cache，再把数据推入 ring，最后重新 arm DMA。这样把"实时搬运字节"和"协议处理"分离。

**关键参数：** DMA buffer 768B；ring 2048B；`HAL_UARTEx_ReceiveToIdle_DMA`。

**源码位置：** `app/BSP/OTA/bsp_ota_uart.c:97-109,187-210`。

**可能追问：** 回调里为什么要重新 arm DMA？

**追问回答：** `ReceiveToIdle` 触发一次回调后不会自动继续接收，必须重新调用以接收下一段数据。

---

### Q048 [A级]

**问题：** DMA buffer 为什么是 768 字节？ring buffer 为什么是 2048 字节？

**面试官关注点：** 缓冲区大小选型的理由。

**标准回答：**
- DMA buffer 768B：`BSP_OTA_RX_BUF_SIZE`，需大于当前最大 DATA 帧 274B，能容纳整帧（甚至一帧多）。
- ring 2048B：作为 DMA 搬运和 OTA 任务解析之间的缓冲，吸收 OTA 任务短时间未运行的输入抖动。
两者都是"够用 + 留余量"，源码没有完整压力 benchmark，不能宣称是精确计算的最优值。

**关键参数：** 768B / 2048B；DATA 帧 274B。

**源码位置：** `app/BSP/OTA/bsp_ota_conf.h`、`bsp_ota_uart.c:21-28`。

**可能追问：** 如何证明 2048 足以吸收抖动？

**追问回答：** 需要在最大 burst 速率 + 最长任务阻塞时间下做压力测试；当前没有该数据。

---

### Q049 [A级]

**问题：** D-Cache 对 UART DMA 有什么影响？怎么处理？

**面试官关注点：** 缓存一致性。

**标准回答：**
DMA 直接写内存，CPU 可能仍读 cache 里旧内容。所以 DMA 回调里先对 DMA buffer 做 `SCB_InvalidateDCache_by_Addr`，再读。注意：这与共享 IPC 不同——共享区靠 MPU 设成 non-cacheable，而 UART DMA buffer 在可缓存的 AXI SRAM，必须显式处理。

**关键参数：** invalidate 在回调中，见 `bsp_ota_uart.c:195-198`。

**源码位置：** `app/BSP/OTA/bsp_ota_uart.c:195-198`。

**可能追问：** invalidate 前要注意什么？

**追问回答：** 注意 cache line 对齐（DMA buffer 32 字节对齐）；invalidate 只能对 DMA 写、CPU 读的场景，若 CPU 写过该区要先 clean 再 invalidate。

---

### Q050 [A级]

**问题：** `ReceiveToIdle` 和普通 DMA 接收、中断接收有什么区别？

**面试官关注点：** 是否理解 RTI 语义。

**标准回答：**
`ReceiveToIdle` 在两种情况下触发回调：检测到空闲线，或 DMA 缓冲填满。回调参数 `Size` 是本轮实际收到的字节数。相比固定长度 DMA 接收，它更适合变长帧；相比逐字节中断，它用 DMA 块搬运，减少 CPU 介入。

**关键参数：** 回调 Size = 本轮字节数。

**源码位置：** `app/BSP/OTA/bsp_ota_uart.c:97-109,179-187`。

**可能追问：** IDLE 检测不到会怎样？

**追问回答：** 靠"DMA 缓冲填满"兜底触发回调，不会无限等待。

---

### Q051 [A级]

**问题：** ring buffer 满了怎么办？

**面试官关注点：** 背压/丢数据策略。

**标准回答：**
满时丢弃剩余字节，依赖帧 CRC 失败 + 主机重试恢复。这是"丢帧重传"策略，不是无限缓冲。

**关键参数：** ring 满丢弃，见 `bsp_ota_uart.c:44-58`。

**源码位置：** `app/BSP/OTA/bsp_ota_uart.c:44-58`。

**可能追问：** 为什么可以依赖重试？

**追问回答：** 因为 OTA 协议有 CRC16 帧校验 + Host 重试机制，丢一帧会被检测到并重传，不必保证零丢失。

---

### Q052 [B级]

**问题：** 现在 OTA 发送路径是 DMA 吗？

**面试官关注点：** 是否清楚收发不对称。

**标准回答：**
不是。RX 用 DMA ReceiveToIdle + ring + notification，但 TX 仍是阻塞式 `HAL_UART_Transmit`。所以准确表述是"RX 是 DMA，TX 是阻塞"，不能概括成收发都 DMA。

**关键参数：** TX `HAL_UART_Transmit`，见 `bsp_ota_uart.c:166-177`。

**源码位置：** `app/BSP/OTA/bsp_ota_uart.c:166-177`。

**可能追问：** 阻塞发送会影响什么？

**追问回答：** 长时间阻塞 TX 会占用 OTA 任务（Realtime），可能挤压 GUI；ACK 帧短影响小，但大数据下发方向是 Host→设备，所以设备 TX 以短 ACK 为主，暂可接受。

---

## H9 LCD / LTDC / LVGL / SDRAM

### Q053 [S级]

**问题：** framebuffer 多大？为什么放 SDRAM？

**面试官关注点：** 显存计算和存储选型。

**标准回答：**
800×480 RGB565，每像素 2 字节：`800*480*2 = 768000 字节 ≈ 750KiB`。这么大的连续 buffer 放不进内部 SRAM（AXI SRAM 512K 还要装应用 .bss），所以放外部 SDRAM（`0xD0000000`）。

**关键参数：** 768000 字节；framebuffer `0xD0000000`。

**源码位置：** `app/CM7/Core/Src/ltdc.c:66-73`、`app/BSP/LCD/lcd_ltdc.h:72-74`。

**可能追问：** 如果做双 framebuffer 需要多少？

**追问回答：** 约 1.5MiB（两个 750KiB）。

---

### Q054 [A级]

**问题：** LTDC 的时序参数是多少？为什么 ActiveWidth 不是 800？

**面试官关注点：** 是否理解 LCD 时序（消隐期）。

**标准回答：**
HBP 46、VBP 23、ActiveW 846、ActiveH 503、TotalW 1056、TotalH 525、HSync/VSync 0。ActiveW 是"有效区 + 前后肩"的累积值，Total 含消隐期，所以不等于分辨率 800。

**关键参数：** TotalW 1056、TotalH 525。

**源码位置：** `app/CM7/Core/Src/ltdc.c:42-77`。

**可能追问：** 消隐期参数错了会怎样？

**追问回答：** 画面偏移、闪烁、花屏或无法正常同步。

---

### Q055 [A级]

**问题：** LVGL 的显示是怎么配置的？

**面试官关注点：** 渲染模式和缓冲策略。

**标准回答：**
`lv_init() → lv_port_disp_init() → lv_port_indev_init()`。显示层创建 800×480 display，绑定 flush 回调，使用 `800*400` 的**部分渲染 buffer**，渲染模式 `LV_DISPLAY_RENDER_MODE_PARTIAL`，flush 回调把脏矩形写入 LCD/LTDC 后调用 `lv_display_flush_ready()`。

**关键参数：** partial buffer `800*400`；render mode PARTIAL。

**源码位置：** `app/BSP/LVGL/lv_port_disp.c:19-21,40-58,78-143`。

**可能追问：** 为什么用 partial 而不是全帧 buffer？

**追问回答：** 全帧 buffer 要 750KiB 且需双缓冲更占内存；partial 用小 buffer 分块刷新，省内存，代价是可能有轻微撕裂（除非用 vsync）。

---

### Q056 [A级]

**问题：** LVGL 任务 1ms 调用 handler，数据刷新为什么是 500ms？

**面试官关注点：** 区分渲染节拍和数据节拍。

**标准回答：**
1ms 的 `lv_timer_handler()` 是 LVGL 的渲染/输入/动画节拍，需高频调用；500ms 是业务数据刷新定时器，只从应用缓存读温湿度更新控件。两者频率不同：一个是引擎节拍，一个是数据刷新周期。

**关键参数：** 1ms handler；500ms 刷新。

**源码位置：** `app/CM7/Core/Src/lvgl_demo.c:100-117`。

**可能追问：** handler 周期和数据周期不匹配会不会有问题？

**追问回答：** 不会。数据刷新慢于渲染没关系；数据更新是低频事件，渲染节拍照常绘制。

---

### Q057 [B级]

**问题：** Touch 是中断驱动还是轮询？

**面试官关注点：** 输入路径细节。

**标准回答：**
是"软件 I2C（GPIO bit-bang）+ 控制器轮询"，不是中断驱动。Touch BSP 先试 GT9xxx，失败再试 FT5206/FT5426，成功后绑定对应 scan 函数；输入回调用轮询扫描。触摸初始化失败时 `touch_ready=false`，UI 仍可显示。

**关键参数：** 探测顺序 GT9xxx → FT5206/FT5426。

**源码位置：** `app/BSP/TOUCH/touch.c:41-78`、`app/BSP/LVGL/lv_port_indev.c:42-65`。

**可能追问：** 为什么软件 I2C 而不是硬件 I2C？

**追问回答：** 触摸控制器用 GPIO bit-bang（`ctiic.c`）实现，具体选型原因需看硬件/历史，但代码路径是软件 I2C。

---

### Q058 [A级]

**问题：** SDRAM 配置成 cacheable，Shared SRAM 配置成 non-cacheable，为什么不同？

**面试官关注点：** 缓存策略按用途区分。

**标准回答：**
- SDRAM（`0xD0000000`）设 cacheable/bufferable：服务于显示访问，CPU 写 framebuffer 走缓存更快；
- Shared SRAM（`0x38000000`）设 non-cacheable：服务于跨核一致性，避免 CM7 cache 读到 CM4 写入前的旧数据。
两者用途不同，缓存策略也不同。

**关键参数：** MPU 两处区域配置。

**源码位置：** `app/BSP/MPU/mpu.c:61-71`。

**可能追问：** framebuffer cacheable 会不会有显示一致性问题？

**追问回答：** 会。LTDC/DMA 读 framebuffer 时，CPU 写的 cache 数据要 clean 才可见，这是需要结合实际刷新策略验证的点。

---

### Q059 [A级]

**问题：** DMA2D 在这个工程里做什么？

**面试官关注点：** 图形加速器的作用。

**标准回答：**
LCD BSP 用 DMA2D 做纯色填充、颜色块填充和区域拷贝（清屏、块填充），减轻 CPU 做像素搬运的负担。

**关键参数：** 无。

**源码位置：** `app/BSP/LCD/lcd_ltdc.c:172-237,305-362`。

**可能追问：** 什么时候适合用 DMA2D？

**追问回答：** 大块纯色填充、位块拷贝、像素格式转换（RGB565↔ARGB8888）等；CPU 逐点写不划算的场景。

---

## H10 Watchdog 可靠性设计

### Q060 [S级]

**问题：** 这个工程的 watchdog 是几层结构？

**面试官关注点：** 软件 + 硬件看门狗的分工。

**标准回答：**
两层：1) 软件任务健康监控——每个槽位用递增 heartbeat counter，判断任务是否持续有进展；2) 硬件 IWDG1——当软件判断有任务不健康时停止刷新 IWDG，等待硬件复位。软件层做"谁死了"的细粒度判断，硬件层做"系统失活"的兜底复位。

**关键参数：** 每槽 counter + 上次观察值 + 最后进展时间。

**源码位置：** `app/BSP/bsp_watchdog.c:18-26,119-143,271-276`。

**可能追问：** 为什么需要两层？

**追问回答：** 软件层能区分"哪个任务死了"，但软件自身可能也挂；硬件 IWDG 在软件彻底停摆时兜底复位。

---

### Q061 [S级]

**问题：** IWDG 的参数和超时是多少？

**面试官关注点：** 是否背熟 IWDG 配置并会算超时。

**标准回答：**
IWDG1，时钟 LSI（约 32kHz），prescaler 128，window 4095，reload 2000。超时估算：`128 × 2000 / 32000 ≈ 8s`。注意 LSI 实际频率有误差，8s 是估算值非精密实测。

**关键参数：** PR=128、RLR=2000、WIN=4095、约 8s。

**源码位置：** `app/CM7/Core/Src/iwdg.c:40-54`。

**可能追问：** LSI 不精确会怎样影响超时？

**追问回答：** LSI 频率偏差直接线性影响超时时间（例如 ±10% 就是 ±0.8s），所以 8s 只是设计值，量产要标定或接受误差。

---

### Q062 [S级]

**问题：** 各监控槽位的超时是多少？

**面试官关注点：** 是否背熟槽位表并理解超时选取。

**标准回答：**
| 槽位 | 超时 |
|---|---|
| LVGL | 5000ms |
| default | 8000ms |
| Modbus | 10000ms |
| ADC | 5000ms |
| CM4 heartbeat | 10000ms |
| W5500 | 60000ms |

watchdog 任务 Realtime、2000ms 喂狗、启动宽限期 45000ms。

**关键参数：** 见上表。

**源码位置：** `app/BSP/bsp_watchdog.c:28-35`。

**可能追问：** 为什么 CM4 槽是 10000ms 而 DHT11 周期是 2000ms？

**追问回答：** 给 5 个采样周期容错——允许 CM4 偶发几次发布延迟，但超过 10s 仍无发布就判死核。用倍数而非紧贴周期，避免误报。

---

### Q063 [A级]

**问题：** heartbeat 为什么用递增 counter，而不是简单 boolean？

**面试官关注点：** 是否理解"进展检测"比"存活标记"更强。

**标准回答：**
`BSP_Watchdog_Kick()` 递增 counter，监控任务比较"当前 counter 和上次观察到的 counter"判断是否有进展。boolean 只能区分"曾经运行过"，counter 能区分"任务持续有进展"。

**关键参数：** counter 递增 + 上次观察值。

**源码位置：** `app/BSP/bsp_watchdog.c:119-143`。

**可能追问：** counter 和"最后进展时间"各自有什么用？

**追问回答：** counter 判断是否前进，时间判断是否超时；两者配合才能得出 stale 结论。

---

### Q064 [S级]

**问题：** OTA 期间 watchdog 为什么曾经误判？boolean 设计有什么缺陷？

**面试官关注点：** 这是重点 Debug 案例的浓缩版。

**标准回答：**
OTA/Flash 擦写期间，LVGL、ADC 等任务被挂起，watchdog 若继续检查所有任务会把"预期挂起"误判为死任务。早期用 boolean 标记长操作，多个调用者嵌套时，一个子系统退出会清零另一个仍在进行的长操作状态，导致豁免被错误清除（或反过来）。修复是改用引用计数：进入递增、退出递减，只有归零才恢复完整监控。

**关键参数：** `s_long_op_active` 引用计数。

**源码位置：** `app/BSP/bsp_watchdog.c:48-49,105-117`、`app/BSP/OTA/bsp_ota_port.c:31-45`。

**可能追问：** 引用计数会不会溢出？

**追问回答：** 理论上需要防护（如饱和处理）；当前成对调用，实际不会无界增长，但可作为改进点。

---

### Q065 [A级]

**问题：** 复位后怎么知道是 IWDG 复位还是别的？

**面试官关注点：** 复位原因诊断。

**标准回答：**
读 `RCC->RSR`，区分 IWDG1（`RCC_RSR_IWDG1RSTF`）、WWDG、POR、NRST、CM7/CM4 软件复位等。

**关键参数：** `RCC->RSR` 各标志位。

**源码位置：** `app/BSP/bsp_watchdog.c:76-98`。

**可能追问：** 为什么要区分复位原因？

**追问回答：** 便于上电后诊断是电源问题、外部复位、软件复位还是看门狗复位，指导故障定位和日志记录。

---

### Q066 [A级]

**问题：** 启动宽限期 45000ms 是干什么的？

**面试官关注点：** 是否理解启动阶段为什么要豁免。

**标准回答：**
系统启动时各任务还没进入稳定周期（SDRAM、LCD、OTA、CM4 唤醒都在初始化），此时强制检查会误判。宽限期内 watchdog 不判 stale，给系统完成初始化。

**关键参数：** 45000ms。

**源码位置：** `app/BSP/bsp_watchdog.c:13-14,242-255`。

**可能追问：** 宽限期太长有什么风险？

**追问回答：** 启动阶段如果真卡死，要等 45s 才复位，首启故障恢复慢。

---

### Q067 [A级]

**问题：** CM4 迁移后，原来的监控为什么失效了？怎么修的？

**面试官关注点：** 跨核心跳的设计动机。

**标准回答：**
DHT11 迁到 CM4 后，原来仅在 CM7 本地 kick 的 heartbeat 不再代表传感器任务实际运行——CM4 死了 CM7 也发现不了。修复：新增 `BSP_WDG_CM4` 槽位，CM7 每次收到 CM4 的 HSEM 发布通知就 kick 该槽；并且启动时先预置一次槽位，防止 CM4 从未启动时被永久忽略。

**关键参数：** CM4 槽 10000ms；启动预置一次。

**源码位置：** `app/BSP/bsp_watchdog.h`、`app/CM7/Core/Src/freertos.c:180-203`。

**可能追问：** 如果 HSEM 中断丢失，软件 watchdog 能区分"CM4 死机"和"通信故障"吗？

**追问回答：** 不能。当前只看"有没有收到发布"，HSEM 链路故障和 CM4 死机表现一样（都无通知），这是设计的局限。

---

## H11 Qt OTA Host 设计

### Q068 [A级]

**问题：** Qt Host 的架构是怎么分的？

**面试官关注点：** 上位机分层。

**标准回答：**
分三层：`engine/`（OTA 更新流程）、`protocol/`（协议 + CRC，`OtaProtocol::tryParseFrame`）、`transport/`（`SerialManager`、`NetworkManager`、抽象传输）。UI 可切换 TCP/UART 模式，串口模式把 `UpdateEngine` 绑定到 `SerialManager`。

**关键参数：** `tools/ota-host/OTA.pro`。

**源码位置：** `tools/ota-host/`。

**可能追问：** Host 和 MCU 协议怎么保证一致？

**追问回答：** 对照 `tools/ota-host/protocol/OtaProtocol.h` 的帧字段、chunk、CRC、ACK 参数，与 MCU parser 对应实现核对；帧格式不变。

---

### Q069 [A级]

**问题：** SerialManager 怎么配串口？

**面试官关注点：** 上位机串口参数。

**标准回答：**
用 `QSerialPort`，参数 Data8、NoParity、OneStop、NoFlowControl，支持 115200/自定义波特率，readyRead 接收，复用 `OtaProtocol::tryParseFrame()` 解析。与 MCU 的 115200 8N1 无流控一致。

**关键参数：** 115200 8N1 NoFlowControl。

**源码位置：** `tools/ota-host/transport/SerialManager.cpp:22-53,87-105`。

**可能追问：** 为什么 USB-TTL 必须共地？

**追问回答：** 串口是单端信号，收发双方需要共同参考地，否则电平无法正确判定，通信不可靠。

---

### Q070 [B级]

**问题：** Host 的重试/window/包间延时是多少？

**面试官关注点：** 传输可靠性参数。

**标准回答：**
重试 5 次；window size 1（当前倾向停等式/单窗口）；默认包间延时 2ms。window=1 意味着发一帧等 ACK，吞吐受 RTT 限制但实现简单、可靠。

**关键参数：** retry 5；window 1；delay 2ms。

**源码位置：** `tools/ota-host/`（协议/Host 常量）。

**可能追问：** window=1 对吞吐有什么影响？

**追问回答：** 停等式吞吐 = 帧长 / RTT，串口 + ACK 往返会限制速度；要提速可增大 window 做流水线/滑动窗口，但复杂度和缓冲需求上升。

---

## H12 工程 Debug 案例

> 本节按"现象 → 定位过程 → 寄存器分析 → 根因 → 解决方案"组织，是面试重点，务必能完整复述。

### D1 [S级] HSEM notification 失效

**现象：**
CM4 第一次发布后 CM7 能收到数据，但后续发布不再唤醒 `SensorIpcTask`，共享数据表现为偶发不及时。

**定位过程：**
1. 检查 CM7 的 HSEM IRQ 入口是否进入 HAL；
2. 检查 HAL 回调是否处理事件 HSEM2 mask；
3. 检查 FreeRTOS task notification 是否发出；
4. 检查 `HAL_HSEM_ActivateNotification()` 的调用时机。

**寄存器分析：**
- HSEM 的中断使能/通知状态由 `HAL_HSEM_ActivateNotification()` 管理；
- `HSEM1_IRQHandler`/`HSEM2_IRQHandler` 进入 `HAL_HSEM_IRQHandler()`；
- `HAL_HSEM_IRQHandler()` 处理事件后会**清除对应 IER**。

**根因：**
`HAL_HSEM_IRQHandler()` 处理后清掉 IER，若回调不重新调用 `HAL_HSEM_ActivateNotification()`，通知只会成功一次。

**解决方案：**
在 `HAL_HSEM_FreeCallback()` 中检测事件 HSEM2 后立即重新 arm，再用 `vTaskNotifyGiveFromISR()` 唤醒任务。

**源码位置：** `app/Common/Src/sensor_shared.c:99-116`。

---

### D2 [S级] UART OTA 轮询导致丢包

**现象：**
115200 下用阻塞式接收 + `osDelay` 时出现丢字节、帧 CRC 错误或协议不同步。

**定位过程：**
- 检查 USART6 FIFO 是否开启；
- 检查 DMA1 Stream2 是否连对 USART6_RX；
- 检查 `ReceiveToIdle` 是否重新 arm；
- 检查 DMA buffer 是否在 D-Cache 下 invalidate；
- 检查 768B DMA buffer 与 2048B ring 的写入边界；
- 检查 Host chunk 256 与 MCU chunk 256 是否一致。

**寄存器分析：**
- USART6 使用 FIFO（TX/RX threshold 1/8）；
- 接收 API `HAL_UARTEx_ReceiveToIdle_DMA`，回调 `Size` 为本轮字节数。

**根因：**
逐字节 `HAL_UART_Receive` 轮询 + `osDelay` 使 CPU 在接收间隙无法及时取走数据，协议解析任务的调度延迟被直接暴露给 UART 接收路径。

**解决方案：**
改为 FIFO → DMA ReceiveToIdle → 768B DMA buffer → 2048B ring buffer → task notification，把字节搬运和协议解析解耦。

**关键参数：** USART6、115200 8N1、DMA1 Stream2、768B/2048B、DATA 帧最大 274B。

**源码位置：** `app/CM7/Core/Src/usart.c`、`app/BSP/OTA/bsp_ota_uart.c`。

---

### D3 [S级] OTA 期间 watchdog 误判

**现象：**
OTA/Flash 擦写期间，LVGL、ADC 等任务被挂起，watchdog 把这些预期挂起误判为死任务，导致误复位。

**定位过程：**
- 检查 OTA peer 挂起逻辑；
- 检查 watchdog 长操作状态；
- 检查健康检查范围是否随长操作调整。

**寄存器分析：**
- 最终硬件复位由 IWDG1 完成，复位原因读 `RCC->RSR` 的 `RCC_RSR_IWDG1RSTF`。

**根因：**
早期 boolean 标记会被多个长操作调用相互覆盖：一个子系统退出长操作后可能清除仍被另一个子系统持有的豁免状态。

**解决方案：**
` s_long_op_active` 改为引用计数：进入递增、退出递减，只有计数归零才恢复完整任务监控。

**关键参数：** `s_long_op_active` 引用计数；长操作期间只监控 W5500 槽。

**源码位置：** `app/BSP/bsp_watchdog.c:48-49,105-117,194-205`。

---

### D4 [S级] CM4 迁移后 heartbeat 失效

**现象：**
DHT11 迁移到 CM4 后，若 CM7 仍只监控本核任务，CM4 死机可能不会被发现，系统不会复位。

**定位过程：**
- 检查 watchdog slot 是否存在 CM4；
- 检查 CM4 是否在每次采集后触发 HSEM2；
- 检查 CM7 `sensor_ipc` 是否在 notification 后更新 CM4 heartbeat；
- 检查 10000ms 超时与 DHT11 2000ms 周期是否匹配。

**寄存器分析：**
- HSEM2 事件通知进入 `HSEM2_IRQHandler()`；
- 软件健康失败后由 IWDG1 复位。

**根因：**
传感器任务已运行在 CM4，原先仅在 CM7 侧 kick 的本地 heartbeat 不再代表传感器任务实际运行。

**解决方案：**
新增 `BSP_WDG_CM4` 槽位，CM7 每次收到 CM4 发布通知后 kick 该槽，且启动时预置一次，防止 CM4 从未启动时被永久忽略。

**关键参数：** CM4 槽 10000ms；DHT11 2000ms。

**源码位置：** `app/BSP/bsp_watchdog.h`、`app/CM7/Core/Src/freertos.c:180-203`。

---

### D5 [A级] IWDG "不复位" 的排查

**现象：**
早期 OTA 过程中曾出现设备长时间（约 47s）不复位，怀疑 IWDG 没工作。

**定位过程（系统化验证三层）：**
1. 验证 LSI 已启动：读 `RCC->CSR` 的 `LSION`/`LSIRDY`；
2. 验证 IWDG 已初始化并计数：读 `IWDG->SR`（PVU/RVU/WVU 全 0 = 写入完成）；
3. 做实验：停喂 → 观察约 8s 是否复位（selftest 确认会复位）；
4. 排查调试器冻结：读 `DBGMCU->APB4FZ1` 的 `DBG_IWDG1_STOP` 和 `DHCSR` 的 `C_DEBUGEN`/`S_HALT`。

**寄存器分析：**
- `RCC->CSR`：`LSIRDY=1` 证明 LSI 在跑；
- `IWDG->SR`：PVU/RVU/WVU=0 证明 reload/prescaler 写入完成；
- `DBGMCU->APB4FZ1`：`DBG_IWDG1_STOP=1` 是调试模式冻结，但**只在 core HALTED（S_HALT=1）时生效**；
- `DHCSR`（0xE000EDF0）：`S_HALT=0` 说明 core 在运行，冻结不生效。

**根因：**
IWDG 本身工作正常（selftest 确认约 8s 复位）；真正的"47s 不复位"是长操作 `s_long_op_active` 被 W5500 子系统错误覆盖（boolean clobber），导致 watchdog 长时间停留在长操作模式、不停喂 IWDG。

**解决方案：**
长操作标志改为引用计数（见 D3），避免多个子系统相互覆盖。

**关键参数：** 约 8s 复位；`DBG_IWDG1_STOP` 仅在 HALT 时冻结。

**源码位置：** `app/CM7/Core/Src/iwdg.c`、`app/BSP/bsp_watchdog.c:48-49`。

---

## H13 工程设计取舍

### Q071 [S级]

**问题：** 为什么不用 OpenAMP/RPMsg，用 Shared SRAM + HSEM 的边界在哪？

**面试官关注点：** 通信方案的工程判断。

**标准回答：**
第一阶段需求是简单传感器数据通道，Shared SRAM + HSEM 足够、轻量、无额外依赖；OpenAMP/RPMsg 会引入更重框架和更大调试面。边界：数据量大、消息种类多、需要标准协议或跨平台生态时，才值得引入 OpenAMP。

**关键参数：** 无。

**源码位置：** 架构层面。

**可能追问：** Shared SRAM 方案的最大缺点？

**追问回答：** 需要自己管理一致性、通知、缓冲区布局和初始化竞态；扩展性（多传感器、多消息）不如标准框架。

---

### Q072 [A级]

**问题：** 这个系统的主要内存压力来自哪里？

**面试官关注点：** 内存预算意识。

**标准回答：**
LCD framebuffer（约 750KiB，放 SDRAM）、LVGL 栈/对象、OTA buffer、以及 AXI SRAM 里的应用 .bss。实际测得 CM7 固件约 441KB flash / 508KB bss，Slot A 只剩约 17KB 余量、AXI SRAM 只剩约 16KB 余量——已经比较紧张。

**关键参数：** `.bss` 508KB；framebuffer 750KiB。

**源码位置：** `arm-none-eabi-size build/GPIO_CM7.elf`、链接脚本。

**可能追问：** 如何查内存碎片和峰值？

**追问回答：** FreeRTOS `xPortGetFreeHeapSize()`/`vTaskList`/stack high-water mark + 链接 map 文件分析。

---

### Q073 [A级]

**问题：** 这个工程最大的实时性风险是什么？

**面试官关注点：** 是否理解系统的薄弱环节。

**标准回答：**
UART/OTA 接收搬运、GUI 高频 handler、DMA cache 一致性、高优先级长操作之间的相互影响。例如 OTA（Realtime）长时间占用会挤压 LVGL；DMA 缓存不处理会导致数据错误。

**关键参数：** 无。

**源码位置：** 架构层面。

**可能追问：** 你会先测哪一项？

**追问回答：** 先做 115200 连续帧压力测试（观察丢包/重传），再做 OTA + GUI 并发测试（观察 UI 卡顿和喂狗）。

---

### Q074 [A级]

**问题：** 怎么证明你"没有修改协议"、切换 transport 是安全的？

**面试官关注点：** 变更边界控制。

**标准回答：**
对照 `tools/ota-host/protocol/OtaProtocol.h` 的帧字段、chunk、CRC、ACK 参数，与 MCU `bsp_ota_protocol.c` 对应实现核对；`bsp_ota_protocol.c` 是字节流解析器，不关心底层 TCP/UART，切换 transport 只替换字节来源，帧格式不变。

**关键参数：** 协议不变。

**源码位置：** `app/BSP/OTA/bsp_ota_protocol.c`、`tools/ota-host/protocol/`。

**可能追问：** 如何做 Host/Device 互操作测试？

**追问回答：** 用相同测试向量（帧字节流、CRC 用例）双端跑；再实机发镜像验证 GET_INFO/BEGIN/DATA/VERIFY/RESET 全流程。

---

### Q075 [A级]

**问题：** 面试时面对"参数很多"的工程，你怎么避免背错？

**面试官关注点：** 学习方法论。

**标准回答：**
以链接脚本、配置头、CubeMX 初始化源文件、Host 常量为一手证据；对公式推导（如 8s、480MHz）和未实测项明确标注"配置推导值"；不把"代码存在"说成"硬件已验证"。

**关键参数：** 无。

**源码位置：** 方法层面。

**可能追问：** 哪些参数要放量产校准表？

**追问回答：** LSI 实际频率（影响 IWDG 超时）、HSE 实际频率、SDRAM 时序参数、面板 ID 映射等。

---

### Q076 [A级]

**问题：** 这个项目还有哪些"代码存在但没完全落地"的设计？

**面试官关注点：** 事实边界是否诚实。

**标准回答：**
1. 完整 rollback（trial 字段有、调用链缺）——`【当前未实现】`；
2. OTA 数字签名/加密/防回滚——`【当前未实现】`；
3. 掉电原子提交——`【当前未实现】`；
4. 跨核心跳对"HSEM 链路故障 vs CM4 死机"的区分——`【设计讨论】`；
5. 收发都 DMA——TX 仍是阻塞，`【设计讨论】`；
6. Modbus TCP / 真正的双镜像无缝切换——`【当前未实现】`。

**关键参数：** 无。

**源码位置：** `STM32H757_OTA模块代码审计报告.md`、`docs/` 各审计文档。

**可能追问：** 这些缺失会不会成为面试减分项？

**追问回答：** 不会。主动说明"哪些做了、哪些是原型、哪些未落地"反而体现工程严谨，比把未实现说成已实现更可信。

---

## Top 50 核心必背题

> 从全题库筛选，按优先级排序，作为最终面试准备重点。括号内为题目编号。

### 优先级 1：项目介绍
1. Q001 项目是什么，用什么 MCU
2. Q005 项目最大亮点
3. Q003 软件分层架构
4. Q002 为什么用 H757 双核

### 优先级 2：双核启动
5. Q007 完整启动流程
6. Q008 CM7 唤醒 CM4（HSEM0 + STOP）
7. Q029 Flash 布局
8. Q009 跳转前关中断/清 SysTick

### 优先级 3：HSEM / IPC
9. Q017 为什么 Shared SRAM + HSEM
10. Q018 三个 HSEM 分工
11. Q019 Shared SRAM non-cacheable
12. Q013 CM4→CM7 数据链路
13. Q021 sensor_ipc_init 初始化竞态

### 优先级 4：OTA
14. Q036 OTA 状态机 + 命令序列
15. Q037 为什么改 USART6 UART
16. Q030 Slot A/B 是不是真正 rollback
17. Q040 SOIP 帧格式
18. Q039 CRC16 vs CRC32
19. Q041 chunk 为什么 256
20. Q042 签名/加密/防回滚是否实现
21. Q038 写哪个槽为什么

### 优先级 5：Debug 案例
22. D1 HSEM notification 失效
23. D2 UART OTA 丢包
24. D3 OTA watchdog 误判
25. D4 CM4 heartbeat 失效
26. D5 IWDG 不复位排查

### 优先级 6：FreeRTOS
27. Q023 任务清单 + 优先级
28. Q024 task notification vs queue
29. Q025 heap 96K vs 24K
30. Q026 为什么 watchdog/OTA Realtime

### 优先级 7：Watchdog
31. Q060 两层 watchdog
32. Q061 IWDG 参数 + 8s
33. Q062 各槽位超时
34. Q064 boolean → refcount
35. Q067 CM4 迁移 heartbeat 失效

### 优先级 8：UART / DMA / Ring
36. Q046 115200 为什么丢包
37. Q047 现在接收设计
38. Q048 768B / 2048B 选型
39. Q049 D-Cache 一致性
40. Q050 ReceiveToIdle 语义

### 优先级 9：Bootloader
41. Q031 B→A 拷贝
42. Q032 metadata 字段/位置
43. Q033 trial ≠ rollback
44. Q034 掉电窗口
45. Q035 跳转设 MSP

### 优先级 10：LCD / 参数
46. Q053 framebuffer 多大为什么放 SDRAM
47. Q055 LVGL partial render buffer
48. Q006 时钟树 + 480MHz
49. Q068 Qt Host 架构
50. Q076 事实边界（哪些未落地）

---

## 附：核心参数速查表（考前过一遍）

| 类别 | 参数 | 值 |
|---|---|---|
| MCU | 型号 | STM32H757（宏 `STM32H757xx`） |
| 时钟 | HSE / VCO / 主频 | 25MHz / 960MHz / ~480MHz（推导） |
| Flash | Bootloader / Slot A / Slot B / CM4 | 0x08000000 128K / 0x08020000 448K / 0x08090000 448K / 0x08100000 1024K |
| RAM | DTCM / AXI / Shared / CM4 | 0x20000000 128K / 0x24000000 512K / 0x38000000 64K / 0x10000000 288K |
| IPC | 共享结构 magic / HSEM | `0x53484D31` / HSEM0 握手、1 数据、2 事件 |
| 传感 | DHT11 引脚 / 周期 | GPIOD Pin12 / 2000ms |
| FreeRTOS | tick / heap | 1000Hz / CM7 96K、CM4 24K |
| UART OTA | 波特率 / 格式 | 115200 / 8N1 无流控 |
| UART OTA | DMA buffer / ring | 768B / 2048B |
| 协议 | SOF / header / chunk | AA55 / 8B / 256B |
| 协议 | DATA 帧最大 | 274B |
| CRC | CRC16 / CRC32 | 0xFFFF/0x1021 / 0xEDB88320 反射 |
| LCD | 分辨率 / 格式 / fb | 800×480 / RGB565 / 0xD0000000 |
| LVGL | partial buffer | 800×400 |
| IWDG | PR/WIN/RLR / 超时 | 128 / 4095 / 2000 / ~8s |
| Watchdog | 槽位超时 | LVGL 5s、default 8s、Modbus 10s、ADC 5s、CM4 10s、W5500 60s |
| Host | retry / window / delay | 5 / 1 / 2ms |
| metadata | 地址 / magic / trial max | 0x38800000 / 0xB007B007 / 3 |
| footer | 大小 / magic | 32B / 0x534F4F54 |

> 提示：以上所有数值均可从源码/链接脚本/配置头/审计文档追溯；未实测项（480MHz、8s 超时、SDRAM 容量）已在对应题目中标注为"配置推导值"。
