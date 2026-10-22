# 大厂面试追问树

> 配套《docs/interview_core30_v2.md》的 30 道核心题，按大厂面试风格做深度追问树。
> 每道核心题至少展开 5 层追问。约束：**不新增工程事实**，所有追问与回答均来自当前工程实现/审计文档。
> 用法：先背熟 30 秒回答，再用本树练"被连续追问不崩"。

---

## Q001 [S级] 项目介绍

**核心问题：** 介绍一下这个项目，用的是什么 MCU？

**追问1：** 怎么从构建系统证明它不是单核工程？

**答：** 存在两个独立 Makefile、两个链接脚本、两个启动文件（`startup_stm32h757xx_CM7.s` / `_CM4.s`），分别产出 `GPIO_CM7.elf` 和 `GPIO_CM4.elf`；CM7 用 Cortex-M7 目标（`fpv5-d16`）、CM4 用 Cortex-M4 目标（`fpv4-sp-d16`）。

**追问2：** CM7 和 CM4 的浮点配置有什么区别？

**答：** CM7 是 `fpv5-d16`（双精度硬浮点），CM4 是 `fpv4-sp-d16`（单精度硬浮点），两者都启用 FreeRTOS 浮点上下文支持。

**追问3：** 双核在工程布局上怎么体现"主核/从核"？

**答：** CM7 的 Makefile 链接 LVGL、LTDC、FMC/SDRAM、SDMMC/FatFs、USART6 OTA、USB 和全部应用任务；CM4 的 Makefile 只列 FreeRTOS、DHT11、共享 IPC 和必要 HAL。CM7 是系统时钟、外部内存、应用调度的主控核。

**追问4：** 你凭什么说 CM4 是"精简"工程，而不是"没写完"？

**答：** CM4 的 `freertos.c` 只做 `sensor_ipc_init(NULL)` + `dht11_app_start()` 两件事，这是有意的职责收敛，而不是半成品；它不链接任何 LCD/OTA/网络模块。

**追问5：** 如果面试官追问"这个项目最终交付了什么"，你怎么回答？

**答：** 交付了可编译链接的 CM7/CM4 双核应用 + Bootloader + Qt OTA Host，以及双核 IPC、UART OTA、watchdog 三层监控这些已实现且可追溯的实现；同时诚实说明：完整 OTA 长时间压力、掉电恢复、跨核故障注入还没有板级验证记录。

---

## Q002 [S级] 为什么用 H757 双核

**核心问题：** 为什么用 H757 双核，CM7/CM4 各承担什么？

**追问1：** DHT11 读 40 bit 数据具体怎么读，为什么要在意中断？

**答：** 单总线时序靠 DWT 周期计数器做微秒级等待，读 40 bit（温湿度各 16 bit + 校验 8 bit）期间关闭中断，避免被中断打断导致时序错乱。

**追问2：** 关中断的时间量级是多少？对系统有什么影响？

**答：** DHT11 一帧约几十 ms 级别（含起始信号 + 40 bit 位时序），关中断会阻塞同一核上的所有中断响应和任务调度，这正是不能和 GUI/OTA 同核的原因。

**追问3：** 如果 GUI 卡顿，CM4 能替 CM7 分担渲染吗？

**答：** 不能。CM4 分担的是"低频、时序敏感的采集"，不承担图形渲染——LTDC/LVGL 都只在 CM7 链接。要分担 GUI 需要把显示数据源搬到 CM4 并引入更大带宽的跨核通道，当前架构没有。

**追问4：** 单核到底能不能做这个项目？

**答：** 能，但要在采集期间容忍 GUI 卡顿、或在采集线程里精心调度。双核的价值是把"微秒时序采集"和"重负载应用"在物理核上隔离，降低调度的复杂度和风险。

**追问5：** DHT11 这种传感器本身适合高实时采样吗？

**答：** 不适合。DHT11 采样周期约 2s，本身就是低频传感器，把它放 CM4 不是因为"高频实时"，而是因为它的"单次读取时序严苛（关中断忙等）"会干扰别的高频实时任务，隔离是为了保护 CM7 的 GUI/OTA。

---

## Q003 [S级] 软件分层架构

**核心问题：** 这个工程的软件分层怎么组织？

**追问1：** Bootloader 和应用是同一个工程吗？

**答：** 不是。`bootloader/` 是独立工程，有独立的 HAL/CMSIS 副本、链接脚本和 Makefile，与 `app/` 分离。

**追问2：** 四个活动构建入口分别是什么？

**答：** CM7 APP（`app/Makefile/CM7`）、CM4 APP（`app/Makefile/CM4`）、CM7 Bootloader（`bootloader/Makefile/CM7`）、Qt Host（`tools/ota-host/OTA.pro`）。

**追问3：** BSP 层具体管哪些东西？

**答：** LCD/LTDC、SDRAM、Touch、ADC、UART OTA、watchdog、MPU、IPC——把硬件初始化细节和应用逻辑隔开。

**追问4：** 驱动层用 HAL 还是 LL？

