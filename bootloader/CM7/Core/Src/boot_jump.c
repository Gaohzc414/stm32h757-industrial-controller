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
    // 1. 从App向量表读取栈指针、复位入口
    uint32_t app_stack = *(__IO uint32_t *)app_base;
    uint32_t app_entry = *(__IO uint32_t *)(app_base + 4U);

    Boot_HoldCm4();        // 双核芯片：先把CM4内核hold暂停，防止内核乱跑
    __disable_irq();       // 关闭Bootloader自身全局中断

    // 关闭Bootloader的SysTick定时器，避免跳转后继续计时干扰App
    SysTick->CTRL = 0U;
    SysTick->LOAD = 0U;
    SysTick->VAL = 0U;

    // 清除全部外部中断：失能中断 + 清除挂起状态
    for (uint32_t i = 0U; i < 8U; i++)
    {
        NVIC->ICER[i] = 0xFFFFFFFFU; // ICER：写1关闭对应中断使能
        NVIC->ICPR[i] = 0xFFFFFFFFU; // ICPR：写1清除中断挂起标志
    }

    // 跳转标记
    *(__IO uint32_t *)BOOT_JUMP_FLAG_ADDR = BOOT_JUMP_MAGIC;

    __set_MSP(app_stack);   // 设置主栈指针MSP，就是切换App的栈
    __set_CONTROL(0U);      // CONTROL寄存器=0：强制使用MSP主栈，不使用PSP进程栈
    __DSB();                // 数据同步屏障：保证前面所有内存写操作全部落地完成
    __ISB();                // 指令同步屏障：刷新CPU流水线

    // 真正跳转，从此不再返回bootloader
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
