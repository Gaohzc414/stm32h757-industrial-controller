#!/usr/bin/env bash
# 一次性读全：选项位 + RCC 运行时状态 + CM4 向量表 + CM4 寄存器
cd "$(dirname "$0")"

openocd \
  -f interface/cmsis-dap.cfg \
  -c "set DUAL_CORE 1" \
  -f target/stm32h7x_dual_bank.cfg \
  -c "init" \
  -c "targets" \
  -c "targets stm32h7x.cpu0" \
  -c "halt" \
  -c "echo ==== FLASH_OPTSR_CUR (BCM4=bit22) ====" \
  -c "mdw 0x5200201c 1" \
  -c "echo ==== RCC_GCR (BOOT_C2=bit3) ====" \
  -c "mdw 0x580044a0 1" \
  -c "echo ==== RCC_CR (D2CKRDY=bit15) ====" \
  -c "mdw 0x58004400 1" \
  -c "echo ==== CM4 vector table @0x08100000 (SP, reset, NMI, HardFault) ====" \
  -c "mdw 0x08100000 8" \
  -c "echo ==== CM4 (cpu1) registers ====" \
  -c "targets stm32h7x.cpu1" \
  -c "halt" \
  -c "reg" \
  -c "shutdown"
