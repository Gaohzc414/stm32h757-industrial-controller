# SOIP v2 — STM32 OTA/IAP 串口协议

上位机 `OTA_APP` 与 Bootloader 共用本协议。默认 UART：115200 8N1。

## 帧格式

| 偏移 | 长度 | 字段 |
|------|------|------|
| 0 | 2 | SOF：`0xAA 0x55` |
| 2 | 1 | VER：`0x02` |
| 3 | 1 | CMD |
| 4 | 2 | SEQ（小端） |
| 6 | 2 | LEN（载荷长度，小端） |
| 8 | N | PAYLOAD |
| 8+N | 2 | CRC16-CCITT |

**CRC16**：对 `[VER .. PAYLOAD 末字节]` 计算，多项式 `0x1021`，初值 `0xFFFF`，结果小端附加。

应答帧：`CMD_response = CMD_request | 0x80`，SEQ 与请求相同。

## 命令字

| 值 | 名称 | 说明 |
|----|------|------|
| 0x01 | PING | 链路检测 |
| 0x02 | GET_INFO | 读取设备信息 |
| 0x03 | BEGIN_SESSION | 建立升级会话 |
| 0x04 | ERASE | 按范围擦除 Flash |
| 0x05 | START_TRANSFER | 声明镜像大小与 CRC32 |
| 0x06 | DATA | 分块写入 |
| 0x07 | END_TRANSFER | 传输结束 |
| 0x08 | VERIFY | 全镜像 CRC32 校验 |
| 0x09 | RESET | 复位并跳转 APP |
| 0x0A | ABORT | 中止会话 |

## 状态码（载荷首字节）

| 值 | 含义 |
|----|------|
| 0x00 | OK |
| 0x01 | 帧 CRC 错误 |
| 0x02 | 未知命令 |
| 0x03 | 状态机错误 |
| 0x04 | Flash 操作失败 |
| 0x05 | 校验失败 |
| 0x06 | 超时 |
| 0x07 | 忙 |
| 0x08 | 参数错误 |
| 0x09 | 会话无效 |
| 0x0A | 已中止 |

## 会话安全

1. 主机 `BEGIN_SESSION` 发送 4 字节随机 `host_nonce`。
2. 设备应答 `status + session_token`（建议 `session_token = CRC32(host_nonce ^ device_uid)` 或简单混合）。
3. 后续 `ERASE/DATA/VERIFY/ABORT` 均携带 `session_token`；token 不匹配返回 `ERR_SESSION`。

## 载荷定义（小端）

### BEGIN_SESSION（请求）

- `host_nonce` u32

### BEGIN_SESSION（应答）

- `status` u8
- `session_token` u32

### GET_INFO（应答）

- `status` u8
- `name_len` u8 + `name[]`
- `chip_id` u32
- `boot_len` u8 + `boot_ver[]`
- `flash_size` u32
- `app_address` u32（如 `0x08020000`）
- `proto_len` u8 + `proto_str[]`（如 `"SOIP v2.0"`）

### ERASE（请求）

- `session_token` u32
- `flash_addr` u32
- `size` u32

### START_TRANSFER（请求）

- `session_token` u32
- `flash_addr` u32
- `image_size` u32
- `image_crc32` u32（IEEE CRC32，与上位机 `CrcUtils::crc32` 一致）

### DATA（请求）

- `session_token` u32
- `offset` u32
- `data[]`（W5500 以太网默认 256 字节；非最后一包须满 chunk）

### VERIFY（请求）

- `session_token` u32
- `image_crc32` u32

设备在 Flash 编程完成后对写入区域计算 CRC32，与请求比对。

## 推荐升级流程

```
GET_INFO → BEGIN_SESSION → ERASE → START_TRANSFER
  → DATA × N → END_TRANSFER → VERIFY → RESET
```

任意失败或用户停止：发送 `ABORT`，Bootloader 清除半完成状态，保持等待命令。

## 与工程 Flash 布局

与 `Bootloader/CM7/Core/Inc/boot_config.h` 一致：

- Bootloader：128KB @ `0x08000000`
- APP：`0x08020000`

## 上位机模块

| 路径 | 职责 |
|------|------|
| `protocol/OtaProtocol.*` | 组帧/解帧 |
| `protocol/CrcUtils.*` | CRC16/CRC32 |
| `transport/SerialManager.*` | 串口收发 |
| `engine/UpdateEngine.*` | 状态机、重试、中止 |

Bootloader 侧需在 `while(1)` 中实现帧解析与上述命令处理（当前仓库 Bootloader 尚未实现，需自行添加）。
