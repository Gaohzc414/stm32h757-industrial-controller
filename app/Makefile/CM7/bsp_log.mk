# BSP UART log module — keep this file when regenerating Makefile from CubeMX
# In Makefile/CM7/Makefile use $(BSP_LOG_INCLUDES), not -I$(BSP_LOG_INCLUDES).

BSP_LOG_INCLUDES := \
  -I../../BSP/Log

BSP_LOG_SOURCES := \
  ../../BSP/Log/bsp_log.c
