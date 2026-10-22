# 面试核心 30 题（高频速记版）V2

> 从《STM32H757 工业控制终端 面试题库 V1.0》的 76 道题中，筛选 30 道最高频核心题（S 级 27 道 + 补 3 道关键 A 级）。
> 每题给出「问题 / 30 秒回答 / 2 分钟回答 / 可能追问链」四段。
> 完整追问展开见《docs/interview_followup_tree.md》。
> 事实原则：所有回答均来自当前工程真实实现，未落地项已标注。
>
> **模块划分**：第 1-30 题为核心题（H1-H13 领域），第 31-37 题为 **H14 工业通信协议（Modbus）** 专项模块（Q077-Q083）。

---

## 1. Q001 [S级] 项目介绍

**问题：** 介绍一下这个项目，用的是什么 MCU？

**30秒回答：**
STM32H757 双核工业控制终端。CM7 做应用主核——LCD/LVGL 图形界面、FreeRTOS、USART6 OTA 升级；CM4 做精简传感核——只跑 DHT11 采集，通过 Shared SRAM + HSEM 把温湿度上报给 CM7。

**2分钟回答：**
构建宏是 `STM32H757xx`，工程拆成 CM7/CM4 两个独立目标，分别用 Cortex-M7（`fpv5-d16`）和 Cortex-M4（`fpv4-sp-d16`）硬浮点编译，产出 `GPIO_CM7.elf` 和 `GPIO_CM4.elf`。CM7 链接 LVGL、LTDC、FMC/SDRAM、SDMMC/FatFs、USART6 OTA、USB 和应用任务；CM4 是精简工程，只有 FreeRTOS + DHT11 + 共享 IPC + 必要 HAL。这是从构建清单能确认的事实。

**可能追问链：**
怎么证明是双核 → 为什么双核分工 → 双核怎么通信 → 为什么不用 OpenAMP → 通信的边界/缺点。

---

## 2. Q002 [S级] 为什么用 H757 双核

**问题：** 为什么用 H757 双核，CM7/CM4 各承担什么？

**30秒回答：**
三点理由：CM7 负载已经重（GUI/OTA/多外设）；DHT11 单总线读取要关中断 + 微秒忙等，放 CM7 会干扰实时任务；CM4 可以只留传感器 + RTOS，上报结果，降低耦合。

**2分钟回答：**
DHT11 读 40 bit 数据时关闭中断并用 DWT 微秒级忙等（`dht11.c:120-154`），和图形刷新、OTA 放同核会引入调度干扰。迁移到 CM4 后，把"时序敏感的采集"和"负载重的应用"隔离。CM4 工程刻意最小化，只编译 FreeRTOS、DHT11、共享 IPC 和必要 HAL。

**可能追问链：**
DHT11 读 40bit 具体怎么读 → 关中断多久 → 如果 GUI 卡顿 CM4 能分担吗 → 单核能不能做 → 迁移后监控怎么保证。

---

## 3. Q003 [S级] 软件分层架构

**问题：** 这个工程的软件分层怎么组织？

**30秒回答：**
五层：应用层（LVGL/OTA/DHT11）、系统层（FreeRTOS + CMSIS-RTOS V2）、BSP 层（LCD/SDRAM/Touch/ADC/UART OTA/watchdog/MPU/IPC）、驱动层（HAL/LL/CMSIS/DMA）、硬件层（H757 双核 + SDRAM + LCD）。

**2分钟回答：**
活动构建入口四个：CM7 APP、CM4 APP、CM7 Bootloader、Qt Host。Bootloader 是独立工程，有独立 HAL/CMSIS 副本和链接脚本。CM7 是应用主核，CM4 是精简传感核，证据是两边 Makefile 的 `C_SOURCES` 差异。

**可能追问链：**
Bootloader 和应用是不是一个工程 → BSP 层具体管什么 → 驱动层用 HAL 还是 LL → 分层的好处 → 跨层调用怎么约束。

---

## 4. Q005 [S级] 项目最大亮点

**问题：** 你觉得这个项目最大的亮点是什么？

**30秒回答：**
六个亮点：双核清晰分工；用官方 Shared SRAM + HSEM 做轻量 IPC；修掉 HSEM 一次性触发问题；USART6 DMA + ring buffer 解决串口丢包；OTA 有 Slot B 下载 + CRC32 + footer + pending 流程；watchdog 三层（本核 + 跨核 + IWDG）。

**2分钟回答：**
关键是没引入 OpenAMP/RPMsg 等重框架，用最小依赖完成跨核通信；并针对真实 Debug 案例做了源码级修复（HSEM re-arm、DMA 接收重构、长操作引用计数、跨核心跳）。要诚实说明：这些已完成构建，但只有 IWDG 复位、DHT11 离线状态等少数项做过实机验证，完整 OTA 压力/掉电恢复/跨核故障注入还没板级记录。

**可能追问链：**
哪些是硬件实测过的 → 完整 OTA 验证过吗 → 掉电恢复验证过吗 → 亮点里哪些是"代码存在"哪些是"已验证" → 你怎么表述事实边界。

---

## 5. Q007 [S级] 完整启动流程

**问题：** 从上电到应用跑起来，完整启动流程？

**30秒回答：**
上电进 CM7 Bootloader → 初始化时钟/USART1/metadata → 检查 Slot，若 pending Slot B 有效则 B→A 拷贝 → 跳转 Slot A APP → APP 配时钟 → EnableBootCore 唤醒 CM4 → 等 D2CKRDY 清 → 释放 HSEM0 → 初始化外设/IWDG/OTA → 启动 FreeRTOS；CM4 从 STOP 返回跑 DHT11。

**2分钟回答：**
Bootloader 跳转前 hold CM4、关中断/SysTick、清 NVIC、写 jump magic、设 MSP（APP 向量第 0 word）、bx 到 Reset_Handler（第 1 word）。VTOR 由 APP 的 `SystemInit()` 设置：CM7 `0x08020000`、CM4 `0x08100000`。CM7 唤醒 CM4 前先等 `RCC_FLAG_D2CKRDY` 清除，再释放 HSEM0，避免 CM4 没进 STOP 就丢唤醒事件。

