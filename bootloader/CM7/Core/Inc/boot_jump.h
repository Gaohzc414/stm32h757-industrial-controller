#ifndef BOOT_JUMP_H
#define BOOT_JUMP_H

#include <stdint.h>
#include <stdbool.h>
#include "boot_config.h"

bool Boot_IsAppValid(void);
bool Boot_IsAppValidAt(uint32_t app_base);
void Boot_JumpToApp(void);
void Boot_JumpToAppAt(uint32_t app_base);

#endif
