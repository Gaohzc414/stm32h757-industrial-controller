##########################################################################################################################
# CM4 minimal wake-from-stop stub — keep only CM4/Core + required HAL.
# CubeMX regen may re-add CM7/FATFS/SDMMC/USB/LVGL sources; remove them again.
##########################################################################################################################

# ------------------------------------------------
# Generic Makefile (based on gcc)
# ------------------------------------------------

######################################
# target
######################################
TARGET = GPIO_CM4

######################################
# building variables
######################################
DEBUG = 1
OPT = -Og

#######################################
# paths
#######################################
BUILD_DIR = build

######################################
# source
######################################
C_SOURCES = \
../../CM4/Core/Src/main.c \
../../CM4/Core/Src/dma.c \
../../CM4/Core/Src/stm32h7xx_it.c \
../../CM4/Core/Src/stm32h7xx_hal_msp.c \
../../CM4/Core/Src/freertos.c \
../../CM4/Core/Src/stm32h7xx_hal_timebase_tim.c \
../../Middlewares/Third_Party/FreeRTOS/Source/croutine.c \
../../Middlewares/Third_Party/FreeRTOS/Source/event_groups.c \
../../Middlewares/Third_Party/FreeRTOS/Source/list.c \
../../Middlewares/Third_Party/FreeRTOS/Source/queue.c \
../../Middlewares/Third_Party/FreeRTOS/Source/stream_buffer.c \
../../Middlewares/Third_Party/FreeRTOS/Source/tasks.c \
../../Middlewares/Third_Party/FreeRTOS/Source/timers.c \
../../Middlewares/Third_Party/FreeRTOS/Source/CMSIS_RTOS_V2/cmsis_os2.c \
../../Middlewares/Third_Party/FreeRTOS/Source/portable/MemMang/heap_4.c \
../../Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM4F/port.c \
../../Middlewares/Third_Party/FreeRTOS/Source/portable/Common/mpu_wrappers_v2.c \
../../Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_cortex.c \
../../Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_rcc.c \
../../Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_rcc_ex.c \
../../Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_flash.c \
../../Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_flash_ex.c \
../../Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_gpio.c \
../../Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_hsem.c \
../../Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_dma.c \
../../Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_dma_ex.c \
../../Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_mdma.c \
../../Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_pwr.c \
../../Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_pwr_ex.c \
../../Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal.c \
../../Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_i2c.c \
../../Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_i2c_ex.c \
../../Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_exti.c \
../../Common/Src/system_stm32h7xx_dualcore_boot_cm4_cm7.c \
../../Common/Src/sensor_shared.c \
../../Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_tim.c \
../../Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_tim_ex.c \
../../CM4/BSP/Sensor/dht11.c \
../../CM4/Core/Src/sysmem.c \
../../CM4/Core/Src/syscalls.c

ASM_SOURCES = \
startup_stm32h757xx_CM4.s

ASMM_SOURCES =

#######################################
# binaries
#######################################
PREFIX = arm-none-eabi-
ifdef GCC_PATH
CC = $(GCC_PATH)/$(PREFIX)gcc
AS = $(GCC_PATH)/$(PREFIX)gcc -x assembler-with-cpp
CP = $(GCC_PATH)/$(PREFIX)objcopy
SZ = $(GCC_PATH)/$(PREFIX)size
else
CC = $(PREFIX)gcc
AS = $(PREFIX)gcc -x assembler-with-cpp
CP = $(PREFIX)objcopy
SZ = $(PREFIX)size
endif
HEX = $(CP) -O ihex
BIN = $(CP) -O binary -S

#######################################
# CFLAGS
#######################################
CPU = -mcpu=cortex-m4
FPU = -mfpu=fpv4-sp-d16
FLOAT-ABI = -mfloat-abi=hard
MCU = $(CPU) -mthumb $(FPU) $(FLOAT-ABI)

AS_DEFS =

