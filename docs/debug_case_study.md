# STM32H757 工业控制终端 — 工程 Debug 案例集

> 面向大厂嵌入式面试的 Debug 案例整理。所有结论、寄存器、源码位置均来自当前工程与之前排查记录。
> 每个案例按「背景 → 异常现象 → 排查思路 → 关键证据 → 根因分析 → 修复方案 → 面试回答版本」组织。
> 标注约定：**【实测】**＝在当前硬件上读寄存器/内存得到的原始值；**【代码推导】**＝修复已合入、现场不可复现，由源码 + 寄存器行为推出。
> 注意：Case 1 的第二层修复（`sensor_ipc_boot()` 顺序调整）已合入源码，因此文中"修复前"的观测均为修复动作**之前**录得的数据。

---

# Case 1：LCD 温湿度恒为 0 —— HSEM 一次性通知 × 双核上电时序竞态

> 这是本工程最值得讲的一个案例：**两处独立缺陷叠加**才产生了"屏幕上一个非 0 值都没出现过"这一个现象。
> 拆开看，任何一层单独存在都不会是这个表现——这正是它比"改一行就好了"的 bug 更有讲头的地方。
>
> 标注约定：**【实测】**＝在当前硬件上读过寄存器/内存得到的原始值；**【代码推导】**＝修复已合入、现场不可复现，由源码 + 寄存器行为推出的结论。

---

## 1. 背景

CM4 采集 DHT11，写入 Shared SRAM（SRAM4，`0x38000000` 上的 `.shared_data` NOLOAD 段），通过 HSEM2 发事件通知 CM7；CM7 的 `SensorIpcTask` 阻塞等待通知，醒来读共享数据刷新 LCD + Modbus。

三把 HSEM 分工：

| HSEM | 用途 | 谁持有 |
| --- | --- | --- |
| HSEM0 | 上电握手：CM7 release 它来把 CM4 从 STOP 唤醒 | CM7 take → release |
| HSEM1 | 共享数据互斥锁 | 发布/读取时临时 take |
| HSEM2 | CM4→CM7 事件通知（电平型，不带数据） | CM4 常态持有，发布时 release + 重新 take |

关键代码：

- `app/Common/Src/sensor_shared.c` —— `sensor_shared_publish()` / `sensor_shared_read()` / `HAL_HSEM_FreeCallback()`
- `app/Common/Inc/sensor_shared.h:16-20` —— HSEM 编号与掩码
- `app/CM4/BSP/Sensor/dht11.c:172-201` —— CM4 采样发布任务 `DHT11Task`
- `app/CM7/Core/Src/freertos.c:174-205` —— CM7 消费任务 `SensorIpcTask`
- `app/CM7/Core/Src/main.c:124-149` —— CM7 启动序列 `Boot_Mode_Sequence_2`

## 2. 异常现象

- 上电后 LCD 上温湿度**一直显示 0**，从不刷新；反复复位都一样；
- CM4 侧独立验证过：DHT11 能正常读出 40% / 27 °C，数据确实写进了共享区；
- 关键限定：**不是"卡在某一次的采样值上"，而是从头到尾一个非 0 值都没出现过。**

> 这一点很重要，也踩过坑。最初的记录写成"卡在第一次采样值"，但那是**猜**的（默认了"第一次采样一定成功"）。
> 真正动手用调试器抓时间轴之后才发现，"卡在第一包"和"从来没出过数"是两种机制、两种修法。

## 3. 排查方法与工具

### 3.1 为什么不用断点、不用 printf

这个 bug 是**启动时序**类的，工具选错就等于把现象抹掉：

- **断点会 halt 内核**。竞态窗口只有几百毫秒，一 halt 就全乱，而且 halt 本身会改变两侧的推进节奏；
- **printf 更糟**。CM4 侧没接串口；就算接了，`printf` 的耗时（几十 µs 到 ms 级）本身就会改变时序；
- **双核各自有 `bsp_log`**，但两边的日志没有共同时间基准，拼不出跨核先后顺序。

所以选了第三条路：**不停核，直接读内存和寄存器，用一个 openocd 会话连续采样。**

### 3.2 核心手法：单会话 + 不停核 + 自计时

`mdw`（memory display word）在目标**全速运行**时也能读——Cortex-M 的调试访问口可以不停核访问内存，这是这个手法成立的前提。

```bash
oc() { openocd -f interface/cmsis-dap.cfg -f target/stm32h7x.cfg "$@"; }

# 一个会话内连续采样：reset run 之后靠 sleep 自计时，全程不 halt
oc -c "init" -c "reset run" \
   -c "sleep 200"   -c "echo P01" -c "mdw 0x2400001c 1" -c "mdw 0x38000000 5" -c "mdw 0x24000468 2" -c "mdw 0x10006da8 1" \
   -c "sleep 200"   -c "echo P02" -c "mdw 0x2400001c 1" -c "mdw 0x38000000 5" -c "mdw 0x24000468 2" -c "mdw 0x10006da8 1" \
   ...                                                                                    # 共 45 次
   -c "shutdown"
```

**为什么必须放在一个会话里**：每次重新起 openocd（init + reset）要几百毫秒，而这个 bug 的关键窗口只有几百毫秒——分开跑等于把窗口整个跳过去。

### 3.3 地址表（符号 → 地址，取自两个 `.map`）

| 符号 | 地址 | 说明 |
| --- | --- | --- |
| `s_sensor_data` | `0x38000000` | 共享结构，5 words（magic / valid+pad / temp / humid / ts） |
| CM7 `uwTick` | `0x2400001c` | CM7 HAL tick，1 kHz，**准确** |
| CM7 `s_live`（`bsp_app_live_t`） | `0x24000468` | 8 字节：`humidity / temperature / adc_mv / dht11_online` |
| CM7 `SystemCoreClock` | `0x2000000c` | |
| CM4 `uwTick` | `0x10006da8` | CM4 HAL tick。（实测：从 CM4 的 AP 读 `0x10006da8` 与 `0x30006da8` 返回同一份数据 ⇒ `0x10000000` 是 CM4 视角下 D2 SRAM 的别名，288 K = SRAM1+2+3） |
| CM4 `SystemCoreClock` | `0x10000010` | 实测 `0x0E4E1C00` = 240 MHz |
| CM4 `htim7` | `0x10000030` | HAL tick 源：Prescaler=239 / Period=999 → 240/240 = 1 kHz |
| HSEM `C1IER` / `C1ICR` / `C1ISR` / `C1MISR` | `0x58026500` / `04` / `08` / `0C` | HSEM base = `0x58026400`（`D3_AHB1PERIPH_BASE + 0x6400`） |

`0x38000000` 的 5 个 word 与结构体字段一一对应：

```
+0x00 magic   +0x04 valid(+3B 填充)   +0x08 temperature   +0x0C humidity   +0x10 timestamp
```

### 3.4 踩过的两个坑

1. **`halt` 之后 `shutdown` 不会恢复运行。** 我一开始用 `-c "halt"` 读内存，读完整理数据时发现 CM7 被留在 halt 状态，跨越了两次采样（两次读到的 `uwTick` 完全相同，都是 `0x3b8f`）。更阴的是 **CM4 不在 openocd 的 target 列表里，所以它照样在跑**——于是"CM7 停了、CM4 还在发"这个假象看起来像极了 bug 本身。修正：只读内存就**永远不要 halt**，用 `reset run` 起，用 `shutdown` 收。
2. **默认的 `stm32h7x.cfg` 里没有 CM4 这个 target。** 直接 `targets stm32h7x.cpu1` 会报 `Target ... is unknown`。cfg 里创建 cpu1 的条件是变量 `DUAL_CORE` 存在，所以要先置位再 source：

   ```bash
   openocd -f interface/cmsis-dap.cfg -c "set DUAL_CORE 1" -f target/stm32h7x.cfg ...
   ```

   （注意是 `DUAL_CORE`，不是 `stm32h7x.DUAL_CORE`——cfg 第 24 行判断的是 `info exists DUAL_CORE`。）

## 4. 排查过程

### 第 1 步：数据源是不是活的？—— 直接读共享内存

```
mdw 0x38000000 5
53484d31 a5a5a501 41d80000 42200000 00001f44
```

逐个字段解：

