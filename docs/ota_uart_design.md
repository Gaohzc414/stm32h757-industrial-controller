# OTA UART 传输设计审计

## 设计结论

当前 OTA 设备侧已经从逐字节轮询路径切换为：

```text
USART6 FIFO
  -> DMA ReceiveToIdle
  -> DMA buffer
  -> SPSC ring buffer
  -> FreeRTOS task notification
  -> OTA manager poll / protocol parser
```

来源：

- `app/BSP/OTA/bsp_ota_uart.c:1-8`
- `app/BSP/OTA/bsp_ota_app.c:99-124`

源码已经编译进入 CM7 工程。没有板级串口抓包，因此“115200 长帧不丢字节”的效果仍需硬件验证。

## 原问题

原设计的问题在当前 UART BSP 文件头部已有明确记录：逐字节 `HAL_UART_Receive` 轮询加 `osDelay` 会让 CPU 在接收间隙无法及时取走数据，115200 下容易出现接收溢出或丢字节：

- `app/BSP/OTA/bsp_ota_uart.c:5-8`

这种模式还会把协议解析任务的调度延迟直接暴露给 UART 接收路径。OTA 帧是连续字节流，不能假设每次任务运行时恰好只到达一个字节。

## 修改后的接收路径

### USART6 FIFO

USART6 初始化启用 FIFO：

- `app/CM7/Core/Src/usart.c:197-223`

FIFO 提供外设侧的短暂硬件缓冲，降低 CPU 响应抖动对接收的直接影响。

### DMA ReceiveToIdle

OTA 初始化时调用：

```c
HAL_UARTEx_ReceiveToIdle_DMA(&huart6, s_dma_rx_buf, sizeof(s_dma_rx_buf));
```

来源：`app/BSP/OTA/bsp_ota_uart.c:97-109`

`ReceiveToIdle` 在两种情况触发回调：

- UART 检测到空闲线；
- DMA 缓冲区填满。

回调参数 `Size` 表示本轮实际收到的字节数：

- `app/BSP/OTA/bsp_ota_uart.c:179-187`

### DMA 缓冲区

DMA 缓冲区放在 `.dma_buffer` 段，32 字节对齐：

- `app/BSP/OTA/bsp_ota_uart.c:30-32`
- `app/Makefile/CM7/stm32h747xx_flash_CM7.ld:167-174`

USART6 DMA 使用：

- RX：DMA1 Stream2；
- TX：DMA1 Stream3。

来源：`app/CM7/Core/Src/usart.c:376-415`

### Cache 处理

DMA 直接写内存，CPU 读取前先失效 DMA 缓冲区对应 Cache：

- `app/BSP/OTA/bsp_ota_uart.c:195-198`

这一步与共享 SRAM IPC 不同：共享 IPC 区域通过 MPU 配置为不可缓存，而 UART DMA 缓冲区位于 CM7 AXI SRAM，需要回调中显式处理 D-Cache。

### SPSC Ring Buffer

Ring buffer 大小为 2048 字节，生产者是 DMA 回调，消费者是 OTA 任务：

- `app/BSP/OTA/bsp_ota_uart.c:21-28`

写入时使用 head/tail 差值判断满缓冲，满时丢弃剩余字节，并依赖帧 CRC 失败及主机重试恢复：

- `app/BSP/OTA/bsp_ota_uart.c:44-58`

任务侧一次或多次从 ring 中取字节：

- `app/BSP/OTA/bsp_ota_uart.c:66-78`

### Task Notification

DMA 回调向 OTA 任务发送 `vTaskNotifyGiveFromISR()`：

- `app/BSP/OTA/bsp_ota_uart.c:200-202`

OTA 任务通过 `bsp_ota_transport_wait_rx()` 进入通知等待，超时只用于周期性喂狗：

- `app/BSP/OTA/bsp_ota_uart.c:151-157`
- `app/BSP/OTA/bsp_ota_app.c:105-124`

这使“数据到达”成为唤醒条件，而不是固定周期轮询条件。

## 发送路径

当前 TX 没有使用 DMA，而是：

```c
HAL_UART_Transmit(&huart6, ...)
```

来源：`app/BSP/OTA/bsp_ota_uart.c:166-177`

因此准确表述应是“RX 使用 FIFO + DMA ReceiveToIdle + ring + notification；TX 仍为阻塞式 HAL 发送”。不能把当前实现概括成收发都 DMA。

## 传输层适配

`bsp_ota_transport.c` 不再依赖 W5500，直接映射 UART BSP：

- 初始化：`app/BSP/OTA/bsp_ota_transport.c:17-20`
- 启动 RX：`app/BSP/OTA/bsp_ota_transport.c:27-30`
- 等待 RX：`app/BSP/OTA/bsp_ota_transport.c:45-47`
- 读：`app/BSP/OTA/bsp_ota_transport.c:81-95`
- 写：`app/BSP/OTA/bsp_ota_transport.c:104-110`

UART 没有 TCP connected 状态，因此 `bsp_ota_transport_ready()` 固定返回 1：

- `app/BSP/OTA/bsp_ota_transport.c:65-72`

`bsp_ota_transport_poll()` 留空，因为 DMA 中断持续接收：

- `app/BSP/OTA/bsp_ota_transport.c:32-38`

## 为什么这套设计适合 115200

1. FIFO 吸收外设侧短时突发。
2. DMA 按块搬运，不需要 CPU 每个字节进入 HAL。
3. IDLE 事件让变长帧可以在一段字节流结束后尽快交给任务。
4. Ring buffer 将 ISR 接收和协议解析解耦。
5. Task notification 比周期性 `osDelay` 更快地唤醒 OTA 任务。
6. OTA manager 保持原协议和状态机不变，传输层只替换字节来源。

## 限制与待验证项

- Ring 满时会丢弃剩余字节：`app/BSP/OTA/bsp_ota_uart.c:51-53`。
- TX 仍是阻塞发送：`app/BSP/OTA/bsp_ota_uart.c:172`。
- DMA 回调重新 arm 失败时会清空通知任务句柄：`app/BSP/OTA/bsp_ota_uart.c:204-208`。
- 当前仓库没有串口压力测试结果或 USB-TTL 实机升级记录。

## Qt Host 兼容性

Qt Host 的 `SerialManager` 使用 `QSerialPort`，以 readyRead 收取字节、复用 `OtaProtocol::tryParseFrame()` 解析协议帧：

- `tools/ota-host/transport/SerialManager.cpp:7-10,123-153,213-217`

Qt 工程显式链接 `serialport` 模块：

- `tools/ota-host/OTA.pro:1`

设备端改为 USART6 UART 后，Qt Host 仍可使用现有 Serial 协议路径，无需修改协议格式。
