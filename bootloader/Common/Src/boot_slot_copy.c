#include "boot_slot_copy.h"

#include <string.h>

#include "boot_slot.h"
#include "bsp_flash_ram.h"
#include "stm32h7xx_hal.h"

#define BOOT_COPY_CHUNK           (256U)

static uint8_t s_sector_backup[64U * 1024U];

static int flash_erase_sectors(uint32_t first_sector, uint32_t last_sector)
{
    return bsp_flash_ram_erase_sectors(first_sector, last_sector);
}

static int flash_program_buffer(uint32_t address, const uint8_t *data, uint32_t length)
{
    return bsp_flash_ram_program_buffer(address, data, length);
}

static int erase_slot_a_preserve_b(void)
{
    uint32_t preserve_addr = CM7_APP_SLOT_B_BASE;
    uint32_t preserve_len = 64U * 1024U;

    memcpy(s_sector_backup, (const void *)preserve_addr, preserve_len);

    if (flash_erase_sectors(FLASH_SECTOR_1, FLASH_SECTOR_3) != 0) {
        return -1;
    }

    if (flash_erase_sectors(FLASH_SECTOR_4, FLASH_SECTOR_4) != 0) {
        return -1;
    }

    return flash_program_buffer(preserve_addr, s_sector_backup, preserve_len);
}

static int copy_image(uint32_t src_base, uint32_t dst_base, uint32_t image_size)
{
    uint8_t chunk[BOOT_COPY_CHUNK];
    uint32_t offset = 0U;

    while (offset < image_size) {
        uint32_t n = image_size - offset;

        if (n > BOOT_COPY_CHUNK) {
            n = BOOT_COPY_CHUNK;
        }

        memcpy(chunk, (const void *)(src_base + offset), n);

        if (flash_program_buffer(dst_base + offset, chunk, n) != 0) {
            return -1;
        }

        offset += n;
    }

    return 0;
}

int boot_slot_copy_to_run_slot(uint8_t src_slot)
{
    boot_slot_footer_t footer;
    uint32_t src_base;
    uint32_t image_size;

    if (src_slot >= CM7_APP_SLOT_COUNT) {
        return -1;
    }

    if (boot_slot_footer_read(src_slot, &footer) != 0) {
        return -1;
    }

    if (footer.magic != BOOT_SLOT_FOOTER_MAGIC) {
        return -1;
    }

    image_size = footer.image_size;
    if ((image_size == 0U) || (image_size > (CM7_APP_SLOT_SIZE - BOOT_SLOT_FOOTER_SIZE))) {
        return -1;
    }

    src_base = boot_slot_base(src_slot);

    if (erase_slot_a_preserve_b() != 0) {
        return -1;
    }

    if (copy_image(src_base, CM7_APP_SLOT_A_BASE, image_size) != 0) {
        return -1;
    }

    return flash_program_buffer(CM7_APP_SLOT_A_BASE + CM7_APP_SLOT_SIZE - BOOT_SLOT_FOOTER_SIZE,
                                (const uint8_t *)&footer, sizeof(footer));
}
