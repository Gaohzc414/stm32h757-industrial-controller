/**
 * @file    mpu.c
 * @brief   Cortex-M7 MPU 内存保护区域配置
 */
#include "mpu.h"

/**
 * @brief  配置单个 MPU 保护区域
 * @param  baseaddr  区域基地址
 * @param  size      区域大小（MPU_REGION_SIZE_x 枚举）
 * @param  rnum      区域编号
 * @param  de        是否禁止指令执行
 * @param  ap        访问权限
 * @param  sen       是否共享
 * @param  cen       是否可缓存
 * @param  ben       是否可缓冲
 * @retval 0 成功
 */
uint8_t mpu_set_protection(uint32_t baseaddr, uint32_t size, uint32_t rnum,
                           uint8_t de, uint8_t ap, uint8_t sen, uint8_t cen, uint8_t ben)
{
    MPU_Region_InitTypeDef mpu_region_init_handle;

    HAL_MPU_Disable();

    mpu_region_init_handle.Enable = MPU_REGION_ENABLE;
    mpu_region_init_handle.Number = rnum;
    mpu_region_init_handle.BaseAddress = baseaddr;
    mpu_region_init_handle.Size = size;
    mpu_region_init_handle.SubRegionDisable = 0x00U;
    mpu_region_init_handle.TypeExtField = MPU_TEX_LEVEL0;
    mpu_region_init_handle.AccessPermission = ap;
    mpu_region_init_handle.DisableExec = de;
    mpu_region_init_handle.IsShareable = sen;
    mpu_region_init_handle.IsCacheable = cen;
    mpu_region_init_handle.IsBufferable = ben;

    HAL_MPU_ConfigRegion(&mpu_region_init_handle);
    HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);

    return 0;
}

/**
 * @brief  按工程内存布局配置全部 MPU 区域
 */
void mpu_memory_protection(void)
{
    mpu_set_protection(0x20000000U, MPU_REGION_SIZE_128KB, MPU_REGION_NUMBER1, 0,
                       MPU_REGION_FULL_ACCESS, MPU_ACCESS_NOT_SHAREABLE,
                       MPU_ACCESS_CACHEABLE, MPU_ACCESS_BUFFERABLE);

    mpu_set_protection(0x24000000U, MPU_REGION_SIZE_512KB, MPU_REGION_NUMBER2, 0,
                       MPU_REGION_FULL_ACCESS, MPU_ACCESS_NOT_SHAREABLE,
                       MPU_ACCESS_CACHEABLE, MPU_ACCESS_BUFFERABLE);

    mpu_set_protection(0x30000000U, MPU_REGION_SIZE_512KB, MPU_REGION_NUMBER3, 0,
                       MPU_REGION_FULL_ACCESS, MPU_ACCESS_NOT_SHAREABLE,
                       MPU_ACCESS_CACHEABLE, MPU_ACCESS_BUFFERABLE);

    mpu_set_protection(0x38000000U, MPU_REGION_SIZE_64KB, MPU_REGION_NUMBER4, 0,
                       MPU_REGION_FULL_ACCESS, MPU_ACCESS_NOT_SHAREABLE,
                       MPU_ACCESS_CACHEABLE, MPU_ACCESS_BUFFERABLE);

    mpu_set_protection(0x60000000U, MPU_REGION_SIZE_64MB, MPU_REGION_NUMBER5, 0,
                       MPU_REGION_FULL_ACCESS, MPU_ACCESS_NOT_SHAREABLE,
                       MPU_ACCESS_NOT_CACHEABLE, MPU_ACCESS_NOT_BUFFERABLE);

    mpu_set_protection(0xD0000000U, MPU_REGION_SIZE_32MB, MPU_REGION_NUMBER6, 0,
                       MPU_REGION_FULL_ACCESS, MPU_ACCESS_NOT_SHAREABLE,
                       MPU_ACCESS_CACHEABLE, MPU_ACCESS_BUFFERABLE);

    mpu_set_protection(0x80000000U, MPU_REGION_SIZE_256MB, MPU_REGION_NUMBER7, 1,
                       MPU_REGION_FULL_ACCESS, MPU_ACCESS_NOT_SHAREABLE,
                       MPU_ACCESS_NOT_CACHEABLE, MPU_ACCESS_NOT_BUFFERABLE);
}