| word | 值 | 含义 |
| --- | --- | --- |
| `+0x00` | `0x53484D31` | magic = `SENSOR_SHARED_MAGIC` ✅ |
| `+0x04` | `0xA5A5A501` | 低字节 `0x01` = `valid` ✅；高 3 字节 `0xA5` 是填充字节（见第 9 节） |
| `+0x08` | `0x41D80000` | float 27.0 °C |
| `+0x0C` | `0x42200000` | float 40.0 % |
| `+0x10` | `0x00001F44` | timestamp = 8004 |

**【实测】**结论：CM4 在发布，数据是好的，`magic`/`valid`/温湿度全对。**问题不在数据，在"通知"。**

这一步就把排查范围砍掉了一半——这是"先证伪、再深入"的标准走法。

### 第 2 步：消费者收到了吗？—— 读 CM7 的 AppData

```
mdw 0x24000468 2
001b0028 00010a4a        # humidity=0x28=40, temperature=0x1b=27, adc_mv=0x0a4a=2634, online=1
```

修复后这里是对的；**修复前这里恒为 `00000000 00000000`**——也就是 LCD 拿到的全是 0。

于是问题被锁死为：**CM4 写的东西，CM7 的 `SensorIpcTask` 没消费到。**

### 第 3 步：查通知机制 —— 读 HSEM 寄存器，挖出第一层根因

```
mdw 0x58026500 4
00000004 00000000 00000003 00000000
                # C1IER  C1ICR  C1ISR  C1MISR
```

读法（`HSEM_COMMON` 在 CM7 上正好别名到 `HSEM->C1*`）：

| 寄存器 | 值 | 解读 |
| --- | --- | --- |
| `C1IER` | `0x00000004` | bit2 = HSEM2（事件通知）**已使能** ✅ 这是 re-arm 修复后的状态 |
| `C1ISR` | `0x00000003` | bit0/bit1 置位 —— HSEM0（启动握手）、HSEM1（数据锁）处于"已被 release"状态，属正常 |
| `C1MISR` | `0x00000000` | **`MISR = ISR & IER`**：`0x3 & 0x4 = 0x0` ✅ |

**【实测】这一行寄存器把 HSEM 中断的触发条件讲透了：**

- 中断条件是 **`MISR & IER ≠ 0`**，是**电平**，不是边沿；
- 所以 **`ActivateNotification()` 只写 `IER`（`stm32h7xx_hal_hsem.c:344` 就一句 `HSEM_COMMON->IER |= SemMask;`），不清 `MISR`**；
- **如果置位 `IER` 的那一刻，对应的 `ISR` 位已经是 1**（对端早就 release 过了），那么 `MISR` 会**立刻**变成非 0，**中断当场就进来一次**。

这个结论直接解释了第一层根因的"为什么只收到一次"：

`app/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_hsem.c:402-412`（`USE_MULTI_CORE_SHARED_CODE` 在本工程未定义，走 `#else` 分支）：

```c
  /* Get the list of masked freed semaphores*/
  statusreg = HSEM_COMMON->MISR;
  /*Disable Interrupts*/
  HSEM_COMMON->IER &= ~((uint32_t)statusreg);   /* ← 先清 IER */
  /*Clear Flags*/
  HSEM_COMMON->ICR = ((uint32_t)statusreg);
  ...
  HAL_HSEM_FreeCallback(statusreg);             /* ← 才回调用户函数 */
```

**`HAL_HSEM_IRQHandler()` 在调用用户回调之前，就把 `IER` 里对应的位清掉了，而且 HAL 不会自动恢复。** 所以：`ActivateNotification()` 只 arm 一次 ⇒ 通知也只送达一次。

#### 第一层根因（【代码推导】，现场不可复现）

修复前 `HAL_HSEM_FreeCallback()` 里没有 re-arm，所以 CM7 这一辈子**只**会收到一次 HSEM2 中断。而按第 3 步的寄存器行为，这一次中断还未必是"有新数据"触发的——如果 CM7 置位 `IER` 时 `ISR` bit2 已经是 1，这次中断是**电平当场满足**触发出来的。结合第 5、6 步的时间轴，修复前的真实顺序恰好就是这样，于是那唯一一次通知唤醒任务后读到的是 0，而之后**再也不会有第二次唤醒**。

> 面试时这一层可以如实表述为"由 HAL 源码 + 寄存器行为推出"，不必声称当场复现。

### 第 4 步：把时间轴量化 —— 用两个核的 `uwTick` 建统一时基

确认"只收到一次"之后，下一个问题必然是被问爆的那句：**"CM4 到底什么时候发的？"**

两个核各自有 1 kHz 的 HAL tick，这里正好可以用作跨核时间基准（这也是本案例最有价值的排查手段）：

- CM7 tick：TIM6，测得准；
- CM4 tick：TIM7，`app/CM4/Core/Src/stm32h7xx_hal_timebase_tim.c`，Prescaler=239 / Period=999，
  时钟来自 `RCC_GetClocksFreq()`（不是 `SystemCoreClock`），所以即使 `HAL_Init()` 时 `SystemCoreClock` 还是初值 64 MHz，tick 也是准的 1 kHz；
- 两个 tick 的**唯一已知偏差来源**见第 9 节。

三组一次性采样的原始值 **【实测】**：

| 采样点 | CM7 `uwTick` | CM4 `uwTick` | 共享 `ts` |
| --- | --- | --- | --- |
| S1 | `0x04fb` = 1275 | `0x04f5` = 1269 | — |
| S2 | `0x0ceb` = 3307 | `0x0ccf` = 3279 | — |
| S3 | `0x2479` = 9337 | `0x2421` = 9249 | `0x1f44` = 8004 |

由 S1→S2（2032 ms 墙钟）与 S2→S3（6030 ms 墙钟）两段，可以反解出：**每个 DHT11 采样周期，CM4 的 tick 比 CM7 少走 20~22 ms**。而 `osDelay(DHT11_SAMPLE_INTERVAL_MS)` = `osDelay(2000)`，名义周期 2000 ms——多出来的这 20 多 ms 正是下面第 9 节要讲的关中断窗口。

### 第 5 步：CM4 的首次发布发生在 `tick = 1`

继续按上面的时基读共享结构，修复后从头到尾都是 **【实测】**：

```
+0x10 timestamp = 1           # 上电后第一次发布，CM4 HAL tick 只走了 1
```

一开始这个 `1` 很反直觉——DHT11 一次读要 20 ms 以上，怎么可能 tick 才 1？

解开它的关键是 `app/CM4/BSP/Sensor/dht11.c:120-125`：

```c
    irq_mask = __get_PRIMASK();
    __disable_irq();                 /* ← 整个 DHT11 协议期间关中断 ~20 ms */
    dht11_pin_output();
    dht11_line_low();
    dht11_delay_us(20000U);          /* ← 拉低 20 ms 起始信号 */
```

这 20 多毫秒里 TIM7 的中断被 PRIMASK 挡住，只能在 `__set_PRIMASK()` 恢复的那一瞬间**补触发一次**，`uwTick` 因此只 +1。

所以 `ts = 1` 的正确读法是：**这次读是真读（确实关中断等了 20 ms），而且它发生在 CM4 HAL tick 刚开始不到 1 ms 的时候。**

于是时间轴被钉死了：

```
CM4 tick 1 ms    : 第一次 DHT11 采样完成并发布（valid=1, ts=1）
CM4 tick 2002 ms : 第二次发布
CM4 tick 4003 ms : 第三次发布
...
```

### 第 6 步：回头审 CM7 的启动序列 —— 找到第二个 memset

CM4 在 tick 1 ms 就发布了，那 CM7 这时候在干什么？去读 CM7 的启动序列（修复前）：

```
main.c:113   HAL_Init()                          ← HAL tick 从 0 开始
main.c:123   SystemClock_Config()
main.c:131   HAL_RCCEx_EnableBootCore(RCC_BOOT_C2)   ← 放行 CM4
main.c:141   HAL_HSEM_FastTake/Release(HSEM_ID_0)    ← 这才是真正唤醒 CM4 的那一下
main.c:156+  MX_GPIO/DMA/FMC/LTDC/DMA2D/ADC/SDMMC/FATFS/UART/SPI 初始化
main.c:177   sdram_init()     ← 慢
main.c:180   lcd_init()       ← 慢
main.c:204   MX_FREERTOS_Init()  →  freertos.c:120  sensor_ipc_init()
```

