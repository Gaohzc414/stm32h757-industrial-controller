/**
 * @file    boot_meta.c
 * @brief   启动元数据（BKPSRAM 持久化）实现
 */
#include "boot_meta.h"

#include <string.h>

#include "stm32h7xx.h"
#include "stm32h7xx_hal.h"
#include "stm32h7xx_hal_pwr.h"
#include "stm32h7xx_hal_pwr_ex.h"
#include "stm32h7xx_hal_rcc.h"
#include "stm32h7xx_hal_rcc_ex.h"

#define BOOT_META_BKPSRAM_ADDR  (0x38800000UL)

/**
 * @brief  获取 BKPSRAM 中 boot_meta_t 存储区指针
 * @retval boot_meta_t 结构体指针
 */
static boot_meta_t *boot_meta_ptr(void)
{
    return (boot_meta_t *)BOOT_META_BKPSRAM_ADDR;
}

/**
 * @brief  计算 boot_meta_t 的 CRC32 校验值（不含 crc32 字段本身）
 * @param  meta 元数据结构指针
 * @retval CRC32 校验值
 */
static uint32_t boot_meta_calc_crc(const boot_meta_t *meta)
{
    uint32_t crc = 0xFFFFFFFFU;
    const uint8_t *data = (const uint8_t *)meta;
    uint32_t length = sizeof(boot_meta_t) - sizeof(uint32_t);

    for (uint32_t i = 0U; i < length; i++) {
        crc ^= data[i];
        /* 标准 CRC-32 反射算法，逐位右移 */
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

/**
 * @brief  将元数据结构重置为出厂默认值
 * @param  meta 元数据结构指针
 * @retval 无
 */
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

/**
 * @brief  初始化 BKPSRAM 硬件，使能备份域访问
 * @retval 无
 */
void boot_meta_hw_init(void)
{
    static uint8_t s_ready = 0U;
    volatile uint32_t retry;

    if (s_ready != 0U) {
        return;
    }

    /* 允许访问备份域寄存器（BKPSRAM、RTC 备份寄存器等） */
    HAL_PWR_EnableBkUpAccess();
    __HAL_RCC_BKPRAM_CLK_ENABLE();

    /* 使能备份调节器 BREN：VBAT 供电或 Standby 模式下 BKPSRAM 内容保持 */
    SET_BIT(PWR->CR2, PWR_CR2_BREN);
    for (retry = 0U; retry < 2000000U; retry++) {
        if (__HAL_PWR_GET_FLAG(PWR_FLAG_BRR) != 0U) {
            break;  /* BRR=1 表示备份调节器就绪，可安全读写 BKPSRAM */
        }
    }

    s_ready = 1U;
}

/**
 * @brief  从 BKPSRAM 加载启动元数据，无效时填充默认值
 * @param  meta 输出元数据结构指针
 * @retval 0 成功，-1 参数无效
 */
int boot_meta_load(boot_meta_t *meta)
{
    const boot_meta_t *stored;

    if (meta == NULL) {
        return -1;
    }

    boot_meta_hw_init();
    stored = boot_meta_ptr();

    if ((stored->magic != BOOT_META_MAGIC) || (stored->version != BOOT_META_VERSION)) {
        /* 首次上电或结构版本不匹配，使用默认元数据 */
        boot_meta_set_defaults(meta);
        return 0;
    }

    *meta = *stored;
    if (meta->crc32 != boot_meta_calc_crc(meta)) {
        /* CRC 损坏（如掉电写一半），回退默认值防止误启动 */
        boot_meta_set_defaults(meta);
        return 0;
    }

    return 0;
}

/**
 * @brief  将启动元数据写入 BKPSRAM 并更新 CRC
 * @param  meta 待保存的元数据结构指针
 * @retval 0 成功，-1 参数无效
 */
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

/**
 * @brief  根据元数据选择本次应启动的槽索引
 * @param  meta 当前元数据结构指针
 * @retval 启动槽索引（当前固定为 Slot A）
 */
uint8_t boot_meta_select_boot_slot(const boot_meta_t *meta)
{
    (void)meta;
    return CM7_APP_SLOT_A_INDEX;
}

/**
 * @brief  启动失败时递减试启动计数并清除待升级标志
 * @param  meta 元数据结构指针（会被修改并保存）
 * @retval 无
 */
void boot_meta_on_boot_failure(boot_meta_t *meta)
{
    if (meta == NULL) {
        return;
    }

    if (meta->boot_trial > 0U) {
        meta->boot_trial--;
    }

    if (meta->boot_trial == 0U) {
        /* 试运行次数耗尽，放弃 pending 槽，回退到 active 槽 */
        meta->flags &= (uint8_t)~BOOT_META_FLAG_PENDING;
        meta->pending_slot = meta->active_slot;
    }

    meta->crc32 = boot_meta_calc_crc(meta);
    (void)boot_meta_save(meta);
}

/**
 * @brief  确认待升级镜像已成功启动，提交 pending 状态
 * @param  meta 元数据结构指针（会被修改并保存）
 * @retval 无
 */
void boot_meta_commit_pending(boot_meta_t *meta)
{
    if ((meta == NULL) || ((meta->flags & BOOT_META_FLAG_PENDING) == 0U)) {
        return;
    }

    /* 试运行成功：pending 槽固件已验证可运行，记为 active（当前仍固定 Slot A 执行） */
    meta->active_slot = CM7_APP_SLOT_A_INDEX;
    meta->boot_trial = 0U;
    meta->flags &= (uint8_t)~BOOT_META_FLAG_PENDING;
    meta->crc32 = boot_meta_calc_crc(meta);
    (void)boot_meta_save(meta);
}

/**
 * @brief  标记指定槽为待升级并记录镜像大小与 CRC
 * @param  meta         元数据结构指针（会被修改并保存）
 * @param  slot_index   待升级槽索引
 * @param  image_size   镜像字节长度
 * @param  image_crc32  镜像 CRC32 校验值
 * @retval 无
 */
void boot_meta_set_pending(boot_meta_t *meta, uint8_t slot_index,
                           uint32_t image_size, uint32_t image_crc32)
{
    if ((meta == NULL) || (slot_index >= CM7_APP_SLOT_COUNT)) {
        return;
    }

    meta->pending_slot = slot_index;
    meta->slot_size[slot_index] = image_size;
    meta->slot_crc[slot_index] = image_crc32;
    /* 给予 BOOT_BOOT_TRIAL_MAX 次试运行机会，失败则 boot_meta_on_boot_failure 回滚 */
    meta->boot_trial = BOOT_BOOT_TRIAL_MAX;
    meta->flags |= BOOT_META_FLAG_PENDING;
    meta->crc32 = boot_meta_calc_crc(meta);
    (void)boot_meta_save(meta);
}
