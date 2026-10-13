#if defined(S12_FRAM_FULL_BACKUP_MODE)

#include "stm32f4xx_hal.h"
#include "stm32/hw/hw_init.h"
#include "platform/fram/mb85rc1mt.h"
#include "platform/fram/fram_full_backup.h"

void SysTick_Handler(void)
{
    HAL_IncTick();
}

int main(void)
{
    HW_Init();
    MB85RC1MT_Init(&hi2c3);
    FramFullBackup_Run();

    while (1)
    {
        __NOP();
    }
}

#endif