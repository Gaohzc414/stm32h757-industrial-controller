#include "boot_slot.h"

#include "stm32h7xx.h"

extern uint32_t g_pfnVectors[];

uint8_t boot_slot_index_from_address(uint32_t address)
{
    if ((address >= CM7_APP_SLOT_B_BASE) && (address < (CM7_APP_SLOT_B_BASE + CM7_APP_SLOT_SIZE))) {
        return CM7_APP_SLOT_B_INDEX;
    }

    return CM7_APP_SLOT_A_INDEX;
}

uint32_t boot_slot_base(uint8_t slot_index)
{
    if (slot_index == CM7_APP_SLOT_B_INDEX) {
        return CM7_APP_SLOT_B_BASE;
    }

    return CM7_APP_SLOT_A_BASE;
}

uint8_t boot_slot_detect_running(void)
{
    return boot_slot_index_from_address((uint32_t)g_pfnVectors);
}

uint8_t boot_slot_inactive_index(uint8_t running_index)
{
    return (running_index == CM7_APP_SLOT_A_INDEX) ? CM7_APP_SLOT_B_INDEX : CM7_APP_SLOT_A_INDEX;
}

static uint32_t boot_slot_footer_address(uint8_t slot_index)
{
    return boot_slot_base(slot_index) + CM7_APP_SLOT_SIZE - BOOT_SLOT_FOOTER_SIZE;
}

int boot_slot_footer_read(uint8_t slot_index, boot_slot_footer_t *footer)
{
    if ((footer == NULL) || (slot_index >= CM7_APP_SLOT_COUNT)) {
        return -1;
    }

    const boot_slot_footer_t *src =
        (const boot_slot_footer_t *)boot_slot_footer_address(slot_index);
    *footer = *src;
    return 0;
}

int boot_slot_is_valid(uint8_t slot_index)
{
    uint32_t base;
    uint32_t stack;
    uint32_t reset;
    boot_slot_footer_t footer;

    if (slot_index >= CM7_APP_SLOT_COUNT) {
        return 0;
    }

    base = boot_slot_base(slot_index);
    stack = *(__IO uint32_t *)base;
    reset = *(__IO uint32_t *)(base + 4U);

    if ((stack & 0xFF000000U) != 0x20000000U) {
        return 0;
    }

    /* 执行分区固定为 Slot A：复位向量须在 0x08020000 段内 */
    if (slot_index == CM7_APP_SLOT_A_INDEX) {
        if ((reset < CM7_APP_SLOT_A_BASE) ||
            (reset >= (CM7_APP_SLOT_A_BASE + CM7_APP_SLOT_SIZE)) ||
            ((reset & 1U) == 0U)) {
            return 0;
        }
    } else {
        /* Slot B 仅作 OTA 缓冲，用 footer 校验 */
        if (boot_slot_footer_read(slot_index, &footer) != 0) {
            return 0;
        }
        if (footer.magic != BOOT_SLOT_FOOTER_MAGIC) {
            return 0;
        }
        if ((footer.image_size == 0U) ||
            (footer.image_size > (CM7_APP_SLOT_SIZE - BOOT_SLOT_FOOTER_SIZE))) {
            return 0;
        }
        return 1;
    }

    if (boot_slot_footer_read(slot_index, &footer) != 0) {
        return 0;
    }

    if (footer.magic != BOOT_SLOT_FOOTER_MAGIC) {
        return 1;
    }

    if ((footer.image_size == 0U) ||
        (footer.image_size > (CM7_APP_SLOT_SIZE - BOOT_SLOT_FOOTER_SIZE))) {
        return 0;
    }

    return 1;
}