**答：** 主要用 HAL，部分用 LL（如 `stm32h7xx_ll_fmc.c`、`stm32h7xx_ll_usb.c` 在构建清单里）。具体以 Makefile `C_SOURCES` 为准。

**追问5：** 分层给你带来的实际好处是什么（结合 Debug）？

**答：** 切换 OTA transport（W5500→UART）时，只需改 transport 层（`bsp_ota_transport.c`/`bsp_ota_uart.c`），manager、protocol、Flash、Bootloader 都不动——这就是分层解耦的直接回报。

---

## Q005 [S级] 项目最大亮点

**核心问题：** 你觉得这个项目最大的亮点是什么？

**追问1：** 这些亮点里，哪些是"硬件实测过"的？

**答：** 只有 IWDG 复位（selftest 确认约 8s 复位）、DHT11 离线状态（拔线显示 OFFLINE）等少数项做过实机验证；双核 IPC、OTA、watchdog 都已完成构建，但完整板级验证不足。

**追问2：** 完整 OTA 做过压力测试吗？

**答：** 没有。仓库没有 USB-TTL 实机升级、长时间压力、掉电恢复的日志记录。

**追问3：** 掉电恢复验证过吗？

**答：** 没有。Slot A/B 共享 Bank1 Sector 4，代码用"备份→整扇区擦除→恢复"处理，存在掉电窗口，这是已知风险（审计 R2），不是已验证的健壮性。

**追问4：** 那你怎么区分"代码存在"和"已验证"？

**答：** 以链接脚本、配置头、初始化源文件、Host 常量为一手证据；对公式推导（8s、480MHz）和未实测项明确标注"配置推导值"；不把"代码存在"说成"硬件已验证"。

**追问5：** 面试官可能问"你做过的最难的 Debug 是什么"，你答哪个？

**答：** HSEM notification 一次性失效——它涉及中断寄存器 IER 清除、回调 re-arm、FreeRTOS 中断优先级三件事叠在一起，最有代表性（详见 followup tree 的 D1 对应题 Q018）。

---

## Q007 [S级] 完整启动流程

**核心问题：** 从上电到应用跑起来，完整启动流程？

**追问1：** Bootloader 是怎么跳转到 APP 的？

**答：** 读 APP 基地址第 0 个 word 作初始 MSP（`__set_MSP`），第 1 个 word 作 Reset_Handler 地址，跳转前关中断/SysTick、清 NVIC、`CONTROL` 清零、写 boot magic，最后 `bx app_entry`。

**追问2：** VTOR 是谁设置的？值是多少？

**答：** 跳转函数本身不写 `SCB->VTOR`，由 APP 的 `SystemInit()` 设置：CM7 `0x08020000`（`FLASH_BANK1_BASE|0x20000`）、CM4 `0x08100000`（`FLASH_BANK2_BASE`）。

**追问3：** 为什么跳转前要关 SysTick 和清 NVIC？

**答：** 防止 Bootloader 的系统 tick 和已使能中断在 APP 未完成初始化时继续触发，导致向量表还没重定位就跳到错误地址。

**追问4：** CM4 是怎么被唤醒的？

**答：** CM7 配完时钟后 `HAL_RCCEx_EnableBootCore(RCC_BOOT_C2)` 释放 CM4；CM4 启动后进 D2 STOP 等 HSEM0；CM7 先等 `RCC_FLAG_D2CKRDY` 清再释放 HSEM0。

**追问5：** 为什么释放 HSEM0 前要先等 D2CKRDY 清？

**答：** 因为 CM4 必须在 STOP 等待态下才接得住 HSEM0 通知，否则通知发出时 CM4 还没在等，唤醒事件就丢了。

---

## Q008 [S级] CM7 唤醒 CM4

**核心问题：** CM7 是怎么把 CM4 唤醒的？

**追问1：** HSEM0 的语义是什么？它和数据 HSEM1、事件 HSEM2 有什么关系？

**答：** HSEM0 只用于启动/唤醒握手，是"启动控制"语义；HSEM1 是数据互斥锁，HSEM2 是 CM4→CM7 的事件通知。三者语义分离，不混用。

**追问2：** BCM4 option byte 是什么？代码里读了吗？

**答：** 源码没有读/写 option byte 的 BCM4 配置，无法从仓库证明实际芯片 option byte 值。当前代码按"BCM4=0 时由 `EnableBootCore(RCC_BOOT_C2)` 释放 CM4"的路径编写。

**追问3：** 如果 BCM4 被烧成 1 会怎样？

**答：** CM4 会自行启动而不是等 CM7 释放，启动时序改变，需要按新路径重新核对握手逻辑。

**追问4：** CM4 被唤醒后做什么？

**答：** 从 STOP 返回 → `HAL_Init` + `SystemCoreClockUpdate` + DMA 初始化 → 创建 FreeRTOS → `sensor_ipc_init(NULL)` + `dht11_app_start()`。

**追问5：** CM4 为什么还要 `SystemCoreClockUpdate()`？