**可能追问链：**
Bootloader 怎么跳转 APP → VTOR 谁设 → 为什么关 SysTick → CM4 怎么被唤醒 → 为什么等 D2CKRDY。

---

## 6. Q008 [S级] CM7 唤醒 CM4

**问题：** CM7 是怎么把 CM4 唤醒的？

**30秒回答：**
按 BCM4=0 路径：CM7 配完时钟后 `HAL_RCCEx_EnableBootCore(RCC_BOOT_C2)` 释放 CM4；CM4 启动后立即进 D2 STOP 等 HSEM0；CM7 先等 `RCC_FLAG_D2CKRDY` 清除，再释放 HSEM0。

**2分钟回答：**
释放 HSEM0 前必须确认 CM4 已进入 STOP 等待态，否则通知发出时 CM4 还没在等，事件就丢了。HSEM0 专门用于启动握手，和数据 HSEM1、事件 HSEM2 分开。

**可能追问链：**
为什么先等 D2CKRDY → HSEM0 的语义 → BCM4 option byte 是什么 → 如果 BCM4=1 会怎样 → CM4 被唤醒后做什么。

---

## 7. Q012 [S级] DHT11 为什么放 CM4

**问题：** DHT11 为什么放到 CM4，而不是留在 CM7？

**30秒回答：**
DHT11 单总线读取要关中断 + 微秒忙等，和图形/OTA 同核会干扰；放到 CM4 隔离时序敏感的采集，CM4 只留传感器 + RTOS，上报结果。

**2分钟回答：**
读取 40 bit 时关闭中断并用 DWT 周期计数做微秒等待（`dht11.c:120-169`）。CM7 不再直接访问 DHT11 GPIO——CM7 Makefile 不含 `bsp_dht11.mk`，活动路径是 `app/CM4/BSP/Sensor/dht11.c`；`app/BSP/DHT11/dht11.c` 是历史残留。

**可能追问链：**
DHT11 时序怎么实现 → 关中断多久 → 如何证明 CM7 不再访问 GPIO → 单核能不能做 → DHT11 适不适合高实时。

---

## 8. Q013 [S级] CM4→CM7 数据链路

**问题：** CM4 采集的数据怎么到 CM7？画出链路。

**30秒回答：**
CM4 DHT11 任务 → `sensor_shared_publish()`（HSEM1 保护写）→ 释放 HSEM2 → CM7 HSEM IRQ → 回调 re-arm + `vTaskNotifyGiveFromISR` 唤醒 SensorIpcTask → `sensor_ipc_read()`（HSEM1 保护读）→ 应用缓存 → LVGL 刷新。

**2分钟回答：**
CM7 任务阻塞在 task notification 上，而不是轮询。HSEM2 是事件通知，HSEM1 才是数据锁。共享结构 `sensor_shared_data_t`（magic/valid/temperature/humidity/timestamp），放 `0x38000000` 的 `.shared_data`，32 字节对齐。

**可能追问链：**
为什么不轮询 → task notification vs queue → HSEM2 是锁还是通知 → 共享结构字段含义 → 一致性怎么保证。

---

## 9. Q015 [S级] DHT11 离线状态设计

**问题：** DHT11 的"离线"状态怎么设计？（非致命，不复位）

**30秒回答：**
CM7 把 `sample.valid` 映射成 `dht11_online` 标志，三处消费：LCD 右上角 ONLINE/OFFLINE、Modbus 寄存器 3、仅展示不复位。CM4 失联走另一条路（watchdog CM4 槽超时 → IWDG 复位）。

**2分钟回答：**
CM4 读失败也照常发布，`valid=0` 但 timestamp 仍更新，这样 CM7 能区分"传感器读失败"（valid=0，显示离线）和"CM4 死了"（无发布，heartbeat 超时）。`dht11_online` 用互斥锁保护。

**可能追问链：**
读失败为什么也发 → valid 和发布频率各承担什么 → 为什么读失败不复位 → 三处消费分别是什么 → 两个故障信号怎么区分。

---

## 10. Q017 [S级] 为什么 Shared SRAM + HSEM

**问题：** 为什么用 Shared SRAM + HSEM，不用 OpenAMP/RPMsg？

**30秒回答：**
第一阶段只是简单传感器数据通道，Shared SRAM + HSEM 足够、轻量、无额外依赖；OpenAMP/RPMsg 会引入更重框架和更大调试面。

**2分钟回答：**
Shared SRAM（`0x38000000` 64KB）放数据，HSEM 管互斥和事件通知，三者角色清晰。扩展边界：数据量大、消息种类多、需要标准协议或跨平台生态时才值得引入 OpenAMP。

**可能追问链：**
什么时候值得 OpenAMP → Shared SRAM 方案最大缺点 → 一致性怎么解决 → 数据量大了怎么扩展 → 多个传感器怎么规划。

---

## 11. Q018 [S级] 三个 HSEM 分工

**问题：** 三个 HSEM 分别做什么？为什么不只用一个？

**30秒回答：**
HSEM0 启动唤醒 CM4、HSEM1 保护共享数据读写、HSEM2 CM4→CM7 事件通知。分开避免一个 semaphore 同时承担三种语义。

**2分钟回答：**
HSEM2 是事件通知，不等同于数据锁；数据锁仍由 HSEM1 保护。这样把"启动控制/数据互斥/事件通知"三种语义解耦。

**可能追问链：**
HSEM2 是锁还是通知 → 通知丢了怎么办 → 为什么数据锁和通知分开 → HSEM 中断优先级要求 → re-arm 在哪做。

---

## 12. Q019 [S级] Shared SRAM 为什么 non-cacheable

**问题：** Shared SRAM 为什么配置为 non-cacheable？

**30秒回答：**
CM7 有 D-Cache，共享区若可缓存会读到 CM4 写入前的旧数据；MPU 把 `0x38000000` 64KB 配成 non-cacheable，从根上降低跨核一致性风险。

