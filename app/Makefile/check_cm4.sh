#!/usr/bin/env bash
# 诊断 CM4 启动状态：读 BCM4 选项位 + 运行时 BOOT_C2 + D2CKRDY
# 用法: ./check_cm4.sh
set -euo pipefail
cd "$(dirname "$0")"

openocd \
  -f interface/cmsis-dap.cfg \
  -f target/stm32h7x.cfg \
  -c "init" \
  -c "halt" \
  -c "echo ==== FLASH_OPTSR_CUR: BCM4 = bit22 (0x00400000), 1=CM4 上电自启动 ====" \
  -c "mdw 0x5200201c" \
  -c "echo ==== RCC_GCR: BOOT_C2 = bit3 (0x00000008), 1=CM4 已从复位释放 ====" \
  -c "mdw 0x580044a0" \
  -c "echo ==== RCC_CR: D2CKRDY = bit15 (0x00008000), 1=D2 时钟运行 / 0=D2 在 STOP ====" \
  -c "mdw 0x58004400" \
  -c "shutdown"
