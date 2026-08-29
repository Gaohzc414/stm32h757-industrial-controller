# Vendor Baseline

Import date: 2026-08-29

## Application

Original repository HEAD:

9ae3a41
完善部分文件注释。

The delivered application working tree contained about 571 tracked
modifications. Audit showed these were primarily whitespace /
line-ending differences rather than functional changes.

## Bootloader

Original repository HEAD:

9215859
Bootloader初始化工程，已经实现跳转APP。

The delivered Bootloader working tree contained substantial functional
changes not committed to the vendor Git history.

Important delivered modules include:

- boot_meta
- boot_slot
- boot_slot_copy
- bsp_flash_ram
- updated boot_jump
- updated main
- updated startup / linker scripts / Makefile

Therefore this repository imports the actual delivered working tree,
rather than resetting to the vendor HEAD.

## OTA Host

The OTA host is the separately delivered Qt/C++ application.

Main components:

- protocol
- transport
- engine
- Qt GUI

Build products and Qt Creator user configuration are not imported.

## Target

Original MCU:

STM32H747XI

Migration target:

STM32H757

Exact H757 part number and board-level differences will be confirmed
against the physical hardware and schematic.

## Planned Toolchain

macOS
VS Code
arm-none-eabi-gcc
GNU Make
OpenOCD
DAPLink / CMSIS-DAP