`sdram_init()` + `lcd_init()` 这些耗时的外设初始化全在放行 CM4 **之后**。也就是说：

```
CM4:  ──被唤醒──boot──[tick 1ms: 首次发布，数据落进 0x38000000]──────────[tick 2002ms: 第二次发布]
CM7:  ──放行CM4──SDRAM/LCD初始化──[tick 350~550ms: sensor_ipc_init() 里 memset 整个共享区]
```

而修复前的 `sensor_ipc_init()`（`app/Common/Src/sensor_shared.c`）第一句就是：

```c
static void sensor_shared_reset(void)
{
    memset((void *)&s_sensor_data, 0, sizeof(s_sensor_data));   /* ← 把 CM4 的第一包抹掉 */
    s_sensor_data.magic = SENSOR_SHARED_MAGIC;
}

void sensor_ipc_init(osThreadId_t notify_task)
{
    sensor_shared_reset();                 /* ← 先清零 */
    s_notify_task = (TaskHandle_t)notify_task;
    HAL_HSEM_ActivateNotification(...);    /* ← 再 arm，此时 ISR bit2 已经是 1 → 立刻来一次中断 */
}
```

**两个核都在 `sensor_shared_reset()` 里 memset 同一块内存** —— CM7 在删 CM4 刚写好的数据。这就是第二层根因。

### 第 7 步：轮询抓窗口 —— 拿到"全 0 窗口"的实锤

时序推理到此已经很完整，但还缺一个直接证据。于是用 3.2 的方法跑 45 次连续采样，把修复前的上电过程整个录下来 **【实测】**：

| 轮询段 | 观察到的现象 |
| --- | --- |
| P01–P02 | `valid=1`、`ts=1` —— **CM4 的首次发布确实落盘了** |
| P03–P09 | `0x38000000` 全 0 —— **CM7 的 memset 把它抹掉了**，这就是"全 0 窗口" |
| P10 之后 | `valid=1`、`ts=2002` —— CM4 第二次发布，CM7 第一次真正看到数据（CM7 tick ≈2020 ms） |

**修复前的完整因果链（全部【实测】）：**

```
CM4 tick 1 ms    : 首次发布 valid=1 ts=1  ─────┐ 数据落盘 ✅
CM7 tick ~350-550: sensor_shared_reset() memset │ 抹掉 ❌（全 0 窗口）
                  然后 ActivateNotification()    │ ISR bit2 已置位 → 电平满足 → 当场来一次中断
CM7 tick ~350-550: 任务被唤醒，读到的正是 0     │ valid == 0
                   → printf 之外什么都没更新    │
CM4 tick 2002 ms : 第二次发布 ─────────────────┘ 但 CM7 的 IER 已被清、没有 re-arm
                                                 ⇒ 这次通知被静默丢弃，之后每一次都是
最终表现：LCD 温湿度恒为 0，且"复位多少次都一样"
```

## 5. 根因分析（两层叠加）

**第 1 层：HSEM 事件通知是一次性的。**
`HAL_HSEM_IRQHandler()` 在回调用户函数之前清掉 `IER` 对应位（`stm32h7xx_hal_hsem.c:402-412`），HAL 不自动恢复；而 HSEM 的触发条件是电平 `MISR & IER`，`ActivateNotification()` 只写 `IER` 不清 `MISR`。所以不 re-arm ⇒ 只送达一次。

**第 2 层：上电时序竞态 —— CM7 的清零晚于 CM4 的首次发布。**
CM4 是纯生产者，CM7 却在放行 CM4 之后（`MX_FREERTOS_Init()` 里）才 memset 共享区，把 CM4 的第一包数据删了。更麻烦的是：**arm 通知和 memset 紧挨着**，于是 CM4 早先那次 release 让 `ISR` bit2 一直挂着，arm 的瞬间电平满足、当场触发一次中断——CM7 收到的"唯一一次通知"就是这样一次**语义上并不代表有新数据**的中断，它唤醒的任务读到的正是刚被清零的结构，`valid == 0`。

**为什么必须两层叠加才是"恒为 0"：**

| 场景 | 表现 |
| --- | --- |
| 只有第 1 层（有 re-arm、memset 仍在后面） | 首屏是 0（第一包被抹），但第 2 s 的第 2 包会 self-heal → **看起来"正常"，缺陷被掩盖** |
| 只有第 2 层（memset 在前面、有 re-arm） | 第一包活着，数据从 ~0.5 s 起就正常 → **也看起来正常** |
| **两层都在** | 唯一那次通知取到 0 值，之后永远不再唤醒 → **恒为 0** ✅ 与现场完全吻合 |

而 `valid == 0` 时 CM7 侧是不写温湿度的（`app/CM7/Core/Src/freertos.c:199`），所以 `AppData` 一直停在初值 0 上——这就是 LCD 上那个刺眼的 0。

**顺带一个设计层面的观察**：`sensor_shared_reset()` 被**两个核都调用**了——共享结构有两个"主人"。结构体里那个 `SENSOR_SHARED_MAGIC` 字段其实是原设计意图的化石（"只有生产者初始化，消费者校验 magic"），但**全工程没有一处读过 magic**，所以这个本可以兜住问题的校验从来没有生效。

## 6. 关键证据汇总

| # | 证据 | 类型 | 说明 |
| --- | --- | --- | --- |
| E1 | `mdw 0x38000000 5` → `53484d31 a5a5a501 41d80000 42200000 00000001` | 实测 | 修复后 CM7 tick 1543 ms 时**首包（ts=1）仍在**，未被抹 |
| E2 | `mdw 0x58026500 4` → `00000004 00000000 00000003 00000000` | 实测 | 演示 `MISR = ISR & IER`；`IER=0x4`（HSEM2 已重新武装），`MISR=0`（无挂起中断） |
| E3 | `stm32h7xx_hal_hsem.c:402-412` 清 `IER` 早于回调 | 源码 | 第一层根因 |
| E4 | `stm32h7xx_hal_hsem.c:344` 只写 `IER`，不清 `MISR` | 源码 | 电平触发 ⇒ arm 时可能当场来一次中断 |
| E5 | 修复前 45 轮采样：P01-02 有数据、P03-09 全 0、P10 起 `ts=2002` | 实测 | "全 0 窗口"实锤 |
| E6 | 修复前 CM7 首次上屏在 CM7 tick ≈2020 ms（第二次发布） | 实测 | 说明首包被吞 |
| E7 | 修复后 `valid=1` 从 CM7 tick 56 ms 起、无全 0 窗口、AppData 40/27 @ 550~712 ms | 实测 | 修复生效 |
| E8 | `ts=1` + `HAL_GetTick()` 在 `__set_PRIMASK()` 之后 | 实测+源码 | 证明是真读（关中断 20 ms，tick 只 +1） |

调试工具链：openocd 0.12.0 + CMSIS-DAP，`mdw` 不停核读内存，`sleep` 自计时；双核 `uwTick` 作跨核时基。

## 7. 修复方案

### 7.1 第一层：回调里重新武装（已随 `47f46c1` 合入）

`app/Common/Src/sensor_shared.c` —— `HAL_HSEM_FreeCallback()`：

```c
void HAL_HSEM_FreeCallback(uint32_t SemMask)
{
    BaseType_t higher_priority_task_woken = pdFALSE;

    if ((SemMask & SENSOR_SHARED_EVENT_HSEM_MASK) != 0U) {
        /* Re-arm the notification. HAL_HSEM_IRQHandler() clears the IER bit
         * before invoking this callback, so without re-arming the CM4->CM7
         * event notification fires only once and every later publish is lost. */
        HAL_HSEM_ActivateNotification(SENSOR_SHARED_EVENT_HSEM_MASK);

        if (s_notify_task != NULL) {
            vTaskNotifyGiveFromISR(s_notify_task, &higher_priority_task_woken);
        }
    }

    portYIELD_FROM_ISR(higher_priority_task_woken);
}
```

### 7.2 第二层：把清零挪到"放行 CM4 之前"——可证明有序，不用猜

第一层的修复让系统能自愈，但**首包被抹**这件事还在（只是被掩盖了）。第二层的关键是：**不要靠延迟去赌顺序，而是找到一个"因果上必然有序"的挂载点。**

