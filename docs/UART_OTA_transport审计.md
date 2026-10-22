# UART OTA transport 接入前置审计

## 结论摘要

当前 OTA 主流程仍是 W5500 TCP transport；`bsp_ota_uart.c` 只是基于 `huart1` 的阻塞式辅助读写，未接入 `bsp_ota_transport.c`。

结合开发板原理图和排针图，推荐后续优先使用 **USART6** 作为 UART OTA 通道，优先考虑 `PG14/PG9`，备选使用 `PC6/PC7` 复用引脚：

- USART1 已被 `printf`、日志和 OTA Flash trace 使用，不应复用。
- USART6 当前软件被 ESP8266 AT/TCP 使用，但本次场景没有 ESP8266 模块；UART OTA 模式下可以停用 ESP8266 软件链路。
- USART3 虽然当前已初始化且有 DMA，但工程使用的 `PC10/PC11` 在开发板上没有形成一组明确、完整的可接线 UART 排针对，因此不作为首选。

本次仅完成审计，未修改任何代码。

## 1. USART 资源分析

审计范围为应用 CM7，主要依据 `app/CM7/Core/Src/main.c`、`app/CM7/Core/Src/usart.c`、中断文件以及 BSP 对句柄的引用。

| UART | 当前初始化 | TX / RX | 当前用途 | 是否推荐 OTA |
|---|---|---|---|---|
| USART1 | 是，115200 8N1，`main.c` 初始化 | TX `PA9`，RX `PA10` | `printf`、`bsp_log`、`bsp_ota_flash` 调试输出；现有 `bsp_ota_uart.c` 也使用 `huart1` | **不推荐**。协议二进制数据与日志会互相污染，且修改会影响启动日志、故障日志和 Flash trace |
| USART3 | 是，115200 8N1，`main.c` 初始化 | 当前 TX `PC10`，RX `PC11` | 未发现应用/BSP 业务层占用；已配置 DMA1 Stream0/1。但原理图中 PC10 还标为 `SDMMC1_D2`，未看到与 PC11 配对的明确 UART 排针接口 | **不首选**。除非确认实物有 PC10 测试点或改用其他复用脚 |
| USART6 | 是，115200 8N1，`main.c` 初始化 | 当前 TX `PG14`，RX `PG9`；可复用 TX `PC6`、RX `PC7` | 软件上由 ESP8266 AT/TCP 使用；原理图将 `PG9/PG14` 引到 P1，`PC6/PC7` 也出现在排针 | **推荐（UART OTA 模式下）**。硬件引出清晰，但不能与 ESP8266 AT/TCP 同时使用 |

### 初始化与占用细节

- 三个 UART 都在 `app/CM7/Core/Src/main.c` 中初始化。
- USART1 的 `_write` / `fputc` 通过 `HAL_UART_Transmit(&huart1, ...)` 输出标准输出；日志模块和 OTA Flash 模块也直接发送到 `huart1`。
- USART6 由 ESP8266 AT 层接管 RX 中断，并将数据路由到 AT 行缓冲或 TCP 接收缓冲；启用 UART OTA 时必须停用这套 AT/TCP 接收路径。
- USART3 的 CubeMX 配置包含 TX/RX DMA，但当前没有发现 `HAL_UART_Receive_DMA`、`HAL_UARTEx_ReceiveToIdle_DMA` 或业务回调接入；此外其当前 PC10/PC11 板级引出不如 USART6 清晰。
- `app/CM7/Core/Src/stm32h7xx_it.c` 中 USART3/6 IRQ 当前只调用 HAL IRQ 处理，尚未存在 UART OTA RX 环形缓冲分发逻辑。

## 2. `bsp_ota_uart.c` 现状

当前已实现接口：

- `bsp_ota_uart_init(void)`：空实现，依赖外部 CubeMX 初始化。
- `bsp_ota_uart_read(uint8_t *buf, uint16_t cap, uint32_t timeout_ms)`：对 `huart1` 调用 `HAL_UART_Receive`，要求收满 `cap` 字节才返回成功；失败或超时返回 0。
- `bsp_ota_uart_write(const uint8_t *buf, uint16_t len)`：对 `huart1` 调用阻塞式 `HAL_UART_Transmit`，成功返回 0，失败返回 -1。

### 与 transport 层需求的差距

