#!/usr/bin/env bash
# 烧录 CM4 固件到 flash bank2 (0x08100000)
# 用法: ./flash_cm4.sh
set -euo pipefail

# 切换到脚本所在目录，保证相对路径正确
cd "$(dirname "$0")"

openocd \
  -f interface/cmsis-dap.cfg \
  -f target/stm32h7x_dual_bank.cfg \
  -c "program CM4/build/GPIO_CM4.hex verify reset exit"