**2分钟回答：**
但这不等于"所有 DMA buffer 都天然一致"——UART DMA buffer 在可缓存的 AXI SRAM，仍需显式 invalidate。non-cacheable 规避 cache 一致性，但跨核可见性仍靠 HSEM 临界区同步。

**可能追问链：**
这样还需要内存屏障吗 → 和 UART DMA buffer 的 cache 处理有何不同 → MPU 还配了哪些区域 → 为什么 SDRAM 又配 cacheable → 对齐保证原子性吗。

---

## 13. Q023 [S级] 任务清单与优先级

**问题：** 这个工程有哪些任务，优先级和栈怎么配？

**30秒回答：**
lvgl_task（AboveNormal/16K）、defaultTask（Normal/2K）、sensor_ipc（AboveNormal/2K）、ota（Realtime/8K）、watchdog（Realtime/1.5K）、adc（Normal/2K）、dht11（CM4 Normal/2K）。tick 1000Hz，CM7 heap 96K、CM4 24K。

**2分钟回答：**
watchdog 和 OTA 是 Realtime——watchdog 要及时判健康喂 IWDG，OTA 要及时搬运字节。但"Realtime"只是 RTOS 优先级枚举，不等于硬实时，还需要 WCET/IRQ 延迟/调度分析。`osThreadNew` 的 stack 参数按 `配置值*4`（字→字节）传入。

**可能追问链：**
为什么 watchdog/OTA Realtime → Realtime 是硬实时吗 → 哪个任务栈最大为什么 → heap 96K vs 24K → 栈和 heap 关系。

---

## 14. Q029 [S级] Flash 布局

**问题：** 整个 Flash 怎么划分？

**30秒回答：**
Bootloader `0x08000000` 128K、CM7 Slot A `0x08020000` 448K、Slot B `0x08090000` 448K、CM4 `0x08100000` 1024K（Bank2）、metadata `0x38800000`（BKPSRAM）。

**2分钟回答：**
Bank1（`0x08000000`-`0x080FFFFF`）放 Bootloader + Slot A + Slot B，Bank2（`0x08100000`-`0x081FFFFF`）放 CM4。Slot footer 32 字节，magic `0x534F4F54`。APP 不能从 `0x08000000` 启动，那是 Bootloader 独占区。

**可能追问链：**
为什么 APP 不占 0x08000000 → Slot A/B 区别 → footer 里有什么 → metadata 为什么放 BKPSRAM → Bank1/Bank2 怎么分。

---

## 15. Q030 [S级] Slot A/B 是不是真正 rollback

**问题：** Slot A/B 是什么关系？是真正的 A/B rollback 吗？

**30秒回答：**
不是。Slot B 是下载/暂存槽，Bootloader 检测 pending 有效后把它**拷贝到 Slot A**，固定从 Slot A 启动。`boot_meta_select_boot_slot()` 固定返回 Slot A。

**2分钟回答：**
所以准确说法是"Slot B 下载 + Bootloader B→A 拷贝 + Slot A 执行"，不是"两镜像直接切换"。真正 rollback 需要：Bootloader 能直接跳 Slot B、启动失败计数自动切回、`boot_meta_on_boot_failure()` 被主流程真正调用、双槽各自完整校验。

**可能追问链：**
真正 rollback 需要什么 → trial 字段为什么不算 → 为什么固定 Slot A → 掉电窗口风险 → 共享扇区怎么处理。

---

## 16. Q031 [S级] B→A 拷贝流程

**问题：** Bootloader 怎么把 Slot B 拷到 Slot A？

**30秒回答：**
Bootloader 先读 A/B 有效性；当 metadata 标记 pending Slot B 且 B footer 有效（magic 正确、size 非零、不超容量）时执行 B→A 拷贝，然后仍从 Slot A 跳转。

**2分钟回答：**
`boot_slot_is_valid()` 主要验证向量和 footer 元数据，不重算整镜像 CRC（CRC32 在 OTA VERIFY 阶段做）。拷贝后 `boot_meta_commit_pending()` 把 active 固定为 Slot A。

**可能追问链：**
boot_slot_is_valid 会重算 CRC 吗 → footer 有效性条件 → 掉电中断怎么办 → 为什么拷贝后还从 A 启动 → commit_pending 做什么。

---

## 17. Q036 [S级] OTA 状态机与命令序列

**问题：** 一次完整 OTA 流程（状态机 + 命令序列）？

**30秒回答：**
状态机 `IDLE→SESSION→ERASING→RECEIVING→READY_VERIFY→IDLE`。命令：GET_INFO→BEGIN_SESSION→ERASE→START_TRANSFER→DATA*→END_TRANSFER→VERIFY→RESET。

**2分钟回答：**
BEGIN_SESSION 选 inactive 槽（Slot B）+ 生成 token；ERASE 擦槽；DATA 按 offset 写 chunk（最大 256B）逐包 ACK；END_TRANSFER 校验 `s_bytes_written == image_size`；VERIFY 做 CRC32 + 写 footer + set pending；RESET ACK 后延时 50ms 再 `NVIC_SystemReset()`。

**可能追问链：**
END_TRANSFER 进 READY_VERIFY 条件 → DATA 为什么逐包 ACK → RESET 为什么延时 50ms → 写哪个槽 → CRC 在哪一层做。

---

## 18. Q037 [S级] 为什么改 USART6 UART

**问题：** OTA 为什么改到 USART6 UART？原 transport 是什么？

**30秒回答：**
原 transport 固定映射 W5500 TCP；后来按板级接线切到 USART6 UART（115200 8N1），复用 SOIP 协议，只替换 transport 后端。USART1 被日志占用，USART3 引出不清，USART6 的 PG9/PG14 引出明确。

**2分钟回答：**
`bsp_ota_protocol.c` 是字节流解析器，不关心底层 TCP/UART；Qt Host 也复用 `tryParseFrame`，帧格式不变。所以切换 transport 不用改协议、不用改 manager、不用改 Flash/Bootloader。