现有 `bsp_ota_transport.c` 暴露的契约是逐字节、非阻塞轮询为主：

- `bsp_ota_transport_ready()`：判断通道可用。
- `bsp_ota_transport_poll()`：把底层收到的数据搬入 pending/RX 缓冲。
- `bsp_ota_transport_try_read()`：非阻塞取出 1 字节。
- `bsp_ota_transport_wait_rx()`：等待收到数据或超时。
- `bsp_ota_transport_read()`：阻塞读 1 字节，目前 manager 主路径没有使用它。
- `bsp_ota_transport_write()`：发送完整 SOIP 帧。
- `bsp_ota_transport_write_once()`：指定超时的单次发送，目前语义属于 TCP 发送辅助接口。
- `bsp_ota_transport_recover_link()`：TCP 重连，目前是 W5500 特有能力。

因此，直接把当前 `bsp_ota_uart_read()` 替换进 transport 并不匹配：它是“收满 N 字节”的阻塞 API，而 manager 需要连续取得任意到达的字节并支持半帧、粘包和多帧。

后续 UART 适配至少需要以下行为：

1. USART6 RX 进入软件环形缓冲或等价 pending 缓冲。
2. `try_read` 每次最多取 1 字节、无数据立即返回，不阻塞。
3. `poll` 对 UART 可以是空操作，或负责 DMA 写指针同步；不能丢弃尚未解析的数据。
4. `wait_rx` 使用 RX 中断/DMA 配合 RTOS semaphore/thread flag，或采用有界短轮询；不能让 OTA 任务永久忙等。
5. `ready` 对 UART 表示串口已初始化并可用，不是 TCP 的连接状态。
6. TX 必须保证完整 SOIP 帧连续发送，不能与日志共享同一 UART。

`bsp_ota_uart.c` 当前使用 `huart1`，后续即使保留该文件名，也应改成可配置的 `huart6` 或抽出 UART adapter，避免把日志 UART 误作为 OTA UART。

## 3. OTA manager 与 protocol 的依赖

### manager 行为

`bsp_ota_manager_poll()` 的核心流程为：

1. 先调用 `bsp_ota_transport_ready()`；未就绪直接返回。
2. 如有待发送 DATA ACK，先尝试补发。
3. 循环调用 `bsp_ota_transport_try_read(&byte)`，直到无数据。
4. 每收到一个字节就追加到 manager 的 `s_rx_buf`。
5. 调用 `bsp_ota_try_parse()`，解析成功后分发 SOIP 命令；同一缓冲支持半帧、粘包和多帧。

`s_rx_buf` 大小为 `BSP_OTA_RX_BUF_SIZE`。缓冲满时当前策略是清空并等待上位机重传，因此 UART 适配必须尽量及时搬运数据，且不能静默覆盖未读数据。

### protocol 行为

`bsp_ota_protocol.c` 是字节流解析器，不关心底层是 TCP 还是 UART。它依赖：

- 字节顺序保持不变；
- 支持任意分段到达；
- 不插入日志、AT 响应或其他协议内容；
- 允许 SOF 前噪声、半帧、粘包和 CRC 错误后的重新同步。

因此 UART OTA 不需要修改 `bsp_ota_protocol.c`，也不需要重新设计 SOIP 帧格式。

### 是否必须阻塞读取、是否需要 RTOS 对象

- manager 主路径不要求 `transport_read()` 阻塞读取；实际使用的是 `try_read()`。
- OTA 任务在非 DATA 阶段调用 `wait_rx()` 降低 CPU 占用；UART 方案应提供对应的 RX 通知机制。
- RX 缓冲是必须的，至少需要 DMA 环形缓冲或 ISR 写入的软件环形缓冲，再由 OTA 任务消费。
- 中断或 DMA 不是协议层强制要求，但对 115200 baud 的连续固件传输是推荐实现；阻塞 `HAL_UART_Receive` 不适合作为 manager 的逐字节 transport 后端。
- FreeRTOS semaphore/thread flag 不是唯一实现，但当前 OTA 任务已有 `wait_rx` 抽象，使用 RX semaphore 或 thread flag 最容易保持任务调度方式不变。

## 4. 最小修改方案

建议保留 OTA manager、SOIP protocol、Flash 和上位机不变，只替换 transport 后端。

### 修改

