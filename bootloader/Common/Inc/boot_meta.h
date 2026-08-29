#ifndef BOOT_META_H
#define BOOT_META_H

#include <stdint.h>
#include "boot_slot.h"

#define BOOT_META_MAGIC         (0xB007B007UL)
#define BOOT_META_VERSION       (1U)
#define BOOT_BOOT_TRIAL_MAX     (3U)

#define BOOT_META_FLAG_PENDING  (1U << 0)

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint8_t active_slot;
    uint8_t pending_slot;
    uint8_t boot_trial;
    uint8_t flags;
    uint32_t slot_crc[CM7_APP_SLOT_COUNT];
    uint32_t slot_size[CM7_APP_SLOT_COUNT];
    uint32_t crc32;
} boot_meta_t;

void boot_meta_hw_init(void);
int boot_meta_load(boot_meta_t *meta);
int boot_meta_save(const boot_meta_t *meta);

uint8_t boot_meta_select_boot_slot(const boot_meta_t *meta);
void boot_meta_on_boot_failure(boot_meta_t *meta);
void boot_meta_commit_pending(boot_meta_t *meta);
void boot_meta_set_pending(boot_meta_t *meta, uint8_t slot_index,
                           uint32_t image_size, uint32_t image_crc32);

#endif /* BOOT_META_H */