**可能追问链：**
切换要不要改协议 → USART6 为什么比 USART1/3 合适 → USART6 和 ESP8266 冲突吗 → 收发都 DMA 吗 → transport ready 为什么直接返回 1。

---

## 19. Q040 [S级] SOIP 帧格式

**问题：** SOIP 帧格式是什么？

**30秒回答：**
`SOF(2) AA55 | Version(1) 0x02 | Command(1) | Seq(2 LE) | PayloadLen(2 LE) | Payload(N) | CRC16(2 LE)`。header 8B，payload 上限 1024B，当前 chunk 256B。

**2分钟回答：**
DATA 帧 = 8 header + 4 token + 4 offset + 256 data + 2 CRC = 274B。CRC16 覆盖 version 到 payload（不含 SOF），初值 `0xFFFF`、多项式 `0x1021`。

**可能追问链：**
CRC16 含不含 SOF → DATA 帧最大多大 → payload 上限和 chunk 关系 → 为什么 chunk 256 → SOF 丢了怎么重新同步。

---

## 20. Q039 [A级] CRC16 vs CRC32

**问题：** CRC16 和 CRC32 分别解决什么？为什么不用一种？

**30秒回答：**
CRC16 保护每帧传输（发现单帧误码并重传），CRC32 保护完整镜像（确认整个下载正确）。粒度不同：帧级止损、镜像级保正确。

**2分钟回答：**
CRC16 初值 `0xFFFF`、多项式 `0x1021`、覆盖 version 到 payload；CRC32 用 reflected 多项式 `0xEDB88320`、初值 `0xFFFFFFFF`、最终取反。VERIFY 阶段对目标槽 Flash 映射地址算 CRC32，失败不写 footer/不置 pending，需主机重试。

**可能追问链：**
CRC32 失败做什么 → 为什么不只 CRC32 → 帧级 CRC 和镜像级 CRC 各自止损什么 → CRC 能防篡改吗 → 还要加什么才安全。

---

## 21. Q046 [S级] 115200 为什么丢包

**问题：** 115200 下原来的 UART OTA 为什么丢包？

**30秒回答：**
原设计逐字节 `HAL_UART_Receive` + `osDelay`，CPU 在接收间隙无法及时取走数据，连续字节流下溢出丢字节。本质是协议解析任务的调度延迟被直接暴露给 UART 接收路径。

**2分钟回答：**
115200 约每字节约 87µs，不能假设任务每次运行时恰好只到一个字节。只提高波特率不能解决——核心是接收搬运和协议处理要解耦 + DMA re-arm + cache 一致 + 足够 ring 容量。

**可能追问链：**
只提高波特率能解决吗 → 根本原因是什么 → 接收和解析为什么要解耦 → 提升到 921600 还要审计什么 → 原设计为什么写出来就有问题。

---

## 22. Q047 [S级] 现在 UART 接收设计

**问题：** 现在 UART 接收是怎么设计的？

**30秒回答：**
`USART6 FIFO → DMA ReceiveToIdle → 768B DMA buffer → D-Cache invalidate → 2048B ring → vTaskNotifyGiveFromISR 唤醒 OTA 任务 → 解析`。把字节搬运和协议处理分离。

**2分钟回答：**
DMA 回调先失效 D-Cache，再把数据推入 ring，最后重新 arm DMA（`ReceiveToIdle` 触发一次后不会自动继续）。ring 满时丢剩余字节，靠 CRC 失败 + Host 重试恢复。TX 仍是阻塞 `HAL_UART_Transmit`。

**可能追问链：**
回调为什么重新 arm → ring 满怎么办 → TX 是 DMA 吗 → DMA buffer 768 为什么 → ring 2048 为什么。

---

## 23. Q049 [A级] D-Cache 对 UART DMA 的影响

**问题：** D-Cache 对 UART DMA 有什么影响？怎么处理？

**30秒回答：**
DMA 写内存后 CPU 可能仍读 cache 旧内容，所以回调里先 `SCB_InvalidateDCache_by_Addr` 再读。和共享 IPC 不同：共享区靠 MPU non-cacheable，UART DMA buffer 在可缓存 AXI SRAM 要显式处理。

**2分钟回答：**
invalidate 前注意 cache line 对齐（DMA buffer 32 字节对齐）；invalidate 只对"DMA 写、CPU 读"场景，若 CPU 写过该区要先 clean 再 invalidate。

**可能追问链：**
invalidate 前注意什么 → 和共享 IPC 的 cache 处理有何不同 → clean 和 invalidate 区别 → cache line 对齐为什么重要 → 不处理会怎样。

---

## 24. Q053 [S级] framebuffer 多大，为什么放 SDRAM

**问题：** framebuffer 多大？为什么放 SDRAM？

**30秒回答：**
800×480 RGB565，每像素 2 字节，`800*480*2 = 768000 字节 ≈ 750KiB`。这么大连续 buffer 放不进内部 SRAM，放外部 SDRAM（`0xD0000000`）。

**2分钟回答：**
AXI SRAM 512K 还要装应用 .bss（实测 508KB，已经很紧张），放不下 framebuffer。LVGL 用 `800*400` 的部分渲染 buffer（partial render mode），flush 回调把脏矩形写入 LCD/LTDC。

**可能追问链：**
双 framebuffer 要多少 → LVGL 为什么用 partial buffer → flush 回调做什么 → SDRAM cacheable 会不会有显示一致性问题 → 内部 SRAM 为什么放不下。

---

## 25. Q060 [S级] 两层 watchdog

**问题：** 这个工程的 watchdog 是几层结构？

**30秒回答：**
两层：软件任务健康监控（每槽递增 heartbeat counter 判断进展）+ 硬件 IWDG1（软件判断不健康就停喂，等硬件复位）。软件层判"谁死了"，硬件层兜底复位。