**答：** CM4 的 `SystemCoreClock` 初值是 64MHz，而实际 D2 时钟由 CM7 配置，不更新会导致 DWT 延时和 FreeRTOS tick 算错。

---

## Q012 [S级] DHT11 为什么放 CM4

**核心问题：** DHT11 为什么放到 CM4，而不是留在 CM7？

**追问1：** DHT11 的微秒级时序是怎么实现的？

**答：** 用 DWT 周期计数器（`CYCCNT`）做微秒级等待，不是靠 `osDelay`（精度不够）。

**追问2：** 读 40 bit 期间为什么要关中断？

**答：** 单总线每位时序有严格的电平窗口，中断打断会让电平采样错位，导致读到错误的位。

**追问3：** 如何证明 CM7 不再直接访问 DHT11 GPIO？

**答：** CM7 的 Makefile 不含 `bsp_dht11.mk`，不链接 DHT11 驱动；`app/BSP/DHT11/dht11.c` 是历史残留文件，当前活动路径是 `app/CM4/BSP/Sensor/dht11.c`。

**追问4：** 数据引脚和周期是多少？

**答：** GPIOD Pin 12，采样周期 2000ms。

**追问5：** 迁移到 CM4 后，CM7 侧还残留什么 DHT11 痕迹？

**答：** 只有 `app/BSP/DHT11/dht11.c` 这个不参与编译的历史文件；CM7 的 `freertos.c` 里是 `SensorIpcTask` 通过共享数据读取，不碰 GPIO。

---

## Q013 [S级] CM4→CM7 数据链路

**核心问题：** CM4 采集的数据怎么到 CM7？

**追问1：** 为什么不轮询共享内存，而用 HSEM 通知？

**答：** 轮询要么空耗 CPU 要么延迟大；HSEM 事件 + task notification 让"数据到达"成为唤醒条件，CM7 任务阻塞等待，低延迟低开销。

**追问2：** task notification 和 queue 相比，为什么这里选 notification？

**答：** 单一事件唤醒单任务，task notification 更轻量（无需预先创建对象、无拷贝）；queue 适合缓冲多条、多生产者或传 payload 的场景。

**追问3：** HSEM2 是"锁"还是"通知"？

**答：** 主要是通知。CM4 release HSEM2 触发 CM7 中断，回调 re-arm；数据互斥由 HSEM1 单独负责。

**追问4：** 共享结构有哪些字段？

**答：** `magic`（`0x53484D31`）、`valid`、`temperature`（float）、`humidity`（float）、`timestamp`（`HAL_GetTick`）。

**追问5：** 跨核一致性怎么保证？

**答：** 共享区靠 CM7 MPU 配 non-cacheable + 结构 32 字节对齐 + HSEM1 临界区保护读写。IPC 数据不走 DMA，所以没有 UART DMA 那种显式 cache invalidate。

---

## Q015 [S级] DHT11 离线状态设计

**核心问题：** DHT11 的"离线"状态怎么设计？

**追问1：** 读失败时为什么也要发布？

**答：** 否则 CM7 无法区分"DHT11 离线"和"CM4 死了"——失败也发，`valid` 标志承担"在线/离线"，发布频率承担"CM4 是否存活"。

**追问2：** `valid` 和发布频率各承担什么语义？

**答：** `valid` = 单次读数是否有效（→ 在线/离线标志）；发布频率（2000ms 是否持续发布）= CM4 是否存活（→ CM4 心跳）。

**追问3：** 为什么"读失败"不直接复位？

**答：** 单次读失败可能是传感器本身问题（非致命），复位解决不了且影响可用性；只有 CM4 整个停止发布才说明核级故障，才需要复位。

**追问4：** `dht11_online` 这个标志在哪些地方消费？

**答：** 三处：LCD 右上角显示 ONLINE/OFFLINE、Modbus 保持寄存器 3、应用数据缓存（互斥锁保护）。

**追问5：** 两个故障信号（读失败 vs 核失联）分别怎么被系统处理？

**答：** 读失败 → `valid=0` → UI 显示 OFFLINE + Modbus 寄存器 3=0，不复位；核失联 → 无 HSEM 发布 → CM4 watchdog 槽 10s 超时 → 停喂 IWDG → 约 8s 后硬件复位。

---

## Q017 [S级] 为什么 Shared SRAM + HSEM

**核心问题：** 为什么用 Shared SRAM + HSEM，不用 OpenAMP/RPMsg？

**追问1：** 什么时候才值得引入 OpenAMP/RPMsg？

**答：** 当跨核数据量大、消息种类多、需要标准协议（virtio/RPMsg）、或要复用多核框架生态时；当前单传感器上报不需要。

**追问2：** Shared SRAM 方案最大的缺点是什么？

**答：** 需要自己管理一致性、通知、缓冲区布局和初始化竞态；扩展性（多传感器、多消息）不如标准框架。

**追问3：** 一致性具体怎么解决？

**答：** CM7 MPU 把 `0x38000000` 64KB 配成 non-cacheable，从根上规避 CM7 D-Cache 读到旧数据；再靠 HSEM1 临界区同步跨核可见性。

