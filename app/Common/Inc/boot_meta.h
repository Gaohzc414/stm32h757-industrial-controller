/**
 * @file    boot_meta.h
 * @brief   启动元数据（BKPSRAM 持久化）接口与数据结构
 *
 * boot_meta_t 存放在 BKPSRAM（掉电可保持，复位不丢失），供 Bootloader 与 App 共享：
 *   - Boot：按 pending/trial 选择启动槽，失败则回滚
 *   - App：OTA VERIFY 成功后 set_pending；上电试运行成功后 commit_pending
 *
 * 结构末尾 crc32 覆盖此前全部字段，用于检测 BKPSRAM 内容损坏。
 */
#ifndef BOOT_META_H
#define BOOT_META_H

#include <stdint.h>
#include "boot_slot.h"

/** BKPSRAM 中元数据魔数，用于识别有效记录 */
#define BOOT_META_MAGIC         (0xB007B007UL)
/** 元数据格式版本，升级结构时递增 */
#define BOOT_META_VERSION       (1U)
/** 新固件试启动最大次数；耗尽后 Boot 应回滚到旧槽 */
#define BOOT_BOOT_TRIAL_MAX     (3U)

/** flags：存在待激活镜像，Boot 应以 pending_slot 试启动 */
#define BOOT_META_FLAG_PENDING  (1U << 0)

/**
 * @brief  双槽启动元数据（BKPSRAM 布局，字段顺序勿随意改动）
 */
typedef struct {
    uint32_t magic;                          /**< 必须为 BOOT_META_MAGIC */
    uint32_t version;                        /**< 结构版本号 */
    uint8_t active_slot;                     /**< 当前已确认可运行的槽索引 */
    uint8_t pending_slot;                    /**< 待试启动/待提交的槽索引 */
    uint8_t boot_trial;                      /**< 剩余试启动次数（pending 时有效） */
    uint8_t flags;                           /**< 位标志，见 BOOT_META_FLAG_* */
    uint32_t slot_crc[CM7_APP_SLOT_COUNT];   /**< 各槽镜像 CRC32（VERIFY 写入） */
    uint32_t slot_size[CM7_APP_SLOT_COUNT];  /**< 各槽镜像有效字节数 */
    uint32_t crc32;                          /**< 本结构自校验（不含本字段） */
} boot_meta_t;

/**
 * @brief  初始化 BKPSRAM 硬件，使能备份域访问
 * @retval 无
 */
void boot_meta_hw_init(void);

/**
 * @brief  从 BKPSRAM 加载启动元数据，无效时填充默认值
 * @param  meta 输出元数据结构指针
 * @retval 0 成功，-1 参数无效
 */
int boot_meta_load(boot_meta_t *meta);

/**
 * @brief  将启动元数据写入 BKPSRAM 并更新 CRC
 * @param  meta 待保存的元数据结构指针
 * @retval 0 成功，-1 参数无效
 */
int boot_meta_save(const boot_meta_t *meta);

/**
 * @brief  根据元数据选择本次应启动的槽索引
 * @param  meta 当前元数据结构指针
 * @retval 启动槽索引（当前固定为 Slot A）
 */
uint8_t boot_meta_select_boot_slot(const boot_meta_t *meta);

/**
 * @brief  启动失败时递减试启动计数并清除待升级标志
 * @param  meta 元数据结构指针（会被修改并保存）
 * @retval 无
 */
void boot_meta_on_boot_failure(boot_meta_t *meta);

/**
 * @brief  确认待升级镜像已成功启动，提交 pending 状态
 * @param  meta 元数据结构指针（会被修改并保存）
 * @retval 无
 */
void boot_meta_commit_pending(boot_meta_t *meta);

/**
 * @brief  标记指定槽为待升级并记录镜像大小与 CRC
 * @param  meta         元数据结构指针（会被修改并保存）
 * @param  slot_index   待升级槽索引
 * @param  image_size   镜像字节长度
 * @param  image_crc32  镜像 CRC32 校验值
 * @retval 无
 */
void boot_meta_set_pending(boot_meta_t *meta, uint8_t slot_index,
                           uint32_t image_size, uint32_t image_crc32);

#endif /* BOOT_META_H */