**2分钟回答：**
每槽位存 counter、上次观察值、最后进展时间。`BSP_Watchdog_Kick()` 递增 counter，监控任务比较 counter 是否变化判断进展。软件自身也可能挂，所以需要 IWDG 兜底。

**可能追问链：**
为什么需要两层 → counter 为什么比 boolean 强 → 停喂后多久复位 → 复位原因怎么读 → 软件挂了 IWDG 能救吗。

---

## 26. Q061 [S级] IWDG 参数与超时

**问题：** IWDG 的参数和超时是多少？

**30秒回答：**
IWDG1，时钟 LSI（约 32kHz），prescaler 128，window 4095，reload 2000。超时 `128×2000/32000 ≈ 8s`。

**2分钟回答：**
LSI 实际频率有误差，8s 是估算值非精密实测。watchdog 任务 2000ms 喂一次，启动宽限期 45000ms。复位原因读 `RCC->RSR` 区分 IWDG/WWDG/POR/NRST/软件复位。

**可能追问链：**
LSI 不精确影响什么 → 8s 怎么算 → 喂狗周期和超时裕量 → 宽限期为什么 45s → 怎么区分复位原因。

---

## 27. Q062 [S级] 各槽位超时

**问题：** 各监控槽位的超时是多少？

**30秒回答：**
LVGL 5000ms、default 8000ms、Modbus 10000ms、ADC 5000ms、CM4 10000ms、W5500 60000ms。watchdog 任务 Realtime、2000ms 喂狗、宽限 45000ms。

**2分钟回答：**
CM4 槽 10000ms 而 DHT11 周期 2000ms——给 5 个采样周期容错，允许偶发延迟但超过 10s 无发布判死核，用倍数避免误报。CM4 心跳由 CM7 SensorIpcTask 收到 HSEM 通知时 kick。

**可能追问链：**
CM4 槽为什么 10000 不是 3000 → CM4 心跳谁 kick → 启动时预置一次为什么 → W5500 为什么 60s → 槽位超时怎么选。

---

## 28. Q064 [S级] boolean → 引用计数

**问题：** OTA 期间 watchdog 为什么误判？boolean 有什么缺陷？

**30秒回答：**
OTA 擦写期间 LVGL/ADC 被挂起，watchdog 会误判死任务。早期 boolean 标记被多调用者嵌套时互相覆盖，一个退出清零另一个还在进行的长操作。改引用计数：进入递增、退出递减、归零才恢复监控。

**2分钟回答：**
` s_long_op_active` 引用计数，`bsp_ota_port_enter()`/`exit()` 成对调用。长操作期间只监控 W5500 槽。这也是之前"IWDG 看似不复位"（47s）的根因——不是 IWDG 坏了，是长操作标志被覆盖导致不停喂狗。

**可能追问链：**
boolean 具体怎么出错 → 引用计数会溢出吗 → 长操作期间监控什么 → 这个 bug 怎么发现的 → 如果 OTA 真死锁怎么兜底。

---

## 29. Q067 [A级] CM4 迁移后 heartbeat 失效

**问题：** CM4 迁移后原来的监控为什么失效？怎么修？

**30秒回答：**
DHT11 迁到 CM4 后，原 CM7 本地 kick 的 heartbeat 不再代表传感器任务实际运行，CM4 死了 CM7 发现不了。新增 `BSP_WDG_CM4` 槽，CM7 收到 HSEM 通知就 kick，启动时预置一次。

**2分钟回答：**
启动预置一次是为了防止 CM4 从未启动时被永久忽略。局限：HSEM 链路故障和 CM4 死机表现一样（都无通知），当前无法区分。

**可能追问链：**
启动为什么预置一次 → HSEM 中断丢失能区分吗 → CM4 读失败但存活时 heartbeat 更新吗 → 跨核 heartbeat 的局限 → 怎么增强。

---

## 30. Q071 [S级] 为什么不用 OpenAMP

**问题：** 为什么不用 OpenAMP/RPMsg？Shared SRAM 方案边界在哪？

**30秒回答：**
简单传感器数据通道用 Shared SRAM + HSEM 足够、轻量、无依赖；OpenAMP/RPMsg 更重、调试面更大。边界是数据量、消息种类、标准协议需求。

**2分钟回答：**
Shared SRAM 方案最大缺点是需自己管理一致性、通知、缓冲布局和初始化竞态，扩展性不如标准框架。当数据量大、消息种类多、要复用多核框架生态时才值得引入 OpenAMP。

**可能追问链：**
Shared SRAM 最大缺点 → 什么时候值得 OpenAMP → 初始化竞态是什么 → 扩展多传感器怎么规划 → 无锁读取怎么做。

---

# H14 工业通信协议（Modbus）

> **模块定位**：工业控制终端没有标准工业总线就不成立。本模块覆盖 Modbus 从站的选型理由、帧格式、主从角色、寄存器设计与内部数据联动。
>
> **验证状态（务必诚实表述）**：Modbus RTU 从站**已实现、已编译进 CM7、已接入运行路径**（`freertos.c:148,157` 无条件启动；`bsp_watchdog.h:14` 有专属 10s 监控槽；`lvgl_demo.c:263,320` 与 LCD 双向联动）。但仓库**没有实机联调日志**，面试时应表述为"已实现并接入系统，实机联调未留记录"。
>
> **未实现项（不可说成已实现）**：**Modbus TCP 未实现**——`modbus-tcp.c` 在仓库中存在，但**不在 CM7 编译清单**里；实际编译的只有 `modbus.c` / `modbus-data.c` / `modbus-st-rtu.c` / `errno.c`。传输是 **RTU over USB CDC**，不是网口。

---

## 31. Q077 [S级] 为什么选择 Modbus

**问题：** 工业控制终端为什么选 Modbus，而不是 MQTT/HTTP/自定义协议？

**30秒回答：**
Modbus 是工业现场事实标准，PLC/SCADA/HMI 原生支持，终端接上去就能被组态软件读写，不需要为每个客户写私有驱动。协议本身极简（请求-应答、无会话、无握手），RTU 是二进制 + CRC16，开销小、适合 MCU，且有 libmodbus 这类成熟库可直接移植。MQTT/HTTP 要跑 TCP/IP 栈和网络基础设施，对现场总线场景过重。

