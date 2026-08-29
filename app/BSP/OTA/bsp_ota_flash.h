/**
 * @file    bsp_ota_flash.h
 * @brief   OTA 目标槽位 Flash 擦写、校验与页脚写入
 */
#ifndef BSP_OTA_FLASH_H
#define BSP_OTA_FLASH_H

#include <stdint.h>

/**
 * @brief  擦除指定 CM7 应用槽位（含共享扇区保护逻辑）
 * @param  slot_index 槽位索引
 * @retval 0 成功
 * @retval -1 失败
 */
int bsp_ota_flash_erase_slot(uint8_t slot_index);

/**
 * @brief  向当前目标槽位写入固件数据
 * @param  offset 槽内偏移（字节）
 * @param  data   数据源
 * @param  length 长度（字节）
 * @retval 0 成功
 * @retval -1 失败
 */
int bsp_ota_flash_write(uint32_t offset, const uint8_t *data, uint32_t length);

/**
 * @brief  校验槽位固件 CRC32
 * @param  slot_index     槽位索引
 * @param  image_size     镜像大小（字节）
 * @param  expected_crc32 期望 CRC32
 * @retval 0 校验通过
 * @retval -1 校验失败或参数无效
 */
int bsp_ota_flash_verify(uint8_t slot_index, uint32_t image_size, uint32_t expected_crc32);

/**
 * @brief  在槽位末尾写入 boot 页脚元数据
 * @param  slot_index   槽位索引
 * @param  image_size   镜像大小（字节）
 * @param  image_crc32  镜像 CRC32
 * @retval 0 成功
 * @retval -1 失败
 */
int bsp_ota_flash_write_footer(uint8_t slot_index, uint32_t image_size, uint32_t image_crc32);

/**
 * @brief  设置当前 Flash 写入目标槽位
 * @param  slot_index 槽位索引
 */
void bsp_ota_flash_set_target_slot(uint8_t slot_index);

/**
 * @brief  获取当前目标槽位基地址
 * @retval  Flash 基地址
 */
uint32_t bsp_ota_flash_target_base(void);

#endif /* BSP_OTA_FLASH_H */
