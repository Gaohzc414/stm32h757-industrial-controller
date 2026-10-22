# Bootloader 与 OTA 审计

## 结论范围

本文按当前源码描述 Bootloader、CM7 OTA BSP 和 Qt Host 的实际路径。Bootloader、OTA 协议、Flash 驱动和 Qt Host 均未修改。

当前结论：

- 有 Bootloader、Slot A、Slot B 三段相关布局；
- Slot B 作为 OTA 下载缓冲，Bootloader 将 B 拷贝到 A；
- 最终执行入口固定为 Slot A；
- 源码存在 `boot_trial` 和失败处理接口，但不能把当前实现描述成 Slot A/Slot B 直接切换执行的成熟 rollback 系统；
- CM7/CM4 应用构建通过，但没有板级 OTA 完整升级日志。

## Flash 与槽位

### 地址布局

- Bootloader：`0x08000000`，`128K`；
- Slot A：`0x08020000`，`448K`；
- Slot B：`0x08090000`，`448K`；
- CM4 应用：`0x08100000` 起。

来源：

- `bootloader/Makefile/CM7/stm32h747xx_flash_CM7.ld:43-49`
- `bootloader/Common/Inc/boot_slot.h:6-19`
- `app/Makefile/CM4/stm32h747xx_flash_CM4.ld:43-49`

按 STM32H757 双 Bank 地址范围整理：

| Bank | 地址范围 | 当前用途 |
| --- | --- | --- |
| Bank1 | `0x08000000` - `0x080FFFFF` | Bootloader `128K`、CM7 Slot A `448K`、CM7 Slot B `448K` |
| Bank2 | `0x08100000` - `0x081FFFFF` | CM4 firmware，链接脚本预留 `1024K` |

这是工程链接脚本和槽位常量形成的逻辑布局；仓库没有提供板级烧录器配置或芯片 option byte dump，因此不能进一步推断实际量产烧录状态。

Slot footer 固定为 32 字节，包含 magic、镜像大小、CRC32、版本和保留字段：

- `bootloader/Common/Inc/boot_slot.h:21-30`

## Boot 流程

### 1. Reset 与 Bootloader 入口

Bootloader 的入口是 `main()`，链接脚本入口符号为 `Reset_Handler`：

- `bootloader/Makefile/CM7/stm32h747xx_flash_CM7.ld:34-35`
- `bootloader/CM7/Core/Src/main.c:74-95`

Bootloader 初始化 GPIO、USART1，并输出启动诊断日志：

- `bootloader/CM7/Core/Src/main.c:111-121`

USART1 仅用于 Bootloader/Flash 调试输出，不能与应用 OTA USART6 混为同一路径。

### 2. Metadata 检查

Boot metadata 存放在 BKPSRAM 地址 `0x38800000`：

- `bootloader/Common/Src/boot_meta.c:12-16`

`boot_meta_load()` 检查 magic、版本和结构 CRC；无效时填充默认值：

- `bootloader/Common/Src/boot_meta.c:73-95`

metadata 结构包含 active slot、pending slot、trial 次数、flags、各槽 CRC/大小和自身 CRC：

- `bootloader/Common/Inc/boot_meta.h:13-23`

### 3. Slot B 检查与 B->A 拷贝

Bootloader 先读取 Slot A、Slot B 的有效性：

- `bootloader/CM7/Core/Src/main.c:123-138`

当 metadata 标记 pending Slot B，且 Slot B footer 有效时，Bootloader 执行 B->A 拷贝：

- `bootloader/CM7/Core/Src/main.c:141-155`

Slot B 有效性的核心条件是：

- footer magic 正确；
- image size 非零；
- image size 不超过槽容量减去 footer。

来源：`bootloader/Common/Src/boot_slot.c:78-90`

注意：`boot_slot_is_valid()` 当前主要验证向量和 footer 元数据，并没有在该函数中重新计算整段镜像 CRC。整镜像 CRC 校验发生在 OTA VERIFY 阶段，见下文。

### 4. APP 跳转

Bootloader 只调用：

```c
Boot_IsAppValid()
Boot_JumpToApp()
```

来源：`bootloader/CM7/Core/Src/main.c:157-180`

`Boot_IsAppValid()` 固定检查 `CM7_APP_SLOT_A_BASE`：

- `bootloader/CM7/Core/Src/boot_jump.c:24-27`

跳转前会 hold CM4、关闭中断和 SysTick、清理 NVIC、设置 MSP，然后跳转到 Slot A 的复位入口：

- `bootloader/CM7/Core/Src/boot_jump.c:29-63`

## OTA 设备侧流程

### 1. USART6 接收

应用 CM7 在启动时初始化 USART6：

- `app/CM7/Core/Src/main.c:158-160`

OTA UART BSP 使用 `huart6`：

- `app/BSP/OTA/bsp_ota_uart.c:10-19`

设备配置为 USART6 115200、8N1、无硬件流控：

- `app/CM7/Core/Src/usart.c:185-208`

接收使用 `HAL_UARTEx_ReceiveToIdle_DMA()`，DMA 缓冲位于 `.dma_buffer`：

- `app/BSP/OTA/bsp_ota_uart.c:30-32,97-109`

### 2. Ring Buffer 与协议解析