**2分钟回答：**
选型主要看三点。**第一是生态互操作性**：工业现场的组态软件（如组态王、WinCC、各种 HMI）默认就支持 Modbus，如果我们自定义协议，每接一个上位机都要提供驱动或文档，集成成本高；用 Modbus 相当于复用了整个行业的既有工具链。**第二是复杂度与资源**：Modbus 是纯请求-应答模型，从站不需要维护会话、心跳、重连状态机，RTU 帧最大 256 字节（`modbus-rtu.h:17`），CRC16 校验，一个 MCU 完全扛得住；相比之下 MQTT 要 TCP + broker，HTTP 要完整报文解析，资源占用和故障面都大。**第三是可靠性模型匹配**：工业总线关注"确定性、可校验、可诊断"，Modbus 的 CRC 校验加异常码应答正好满足，而且链路简单意味着排障容易。

补充一点边界：Modbus 是**明文、无认证**的，没有加密和访问控制，本质适用于受控的现场网络。要做远程/公网场景就需要额外的安全层，这超出 Modbus 本身的范畴。

**可能追问链：**
为什么不用 MQTT → Modbus 有什么缺点 → 明文无认证怎么办 → 为什么不在网口上做 Modbus TCP → 怎么和 OTA 共存。

---

## 32. Q078 [S级] Modbus RTU 帧格式

**问题：** Modbus RTU 的帧格式是什么？

**30秒回答：**
RTU 的 ADU = **从站地址（1B）+ 功能码（1B）+ 数据（N）+ CRC16（2B）**，CRC 低字节在前。最大 ADU 长度 256 字节（`modbus-rtu.h:17`）。CRC16 初值 0xFFFF、多项式 0xA001（即反射形式的 0x8005），库里用高低字节查表实现（`modbus-st-rtu.c:23-66`）。标准 RTU 靠 T3.5 静默间隔判帧边界。

**2分钟回答：**
帧结构上是地址、功能码、数据域、CRC 四段。**地址域**用于多从站寻址，我们的从站地址固定为 1（`MODBUS_SLAVE_ID`，`bsp_modbus_app.h:9`），广播地址 0 由 libmodbus 内部处理（`modbus-st-rtu.c:208`）。**功能码**决定操作，我们实现了 FC01 读线圈、FC03 读保持寄存器、FC05 写单线圈、FC0F 写多线圈（`bsp_modbus_app.h:11-17`）。**数据域**长度随功能码变化，所以接收侧必须根据功能码动态计算期望长度，库里的 `_modbus_rtu_check_integrity` 就是干这个的，CRC 不符会置 `errno = EMBBADCRC`（`modbus-st-rtu.c:225-258`）。

**这里有个来自本工程的关键点**：标准 Modbus RTU 规定帧间隔至少 3.5 个字符时间（T3.5），波特率高于 19200 时固定按 1750µs 算，用来判定"一帧结束"。但我们这条链路跑在 **USB CDC 虚拟串口**上——USB 是包传输、**没有真实波特率**，T3.5 在物理上不存在。所以不能依赖静默间隔判帧，改为**用超时判帧**：代码里显式设置 `modbus_set_indication_timeout(ctx, 0, 1000000U)` 和 `modbus_set_byte_timeout(ctx, 0, 20000U)`（单位微秒，`bsp_modbus_app.c:349-351`），并注释说明默认 `indication_timeout=0` 会导致 `RecvByte` 超时 0ms 收不到数据。这是"把 RTU 跑在非真实串口上"必须处理的适配点。

**可能追问链：**
CRC16 在这个工程里出现几次 → 为什么 CRC 低字节在前 → T3.5 怎么处理 → 一帧最长多少字节 → 收到坏 CRC 怎么响应。

---

## 33. Q079 [S级] 你的设备是主站还是从站

**问题：** 你的设备在 Modbus 网络里是主站还是从站？

**30秒回答：**
**从站（slave/server），地址为 1**。设备被动响应，不主动发起请求。代码上体现为服务任务阻塞在 `modbus_receive()` 等请求，收到后用 `modbus_reply()` 应答（`bsp_modbus_app.c:363,382`），完全没有 `modbus_write_*` 这类主站 API 调用。

**2分钟回答：**
角色选择的理由是**谁掌握时序**。在工业现场，上位机（PLC、SCADA、组态软件、HMI）是数据的消费者和控制指令的发出者，它需要轮询多个设备，所以由它做**主站**掌握轮询时序；我们的终端是"被采集、被控制"的对象，做**从站**，不参与调度。这样做还有个好处：从站实现比主站简单得多——不需要管轮询调度、超时重试、多从站寻址、链路恢复，只需要"收到合法请求就正确应答"。

主从在代码里的具体差异：从站用 `modbus_receive` + `modbus_reply`，主站才用 `modbus_read_registers` / `modbus_write_register` 这类封装。我们这里 `modbus_set_slave(ctx, 1)` 设定自己的地址（`bsp_modbus_app.c:348`），并用 `modbus_connect` 把传输层接上（`:355`）。

**异常路径**也按从站规范处理：CRC 错误时（`errno == EMBBADCRC`）**不跳过**，仍然尝试走 reply 路径返回异常码，让主机知道帧被收到了但内容无效；其他错误（如无数据）才直接 continue（`bsp_modbus_app.c:372-376`）。另外空帧/确认帧会 `osDelay(1)` 让出 CPU，避免把 ADC/LVGL 饿死（`:365-370`）——这是从站任务作为低优先级后台服务时必须做的礼让。

**可能追问链：**
为什么不做主站 → 从站怎么知道一帧结束了 → 广播地址怎么处理 → CRC 错误为什么不直接丢弃 → 从站任务会不会饿死别的任务。

---

## 34. Q080 [S级] 寄存器如何设计

**问题：** 你的 Modbus 寄存器/线圈是怎么设计的？

