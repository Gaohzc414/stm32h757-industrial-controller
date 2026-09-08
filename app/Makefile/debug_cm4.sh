#!/usr/bin/env bash
# 双核模式连上 CM4，读取它的 PC/SP，判断它停在哪
# 用法: ./debug_cm4.sh
set -euo pipefail
cd "$(dirname "$0")"

openocd \
  -f interface/cmsis-dap.cfg \
  -c "set DUAL_CORE 1" \
  -f target/stm32h7x_dual_bank.cfg \
  -c "init" \
  -c "echo ==== 目标列表 (cpu0=CM7, cpu1=CM4) ====" \
  -c "targets" \
  -c "halt" \
  -c "echo ==== CM7 (cpu0) ====" \
  -c "targets stm32h7x.cpu0" \
  -c "reg pc sp xPSR" \
  -c "echo ==== CM4 (cpu1) ====" \
  -c "targets stm32h7x.cpu1" \
  -c "reg pc sp xPSR" \
  -c "shutdown"