C_DEFS = \
-DCORE_CM4 \
-DUSE_HAL_DRIVER \
-DSTM32H757xx \
-DUSE_PWR_LDO_SUPPLY

AS_INCLUDES =

C_INCLUDES = \
-I../../CM4/Core/Inc \
-I../../CM4/BSP/Sensor \
-I../../Common/Inc \
-I../../Drivers/STM32H7xx_HAL_Driver/Inc \
-I../../Drivers/STM32H7xx_HAL_Driver/Inc/Legacy \
-I../../Drivers/CMSIS/Device/ST/STM32H7xx/Include \
-I../../Drivers/CMSIS/Include \
-I../../Middlewares/Third_Party/FreeRTOS/Source/include \
-I../../Middlewares/Third_Party/FreeRTOS/Source/CMSIS_RTOS_V2 \
-I../../Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM4F \
-I../../Drivers/CMSIS/RTOS2/Include

ASFLAGS = $(MCU) $(AS_DEFS) $(AS_INCLUDES) $(OPT) -Wall -fdata-sections -ffunction-sections

CFLAGS += $(MCU) $(C_DEFS) $(C_INCLUDES) $(OPT) -Wall -fdata-sections -ffunction-sections

ifeq ($(DEBUG), 1)
CFLAGS += -g -gdwarf-2
endif

CFLAGS += -MMD -MP -MF"$(@:%.o=%.d)"

#######################################
# LDFLAGS
#######################################
LDSCRIPT = stm32h747xx_flash_CM4.ld

LIBS = -lc -lm -lnosys
LIBDIR =
LDFLAGS = $(MCU) -specs=nano.specs -T$(LDSCRIPT) $(LIBDIR) $(LIBS) -Wl,-Map=$(BUILD_DIR)/$(TARGET).map,--cref -Wl,--gc-sections

all: $(BUILD_DIR)/$(TARGET).elf $(BUILD_DIR)/$(TARGET).hex $(BUILD_DIR)/$(TARGET).bin

#######################################
# build the application
#######################################
OBJECTS = $(addprefix $(BUILD_DIR)/,$(notdir $(C_SOURCES:.c=.o)))
vpath %.c $(sort $(dir $(C_SOURCES)))
OBJECTS += $(addprefix $(BUILD_DIR)/,$(notdir $(ASM_SOURCES:.s=.o)))
vpath %.s $(sort $(dir $(ASM_SOURCES)))
OBJECTS += $(addprefix $(BUILD_DIR)/,$(notdir $(ASMM_SOURCES:.S=.o)))
vpath %.S $(sort $(dir $(ASMM_SOURCES)))

$(BUILD_DIR)/%.o: %.c Makefile | $(BUILD_DIR)
	$(CC) -c $(CFLAGS) -Wa,-a,-ad,-alms=$(BUILD_DIR)/$(notdir $(<:.c=.lst)) $< -o $@

$(BUILD_DIR)/%.o: %.s Makefile | $(BUILD_DIR)
	$(AS) -c $(CFLAGS) $< -o $@

$(BUILD_DIR)/%.o: %.S Makefile | $(BUILD_DIR)
	$(AS) -c $(CFLAGS) $< -o $@

$(BUILD_DIR)/$(TARGET).elf: $(OBJECTS) Makefile
	$(CC) $(OBJECTS) $(LDFLAGS) -o $@
	$(SZ) $@

$(BUILD_DIR)/%.hex: $(BUILD_DIR)/%.elf | $(BUILD_DIR)
	$(HEX) $< $@

$(BUILD_DIR)/%.bin: $(BUILD_DIR)/%.elf | $(BUILD_DIR)
	$(BIN) $< $@

$(BUILD_DIR):
	mkdir $@

clean:
	-rm -fR $(BUILD_DIR)

-include $(wildcard $(BUILD_DIR)/*.d)

# *** EOF ***
