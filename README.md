# STM32H757 双核工业控制器

基于 **STM32H757XIH6**（Cortex-M7 + Cortex-M4）的工业控制器固件工程：CM7 负责 GUI、显示与 OTA，CM4 负责传感器采集，两核通过 HSEM + 共享内存通信；配套 A/B 双槽 Bootloader 和 Qt 上位机 OTA 工具。

## 功能概览

| 模块 | 说明 |
| --- | --- |
| Bootloader | 128 KB，位于 `0x08000000`；A/B 双槽、CRC 校验、元数据管理、B→A 拷贝后跳转 |
| CM7 应用 | FreeRTOS + LVGL GUI、LTDC 4.3" LCD、GT1151 触摸、SDRAM、FatFs/SD、USB CDC、ADC、看门狗 |
| CM4 应用 | FreeRTOS、DHT11 周期采集，采集结果经共享内存 + HSEM 通知交给 CM7 |
| 双核 IPC | 共享区位于 `0x38000000`，HSEM 通知，MPU 属性配置 |
| OTA | USART6 串口传输，或 ESP8266 WiFi + TCP；CM7 侧负责落盘与槽位切换 |
| 上位机 | `tools/ota-host`，Qt 工程，支持串口 / 网络两种传输 |

## 目录结构

```text
.
├── app/                  # 主应用（CM7 / CM4 双核）
│   ├── CM7/              # CM7 核：main、外设、FreeRTOS、GUI 入口
│   ├── CM4/              # CM4 核：main、FreeRTOS、DHT11 采集
│   ├── BSP/              # LCD、SDRAM、触摸、OTA、看门狗、MPU 等板级驱动
│   ├── Common/           # 双核共享区、公共 SystemInit
│   ├── Drivers/          # STM32H7 HAL / CMSIS
│   ├── Middlewares/      # FreeRTOS、LVGL、FatFs、USB Device 等
│   └── Makefile/         # CM7 / CM4 的 Makefile、启动文件、链接脚本、烧录脚本
├── bootloader/           # A/B 双槽 Bootloader（独立的 HAL/CMSIS 副本）
├── tools/ota-host/       # Qt OTA 上位机
├── docs/                 # 架构/OTA/IPC 等设计文档，以及原理图、显示屏、调试器资料
└── scripts/              # 预留脚本目录
```

## Flash 布局

| 区域 | 起始地址 | 大小 | 说明 |
| --- | --- | --- | --- |
| Bootloader | `0x08000000` | 128 KB | CM7 引导 |
| CM7 App Slot A | `0x08020000` | 448 KB | 运行槽 |
| CM7 App Slot B | `0x08090000` | 448 KB | OTA 下载槽 |
| CM4 App | `0x08100000` | 1024 KB | Bank2，CM7 启动后释放 |
| 共享 RAM | `0x38000000` | 64 KB | 双核 IPC |

## 环境要求

- `arm-none-eabi-gcc` + `make`（GCC 工具链需在 `PATH`，或用 `make GCC_PATH=/path/to/bin` 指定）
- `openocd`（烧录/调试，配合 DAP-Link / CMSIS-DAP）
- Qt 5/6（仅上位机 `tools/ota-host` 需要）

## 编译

```bash
# CM7 应用
cd app/Makefile/CM7 && make -j

# CM4 应用
cd app/Makefile/CM4 && make -j

# Bootloader（CM7 与 CM4 各一份）
cd bootloader/Makefile/CM7 && make -j
cd bootloader/Makefile/CM4 && make -j
```

产物位于各自的 `build/` 目录：`GPIO_CM7.elf/.hex/.bin`、`GPIO_CM4.elf/.hex/.bin`。`build/` 已被 `.gitignore` 忽略。

## 烧录

使用 DAP-Link（CMSIS-DAP）时，先烧 Bootloader，再烧应用：

```bash
# 应用：一次烧录 CM7 + CM4
cd app/Makefile
./flash_all.sh          # 整片复位后由 CM7 唤醒 CM4
./flash_cm4.sh          # 仅烧 CM4（bank2）

# 诊断脚本
./check_cm4.sh          # 读 BCM4 / BOOT_C2 / D2CKRDY
./debug_cm4.sh          # 连接 CM4 查看 PC/SP
./diag.sh               # 选项位 + RCC + CM4 向量表
```

Bootloader 可用 openocd 直接烧写：

```bash
openocd -f interface/cmsis-dap.cfg -f target/stm32h7x_dual_bank.cfg \
  -c "program bootloader/Makefile/CM7/build/GPIO_CM7.hex verify reset exit"
```

## OTA 升级

1. Bootloader 校验元数据并跳转到 CM7 App Slot A。
2. 新固件通过 USART6 或 ESP8266 TCP 下载到 Slot B，下载完成后校验 CRC。
3. 重启后 Bootloader 将 Slot B 拷贝到 Slot A 并跳转。

上位机在 `tools/ota-host`，用 Qt Creator 打开 `OTA.pro` 构建。串口/WiFi 相关参数见 `app/BSP/ESP8266/bsp_esp8266_conf.h`，**使用前请改成你自己的热点和 OTA 主机 IP**。

## 文档

| 文档 | 内容 |
| --- | --- |
| [`docs/project_architecture.md`](docs/project_architecture.md) | 工程整体架构与目录说明 |
| [`docs/bootloader_ota.md`](docs/bootloader_ota.md) | Bootloader 与 A/B 槽 OTA 设计 |
| [`docs/ota_uart_design.md`](docs/ota_uart_design.md) | UART OTA 传输设计 |
| [`docs/cm7_cm4_ipc.md`](docs/cm7_cm4_ipc.md) | 双核通信（HSEM + 共享内存） |
| [`docs/freertos_tasks.md`](docs/freertos_tasks.md) | FreeRTOS 任务划分 |
| [`docs/watchdog_design.md`](docs/watchdog_design.md) | 看门狗设计 |
| [`docs/gui.md`](docs/gui.md) | GUI / LVGL 说明 |
| [`docs/debug_case_study.md`](docs/debug_case_study.md) | 调试案例复盘 |

硬件资料（原理图、显示屏、调试器手册）在 `docs/2. 原理图`、`docs/3. 调试器`、`docs/4. 显示屏`。

## 说明

- 仓库内 WiFi SSID / 密码 / OTA 主机为调试用配置，公开环境请自行替换。
- 未附加开源许可证；转载请注明出处。