**追问4：** 如果数据量大了怎么扩展？

**答：** 要么在共享区规划多对象布局（每个对象独立对齐 + 独立 HSEM），要么引入带 ring/queue 的共享缓冲协议，最重就是上 OpenAMP/RPMsg。

**追问5：** 多个传感器共享一把 HSEM1 锁会有什么问题？

**答：** 锁粒度太粗，一个传感器的写会阻塞另一个的读/写，需要按对象拆分锁或改用无锁（seqlock）方案。

---

## Q018 [S级] 三个 HSEM 分工

**核心问题：** 三个 HSEM 分别做什么？为什么不只用一个？

**追问1：** HSEM2 是锁还是通知？

**答：** 主要是通知；数据互斥由 HSEM1 负责，两者语义不混。

**追问2：** 通知丢了会怎样？（对应 Debug 案例 D1）

**答：** 现象是 CM4 第一次发布后 CM7 能收到，后续不再唤醒 `SensorIpcTask`。根因是 `HAL_HSEM_IRQHandler()` 处理后清掉 IER，若回调不重新 `HAL_HSEM_ActivateNotification()`，通知只成功一次。

**追问3：** 修复在哪里做的？

**答：** 在 `HAL_HSEM_FreeCallback()` 里检测事件 HSEM2 后立即重新 arm，再 `vTaskNotifyGiveFromISR()` 唤醒任务（`sensor_shared.c:99-116`）。

**追问4：** HSEM 中断的优先级有什么要求？

**答：** 回调要调 `vTaskNotifyGiveFromISR`，所以 HSEM IRQ 优先级必须落在 FreeRTOS 可调用中断范围内（`configMAX_SYSCALL_INTERRUPT_PRIORITY=5`）。

**追问5：** 为什么"数据锁"和"事件通知"要分成 HSEM1 和 HSEM2 两把？

**答：** 因为互斥锁的持有/释放是成对的临界区，而事件通知是"发布即触发"的一次性信号；混用会带来"解锁即误触发"或"通知被当锁持有没有释放"的语义混乱。

---

## Q019 [S级] Shared SRAM 为什么 non-cacheable

**核心问题：** Shared SRAM 为什么配置为 non-cacheable？

**追问1：** non-cacheable 是不是就等于不需要内存屏障了？

**答：** 不是。non-cacheable 规避了 cache 一致性问题，但跨核可见性仍依赖 HSEM 临界区同步；不能因此推断"所有 DMA buffer 都天然一致"。

**追问2：** 它和 UART DMA buffer 的 cache 处理有什么不同？

**答：** 共享 IPC 区靠 MPU 设成 non-cacheable（不显式 invalidate）；UART DMA buffer 在可缓存的 AXI SRAM，DMA 写后必须显式 `SCB_InvalidateDCache_by_Addr`。

**追问3：** CM7 MPU 还配了哪些区域？

**答：** 除 `0x38000000` non-cacheable 外，`0x20000000`/`0x24000000`/`0x30000000`/`0xD0000000`（SDRAM）配 cacheable/bufferable，`0x80000000` 配 XN + non-cacheable。

**追问4：** 为什么 SDRAM（framebuffer）又配 cacheable？

**答：** 用途不同：framebuffer 服务显示访问，CPU 写走缓存更快；共享区服务跨核一致性，必须 non-cacheable。

**追问5：** 32 字节对齐能保证原子性吗？

**答：** 不能。对齐只降低跨 cache line 的一致性复杂度，float 温湿度字段仍可能撕裂，所以用 HSEM1 保护；要无锁读需要 seqlock（版本号）方案。

---

## Q023 [S级] 任务清单与优先级

**核心问题：** 这个工程有哪些任务，优先级和栈怎么配？

**追问1：** 为什么 watchdog 和 OTA 都是 Realtime 优先级？

**答：** watchdog 要及时判健康喂 IWDG，延迟判断会误复位；OTA 接收要及时搬运字节避免溢出。

**追问2：** Realtime 优先级是硬实时保证吗？

**答：** 不是。它只是 RTOS 优先级枚举映射，真正的硬实时还要 WCET、IRQ 延迟和调度分析。

**追问3：** 哪个任务栈最大？为什么？

**答：** `lvgl_task`，`4096*4` = 16KB。GUI 调用链更深、局部对象和绘制处理更复杂；最终应以栈水位验证。

**追问4：** CM7 heap 96K、CM4 24K，为什么差这么多？

**答：** CM7 承载 LVGL（对象/字体缓存/绘制缓冲）、OTA、Modbus、USB 等大量动态内存；CM4 只跑 DHT11，动态分配很少。

**追问5：** 任务栈和 FreeRTOS heap 是什么关系？

**答：** 任务栈在 `osThreadNew` 时从 heap 里分配，heap 是动态内存池上限，不等于所有任务栈总和。

---

## Q029 [S级] Flash 布局

**核心问题：** 整个 Flash 怎么划分？

**追问1：** 为什么 APP 不能从 `0x08000000` 启动？