CM4 是被 HSEM0 从 STOP 唤醒的，**在 CM7 release HSEM0 之前，CM4 连 `HAL_Init()` 都没执行**。所以只要在 release HSEM0 之前做清零+武装，就**可证明**早于 CM4 的任何一次发布：

`app/CM7/Core/Src/main.c:124-142`：

```c
/* USER CODE BEGIN Boot_Mode_Sequence_2 */
  /* Bring up the CM4<->CM7 shared sensor block BEFORE releasing CM4.  This core
     is parked in STOP until the HSEM #0 release below, so the reset performed
     by sensor_ipc_boot() is provably ordered before CM4's first publish. */
  __HAL_RCC_HSEM_CLK_ENABLE();
  sensor_ipc_boot();

  HAL_RCCEx_EnableBootCore(RCC_BOOT_C2);
  ...
  HAL_HSEM_FastTake(HSEM_ID_0);
  HAL_HSEM_Release(HSEM_ID_0, 0);      /* ← 这一行之后 CM4 才可能跑起来 */
/* USER CODE END Boot_Mode_Sequence_2 */
```

`app/Common/Src/sensor_shared.c:43-62`：

```c
void sensor_ipc_boot(void)
{
#if defined(CORE_CM7)
    /* CM7 boots first and releases CM4 only afterwards, so resetting the shared
     * block here is provably ordered before CM4 can publish anything.  Doing it
     * later (from MX_FREERTOS_Init) let CM4's first publish land first and then
     * be wiped by this memset, which is what made the display show zeros. */
    sensor_shared_reset();

    /* Drop any release flag left over from the previous run: arming below must
     * not immediately re-raise a stale CM4->CM7 event.  Without this, CM7 would
     * consume one bogus notification whose payload is the just-cleared block. */
    __HAL_HSEM_CLEAR_FLAG(SENSOR_SHARED_EVENT_HSEM_MASK);
    HAL_HSEM_ActivateNotification(SENSOR_SHARED_EVENT_HSEM_MASK);
#else
    /* CM4 only arms the event edge.  The shared block belongs to CM7, which
     * initialises it before releasing this core. */
    sensor_shared_take_lock(SENSOR_SHARED_EVENT_HSEM_ID);
#endif
}
```

**所有权归一**：清零只由 CM7 做（`sensor_shared_reset()` 整体包在 `#if defined(CORE_CM7)` 里）。CM4 变成纯生产者，`app/CM4/Core/Src/freertos.c`：

```c
void MX_FREERTOS_Init(void)
{
    /* 只武装 CM4->CM7 事件边沿；共享数据块由 CM7 在放行本核之前初始化 */
    sensor_ipc_boot();
    dht11_app_start();
}
```

> 回答"谁做一次就够了"这个问题：**CM7 做、且在放行 CM4 之前做**。理由是这条时间线上"CM7 早于 CM4"是硬件保证的，不需要任何 delay 去赌；让 CM4 做则无法保证它不覆盖 CM7 正在读的数据。

### 7.3 为什么把 `sensor_ipc_init()` 拆成两个函数

原来的 `sensor_ipc_init(task)` 一次干了两件事：**碰共享数据**（reset）和**挂任务句柄**（attach）。现在这两件事必须落在**完全不同的时间点**：

- 清零必须在**放行 CM4 之前**（越早越好，早到 CM4 还没出生）；
- 任务句柄只能等 `osThreadNew()` 创建完任务之后才能拿到。

硬塞在一个函数里就必然有一头被牺牲——这正是原 bug 的形态。拆开：

```c
/* app/Common/Inc/sensor_shared.h */
void sensor_ipc_boot(void);                       /* 清零 + 武装，放行对端内核之前调 */
void sensor_ipc_attach(osThreadId_t notify_task); /* 只挂句柄，不碰共享数据 */
```

`app/CM7/Core/Src/freertos.c:119-120`：`osThreadNew(...)` 之后 `sensor_ipc_attach(sensorIpcTaskHandle);`。

**"只赋值、不碰共享数据"是刻意的**——`attach` 之后系统里就不再有第二个 memset 了。

### 7.4 为什么 arm 之前要先 `__HAL_HSEM_CLEAR_FLAG()`

按第 3 步的寄存器行为：`ActivateNotification()` 只写 `IER`，不清 `MISR`。如果 arm 的那一刻 `ISR` bit2 还挂着（上一轮运行或本次 CM4 早先的 release 留下的），`MISR` 会立刻非 0 → **当场产生一次"空"中断**。先 `__HAL_HSEM_CLEAR_FLAG()` 把状态位清掉再 arm，就能保证：**CM7 收到的第一次通知，一定来自一次真正的、新发生的 release。**

### 7.5 为什么不用 `osDelay(1000U)` 绕过去

调试期一度在 CM4 的 `DHT11Task` 里加过 `osDelay(1000U)`，屏幕确实出数了——但那是**把首次发布推迟到 CM7 的 memset 之后**，让它侥幸不被抹。这是"用延迟去掩盖竞态"，没有解决问题：

- 时序一改（换 Flash 等待周期、开 D-Cache、调整初始化顺序）竞态就可能回来；
- 而且它同时把"用户看到第一个数据"的时间从 ~0.5 s 拖到了 ~1.5 s+。

**修竞态的正确姿势是消除无序，而不是把无序的概率调小。**

## 8. 验证：修复前后对照

| 观察项 | 修复前【实测】 | 修复后【实测】 |
| --- | --- | --- |
| CM7 清零时刻 | CM7 tick ≈350–550 ms（`MX_FREERTOS_Init` 里） | CM7 tick ≈0（`EnableBootCore` 之前） |
| CM4 首包（`ts=1`） | 落盘后被抹掉 | **存活**（CM7 tick 1543 ms 时读仍为 `ts=1`） |
| 共享区全 0 窗口 | **有**（P03–P09） | **无**（`valid=1` 自 CM7 tick 56 ms 起） |
| CM7 首次上屏 | CM7 tick ≈2020 ms（第二次发布 `ts=2002`） | CM7 tick ≈550–712 ms |
| 上屏数据来源 | 第 2 次采样 | **第 1 次采样** |
| 通知链路持续可用 | 只送达 1 次 | 每 2 s 一次，跨过 10 s 看门狗窗口不复位 |

修复后连续 6 轮监测（CM7 运行中，全程不 halt）**【实测】**：

```
CM7 tick    58 ms | 共享 ts=1     | AppData 0/0            ← CM4 首包已落盘，CM7 任务尚未消费
CM7 tick  3066 ms | 共享 ts=2002  | 40%/27°C  adc 2592mV  online=1
CM7 tick  6083 ms | 共享 ts=6004  | 40%/27°C  adc 2616mV  online=1
CM7 tick  9099 ms | 共享 ts=8005  | 40%/27°C  adc 2610mV  online=1
CM7 tick 12118 ms | 共享 ts=10006 | 40%/27°C  adc 2629mV  online=1
CM7 tick 15139 ms | 共享 ts=14007 | 40%/27°C  adc 2570mV  online=1
```

- CM7 tick **单调增长跨过 15139 ms，穿过两个 10 s 的 `BSP_WDG_CM4` 超时窗口没有复位** → 通知路径没断（断掉的话软件看门狗会在 ~10 s 停喂 IWDG、把 CM7 打回来，tick 会掉回小值）；
- `ts` 每个发布间隔 +2000（表中 4001 的跨度只是 3 s 轮询跨过了一次发布）；
- 首包被消费前的 `AppData 0/0` 不是 bug：`SensorIpcTask` 的预读（`freertos.c:183`）先于 CM4 发布执行，而那次发布的中断进来时 `s_notify_task` 可能还是 NULL。

> **`SensorIpcTask` 循环前的那次预读是承重的，不是装饰。** `sensor_ipc_boot()`（main.c:129）和 `sensor_ipc_attach()`（freertos.c:120）之间存在一个窗口，CM4 首包的中断完全可能落在这个窗口里，此时回调里的 `s_notify_task == NULL`，通知被丢——是这次预读把数据补上的。

## 9. 顺带量化出的另一个缺陷：CM4 的 HAL tick 慢约 1%

排查过程中发现的两个 tick 差值反过来暴露了一个真实缺陷 **【实测】**：

