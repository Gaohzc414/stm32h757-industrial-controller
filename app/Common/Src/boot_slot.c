/**
 * @file    boot_slot.c
 * @brief   CM7 双槽 Flash 分区布局与槽操作实现
 */
#include "boot_slot.h"

#include "stm32h7xx.h"

extern uint32_t g_pfnVectors[];

/**
 * @brief  根据 Flash 地址判断所属槽索引
 * @param  address Flash 地址
 * @retval 槽索引（Slot A 或 Slot B）
 */
uint8_t boot_slot_index_from_address(uint32_t address)
{
    /* B 槽地址范围 0x08090000 ~ 0x08100000，其余归 A 槽 */
    if ((address >= CM7_APP_SLOT_B_BASE) && (address < (CM7_APP_SLOT_B_BASE + CM7_APP_SLOT_SIZE))) {
        return CM7_APP_SLOT_B_INDEX;
    }

    return CM7_APP_SLOT_A_INDEX;
}

/**
 * @brief  获取指定槽的 Flash 基地址
 * @param  slot_index 槽索引
 * @retval 槽 Flash 基地址
 */
uint32_t boot_slot_base(uint8_t slot_index)
{
    if (slot_index == CM7_APP_SLOT_B_INDEX) {
        return CM7_APP_SLOT_B_BASE;
    }

    return CM7_APP_SLOT_A_BASE;
}

/**
 * @brief  检测当前运行代码所在的槽索引
 * @retval 当前运行槽索引
 */
uint8_t boot_slot_detect_running(void)
{
    return boot_slot_index_from_address((uint32_t)g_pfnVectors);
}

/**
 * @brief  获取与指定运行槽相对的备用槽索引
 * @param  running_index 当前运行槽索引
 * @retval 备用槽索引
 */
uint8_t boot_slot_inactive_index(uint8_t running_index)
{
    return (running_index == CM7_APP_SLOT_A_INDEX) ? CM7_APP_SLOT_B_INDEX : CM7_APP_SLOT_A_INDEX;
}

/**
 * @brief  计算指定槽 footer 在 Flash 中的地址
 * @param  slot_index 槽索引
 * @retval footer Flash 地址
 */
static uint32_t boot_slot_footer_address(uint8_t slot_index)
{
    return boot_slot_base(slot_index) + CM7_APP_SLOT_SIZE - BOOT_SLOT_FOOTER_SIZE;
}

/**
 * @brief  读取指定槽尾部的 footer 元数据
 * @param  slot_index 槽索引
 * @param  footer     输出 footer 结构指针
 * @retval 0 成功，-1 参数无效
 */
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

/**
 * @brief  校验指定槽是否包含有效应用程序镜像
 * @param  slot_index 槽索引
 * @retval 1 有效，0 无效
 */
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
    /* Cortex-M 向量表：word0=初始栈指针，word1=Reset_Handler */
    stack = *(__IO uint32_t *)base;
    reset = *(__IO uint32_t *)(base + 4U);

    /* Cortex-M 向量表首字为 MSP，须在 SRAM 区间 0x20000000~0x2FFFFFFF */
    if ((stack & 0xFF000000U) != 0x20000000U) {
        return 0;
    }

    if (slot_index == CM7_APP_SLOT_A_INDEX) {
        /* Slot A 为执行分区：Reset 向量须在 A 槽范围内且 Thumb 位为 1 */
        if ((reset < CM7_APP_SLOT_A_BASE) ||
            (reset >= (CM7_APP_SLOT_A_BASE + CM7_APP_SLOT_SIZE)) ||
            ((reset & 1U) == 0U)) {
            return 0;
        }
    } else {
        /* Slot B 为 OTA 下载槽：必须有合法 footer 才视为有效 */
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
        /* B 槽有 footer 即可，Reset 向量范围在 OTA 写入时已保证 */
        return 1;
    }

    /* A 槽：footer 可选（出厂烧录可能无 footer），有 footer 则进一步校验 image_size */
    if (boot_slot_footer_read(slot_index, &footer) != 0) {
        return 0;
    }

    if (footer.magic != BOOT_SLOT_FOOTER_MAGIC) {
        /* 无 footer 时 A 槽仍可能有效（出厂烧录），B 槽则已在上面分支处理 */
        return 1;
    }

    if ((footer.image_size == 0U) ||
        (footer.image_size > (CM7_APP_SLOT_SIZE - BOOT_SLOT_FOOTER_SIZE))) {
        return 0;
    }

    return 1;
}
