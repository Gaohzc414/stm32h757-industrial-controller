#ifndef BOOT_SLOT_COPY_H
#define BOOT_SLOT_COPY_H

#include <stdint.h>

/**
 * 将源槽镜像复制到 Slot A（执行分区）。
 * 固件链接地址为 0x08020000，OTA 下载到 Slot B 后须复制到 A 才能运行。
 */
int boot_slot_copy_to_run_slot(uint8_t src_slot);

#endif /* BOOT_SLOT_COPY_H */