| 区间 | ΔCM7 tick（墙钟） | ΔCM4 tick | 差值 |
| --- | --- | --- | --- |
| S1→S2（2032 ms） | 2032 | 2010 | 22 ms |
| S2→S3（6030 ms） | 6030 | 5970 | 60 ms（3 个周期） |

**每个 DHT11 采样周期，CM4 的 tick 比墙钟少走 20~22 ms** —— 精确对应 `dht11_read()` 里那段 `__disable_irq()` 包住的 ~20 ms 起始信号 + 响应窗口。这段时间 TIM7 只能补触发一次，`uwTick` 只 +1。

后果有两条，面试时值得主动说：

1. **共享的 `timestamp` 是"CM4 tick 毫秒"，不是墙钟毫秒**，它比真实时间慢约 1%。上位机若拿它算采样率会偏；
2. **每个采样周期的真实耗时是 ~2010–2024 ms，不是名义的 2000 ms**（`osDelay(2000)` 等的是 tick）。

根因就是 Case 6 之外的那个老问题：`dht11_read()` 为了满足 DHT11 的微秒级时序，把**整个协议**关在 `__disable_irq()` 里（`app/CM4/BSP/Sensor/dht11.c:120-121`）。正确做法是用一个高优先级定时器/输入捕获去量脉宽，只在必要的几个微秒窗口关中断——这是个还没做的改进项。

## 10. 面试回答版本（口述版）

> 现象是 LCD 上温湿度恒为 0，复位多少次都一样，但 CM4 那边 DHT11 读数完全正常。
>
> 我用了三条手段，都是不停核的：一是 openocd 用 `mdw` 直接在目标全速运行时读内存和寄存器，全程不 halt；二是把双核各自的 HAL tick 当跨核时基，因为两边的 `bsp_log` 没有共同时间基准；三是整个上电过程放在**同一个 openocd 会话**里跑，靠 `sleep` 自计时——因为竞态窗口只有几百毫秒，分会话跑就把窗口跳过去了。
>
> 排查是分层的。先读共享内存，`0x38000000` 上 magic、valid、温湿度全对，说明数据没问题，范围砍到"通知"。然后读 HSEM 寄存器，`C1IER` 和 `C1ISR`，看懂了一件事：**HSEM 的中断条件是电平 `MISR & IER`，而 `HAL_HSEM_ActivateNotification()` 只写 `IER`、不清 `MISR`**；再翻 HAL 源码发现 `HAL_HSEM_IRQHandler()` 在回调用户函数**之前**就把 `IER` 清了，HAL 不会自动恢复。所以不 re-arm 的话，通知只送达一次。
>
> 但只有这一层解释不了"恒为 0"。我就去量时间轴：CM4 第一次发布的时间戳是 `ts=1`，只有 1 个 tick——因为 DHT11 整个协议是关中断跑的 20 ms，TIM7 只能补触发一次，所以 `ts=1` 意味着这次读**确实等了 20 ms**，而且发生在 CM4 tick 刚开始不到 1 ms 的时候。回头再看 CM7 的启动序列：放行 CM4 之后还要做 SDRAM、LCD 初始化，等 `MX_FREERTOS_Init()` 里才去 memset 共享区——**CM7 在删 CM4 刚写好的第一包数据**。两个核都调了同一个 `sensor_shared_reset()`。
>
> 这两层叠起来才刚好等于现场：CM7 收到的唯一那次通知，是 arm 的瞬间电平满足触发出来的，唤醒的任务读到的是刚被清零的结构，`valid==0` 于是不写温湿度，而之后再也不会有第二次唤醒。
>
> 修法是分开处理的。第一层在 `HAL_HSEM_FreeCallback` 里重新 `ActivateNotification`。第二层不是加 delay 去赌顺序，而是**把清零挪到一个因果上必然有序的位置**——CM4 是被 HSEM0 从 STOP 唤醒的，在 CM7 release HSEM0 之前它连 `HAL_Init()` 都没跑，所以在那之前清零就**可证明**早于 CM4 的任何发布。为此我把 `sensor_ipc_init()` 拆成 `sensor_ipc_boot()`（清零+武装）和 `sensor_ipc_attach()`（只挂任务句柄），清零只由 CM7 做一次，CM4 变成纯生产者。另外 arm 之前加了一句 `__HAL_HSEM_CLEAR_FLAG()`，保证收到的第一次通知一定来自一次真正的新 release。
>
> 验证下来：全 0 窗口没了，`ts=1` 的首包存活，首次上屏从 2.0 s 提前到 0.5~0.7 s，而且连续跑 15 s 穿过两个看门狗窗口不复位。顺带还量出 CM4 的 tick 因为那段关中断每周期慢 20 ms，约占 1%。
>
> 这个案例对我最大的两个提醒：一是**外设通知类中断基本都要 re-arm，而且要留意它是电平还是边沿触发**；二是**共享内存这类资源必须明确"谁初始化"**——两个核都 memset 同一块内存，就一定会产生一个几百毫秒的竞态窗口。

## 11. 可能的追问与回答

**问 1：为什么第一次发布会在 tick 1？刚上电就跑完一次 DHT11 读？**

> 因为 `ts` 是 `HAL_GetTick()`，而 DHT11 整个协议期间是 `__disable_irq()` 的。这 20 多毫秒里 TIM7 中断被挡住，只能在恢复中断的那一瞬间补触发一次，`uwTick` 只 +1。所以 `ts=1` 不代表"1 ms 读完"，而是"这次读确实等了 20 ms，且它发生在 CM4 tick 刚开始不到 1 ms 时"。这个 `1` 反过来是"读是真读"的证据。

**问 2：为什么不能只做一次 memset？CM7 做还是 CM4 做？**

> 做一次就够，关键是**在哪做、谁做**。必须 CM7 做、且在放行 CM4 之前做。因为"CM7 的启动序列早于 CM4 从 STOP 醒来"是硬件保证的，这条顺序不需要任何 delay 去赌；如果让 CM4 做，CM7 就可能在读一半的时候被 CM4 覆盖，而且 CM4 也无法保证在 CM7 第一次读之前完成清零。所有权归一之后，CM4 退化成纯生产者，这个竞态就从结构上消失了。

**问 3：为什么 arm 之前要先 `__HAL_HSEM_CLEAR_FLAG()`？**

> 因为 HSEM 的触发条件是电平 `MISR & IER`，而 `ActivateNotification()` 只置 `IER`、不清 `MISR`。如果 arm 的时候 `ISR` 里对应位还挂着（SRAM4 是复位保持的，`MISR` 也可能残留），`MISR` 会立刻非 0，当场产生一次"空"中断，CM7 会白白消费掉一次通知、拿到的还是刚清零的数据。先清状态位再 arm，第一次收到通知就一定对应一次真正的新 release。

**问 4：`ts=1` 那次发布，CM7 为什么没立刻显示？**

> 因为 `sensor_ipc_boot()`（武装通知）和 `sensor_ipc_attach()`（挂任务句柄）之间存在一个窗口。CM4 首包的中断如果落在这个窗口里，回调里 `s_notify_task` 还是 NULL，通知就被丢了。所以 `SensorIpcTask` 进循环之前有一次**预读**（`freertos.c:183`），把可能丢掉的这一次补上——这次预读是承重的，不是装饰。整个链路上有两个互补的兜底：句柄已挂好时靠 task notification（FreeRTOS 的 notification 会latch，任务还没阻塞也能收到），句柄没挂好时靠预读。

**问 5：这个 bug 如果换你重做设计，怎么从结构上避免？**

> 三点。一是**单一所有权**：共享块的初始化只能有一个核负责，另一个核只写自己的字段；二是**启动顺序要显式化**——把"初始化共享资源"这件事挂在"放行对端内核"之前，而不是散落在 `MX_FREERTOS_Init()` 里，它的位置本身就承载了语义；三是**生产者要有个"我起来了"的标志**，让消费者能区分"还没有数据"和"数据是 0"。当前结构里的 `SENSOR_SHARED_MAGIC` 本来就是这个用途，但全工程没有一处读它，形同虚设——这属于"设计了机制但没接上"的典型。

**问 6：SRAM4 是复位保持的，这对这个 bug 有什么影响？**

