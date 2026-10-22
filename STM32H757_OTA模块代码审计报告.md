# STM32H757 OTA 模块代码审计报告

## 审计范围

- 仓库：/Users/gaohuangzichen/work/stm32/stm32h757-industrial-controller
- 重点目录：app/BSP/OTA/
- 关联代码：app/CM7、app/Common、app/BSP/W5500、bootloader、APP/Bootloader linker script
- 本报告只描述当前代码事实，不修改代码，不执行生成或测试。

## 1. 总体结论

当前实现是：APP 在线接收升级镜像，写入 inactive Slot B；APP 完成 CRC/footer/meta 后请求复位；Bootloader 下次启动把 Slot B 复制到固定运行 Slot A，再跳转 APP。
当前实际 OTA 网络链路是 W5500 TCP。UART 文件存在，但只是阻塞式辅助 API，没有接入 bsp_ota_transport.c 的默认数据路径。

已实现：SOIP v2 帧协议、CRC16 帧校验、CRC32 镜像校验、双槽逻辑、版本字段和 session token、pending/trial 元数据、Bootloader B->A 复制、部分 trial 失败处理、OTA 擦写期间 CM4 hold 和 peer task 协作。
未实现或当前代码未体现：数字签名、公钥/证书链验证、镜像加密、防回滚版本策略、掉电原子提交、真正的 A/B 独立启动槽切换。
重要事实：逻辑 Slot A/B 共享 Bank1 Sector 4，当前代码通过备份另一侧 64 KiB 后整扇区擦除再恢复来处理，存在掉电窗口。

## 2. OTA 架构和入口

APP CM7 的早期入口在 app/CM7/Core/Src/main.c：BSP_Ota_Init() 启动早期初始化 boot metadata 和 transport；BSP_Ota_OnBoot() 启动时处理 pending/trial 确认。
RTOS 入口在 app/CM7/Core/Src/freertos.c:144，调用 BSP_Ota_Start()。
BSP_Ota_Start() 位于 app/BSP/OTA/bsp_ota_app.c:37，创建 OtaTask：
    osThreadNew(OtaTask, NULL, &attr)
任务属性：名称 ota；栈 BSP_OTA_TASK_STACK_WORDS * 4，当前 8192 bytes；优先级 osPriorityRealtime。

调用关系：
    CM7 main -> BSP_Ota_Init -> boot_meta_hw_init / bsp_ota_transport_init
      -> BSP_Ota_OnBoot -> boot_meta_load / boot_meta_commit_pending
      -> freertos StartDefaultTask -> bsp_w5500_app_start -> BSP_Ota_Start
           -> osThreadNew(OtaTask)
                -> transport poll -> protocol parse -> ota_dispatch_frame
                     -> flash / CRC / metadata / response
peer task 通过 bsp_ota_coop_register() 注册，OTA 激活后由 bsp_ota_coop_suspend_peers() 挂起，结束后恢复。

## 3. 端到端升级流程

1. PING / GET_INFO：ota_handle_ping() 和 ota_handle_get_info()，位置 bsp_ota_manager.c:249、258。返回设备名、UID word0、协议字符串和 inactive 槽基址。
2. BEGIN_SESSION：ota_handle_begin_session()，位置 bsp_ota_manager.c:344。payload 前 4 bytes 为 host_nonce；生成 session_token；状态 IDLE -> SESSION，选择 inactive 槽。
3. ERASE：ota_handle_erase()，位置 bsp_ota_manager.c:413。验证 token 和地址，调用 bsp_ota_port_enter()、bsp_ota_flash_erase_slot()；状态 SESSION -> ERASING -> RECEIVING。
4. START_TRANSFER：ota_handle_start_transfer()，位置 bsp_ota_manager.c:470。payload 为 token、addr、image_size、image_crc32，检查地址和镜像容量。
5. DATA：ota_handle_data()，位置 bsp_ota_manager.c:508。payload 为 token、offset、chunk_data；最大分片 256 bytes；写入 boot_slot_base(target_slot) + offset；每包写后 ACK。
6. END_TRANSFER：ota_handle_end_transfer()，位置 bsp_ota_manager.c:593。只有 s_bytes_written == s_image_size 才进入 READY_VERIFY。
7. VERIFY：ota_handle_verify()，位置 bsp_ota_manager.c:615。CRC32 校验、写 footer、boot_meta_set_pending()，状态 READY_VERIFY -> IDLE。
8. RESET：ota_handle_reset()，位置 bsp_ota_manager.c:659。应答后延迟 50 ms，调用 NVIC_SystemReset()。

SOIP 帧定义在 bsp_ota_protocol.h:17-72：
    [0xAA][0x55][version][cmd][seq_lo][seq_hi]
    [payload_len_lo][payload_len_hi][payload...][crc16_lo][crc16_hi]
CRC16 覆盖 version 到 payload，初值 0xFFFF，多项式 0x1021。CRC32 由 bsp_ota_crc32() 用于镜像校验和 session token。

## 4. Flash 分区和数据位置

当前逻辑分区：
    0x08000000  Bootloader CM7 128 KiB
    0x08020000  CM7 APP Slot A 448 KiB
    0x08090000  CM7 APP Slot B 448 KiB
    0x08100000  CM4 firmware 1 MiB
常量位于 app/Common/Inc/boot_slot.h:10-17 和 bootloader/Common/Inc/boot_slot.h:10-17。
OTA DATA 写入 inactive 槽；槽尾最后 32 bytes 预留给 boot_slot_footer_t，位置由 bsp_ota_flash.c:322-324 计算。
metadata 位于 0x38800000，由 app/Common/Src/boot_meta.c 和 Bootloader 对应实现使用。