**答：** 那是 Bootloader 独占区（128K），保证上电先进入启动决策逻辑；APP 从 `0x08020000` 起，与 Bootloader 分离。

**追问2：** Slot A 和 Slot B 有什么区别？

**答：** 都是 448K，Slot A 是当前执行槽，Slot B 是 OTA 下载/暂存槽。当前启动固定 Slot A，Slot B 下载完成后 Bootloader 拷贝到 Slot A。

**追问3：** Slot footer 里有什么？

**答：** 32 字节，含 magic（`0x534F4F54`）、镜像大小、CRC32、版本和保留字段。

**追问4：** metadata 为什么放 BKPSRAM 而不是普通 Flash？

**答：** BKPSRAM 避免普通 Flash 擦写影响启动状态记录，写入粒度小、擦写次数无限制。

**追问5：** Bank1 和 Bank2 怎么划分？

**答：** Bank1（`0x08000000`-`0x080FFFFF`）放 Bootloader + Slot A + Slot B，Bank2（`0x08100000`-`0x081FFFFF`）放 CM4 镜像（预留 1024K）。

---

## Q030 [S级] Slot A/B 是不是真正 rollback

**核心问题：** Slot A/B 是什么关系？是真正的 A/B rollback 吗？

**追问1：** 真正 rollback 需要哪些状态机？

**答：** Bootloader 能直接跳 Slot B 执行、启动失败计数 + 自动切回旧槽、`boot_meta_on_boot_failure()` 被主流程真正调用、双槽各自完整校验。

**追问2：** trial 字段存在，为什么不算 rollback？

**答：** 源码有 `boot_trial` 字段和 `boot_meta_on_boot_failure()`，但当前 Bootloader 主流程没有调用它，`boot_meta_commit_pending()` 把 active 固定为 Slot A。所以只能评"代码存在、调用链证据不足"。

**追问3：** 为什么启动选择固定 Slot A？

**答：** `boot_meta_select_boot_slot()`、`Boot_IsAppValid()`、`Boot_JumpToApp()` 都固定用 Slot A，这是当前实现的简化设计。

**追问4：** 掉电窗口风险是什么？

**答：** 逻辑 Slot A/B 共享 Bank1 Sector 4，代码用"备份另一侧 64KB → 整扇区擦除 → 恢复"处理，掉电可能损坏当前 APP 或两个逻辑槽（审计 R2）。

**追问5：** 共享扇区是怎么处理的？

**答：** 先备份另一侧 64KB，再整扇区擦除，然后恢复备份内容，最后写目标数据。这个过程中间的掉电就是风险窗口。

---

## Q031 [S级] B→A 拷贝流程

**核心问题：** Bootloader 怎么把 Slot B 拷到 Slot A？

**追问1：** `boot_slot_is_valid()` 会重算镜像 CRC 吗？

**答：** 不会。它主要验证向量和 footer 元数据（magic、size 非零、不超槽容量），整镜像 CRC32 在 OTA VERIFY 阶段做。

**追问2：** Slot B 有效性的核心条件是什么？

**答：** footer magic 正确、image size 非零、image size 不超过槽容量减去 footer。

**追问3：** B→A 拷贝时掉电怎么办？

**答：** 存在掉电窗口（共享 Sector 4 问题），当前没有掉电原子提交机制，这是已知风险。

**追问4：** 拷贝后为什么还从 Slot A 启动？

**答：** 因为执行入口固定为 Slot A，`boot_meta_commit_pending()` 把 active slot 固定为 A。

**追问5：** `commit_pending` 做了什么？

**答：** 把 pending 状态收敛，active slot 固定为 A；它不实现"切到 Slot B 运行"的语义。

---

## Q036 [S级] OTA 状态机与命令序列

**核心问题：** 一次完整 OTA 流程？

**追问1：** END_TRANSFER 进入 READY_VERIFY 的条件是什么？

**答：** 只有 `s_bytes_written == s_image_size` 才进入 READY_VERIFY，防止写入不完整就进入校验。

**追问2：** DATA 为什么逐包 ACK？

**答：** 停等式/单窗口传输（window=1），发一包 ACK 一包，保证可靠性，代价是吞吐受 RTT 限制。

**追问3：** RESET 命令为什么 ACK 后延时 50ms？

**答：** 给阻塞式 TX 时间把 ACK 完整发出，否则立即复位主机可能收不到应答。

**追问4：** OTA 写到哪个槽？

**答：** 写 inactive 槽（正常场景 Slot B），BEGIN_SESSION 时选择当前运行槽的 inactive 槽。

**追问5：** CRC 校验在哪一层做？

**答：** 传输帧用 CRC16（帧级），完整镜像用 CRC32（VERIFY 阶段对目标槽 Flash 映射地址算）。

---

## Q037 [S级] 为什么改 USART6 UART

**核心问题：** OTA 为什么改到 USART6 UART？

**追问1：** 切换 transport 要不要改协议？

**答：** 不用。`bsp_ota_protocol.c` 是字节流解析器，不关心底层 TCP/UART；Qt Host 也复用 `tryParseFrame`，帧格式不变。