> 两重影响。一是**好的一面**：它在复位后保留内容，所以如果不清零，CM7 有可能读到上一次运行的残留数据——这也是为什么不清零不行。二是**坏的一面**：它让 bug 的表现带上了"历史相关"的味道——`MISR` 等状态位、共享区里的残留值都可能跨复位存活，所以 `sensor_ipc_boot()` 里既清零又清 `MISR`，两件事都要做，缺一个就会读到"上一次运行留下的东西"。

---

# Case 2：UART OTA 轮询导致 115200 丢包

## 1. 背景

OTA 传输从 W5500 TCP 迁移到 USART6（PG9 RX / PG14 TX，AF7），波特率 115200、8N1、无硬件流控。设备侧由 OTA 任务接收字节流、走 SOIP 协议解析 + 状态机写 Flash。

## 2. 异常现象

- 115200 下 OTA 持续丢字节：帧 CRC 校验失败、上位机频繁重传；
- 低速下勉强可用，波特率一提就恶化；
- 表现为"高波特率反而升级更慢、更容易失败"。

## 3. 排查思路

1. **现象分析** —— 丢的是"字节流"不是"整帧"，且随波特率升高恶化，指向接收搬运能力不足，而非协议本身。
2. **假设 1：CPU 没来得及取字节** —— 原始实现是**逐字节 `HAL_UART_Receive` 轮询 + `osDelay` 睡眠**；每字节都要一次函数调用和一次任务睡眠调度，115200 下每字符约 87µs，任务调度间隙根本来不及逐字节搬运。
3. **验证** —— 在 RX 溢出标志位（ORE）和接收错误计数上打点，确认存在溢出；对照时间轴确认丢包发生在 OTA 任务被调度走的窗口内。
4. **假设 2：接收与协议处理耦合** —— 接收路径里同时做"搬字节"和"解析协议"，两者节奏互相拖累。
5. **缩小范围 → 定位根因** —— 结论：接收搬运与协议处理未解耦，且逐字节轮询在 115200 下跟不上。

## 4. 关键证据

- **源码位置（新实现）**：
  - `app/BSP/OTA/bsp_ota_uart.c:187-211`（`HAL_UARTEx_RxEventCallback`：invalidate → 入 ring → notify → re-arm）
  - `app/BSP/OTA/bsp_ota_uart.c:31-32`（DMA 缓冲 768B，`.dma_buffer` 段、32 字节对齐）
  - `app/BSP/OTA/bsp_ota_uart.c:22-23`（环形缓冲 2048B，2 的幂 + 掩码取模）
  - `app/CM7/Core/Src/usart.c:378-393`（DMA1 Stream2 = USART6 RX）、`usart.c:414`（USART6 IRQ 优先级 5）
  - `app/BSP/OTA/bsp_ota_conf.h:35`（`BSP_OTA_RX_BUF_SIZE` = 768）
- **日志**：`OTA-UART` tag 的 "DMA RX armed" / "first RX byte drained from ring"。
- **调试工具**：USART 溢出标志位、串口日志、上位机 CRC 失败计数。

## 5. 根因分析

逐字节 `HAL_UART_Receive` 把"实时搬运字节"和"协议处理"耦合在同一任务里，且每个字节都伴随一次 `osDelay` 调度；115200 下每字符仅约 87µs，任务一旦被切走，RX 数据寄存器来不及被读走就产生溢出（ORE），字节丢失后帧 CRC 必然失败。

## 6. 修复方案

改为 DMA + 环形缓冲 + 事件通知的异步接收：

1. `HAL_UARTEx_ReceiveToIdle_DMA` 循环接收（IDLE 或 DMA 满时触发回调）；
2. 回调里先 `SCB_InvalidateDCache_by_Addr` 失效 D-Cache，再把数据推入 2048B SPSC 环形缓冲；
3. `vTaskNotifyGiveFromISR` 唤醒 OTA 任务，任务从 ring 取字节做协议解析；
4. 回调末尾**立即重新 arm DMA**，消除接收空窗。

接收搬运（ISR/DMA 上下文）与协议解析（任务上下文）彻底解耦。

## 7. 面试回答版本

> 原方案是逐字节 `HAL_UART_Receive` 加 `osDelay`，115200 下每字符才 87µs，任务一被切走就溢出丢字节，帧 CRC 全失败。我改成 `ReceiveToIdle_DMA` 循环接收，DMA 写 768 字节缓冲，回调里先 invalidate D-Cache 再把数据推进 2048 字节环形缓冲，用 task notification 唤醒 OTA 任务做解析，回调末尾立刻重新 arm DMA。核心是"实时搬运"和"协议处理"解耦，同时解决 DMA 与 CPU 的 cache 一致性问题。

---

# Case 3：OTA 期间 Watchdog 误判

## 1. 背景

软件层 watchdog 用"任务心跳计数"监控各任务：每个任务周期 `BSP_Watchdog_Kick()` 递增自己的 counter，watchdog 任务比较 counter 是否有进展，停滞超时则判定死机、停止喂 IWDG 触发硬件复位。

OTA/Flash 擦写是长操作，期间相关任务会暂停，为避免误判，引入了"长操作"机制让 watchdog 在长操作期间放宽监控。

## 2. 异常现象

- 早期 OTA 场景下，系统出现约 47 秒"该复位却不复位"的异常；
- 长操作期间 watchdog 状态混乱：一个子系统退出长操作时，把另一个仍在进行的长操作标志也清掉了（或标志卡住导致永远不退出长操作模式）。

## 3. 排查思路

1. **现象分析** —— 47 秒不喂狗却不复位，说明 IWDG 仍在被喂，问题在软件层的"要不要喂"判断。
2. **假设 1：长操作标志被错误清除** —— 长操作用的是**布尔标志**，多个调用者（OTA / W5500 / ESP8266）各自 `SetLongOp(1/0)`，一个退出会覆盖另一个的进入状态。
3. **验证** —— 在 `BSP_Watchdog_SetLongOp` 打点，观察多模块嵌套调用时标志的翻转序列，确认存在"A 退出把 B 的 1 清成 0"的覆盖。
4. **缩小范围 → 定位根因** —— 布尔标志无法表达"多个子系统并发持有"，需要计数语义。

## 4. 关键证据

- **源码位置**：
  - `app/BSP/bsp_watchdog.c:49`（`s_long_op_active`，注释明确"OTA / W5500 / ESP8266 各自 enter/exit 互不覆盖"）
  - `app/BSP/bsp_watchdog.c:105-117`（`BSP_Watchdog_SetLongOp`：临界区内的自增/自减）
  - `app/BSP/bsp_watchdog.c:200-204`（长操作期间仅监控 W5500 槽）
  - `app/BSP/bsp_watchdog.h:47`（接口文档：引用计数语义）
- **日志**：`WDG` tag 的 slot 状态打印（`WatchdogLogSlotStatus`）。

## 5. 根因分析

布尔标志只能表达"有/无长操作"，无法表达"有几个子系统在长操作"。当 OTA 和 W5500 同时持有长操作时，其中一个子系统退出就会把共享的布尔值从 1 清成 0，导致 watchdog 误以为长操作结束、恢复全量监控——或者反过来标志被卡在 1，使 watchdog 长期处于"仅监控 W5500"的宽松模式，一直喂 IWDG，出现"该复位不复位"。

## 6. 修复方案

把布尔标志改为**引用计数** `s_long_op_active`：`active!=0` 时自增、`active==0` 时自减，用 `__disable_irq()` / 恢复 PRIMASK 做临界区保护读-改-写。只要计数 > 0 就仍处于长操作；多子系统并发持有时互不覆盖。

## 7. 面试回答版本

> 看门狗靠任务心跳计数判断卡死，但 OTA 长操作期间任务会暂停，就加了长操作豁免。结果用的是布尔标志，多个模块（OTA、W5500、ESP8266）同时持有长操作时，一个模块退出会把共享的 1 清成 0，标志互相覆盖，出现该复位不复位。修复是把布尔改成引用计数，enter 加一、exit 减一，再用关中断做临界区保护，多模块并发持有就互不干扰了。这个案例说明：跨模块共享的"状态"要么用计数，要么必须有明确的所有权。

---

# Case 4：CM4 迁移后 DHT11 watchdog 失效

## 1. 背景

DHT11 采样原本在 CM7 上，随单核任务一起被 CM7 的软件 watchdog 监控。后来为隔离"微秒级时序 + 关中断忙等"的采集逻辑，把 DHT11 下沉到 CM4，CM4 通过 Shared SRAM + HSEM 上报数据。