**30秒回答：**
只实现两类对象：**10 个线圈**（FC01/FC05/FC0F）和 **10 个保持寄存器**（FC03）。保持寄存器映射为：`0`=湿度、`1`=温度、`2`=ADC 电压(mV)、`3`=DHT11 在线状态。线圈 `0` 控制 PA3 LED。输入寄存器、输入位、离散输入都没用（`tab_input_bits` / `tab_input_registers` 为 NULL，`bsp_modbus_app.c:43-51`）。

**2分钟回答：**
设计原则是**按数据方向分类**，而不是把所有量堆在一张表里：

- **只读的传感器量 → 保持寄存器**（FC03 读）：湿度、温度、ADC、DHT11 状态。这里有个工程取舍：严格按 Modbus 语义，"输入量"应该用输入寄存器（FC04），但现场很多主站/组态软件的默认轮询习惯就是读 FC03 保持寄存器，所以放在 FC03 是为了**兼容主站习惯**。代价是语义上不够标准——这是个可以主动坦白的点。
- **可写的执行器 → 线圈**（FC01/FC05/FC0F）：线圈 0 映射 PA3 LED，注意板载 LED 是**低电平点亮**，所以代码里有 `MODBUS_LED_PA3_ACTIVE_LOW = 1` 做极性转换（`bsp_modbus_app.c:32-35,98-111`）——这类"逻辑值 vs 电气极性"的映射是实际硬件接入常见的坑。

**地址约定**：起始地址用 0（`MODBUS_HOLDING_START = 0`）。Modbus 文档里常说的"40001"是 1-based 的传统编号，换算成协议里的 offset 要减 1，即 40001 → offset 0。代码直接用 offset，避免混淆。

**一个必须坦白的数据精度问题**：保持寄存器是 16 位，而 DHT11 驱动其实读出了**整数位和小数位**（`dht11.c:161-164` 的 `humidity_dec` / `temperature_dec`），但共享结构体 `sensor_shared_data_t` 里只有 `float temperature / humidity` 两个字段，小数位**在发布环节就丢了**；CM7 侧再 `(uint16_t)` 取整存进寄存器。所以寄存器和 LCD 显示的都是**整数℃ / 整数 %RH**。另外 `bsp_app_data.h` 的注释写"通常为 %RH×10"，但实际链路并没有乘以 10（LVGL 是直接 `"%u C"` 打印整数，`lvgl_demo.c:267`），**注释与实际约定不一致**，属于注释过时。

**可能追问链：**
为什么不用输入寄存器 → 温湿度小数怎么处理 → 地址是 0-based 还是 1-based → LED 极性怎么处理 → 寄存器数量不够了怎么扩展。

---

## 35. Q081 [S级] Modbus 与内部共享变量如何关联

**问题：** Modbus 寄存器和设备内部变量是怎么关联的？

**30秒回答：**
核心是 **`BSP_AppData` 作为唯一数据源**，Modbus 层不缓存自己的状态。**读方向**：在应答前调 `BSP_Modbus_RefreshSensorHoldings()` 从 `BSP_AppData_Get*()` 刷新到 `tab_registers[]`（`bsp_modbus_app.c:116-126,380`）。**写方向**：`BSP_Modbus_SetHoldingRegister()` 写 `BSP_AppData`；`BSP_Modbus_SetCoil()` 写线圈后驱动 PA3 GPIO（`:133-152,182-195`）。

**2分钟回答：**
先说**为什么应答前刷新，而不是周期刷新**。Modbus 是请求-应答模型，寄存器的值只有在"被主机读到的那一刻"才有意义。如果按固定周期刷新，要么周期太慢导致主机读到旧值，要么周期太快白白占用 CPU。放在应答前刷新是**按需刷新**，只在真正要被观测时取一次最新值，既不旧也不浪费。

再看**完整的数据流向**，这是这个项目最有意思的地方——Modbus 是双核链路的对外出口：

```
DHT11(CM4, GPIOD Pin12)
  → 共享 SRAM + HSEM2 通知
  → CM7 SensorIpcTask（ulTaskNotifyTake 唤醒）
  → BSP_AppData_SetHumidity / SetTemperature / SetDht11Online（freertos.c:184-188）
  → 应答前 RefreshSensorHoldings
  → Modbus 保持寄存器 0/1/3
  → USB CDC → 上位机
```

也就是说，**寄存器 3（DHT11 在线状态）和 LCD 右上角的 ONLINE/OFFLINE 是同一个 `dht11_online` 标志的两个出口**：CM4 读数失败也照常发布（`valid=0`），CM7 收到后既显示 OFFLINE、也把寄存器 3 置 0。这说明 Modbus 不是独立子系统，而是双核传感链路的暴露面。

**反向链路**（上位机控制）是另一条：

```
上位机写线圈 00000
  → ModbusServerTask 检测到写线圈功能码（bsp_modbus_app.c:384）
  → BSP_Modbus_ApplyCoilToLedPa3() 驱动 PA3 GPIO
  → BSP_Modbus_RequestPersistCoils() 异步落盘
  → LVGL 从 BSP_Modbus_GetCoil() 读回，同步 UI 开关（lvgl_demo.c:263）
```

**双向同步的坑**：LCD 开关和 Modbus 线圈是同一个状态的两种表示，两边都能改，容易形成"UI 改 → 写线圈 → 线圈变化 → 刷新 UI → 又写线圈"的回调递归。代码用 `s_syncing_coil` 标志（`lvgl_demo.c:56,312`）在"UI 正从线圈同步"期间**屏蔽触摸回调**，打断递归（`:291` 注释"期间屏蔽 switch 回调防止循环写线圈"）。这和 Case 3 的 watchdog 是同一种设计直觉：**双向绑定的状态必须有一个"谁在主导"的标志来防重入**。

**可能追问链：**
为什么应答前刷新而不是周期刷新 → 两处改同一个状态怎么防循环 → 读和写会不会并发冲突 → 这个链路和 LCD 显示是同一份数据吗 → CM4 挂了寄存器 3 会怎样。

