#ifndef BSP_FLASH_RAM_H
#define BSP_FLASH_RAM_H

#include <stdint.h>

/**
 * Bank1 Flash erase/program routines executed from DTCM (.RamFunc).
 * Required when CM7 code runs from the same flash bank being modified.
 */
int bsp_flash_ram_erase_sectors(uint32_t first_sector, uint32_t last_sector);
int bsp_flash_ram_program_buffer(uint32_t address, const uint8_t *data, uint32_t length);

#endif /* BSP_FLASH_RAM_H */