## 2. 异常现象

- 迁移后，CM7 的软件 watchdog 里**DHT11 对应的监控槽不再被更新**；
- 如果 CM4 真死机（不再发布数据），CM7 侧的 watchdog 无法及时发现——监控对象"跟着任务一起搬走了"，但监控主体没跟着迁。

## 3. 排查思路

1. **现象分析** —— 迁移前 CM7 任务直接 kick 自己的 DHT11 槽；迁移后 CM7 不再跑 DHT11，原来 kick 的代码跟着没了，槽位变成"死槽"。
2. **假设 1：监控主体没有迁移** —— DHT11 采集迁到 CM4，但监控逻辑（CM7 的 watchdog 槽）仍按"CM7 本地任务"理解。
3. **验证** —— 查看 watchdog 槽位表：新增了 `BSP_WDG_CM4` 槽（超时 10000ms），但没有任何代码在 CM4 侧直接 kick 它。
4. **缩小范围 → 定位根因** —— 关键是要重新定义"CM4 健康"的观测点：CM4 每次发布（无论读数成功与否）都会通过 HSEM 通知 CM7，CM7 收到通知即可视为"CM4 还活着"。

## 4. 关键证据

- **源码位置**：
  - `app/BSP/bsp_watchdog.c:33`（`BSP_WDG_CM4` 槽超时 10000ms）
  - `app/Common/Src/sensor_shared.c:100-116`（CM7 收到 HSEM 通知的回调，此处顺带 kick CM4 槽）
  - `app/Common/Src/sensor_shared.c:52-71`（CM4 发布失败也照发，`valid` 区分在线/离线）
- **日志**：`WDG` tag 的 `cm4` 槽 stale 计数。
- **调试工具**：串口日志 + 断点观察 HSEM 回调是否触发 kick。

## 5. 根因分析

DHT11 采集迁移到 CM4 后，"采集任务是否存活"不再由 CM7 本地任务表达，而 CM7 的 watchdog 槽位仍沿用旧思路（本地任务 kick），导致监控断链。真正的观测点应该是"CM7 是否持续收到 CM4 的 HSEM 发布"——这是 CM4 健康在 CM7 侧的代理信号。

## 6. 修复方案

新增 `BSP_WDG_CM4` 槽（超时 10000ms，约 5 个 DHT11 采样周期容错）：

1. CM7 的 `SensorIpcTask` / HSEM 回调收到 CM4 发布时，kick `BSP_WDG_CM4` 槽；
2. 启动时**预置一次**该槽（防止 CM4 从未启动时被永久忽略，让 watchdog 在收不到任何发布时也能在超时后判死核）；
3. CM4 读失败也照常发布（`valid=0`），所以心跳反映"CM4 是否在发布"，而不是"读数是否成功"。

## 7. 面试回答版本

> DHT11 原来在 CM7，watchdog 直接监控本地任务。迁到 CM4 后，采集任务没了，但监控槽还按本地任务理解，结果断链。我把监控改成"CM4 心跳"：CM4 每次发布数据都会用 HSEM 通知 CM7，CM7 收到通知就 kick 一个 CM4 槽（超时 10 秒，留 5 个采样周期容错），启动时还预置一次防止 CM4 从没起来。这样无论读数成不成功，只要 CM4 还在发布就代表核还活着。关键点：跨核监控要找一个在监控侧能观测到的代理信号。

---

# Case 5：IWDG 不复位排查

## 1. 背景

软件 watchdog 判定某任务卡死后会**停止喂 IWDG**，期待约 8 秒后硬件复位。IWDG 配置：LSI 时钟，PR=128、RL=2000、WIN=4095，理论超时 `128 × 2000 / 32000 ≈ 8s`。

## 2. 异常现象

- 出现"停止喂狗后长时间不复位"的观测（早期观察到约 47 秒）；
- 需要确认到底是 IWDG 硬件没工作，还是软件根本没真正"停止喂狗"。

## 3. 排查思路

1. **现象分析** —— "停喂却不复位"有两种可能：IWDG 没在跑（时钟/配置/调试冻结），或软件仍在偷偷喂狗。
2. **假设 1：LSI 没起振** —— 读 `RCC->CSR` 的 `LSION`/`LSIRDY` 位。
3. **假设 2：IWDG 参数没写进去** —— 读 `IWDG1->SR` 的 `PVU`/`RVU`/`WVU` 更新标志（全 0 表示写入完成）。
4. **假设 3：调试器冻结了 IWDG** —— 读 `DBGMCU->APB4FZ1` 的 `DBG_IWDG1_STOP` 位（1 表示调试模式冻结 IWDG），并用 `DHCSR`（`0xE000EDF0`）的 `C_DEBUGEN`/`S_HALT` 判断核心是否真的被暂停。
5. **缩小范围 → 定位根因** —— 寄存器诊断全部正常（LSI 就绪、参数已写入、未冻结），做 selftest 确认约 8s 复位确实生效。结论：IWDG 本身没问题，之前的"47 秒不复位"是软件层长操作布尔标志被覆盖（见 Case 3）导致 watchdog 一直处于长操作模式持续喂狗，不是 IWDG 硬件故障。

## 4. 关键证据

- **源码位置（排障诊断，已合入初始化）**：
  - `app/CM7/Core/Src/iwdg.c:41-45`（PR=128 / RL=2000 / WIN=4095）
  - `app/CM7/Core/Src/iwdg.c:56-79`（RCC->CSR / IWDG->SR / DBGMCU->APB4FZ1 / DHCSR 诊断日志）
- **寄存器**：
  - `RCC->CSR`：`LSION`、`LSIRDY`
  - `IWDG1->SR`：`PVU`/`RVU`/`WVU`
  - `DBGMCU->APB4FZ1`：`DBG_IWDG1_STOP`
  - `DHCSR`（`0xE000EDF0`）：`C_DEBUGEN`(bit0)、`S_HALT`(bit17)
- **日志**：`IWDG` tag 的 `DIAG:` 系列输出。
- **调试工具**：寄存器观察窗口、selftest（主动停喂后计时）。

## 5. 根因分析

IWDG 硬件本身健康（LSI 起振、参数写入完成、未被调试器冻结），约 8s 复位经 selftest 验证有效。之前"停止喂狗却不复位"的根因不在 IWDG，而在软件 watchdog 的长操作标志被布尔值覆盖（Case 3），导致 watchdog 长期处于"仅监控 W5500"的宽松模式、继续喂 IWDG。这是典型的"现象在硬件层、根因在软件层"的排查。

## 6. 修复方案

1. 把长操作布尔标志改为引用计数（修复真正根因，见 Case 3）；
2. 在 IWDG 初始化里保留这段寄存器诊断日志，便于复现"不复位"时一眼区分"硬件没跑"还是"软件还在喂"。

## 7. 面试回答版本

> 现象是停喂狗却不复位。我没有瞎猜，先把 IWDG 的四个关键点逐一排除：读 RCC->CSR 看 LSI 是否起振、读 IWDG->SR 看参数写没写完、读 DBGMCU->APB4FZ1 看是不是调试器把 IWDG 冻结了、读 DHCSR 看核心是不是真被 halt。寄存器全正常，selftest 也确认 8 秒复位有效。那根因就回到软件层：长操作标志是个布尔值，被多模块覆盖导致 watchdog 一直处在宽松模式继续喂狗。这个案例的教训是——先分清现象出在硬件层还是软件层，再下手。

---

# Case 6：双核 Cache 一致性问题分析

## 1. 背景

CM7 是 Cortex-M7，带 D-Cache（数据缓存）；CM4 是 Cortex-M4，无数据缓存。CM4 写共享传感器数据到 Shared SRAM（`0x38000000`），再通过 HSEM 通知 CM7 读取。涉及"两个核 + 一个共享内存 + 一个带缓存的核"。

> 本案例重在"分析"，不是"已发生过的 Bug"。**下面严格区分「当前工程已实现机制」与「理论可优化方案」**，不把未实现方案描述成当前实现。

## 2. 需要回答的核心问题

