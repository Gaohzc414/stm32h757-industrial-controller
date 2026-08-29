#ifndef BOOT_SLOT_H
#define BOOT_SLOT_H

#include <stdint.h>

#define BOOTLOADER_FLASH_BASE   (0x08000000UL)
#define BOOTLOADER_FLASH_SIZE   (128U * 1024U)

#define CM7_APP_SLOT_COUNT      (2U)
#define CM7_APP_SLOT_SIZE       (448U * 1024U)

#define CM7_APP_SLOT_A_BASE     (0x08020000UL)
#define CM7_APP_SLOT_B_BASE     (0x08090000UL)

#define CM7_APP_SLOT_A_INDEX    (0U)
#define CM7_APP_SLOT_B_INDEX    (1U)

/** 默认出厂 APP 烧录分区（Slot A），也是唯一执行分区 */
#define CM7_APP_FLASH_BASE      CM7_APP_SLOT_A_BASE

#define BOOT_SLOT_FOOTER_MAGIC  (0x534F4F54UL) /* "SOOT" */
#define BOOT_SLOT_FOOTER_SIZE   (32U)

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint32_t image_size;
    uint32_t image_crc32;
    uint32_t version;
    uint32_t reserved[4];
} boot_slot_footer_t;

uint8_t boot_slot_index_from_address(uint32_t address);
uint32_t boot_slot_base(uint8_t slot_index);
uint8_t boot_slot_detect_running(void);
uint8_t boot_slot_inactive_index(uint8_t running_index);
int boot_slot_footer_read(uint8_t slot_index, boot_slot_footer_t *footer);
int boot_slot_is_valid(uint8_t slot_index);

#endif /* BOOT_SLOT_H */