---

## 36. Q082 [A级] Modbus 的传输层设计

**问题：** Modbus 跑在什么物理层上？为什么这么设计？

**30秒回答：**
跑在 **USB CDC 虚拟串口**上，是 **Modbus RTU over USB CDC**——不是网口、不是真实 UART。设计上做了一层 **UART 抽象**：libmodbus 的 RTU 后端通过 `struct UART_Device` 函数表（`Init/Send/RecvByte/Flush`，`uart_device.h`）访问底层，注册表里只有一项 USB 实现（name = `"usb"`，`uart_device.c:9-10`、`uart_device_usb.c:67-73`）。

**2分钟回答：**
这层的价值是**让成熟的 Modbus 协议栈以为自己连的是串口**。libmodbus 的 RTU 后端本来是给真实 UART 写的，直接移植到 MCU 上要么改库、要么绑死硬件。这里插了一层抽象：协议栈只认 `UART_Device` 接口，具体实现换成 USB CDC 就行，**Modbus 层和上层应用代码一行不用改**。

有个细节能说明这层抽象的"假串口"性质：任务构造时传的是 `modbus_new_st_rtu("usb", 115200, 'N', 8, 1)`（`bsp_modbus_app.c:342`），但 `USBSerial_Init` 里把 baud/parity/data_bit/stop_bit **全部 `(void)` 丢掉了**（`uart_device_usb.c:18-26`）——USB CDC 是包传输，根本没有波特率概念，这些参数只是为了满足接口签名。

**数据通路**是：USB CDC 接收中断 → `BSP_USB_Modbus_OnRx` → 逐字节 `xQueueSendFromISR` 入队（队列长度 1024 字节，`bsp_usb_modbus.c:17,55,75`）→ `ModbusServerTask` 通过 `RecvByte` 逐字节出队（`:145`）。

**值得对比的是**：这里用的是 **FreeRTOS queue**，而 OTA 的 UART 接收用的是**手写 SPSC 环形缓冲 + DMA**（`bsp_ota_uart.c`）。差异来自数据特性——Modbus RTU 帧短（≤256B）、速率低，queue 的逐字节进出完全够用且实现简单；OTA 要跑 274B 帧的高吞吐连续流，才需要 DMA 搬运加 ring 来解耦。**同一个系统里针对不同数据特征选不同缓冲机制**，这是可以主动讲的设计权衡。

**可能追问链：**
为什么不用真实 UART → 115200 这个参数有意义吗 → 为什么用 queue 不用 ring buffer → 队列满了怎么办 → 换传输要改哪些代码。

---

## 37. Q083 [A级] 线圈持久化与 watchdog 集成

**问题：** 线圈状态怎么持久化？Modbus 任务怎么和 watchdog 配合？

**30秒回答：**
线圈状态落 **SD 卡二进制文件** `0:/mb_coils.bin`，结构含 magic `'MCOL'` + version + count + coils + CRC16（`bsp_modbus_coil_nv.c:15-26`），写入走**异步后台任务**不阻塞应答。watchdog 侧 Modbus 有**专属 10 秒槽**（`bsp_watchdog.h:14`、`bsp_watchdog.c:33`），且注册为 **OTA 协作 peer**，OTA 擦写期间可被挂起。

**2分钟回答：**
**持久化的必要性**：工业设备断电重启后，执行器（这里是 LED/继电器类负载）的状态应该保持原样，否则重启就把现场状态丢了。所以线圈一变就落盘。

**异步设计是关键**：`BSP_Modbus_RequestPersistCoils()` 只置一个 `s_coil_save_pending` 标志并用 `osThreadFlagsSet` 通知存储任务（`bsp_modbus_app.c:75-82`），**应答路径不做文件 I/O**。原因是 SD 卡写可能耗时几十到几百毫秒，如果同步写会直接拖垮 Modbus 响应时间。把慢操作从实时路径摘出去、交给低优先级后台任务（`ModbusCoilStorageTask`，`osPriorityBelowNormal`，`:300`）是标准做法。

**恢复路径有个时序坑**：`ModbusCoilRestoreTask` 要**延时 500ms** 等 SD 卡挂载完成才能读文件（`MODBUS_COIL_RESTORE_DELAY_MS`，`:25,317`），并且先检查 `BSP_Modbus_CoilNv_MediaReady()` 再尝试（`:238`）——启动阶段存储介质不一定就绪，这种"等外设就绪"的防御在嵌入式里很常见。文件本身带 magic + version + CRC16，能识别损坏或版本不匹配，不会把垃圾数据当配置恢复。

**watchdog 集成**有三点：

1. Modbus 服务任务三处 kick `BSP_WDG_MODBUS`（`:367,374,378`），10 秒超时——比 LVGL 的 5 秒宽松，因为 Modbus 是**被动响应**的，主机不发起请求时任务就阻塞在 `modbus_receive`，本来就没有"周期进展"，用别的任务那种短超时会误判。
2. 三个 Modbus 任务（服务、存储、恢复）都调 `bsp_ota_coop_register()` 注册为 OTA 协作 peer（`:313,318,417`），OTA 擦写 Flash 期间可被挂起，避免和 Flash 操作争 CPU。
3. 注意一个细节：Modbus 任务**没有自己的专属长操作豁免**，而是通过 `bsp_ota_coop` 在 OTA 期间被挂起——挂起后再配合长操作模式（`s_long_op_active>0` 时 watchdog 只查 W5500 槽），被挂起的 Modbus 槽就不会被误判。这与前面 Case 3 的引用计数机制是同一套体系。

**可能追问链：**
为什么异步落盘 → SD 卡写失败怎么办 → 断电瞬间写一半怎么办 → 为什么超时是 10 秒 → OTA 期间 Modbus 会不会被误判死机。

---

> 注：完整 5 层追问展开见《docs/interview_followup_tree.md》，其中每道核心题按大厂面试风格做深度追问树，所有追问均来自现有工程实现，不新增工程事实。
