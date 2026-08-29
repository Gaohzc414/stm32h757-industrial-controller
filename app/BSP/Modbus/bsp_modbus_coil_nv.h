/**
 * @file    bsp_modbus_coil_nv.h
 * @brief   Modbus 线圈非易失性存储（SD 卡 FatFS）
 */
#ifndef BSP_MODBUS_COIL_NV_H
#define BSP_MODBUS_COIL_NV_H

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief  检查 SD 卡介质是否就绪
 * @retval true  SD 卡已插入；false 未就绪
 */
bool BSP_Modbus_CoilNv_MediaReady(void);

/**
 * @brief  初始化线圈 NV 存储（挂载 FatFS、创建互斥量）
 * @retval true  成功；false 失败
 */
bool BSP_Modbus_CoilNv_Init(void);

/**
 * @brief  从 SD 卡加载线圈状态
 * @param  coils  输出缓冲区
 * @param  count  线圈数量
 * @retval true  加载成功；false 失败
 */
bool BSP_Modbus_CoilNv_Load(uint8_t *coils, uint16_t count);

/**
 * @brief  将线圈状态保存到 SD 卡
 * @param  coils  线圈数据
 * @param  count  线圈数量
 * @retval true  保存成功；false 失败
 */
bool BSP_Modbus_CoilNv_Save(const uint8_t *coils, uint16_t count);

#endif