**追问2：** USART6 为什么比 USART1/USART3 合适？

**答：** USART1 已被 `printf`/日志/Flash trace 占用（协议二进制与日志会互相污染）；USART3 的板级引出（PC10/PC11）不如 USART6 清晰；USART6 的 PG9(RX)/PG14(TX) 引出明确。

**追问3：** USART6 和 ESP8266 会冲突吗？

**答：** 会。软件上 USART6 曾被 ESP8266 AT/TCP 使用，UART OTA 模式下需停用 ESP8266 软件链路，两者不能同时用。

**追问4：** 现在收发都是 DMA 吗？

**答：** 不是。RX 是 DMA ReceiveToIdle + ring + notification，TX 仍是阻塞 `HAL_UART_Transmit`。

**追问5：** `bsp_ota_transport_ready()` 为什么固定返回 1？

**答：** UART 没有 TCP connected 状态，串口打开后不需要网络连接握手，所以 transport 层把 ready 视为可用。

---

## Q040 [S级] SOIP 帧格式

**核心问题：** SOIP 帧格式是什么？

**追问1：** CRC16 是否包含 SOF？

**答：** 不包含，覆盖范围是 version 到 payload。

**追问2：** DATA 帧最大多大？

**答：** 8 header + 4 token + 4 offset + 256 data + 2 CRC = 274 字节。

**追问3：** payload 上限 1024 和 chunk 256 是什么关系？

**答：** 协议解析器上限是 1024 payload，但当前业务 chunk 和 Host 默认 chunk 都是 256，实际 DATA 帧 274B。

**追问4：** 为什么 chunk 选 256 而不是 1024？

**答：** 274B 的 DATA 帧明显小于 768B 接收 buffer，能放入缓冲；但源码没有完整性能 benchmark，不能宣称 256 是性能最优实验得出。

**追问5：** SOF 丢了，解析器怎么重新同步？

**答：** 协议解析器允许 SOF 前噪声、半帧、粘包和 CRC 错误后重新同步（字节流解析器设计）。

---

## Q039 [A级] CRC16 vs CRC32

**核心问题：** CRC16 和 CRC32 分别解决什么？

**追问1：** CRC32 失败后设备做什么？

**答：** VERIFY 失败不写 footer、不置 pending，镜像不会被 Bootloader 采用，需主机重试。

**追问2：** 为什么不能只用 CRC32？

**答：** CRC32 是整镜像校验，发现错误时已传完整张图，代价大；CRC16 帧级校验能及时止损、单帧重传，两者粒度不同。

**追问3：** CRC16 和 CRC32 各止损什么？

**答：** CRC16 止损"单帧误码"（发现就重传该帧），CRC32 止损"整镜像错误"（发现整包报废）。

**追问4：** CRC 能防篡改吗？

**答：** 不能。CRC 只做误码检测，不是安全机制；防篡改需要签名/哈希（当前未实现）。

**追问5：** 还要加什么才安全？

**答：** 数字签名/公钥验签、防回滚版本比较、加密和密钥管理（当前 OTA 均未实现，属于企业级增强项）。

---

## Q046 [S级] 115200 为什么丢包

**核心问题：** 115200 下原来的 UART OTA 为什么丢包？

**追问1：** 只提高波特率能解决吗？

**答：** 不能。核心是接收搬运和协议处理要解耦 + DMA re-arm + cache 一致 + 足够 ring 容量；提高波特率只是把阈值抬高，根本问题还在。

**追问2：** 根本原因是什么？

**答：** 逐字节 `HAL_UART_Receive` + `osDelay` 让 CPU 在接收间隙无法及时取走数据，协议解析任务的调度延迟被直接暴露给 UART 接收路径。

**追问3：** 接收和解析为什么要解耦？

**答：** OTA 帧是连续字节流，不能假设任务运行时恰好只到一个字节；把"实时搬运字节"（DMA/ring）和"协议处理"（OTA 任务）分离，才能各按各的节奏跑。

**追问4：** 提升到 921600 还要重新审计什么？

**答：** 每字节约 10.8µs，要重新评估 DMA buffer/ring 容量、CPU 搬运速度、Flash 写速度是否跟得上、以及 Host 端吞吐。

**追问5：** 115200 下每个字节的间隔大约多少？

**答：** 约 87µs（8N1 每字符 10 bit / 115200），CPU 若在任务调度间隙处理不过来就会溢出。

---

## Q047 [S级] 现在 UART 接收设计

**核心问题：** 现在 UART 接收是怎么设计的？

**追问1：** DMA 回调里为什么要重新 arm？

**答：** `ReceiveToIdle` 触发一次回调后不会自动继续接收，必须重新调用以接收下一段数据。

**追问2：** ring buffer 满了怎么办？

**答：** 满时丢弃剩余字节，依赖帧 CRC 失败 + Host 重试恢复。

**追问3：** TX 是 DMA 吗？

