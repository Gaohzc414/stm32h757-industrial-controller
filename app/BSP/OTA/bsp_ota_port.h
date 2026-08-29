/**
 * @file    bsp_ota_port.h
 * @brief   OTA 期间双核与看门狗端口控制
 */
#ifndef BSP_OTA_PORT_H
#define BSP_OTA_PORT_H

/**
 * @brief  进入 OTA 端口模式（挂起 CM4、启用长操作看门狗）
 */
void bsp_ota_port_enter(void);

/**
 * @brief  退出 OTA 端口模式（恢复正常看门狗策略）
 */
void bsp_ota_port_exit(void);

/**
 * @brief  阻止 CM4 从 Flash 启动（保持 CM7 独占 Flash 总线）
 */
void bsp_ota_port_hold_cm4(void);

#endif /* BSP_OTA_PORT_H */