- `app/BSP/OTA/bsp_ota_transport.c`：将底层调用从 W5500 TCP 切换到 UART adapter；实现 UART 语义下的 `ready`、`poll`、`try_read`、`wait_rx`、`write`。
- `app/BSP/OTA/bsp_ota_transport.h`：仅在需要区分或收敛 `write_once` / `recover_link` 语义时调整注释或接口；能保持现有接口则不改。
- `app/BSP/OTA/bsp_ota_uart.c` / `.h`：改为 USART3 专用实现，或把现有文件收敛成 adapter；补充 RX 缓冲、非阻塞取字节、RX 通知和串口就绪状态。
- `app/CM7/Core/Src/stm32h7xx_it.c`：若采用自定义 ISR 路由，在 USART3 IRQ 或 HAL 回调处把 RX 数据放入 UART OTA 缓冲并通知任务。
- `app/CM7/Core/Src/usart.c`：若采用 DMA 环形/Receive-to-Idle 方案，需要补充 USART3 的 RX 启动方式和对应配置；当前只是 DMA 句柄初始化，并没有开始接收。
- `app/BSP/OTA/bsp_ota_app.c`：当前 `BSP_Ota_Start()` 无条件创建 W5500 RX 通知，UART 模式下应改成 transport 通用初始化或条件编译；这是接入时容易遗漏的地方。
- 工程构建文件：确认 `bsp_ota_uart.c` 已纳入应用构建；若新增 adapter 文件，则加入对应 IDE/Makefile/CMake 工程。

### 保持不变

- `app/BSP/OTA/bsp_ota_manager.c`
- `app/BSP/OTA/bsp_ota_protocol.c/.h`
- OTA Flash、boot metadata、Bootloader
- Qt OTA Host 和 SOIP v2 帧格式

说明：如果继续保留 `bsp_ota_transport_write_once()` 和 `bsp_ota_transport_recover_link()` 这两个 TCP 专用接口，UART 后端可以分别映射为普通发送和“无动作/失败”；更干净的做法是让 transport 内部按后端条件处理，但不需要改 manager。

## 5. 硬件连接方案

以推荐的 USART6 默认引脚为例：

```text
USB-UART TX  ->  STM32 USART6_RX (PG9)
USB-UART RX  ->  STM32 USART6_TX (PG14)
USB-UART GND ->  STM32 GND
```

如果 PG9/PG14 在实物上不便接线，可改用 USART6 的另一组复用引脚：

```text
USB-UART TX  ->  STM32 USART6_RX (PC7)
USB-UART RX  ->  STM32 USART6_TX (PC6)
USB-UART GND ->  STM32 GND
```

串口参数应与固件一致，当前初始化值为 `115200, 8 data bits, no parity, 1 stop bit`。USB-UART 模块必须使用 **3.3 V TTL 电平**；不要把 RS-232 电平或 5 V 推挽信号直接接到 STM32 GPIO。模块的 VCC 是否需要连接取决于模块型号和板卡供电：

- 若模块由 USB 自供电且 IO 电平明确为 3.3 V，通常只连接 TX/RX/GND；
- 若模块需要外部供电，应按模块规格供电，不能仅依据 USB 端口电压假设；
- USB-UART 与 STM32 必须共地；仅连接 TX/RX 不足以保证可靠通信。

### 与 DAP-Link COM 并存

可以并存，前提是 DAP-Link COM 使用的不是同一组 USART 引脚，且不把两个发送端并联到同一 UART RX/TX 网络。推荐安排为：

- DAP-Link COM 继续连接 USART1，用于日志/printf；
- USB-UART OTA 连接 USART6，作为 UART OTA 通道；此时不连接或不启用 ESP8266。

这样既保留调试日志，又避免 OTA 二进制流被日志污染。实际接线仍应以开发板原理图和 DAP-Link 的具体引脚映射为最终依据。

## 最终推荐

选择 **USART6** 作为 OTA 专用串口。默认接线使用 `PG14(TX) / PG9(RX)`，也可评估 `PC6(TX) / PC7(RX)` 复用方案。第一阶段只实现 transport adapter 和 UART RX 缓冲/通知，不改 manager、protocol、Flash、Bootloader 或 Qt Host。USART1 保持日志用途；USART6 在 UART OTA 模式下不能同时运行 ESP8266 AT/TCP。
