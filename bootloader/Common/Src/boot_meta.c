#include "boot_meta.h"

#include <string.h>

#include "stm32h7xx.h"
#include "stm32h7xx_hal.h"
#include "stm32h7xx_hal_pwr.h"
#include "stm32h7xx_hal_pwr_ex.h"
#include "stm32h7xx_hal_rcc.h"
#include "stm32h7xx_hal_rcc_ex.h"

#define BOOT_META_BKPSRAM_ADDR  (0x38800000UL)

static boot_meta_t *boot_meta_ptr(void)
{
    return (boot_meta_t *)BOOT_META_BKPSRAM_ADDR;
}

static uint32_t boot_meta_calc_crc(const boot_meta_t *meta)
{
    uint32_t crc = 0xFFFFFFFFU;
    const uint8_t *data = (const uint8_t *)meta;
    uint32_t length = sizeof(boot_meta_t) - sizeof(uint32_t);

    for (uint32_t i = 0U; i < length; i++) {
        crc ^= data[i];
        for (uint32_t bit = 0U; bit < 8U; bit++) {
            if ((crc & 1U) != 0U) {
                crc = (crc >> 1) ^ 0xEDB88320U;
            } else {
                crc >>= 1;
            }
        }
    }

    return ~crc;
}

static void boot_meta_set_defaults(boot_meta_t *meta)
{
    memset(meta, 0, sizeof(*meta));
    meta->magic = BOOT_META_MAGIC;
    meta->version = BOOT_META_VERSION;
    meta->active_slot = CM7_APP_SLOT_A_INDEX;
    meta->pending_slot = CM7_APP_SLOT_A_INDEX;
    meta->boot_trial = 0U;
    meta->flags = 0U;
    meta->crc32 = boot_meta_calc_crc(meta);
}

void boot_meta_hw_init(void)
{
    static uint8_t s_ready = 0U;
    volatile uint32_t retry;

    if (s_ready != 0U) {
        return;
    }

    HAL_PWR_EnableBkUpAccess();
    __HAL_RCC_BKPRAM_CLK_ENABLE();

    SET_BIT(PWR->CR2, PWR_CR2_BREN);
    for (retry = 0U; retry < 2000000U; retry++) {
        if (__HAL_PWR_GET_FLAG(PWR_FLAG_BRR) != 0U) {
            break;
        }
    }

    s_ready = 1U;
}

int boot_meta_load(boot_meta_t *meta)
{
    const boot_meta_t *stored;

    if (meta == NULL) {
        return -1;
    }

    boot_meta_hw_init();
    stored = boot_meta_ptr();

    if ((stored->magic != BOOT_META_MAGIC) || (stored->version != BOOT_META_VERSION)) {
        boot_meta_set_defaults(meta);
        return 0;
    }

    *meta = *stored;
    if (meta->crc32 != boot_meta_calc_crc(meta)) {
        boot_meta_set_defaults(meta);
        return 0;
    }

    return 0;
}

int boot_meta_save(const boot_meta_t *meta)
{
    boot_meta_t *stored = boot_meta_ptr();
    boot_meta_t temp;

    if (meta == NULL) {
        return -1;
    }

    temp = *meta;
    temp.magic = BOOT_META_MAGIC;
    temp.version = BOOT_META_VERSION;
    temp.crc32 = boot_meta_calc_crc(&temp);
    *stored = temp;
    return 0;
}

uint8_t boot_meta_select_boot_slot(const boot_meta_t *meta)
{
    (void)meta;
    /* 固件链接在 Slot A，始终从 A 启动 */
    return CM7_APP_SLOT_A_INDEX;
}

void boot_meta_on_boot_failure(boot_meta_t *meta)
{
    if (meta == NULL) {
        return;
    }

    if (meta->boot_trial > 0U) {
        meta->boot_trial--;
    }

    if (meta->boot_trial == 0U) {
        meta->flags &= (uint8_t)~BOOT_META_FLAG_PENDING;
        meta->pending_slot = meta->active_slot;
    }

    meta->crc32 = boot_meta_calc_crc(meta);
    (void)boot_meta_save(meta);
}

void boot_meta_commit_pending(boot_meta_t *meta)
{
    if ((meta == NULL) || ((meta->flags & BOOT_META_FLAG_PENDING) == 0U)) {
        return;
    }

    meta->active_slot = CM7_APP_SLOT_A_INDEX;
    meta->boot_trial = 0U;
    meta->flags &= (uint8_t)~BOOT_META_FLAG_PENDING;
    meta->crc32 = boot_meta_calc_crc(meta);
    (void)boot_meta_save(meta);
}

void boot_meta_set_pending(boot_meta_t *meta, uint8_t slot_index,
                           uint32_t image_size, uint32_t image_crc32)
{
    if ((meta == NULL) || (slot_index >= CM7_APP_SLOT_COUNT)) {
        return;
    }

    meta->pending_slot = slot_index;
    meta->slot_size[slot_index] = image_size;
    meta->slot_crc[slot_index] = image_crc32;
    meta->boot_trial = BOOT_BOOT_TRIAL_MAX;
    meta->flags |= BOOT_META_FLAG_PENDING;
    meta->crc32 = boot_meta_calc_crc(meta);
    (void)boot_meta_save(meta);
}
