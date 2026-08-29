/**
 * @file    boot_slot.h
 * @brief   CM7 双槽 Flash 分区布局与槽操作接口
 */
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

/**
 * @brief  根据 Flash 地址判断所属槽索引
 * @param  address Flash 地址
 * @retval 槽索引（Slot A 或 Slot B）
 */
uint8_t boot_slot_index_from_address(uint32_t address);

/**
 * @brief  获取指定槽的 Flash 基地址
 * @param  slot_index 槽索引
 * @retval 槽 Flash 基地址
 */
uint32_t boot_slot_base(uint8_t slot_index);

/**
 * @brief  检测当前运行代码所在的槽索引
 * @retval 当前运行槽索引
 */
uint8_t boot_slot_detect_running(void);

/**
 * @brief  获取与指定运行槽相对的备用槽索引
 * @param  running_index 当前运行槽索引
 * @retval 备用槽索引
 */
uint8_t boot_slot_inactive_index(uint8_t running_index);

/**
 * @brief  读取指定槽尾部的 footer 元数据
 * @param  slot_index 槽索引
 * @param  footer     输出 footer 结构指针
 * @retval 0 成功，-1 参数无效
 */
int boot_slot_footer_read(uint8_t slot_index, boot_slot_footer_t *footer);

/**
 * @brief  校验指定槽是否包含有效应用程序镜像
 * @param  slot_index 槽索引
 * @retval 1 有效，0 无效
 */
int boot_slot_is_valid(uint8_t slot_index);

#endif /* BOOT_SLOT_H */
