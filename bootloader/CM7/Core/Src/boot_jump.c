#include "boot_jump.h"

#include "boot_meta.h"

#include "stm32h7xx.h"
#include "stm32h7xx_hal_rcc_ex.h"

static void Boot_HoldCm4(void)
{
  CLEAR_BIT(RCC->GCR, RCC_GCR_BOOT_C2);
}

bool Boot_IsAppValidAt(uint32_t app_base)
{
  uint8_t slot = boot_slot_index_from_address(app_base);

  if (boot_slot_base(slot) != app_base) {
    return false;
  }

  return (boot_slot_is_valid(slot) != 0) ? true : false;
}

bool Boot_IsAppValid(void)
{
  return Boot_IsAppValidAt(CM7_APP_SLOT_A_BASE);
}

void Boot_JumpToAppAt(uint32_t app_base)
{
  uint32_t app_stack = *(__IO uint32_t *)app_base;
  uint32_t app_entry = *(__IO uint32_t *)(app_base + 4U);

  Boot_HoldCm4();

  __disable_irq();

  SysTick->CTRL = 0U;
  SysTick->LOAD = 0U;
  SysTick->VAL = 0U;

  for (uint32_t i = 0U; i < 8U; i++)
  {
    NVIC->ICER[i] = 0xFFFFFFFFU;
    NVIC->ICPR[i] = 0xFFFFFFFFU;
  }

  *(__IO uint32_t *)BOOT_JUMP_FLAG_ADDR = BOOT_JUMP_MAGIC;

  __set_MSP(app_stack);
  __set_CONTROL(0U);
  __DSB();
  __ISB();

  __asm volatile(
    "bx %0\n"
    :
    : "r"(app_entry)
    : "memory");
}

void Boot_JumpToApp(void)
{
  Boot_JumpToAppAt(CM7_APP_SLOT_A_BASE);
}