1. 为什么 Shared SRAM 需要考虑 Cache 一致性？
2. HSEM 为什么不能解决 Cache 问题？
3. 当前项目采用什么方案？
4. 同步问题 vs 数据一致性问题怎么区分？
5. 单向通信为什么只需 invalidate？反向通信为什么要 clean？
6. 面试追问：为什么不用 MPU 配 non-cacheable？为什么不用关闭 Cache？两个核同时写怎么办？

## 3. 逐条分析

### 3.1 为什么 Shared SRAM 需要考虑 Cache 一致性

CM7 的 D-Cache 会把 CPU 读过的内存数据缓存一份在 core 内部。如果 Shared SRAM 被配成可缓存（cacheable），CM7 第一次读共享数据后，该数据就进了 D-Cache；此后 CM4 直接改写了 SRAM 里的新值，CM7 若再次读，命中的是 D-Cache 里的**旧值**，看不到 CM4 的新数据。这就是"共享内存 + 带缓存核"必须面对的 Cache 一致性问题。

### 3.2 HSEM 为什么不能解决 Cache 问题

HSEM 是硬件信号量/互斥，本质只做两件事：**互斥**（同一时刻只有一个核持有）和**事件通知**（release 触发对侧中断）。它保证的是"读写顺序/原子性"，但它**不碰 Cache 行**，不执行 clean/invalidate。也就是说：HSEM 能保证"CM4 写完了、通知 CM7 去读"这个顺序，但 CM7 读到的是 Cache 里的旧数据还是 SRAM 里的新数据，HSEM 管不了。**同步 ≠ 一致**。

### 3.3 当前项目采用的方案（已实现）

当前工程在 CM7 侧用 **MPU 把 `0x38000000`（Shared SRAM，64KB）配成 non-cacheable + non-bufferable**：

- `app/BSP/MPU/mpu.c:61-63`（区域 4：`0x38000000`、64KB、`MPU_ACCESS_NOT_CACHEABLE`、`MPU_ACCESS_NOT_BUFFERABLE`）

于是共享 IPC 路径的实际数据流是：

```text
CM4 写共享数据（写 SRAM 物理内存）
  -> CM4 release HSEM2 通知 CM7
  -> CM7 收到通知，直接读 0x38000000 物理内存（该区域 non-cacheable，无缓存参与）
```

**关键澄清**：因为该区域被 MPU 配成 non-cacheable，CM7 读共享数据时**根本不会命中缓存、也不需要显式 invalidate**。当前工程的共享 IPC 读路径（`sensor_shared_read`）没有 `SCB_InvalidateDCache_by_Addr` 调用。

而"读取前 cache invalidate"这个动作，当前工程里确实存在，但用在了**另一个可缓存区域**——UART OTA 的 DMA 接收缓冲（位于可缓存的 AXI SRAM `0x24000000`）：

- `app/BSP/OTA/bsp_ota_uart.c:196`：DMA 写物理内存后，CPU 侧先 `SCB_InvalidateDCache_by_Addr` 再读取。

这是两处不同内存的不同处理方式，面试时务必分清：

| 内存区域 | 地址 | MPU 属性（mpu.c） | Cache 处理方式 |
| --- | --- | --- | --- |
| Shared SRAM（IPC） | `0x38000000` | **non-cacheable** | 无需 invalidate，直接读 |
| AXI SRAM（UART DMA 缓冲） | `0x24000000` | cacheable + bufferable | DMA 写后显式 `SCB_InvalidateDCache_by_Addr` |

### 3.4 同步问题 vs 数据一致性问题

这是两个正交的问题：

- **同步问题**（谁先谁后、会不会读写冲突）：由 HSEM 解决。HSEM1 做数据互斥，HSEM2 做事件通知，保证"CM4 写完整、CM7 才开始读"。
- **数据一致性问题**（读到的是不是最新值）：由 Cache 策略解决（non-cacheable 或显式 clean/invalidate）。

只用 HSEM 能保证顺序，但保证不了 CM7 读到的值是最新的；只用 non-cacheable 能保证值最新，但保证不了两个核不会同时读写。所以两者都要有。

### 3.5 单向通信为什么只需 invalidate；反向通信为什么要 clean

这是"CPU 是生产者还是消费者"决定的：

- **单向（CM4 → CM7，DMA 也是这个方向）**：CM4/DMA 是生产者写内存，CM7 是消费者读内存。对 CM7 而言，风险是"自己的 Cache 里有旧数据"，处理方式是 **invalidate**（丢弃 Cache 里的旧行，强制从内存重读）。
- **反向（CM7 → CM4）**：CM7 是生产者，写进 Cache 后数据可能还没回写内存（脏行）；CM4 读内存只能读到旧值。处理方式是 **clean**（把 Cache 里的脏数据写回内存），必要时再 invalidate。

一句话：**读之前 invalidate（防读到旧值），写之后 clean（防别人读不到新值）。**

当前工程是单向 CM4→CM7，所以 IPC 只涉及"读侧一致性"；又因为用 non-cacheable 直接规避了读侧缓存，因此连 invalidate 都不需要。

### 3.6 面试追问与回答

**问 1：为什么不用 MPU 配 non-cacheable？**

> 实际上这个工程就是用的 MPU non-cacheable（`0x38000000` 配成 NOT_CACHEABLE）。要答清楚它的**代价**：non-cacheable 让每次读都走总线、没有缓存加速，频繁访问该区域会变慢；共享数据量小、访问频率低时这个代价可忽略，是最简单的方案。反过来，如果共享数据量大、访问频繁，non-cacheable 的吞吐损失就值得改用"cacheable + 显式 clean/invalidate"。

**问 2：为什么不用关闭 Cache？**

> 关掉整个 D-Cache 是"杀鸡用牛刀"：CM7 的 D-Cache 对 AXI SRAM 里的应用数据、SDRAM framebuffer 的性能提升很大，关掉会让整个系统变慢。正确做法是**按区域**处理：只对需要一致性的共享区域做 non-cacheable（或局部 clean/invalidate），其他区域保持 cacheable。MPU 的分区能力就是用来干这个的。

**问 3：两个核同时写怎么办？**

> 这是**同步问题**，不是 Cache 问题，用 HSEM1 互斥锁解决：写方先 take 锁、写完 release，读方读前 take、读完 release（当前 `sensor_shared_publish`/`sensor_shared_read` 就是这么做的）。更深一层，如果追求无锁读，可以用 seqlock（版本号）：写前递增奇数序号、写后递增偶数序号，读者读前后比对序号判断是否被并发写撕裂。但要注意，HSEM 保证的是"逻辑原子性"，Cache 保证的是"读到最新值"，两者要一起考虑，不能互相替代。

## 4. 一句话总结（面试）

> 共享内存 + 带缓存的 CM7 必然有 Cache 一致性问题；HSEM 只解决同步（顺序/互斥），不解决一致（旧值）。当前工程用 MPU 把 `0x38000000` 配成 non-cacheable 直接规避，所以 IPC 读路径不需要显式 invalidate；而可缓存的 AXI SRAM 里的 UART DMA 缓冲才需要 `SCB_InvalidateDCache_by_Addr`。单向读只需 invalidate，反向写才要 clean。关 Cache 是全局性能损失，MPU 分区才是精准做法；两个核同时写用 HSEM 锁解决，是无锁前最直接的方案。

---

> 完。以上 6 个案例全部来自当前工程实现与排查记录。Case 1 的两层修复均已合入源码；Case 6 严格区分「已实现（MPU non-cacheable + UART DMA invalidate）」与「理论方案（cacheable + 显式 clean/invalidate、seqlock 无锁读）」。
>
> Case 1 遗留、尚未处理的项（面试若被追问可如实说"已定位、未修"）：
> 1. `dht11_read()` 用 `__disable_irq()` 包住整个 ~20 ms 协议，导致 CM4 的 HAL tick 每周期少走 20~22 ms（实测约 1%），共享 `timestamp` 因此不是墙钟毫秒；
> 2. `SENSOR_SHARED_MAGIC` 全工程无人校验，是"设计了机制但没接上"的死字段；
> 3. `sensor_shared_publish()` 整体拷贝结构体，`valid` 后 3 字节填充（实测 `0xA5`）随之泄漏到共享区；
> 4. `sensor_shared_reset()` 由 CM7 单方拥有，但 `sensor_ipc_boot()` 现在跑在 `mpu_memory_protection()`（main.c:175）之前——当前 D-Cache 未开所以无害，将来开 D-Cache 需要重新审这个顺序。