当前 H757XIH6 设备头文件定义 2 MB Flash、Bank1/Bank2 基址和 4 KiB Backup SRAM；因此上述地址设计与设备地址空间相符，但 A/B 共享 Bank1 Sector 4。

## 5. Bootloader 与 OTA 关系

当前是 APP 下载、Bootloader 下次启动处理的模式：
    APP -> 写 inactive Slot B -> CRC32 -> footer -> boot_meta_set_pending(B) -> reset
    Bootloader -> 检查 B footer -> boot_slot_copy_to_run_slot(B) -> 检查 Slot A -> Boot_JumpToApp
    APP -> BSP_Ota_OnBoot -> boot_meta_commit_pending()
Bootloader 主流程位于 bootloader/CM7/Core/Src/main.c:143-155。
boot_meta 字段包括 active_slot、pending_slot、boot_trial、flags、slot_size[]、slot_crc[] 和 crc32，定义在 app/Common/Inc/boot_meta.h。
当前 active 最终固定为 Slot A；这不是直接启动 Slot B 的真正 A/B 启动。
boot_meta_on_boot_failure() 存在并会递减 trial，但当前 Bootloader 主流程没有发现对它的实际调用。因此自动回滚只能评为代码存在、完整调用链证据不足。

## 6. 通信方式

当前 transport 固定映射 W5500 TCP：
    bsp_ota_transport_poll -> bsp_w5500_tcp_poll_rx
    bsp_ota_transport_try_read -> bsp_w5500_tcp_try_recv_byte
    bsp_ota_transport_write -> bsp_w5500_tcp_send
    bsp_ota_transport_recover_link -> bsp_w5500_tcp_recover_ota_link
freertos.c:142-144 先启动 W5500，再调用 BSP_Ota_Start()。W5500 OTA TCP 端口由 app/BSP/W5500/bsp_w5500_conf.h 配置，当前为 5000。
UART：bsp_ota_uart.c 提供 bsp_ota_uart_init、bsp_ota_uart_read、bsp_ota_uart_write，底层使用 huart1；init 为空实现，依赖外部 UART 初始化。它没有接入 bsp_ota_transport.c。
USB 当前为 USB Device/Modbus 路径，没有 OTA transport 映射。ESP8266 有相关 TCP 辅助代码，但当前 transport 仍绑定 W5500。

## 7. 完整性与企业级 OTA 能力

已实现：CRC16 帧校验、CRC32 镜像校验、image size/offset/chunk 边界检查、footer magic/size 校验、metadata CRC、session token、双槽逻辑、pending/trial 字段、B->A 复制、OTA 期间 CM4 hold 和 peer task 挂起。
未实现或证据不足：数字签名、来源认证、公钥/证书验证、加密、密钥管理、防回滚版本比较、掉电原子写入、完整自动 rollback 调用链、真正独立 A/B 启动。
版本字段存在于 boot_slot_footer_t.version 和 BOOT_META_VERSION，但当前没有按版本号拒绝旧镜像的比较逻辑。

## 8. 关键风险

### R1：GET_INFO Flash 容量硬编码错误
app/BSP/OTA/bsp_ota_manager.c:295-300 上报 1 MiB，而 H757XIH6 实际为 2 MiB。
### R2：Slot A/B 共享物理擦除扇区
app/BSP/OTA/bsp_ota_flash.c:142-174、216-237 和 bootloader/Common/Src/boot_slot_copy.c:23-39 对共享 sector 做备份、整扇区擦除和恢复。掉电可能损坏当前 APP 或两个逻辑槽。
### R3：Flash 操作固定 Bank1
bsp_flash_ram.c 直接操作 FLASH CR1、SR1、KEYR1，并按 Bank1 sector 擦除；当前设计只支持 Bank1 的 A/B 槽。
### R4：回滚实际调用链不足
boot_meta_on_boot_failure() 存在，但当前 Bootloader 主流程未调用它。
### R5：RESET 状态约束不足
ota_handle_reset() 不检查 session 或 READY_VERIFY 状态。
### R6：DATA 未强制连续 offset
代码允许一定程度的非连续/重叠 offset；只对已覆盖区域直接 ACK，未强制每个新包 offset 等于 s_bytes_written。
### R7：transport 虽抽象命名但固定 W5500
UART 和 ESP8266 不能自动作为当前 OTA 后端。
### R8：OTA 任务依赖 W5500 RX 通知初始化时序
W5500 task 和 OTA task 都参与 RX notify 初始化，当前依赖 StartDefaultTask 的调用顺序。

## 9. 审计摘要

| 项目 | 当前结论 |
|---|---|
| OTA 入口 | BSP_Ota_Init、BSP_Ota_OnBoot、BSP_Ota_Start |
| OTA task | BSP_Ota_Start 创建 OtaTask |
| 当前传输 | W5500 Ethernet TCP |
| UART OTA | 辅助 API，未接入 transport |
| 写入目标 | inactive 槽，通常 Slot B |
| 校验 | CRC16 帧校验 + CRC32 镜像校验 |
| Bootloader 角色 | 下次启动 B->A 复制并跳转 |
| metadata | BKPSRAM 0x38800000 |
| footer | 每槽末尾 32 bytes |
| active/pending | 有字段，实际运行固定 Slot A |
| trial | 有字段和部分处理，完整失败调用链不足 |
| 签名/加密 | 未实现 |
| 主要风险 | 共享 Sector 4、掉电窗口、容量上报错误 |

当前实现可以视为具备 CRC、会话控制和双槽意图的工程 OTA 原型；从代码证据看，认证安全、掉电原子性和完整回滚仍未达到企业级 OTA 的完整程度。