**答：** 不是，TX 是阻塞 `HAL_UART_Transmit`；准确表述是"RX DMA、TX 阻塞"。

**追问4：** DMA buffer 为什么 768 字节？

**答：** `BSP_OTA_RX_BUF_SIZE`，需大于当前最大 DATA 帧 274B，能容纳整帧甚至多帧。

**追问5：** ring 为什么 2048 字节？

**答：** 作为 DMA 搬运和 OTA 任务解析之间的缓冲，吸收 OTA 任务短时间未运行的输入抖动。

---

## Q049 [A级] D-Cache 对 UART DMA 的影响

**核心问题：** D-Cache 对 UART DMA 有什么影响？

**追问1：** invalidate 前要注意什么？

**答：** cache line 对齐（DMA buffer 32 字节对齐）；invalidate 只对"DMA 写、CPU 读"场景，若 CPU 写过该区要先 clean 再 invalidate。

**追问2：** 它和共享 IPC 的 cache 处理有何不同？

**答：** 共享 IPC 区靠 MPU non-cacheable（不显式 invalidate）；UART DMA buffer 在可缓存 AXI SRAM，DMA 写后要显式 invalidate。

**追问3：** clean 和 invalidate 的区别是什么？

**答：** clean = 把 cache 里脏数据写回内存（CPU 写→DMA 读场景）；invalidate = 丢弃 cache 里旧数据强制重读内存（DMA 写→CPU 读场景）。

**追问4：** cache line 对齐为什么重要？

**答：** clean/invalidate 以 cache line（通常 32B）为粒度，不对齐会波及相邻数据，造成错误或不一致。

**追问5：** 不处理 cache 会怎样？

**答：** CPU 读到 cache 里的旧数据，导致协议解析错乱、CRC 失败、丢帧。

---

## Q053 [S级] framebuffer 多大，为什么放 SDRAM

**核心问题：** framebuffer 多大？为什么放 SDRAM？

**追问1：** 双 framebuffer 要多少？

**答：** 约 1.5MiB（两个 750KiB）。

**追问2：** LVGL 为什么用 partial buffer？

**答：** 全帧 buffer 要 750KiB 且需双缓冲更占内存；partial 用小 buffer（800×400）分块刷新，省内存，代价可能有轻微撕裂（除非 vsync）。

**追问3：** flush 回调做什么？

**答：** 把脏矩形区域写入 LCD/LTDC，完成后调 `lv_display_flush_ready()`。

**追问4：** SDRAM 配 cacheable，会不会有显示一致性问题？

**答：** 会。LTDC/DMA 读 framebuffer 时，CPU 写的 cache 数据要 clean 才可见，这是需要结合实际刷新策略验证的点。

**追问5：** 为什么内部 SRAM 放不下 framebuffer？

**答：** AXI SRAM 512K 还要装应用 .bss（实测 508KB，已经紧张），750KiB 的 framebuffer 放不下。

---

## Q060 [S级] 两层 watchdog

**核心问题：** 这个工程的 watchdog 是几层结构？

**追问1：** 为什么需要两层？

**答：** 软件层能区分"哪个任务死了"，但软件自身可能也挂；硬件 IWDG 在软件彻底停摆时兜底复位。

**追问2：** heartbeat counter 为什么比 boolean 强？

**答：** boolean 只能区分"曾经运行过"，counter 能区分"任务持续有进展"。

**追问3：** 停喂后多久复位？

**答：** 软件判断不健康后停止刷新 IWDG，约 8s（IWDG 超时）后硬件复位。

**追问4：** 复位原因怎么读？

**答：** 读 `RCC->RSR`，区分 IWDG1（`RCC_RSR_IWDG1RSTF`）、WWDG、POR、NRST、CM7/CM4 软件复位。

**追问5：** 如果软件本身挂了，IWDG 还能救吗？

**答：** 能。IWDG 是独立硬件，一旦没人喂就按 LSI 计时复位，不依赖软件存活。

---

## Q061 [S级] IWDG 参数与超时

**核心问题：** IWDG 的参数和超时是多少？

**追问1：** LSI 不精确会怎样影响超时？

**答：** LSI 频率偏差线性影响超时（如 ±10% 就是 ±0.8s），8s 只是设计值，量产要标定或接受误差。

**追问2：** 8s 是怎么算的？

**答：** `timeout ≈ prescaler × reload / LSI = 128 × 2000 / 32000 ≈ 8s`。

**追问3：** 喂狗周期和超时的裕量是多少？

**答：** 喂狗 2000ms，超时约 8000ms，约 4 倍裕量。

**追问4：** 启动宽限期为什么 45000ms？

**答：** 系统启动时各任务还没进入稳定周期（SDRAM/LCD/OTA/CM4 唤醒都在初始化），宽限期内不判 stale，避免误判。

**追问5：** 怎么区分复位原因？

**答：** 读 `RCC->RSR` 各标志位，区分电源、外部复位、软件复位、看门狗复位。

---

## Q062 [S级] 各槽位超时

**核心问题：** 各监控槽位的超时是多少？

