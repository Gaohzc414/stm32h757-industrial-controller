#!/usr/bin/env bash
# 一次烧录 CM7 + CM4（整片复位后由 CM7 唤醒 CM4）
# 用法: ./flash_all.sh
set -euo pipefail

# 切换到脚本所在目录，保证相对路径正确
cd "$(dirname "$0")"

openocd \
  -f interface/cmsis-dap.cfg \
  -f target/stm32h7x_dual_bank.cfg \
  -c "init" \
  -c "reset halt" \
  -c "flash write_image erase CM7/build/GPIO_CM7.hex" \
  -c "flash write_image erase CM4/build/GPIO_CM4.hex" \
  -c "reset" \
  -c "shutdown"
