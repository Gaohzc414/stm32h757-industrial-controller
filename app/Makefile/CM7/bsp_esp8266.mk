# BSP ESP8266 WiFi — keep this file when regenerating Makefile from CubeMX

BSP_ESP8266_INCLUDES := \
  -I../../BSP/ESP8266

BSP_ESP8266_SOURCES := \
  ../../BSP/ESP8266/bsp_esp8266_at.c \
  ../../BSP/ESP8266/bsp_esp8266_wifi.c \
  ../../BSP/ESP8266/bsp_esp8266_tcp.c \
  ../../BSP/ESP8266/bsp_esp8266_app.c