**追问1：** CM4 槽为什么 10000ms 而不是 3000ms？

**答：** DHT11 周期 2000ms，10000ms 给 5 个采样周期容错，允许偶发延迟但超过 10s 无发布判死核，避免误报。

**追问2：** CM4 心跳是谁 kick 的？

**答：** CM7 的 `SensorIpcTask` 收到 HSEM 发布通知时 kick `BSP_WDG_CM4` 槽。

**追问3：** 启动时为什么预置一次 CM4 槽？

**答：** 防止 CM4 从未启动时被永久忽略——先 kick 一次，让 watchdog 在 CM4 从未发布时也能在超时后判定死核。

**追问4：** W5500 槽为什么 60000ms？

**答：** 网络操作本身慢（TCP 超时/重连），给 60s 容错，避免网络抖动误复位。

**追问5：** 槽位超时值是怎么选的？

**答：** 基于各任务的周期乘一个容错倍数（如 CM4 是 5×2000ms），源码没有完整 deadline 表，是按经验取整数值。

---

## Q064 [S级] boolean → 引用计数

**核心问题：** OTA 期间 watchdog 为什么误判？boolean 有什么缺陷？

**追问1：** boolean 具体怎么出错？

**答：** 多个长操作调用者嵌套时，一个子系统退出会清零另一个仍在进行的长操作标志，导致豁免被错误清除（或反过来标志卡住）。

**追问2：** 引用计数会溢出吗？

**答：** 理论上需要防护（如饱和处理）；当前进入/退出成对调用，实际不会无界增长，但可作为改进点。

**追问3：** 长操作期间 watchdog 监控什么？

**答：** 当前长操作期间只监控 W5500 槽（`bsp_watchdog.c:200-204`）。

**追问4：** 这个 bug 是怎么发现的？

**答：** 早期 OTA 出现约 47s 不复位，排查后定位到长操作标志被 W5500 子系统错误覆盖，导致 watchdog 长时间停在长操作模式不停喂 IWDG（对应 Debug D5）。

**追问5：** 如果 OTA 任务真的死锁，怎么兜底？

**答：** 引用计数只保证"豁免状态正确"，真死锁时长操作不退出、计数不归零，最终要靠"长操作期间的有限监控（如 W5500 槽）+ 更上层超时"兜底，这是一个需要持续复盘的点。

---

## Q067 [A级] CM4 迁移后 heartbeat 失效

**核心问题：** CM4 迁移后原来的监控为什么失效？

**追问1：** 启动时为什么预置一次 CM4 槽？

**答：** 防止 CM4 从未启动时被永久忽略，让 watchdog 即使收不到发布也能在超时后判死核。

**追问2：** HSEM 中断丢失，软件 watchdog 能区分"CM4 死机"和"通信故障"吗？

**答：** 不能。两者表现都是"无 HSEM 发布"，当前只按"有没有收到通知"判断。

**追问3：** CM4 读失败但存活时，heartbeat 会更新吗？

**答：** 会。CM4 失败也照常发布（valid=0），CM7 仍收到 HSEM 通知并 kick CM4 槽——所以心跳反映的是"CM4 是否在发布"，不是"读数是否成功"。

**追问4：** 跨核 heartbeat 的最大局限是什么？

**答：** 无法区分 HSEM 通信链路故障和 CM4 真死机；也没有对 CM4 内部任务做细粒度监控（CM4 不直接调 CM7 watchdog API）。

**追问5：** 怎么增强？

**答：** 在心跳里加"发布序号/时间戳"，CM7 比对时间戳判断"CM4 在跑但 HSEM 可能丢"；或 CM4 也挂自己的 IWDG 做本地兜底。

---

## Q071 [S级] 为什么不用 OpenAMP

**核心问题：** 为什么不用 OpenAMP/RPMsg？Shared SRAM 方案边界在哪？

**追问1：** Shared SRAM 方案最大的缺点？

**答：** 需要自己管理一致性、通知、缓冲区布局和初始化竞态，扩展性不如标准框架。

**追问2：** 什么时候值得引入 OpenAMP/RPMsg？

**答：** 数据量大、消息种类多、需要标准协议（virtio/RPMsg）、或要复用多核框架生态时。

**追问3：** 初始化竞态具体是什么？

**答：** `sensor_ipc_init()` 双核都调 `sensor_shared_reset()`，若两核启动时序交叉，后启动的一侧可能清零先前已发布的数据（审计风险）。

**追问4：** 扩展多个传感器怎么规划？

**答：** 在共享区规划多对象布局（每个对象独立 32 字节对齐 + 独立 HSEM），或引入带 ring/queue 的共享缓冲协议。

**追问5：** 无锁读取怎么做？

**答：** 用 seqlock（版本号）：写前递增奇数序号、写后递增偶数序号，读者读前后比对序号判断是否有并发写。

---

> 完。以上 30 道核心题 × 5 层追问均来自现有工程实现与审计文档；如需针对某一题继续加深到第 6、7 层，或按"芯片原厂 vs 互联网大厂"分风格再组织，可继续扩展。