DMA 回调先失效 D-Cache，再将本次数据推入 2048 字节 SPSC ring，并通过 FreeRTOS task notification 唤醒 OTA 任务，最后重新武装 DMA：

- `app/BSP/OTA/bsp_ota_uart.c:187-210`

OTA 任务调用 `bsp_ota_manager_poll()` 处理 ring 中的数据：

- `app/BSP/OTA/bsp_ota_app.c:99-124`

协议解析和状态机仍在 `bsp_ota_manager.c`，本任务没有修改该模块。

### 3. OTA 状态机与 Flash

源码定义的状态顺序为：

```text
IDLE -> SESSION -> ERASING -> RECEIVING -> READY_VERIFY -> IDLE
```

来源：`app/BSP/OTA/bsp_ota_manager.c:37-44`

实际命令顺序由代码注释和 dispatch 确认：

```text
GET_INFO
BEGIN_SESSION
ERASE
START_TRANSFER
DATA*
END_TRANSFER
VERIFY
RESET
```

来源：

- `app/BSP/OTA/bsp_ota_manager.c:5-15`
- `app/BSP/OTA/bsp_ota_manager.c:686-717`

`BEGIN_SESSION` 选择当前运行槽的 inactive 槽，当前正常场景为 Slot B：

- `app/BSP/OTA/bsp_ota_manager.c:360-371`

`ERASE` 擦除目标槽，`DATA` 按 offset 写入，`END_TRANSFER` 检查写入总量：

- `app/BSP/OTA/bsp_ota_manager.c:412-463`
- `app/BSP/OTA/bsp_ota_manager.c:507-607`

Flash 写入禁止覆盖槽尾 footer 区：

- `app/BSP/OTA/bsp_ota_flash.c:249-270`

### 4. CRC32、footer 与 pending metadata

VERIFY 阶段直接对目标槽 Flash 映射地址计算 CRC32：

- `app/BSP/OTA/bsp_ota_flash.c:281-297`

CRC 成功后写入槽 footer，再在 BKPSRAM metadata 中设置 pending：

- `app/BSP/OTA/bsp_ota_manager.c:614-651`
- `app/BSP/OTA/bsp_ota_flash.c:307-324`

CRC 算法使用反射多项式 `0xEDB88320`，初值 `0xFFFFFFFF`，最终按位取反：

- `app/BSP/OTA/bsp_ota_crc.c:46-77`

### 5. 重启

RESET 命令发送应答后延时 50ms，再调用 `NVIC_SystemReset()`：

- `app/BSP/OTA/bsp_ota_manager.c:654-663`

## Slot A/B 与 rollback 判断

### 是否是 Slot A/B？

是。源码定义了 Slot A 和 Slot B，且两个槽都有 448KB 布局：

- `bootloader/Common/Inc/boot_slot.h:9-19`

### 是否有下载槽？

有。Slot B 由 OTA 正常流程作为 inactive 目标槽，Bootloader 对有效 Slot B 执行 B->A 拷贝：

- `app/BSP/OTA/bsp_ota_manager.c:360-371`
- `bootloader/CM7/Core/Src/main.c:143-155`

### 是否是真正的 A/B 直接启动切换？

不是当前源码表现。`boot_meta_select_boot_slot()` 固定返回 Slot A：

- `bootloader/Common/Src/boot_meta.c:115-120`

`Boot_IsAppValid()` 和 `Boot_JumpToApp()` 也固定使用 Slot A：

- `bootloader/CM7/Core/Src/boot_jump.c:24-27,66-69`

因此当前实现是：

```text
Slot B 下载
 -> CRC 校验
 -> footer + pending metadata
 -> Bootloader B->A 拷贝
 -> 从 Slot A 启动
```

### 是否真正 rollback？

源码具备 trial 字段及 `boot_meta_on_boot_failure()`：

- `bootloader/Common/Src/boot_meta.c:122-139`
- `bootloader/Common/Src/boot_meta.c:154-168`

但当前 Bootloader 主流程在 B->A 拷贝后仍从 A 启动，`boot_meta_commit_pending()` 也把 active slot 固定为 A：

- `bootloader/CM7/Core/Src/main.c:143-180`
- `bootloader/Common/Src/boot_meta.c:141-151`

因此审计结论是：存在 pending/trial 元数据和失败处理代码，但不能把当前系统描述为两个独立运行镜像之间的成熟运行时 rollback。

## Qt Host

Qt Host 的 `SerialManager` 使用 `QSerialPort`，支持端口枚举、115200/自定义波特率、8N1、无流控、readyRead 接收和相同协议帧解析：

- `tools/ota-host/transport/SerialManager.cpp:22-53`
- `tools/ota-host/transport/SerialManager.cpp:87-105`
- `tools/ota-host/transport/SerialManager.cpp:133-153`
- `tools/ota-host/OTA.pro:1-20`

Qt Host 的 UI 有 TCP/串口模式切换，串口模式会将 `UpdateEngine` 绑定到 `SerialManager`：

- `tools/ota-host/mainwindow.cpp:136-164`
- `tools/ota-host/mainwindow.cpp:341-370`
- `tools/ota-host/mainwindow.cpp:477-517`

所以当前 Qt Host 并非只能 TCP，PC 通过 USB-TTL 使用 Serial 路径是源码支持的。
