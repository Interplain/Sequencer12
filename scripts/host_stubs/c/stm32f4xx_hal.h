#ifndef STM32F4XX_HAL_H
#define STM32F4XX_HAL_H

/* C-compatible HAL stub for host tests of C UI modules. */
#include <stdint.h>

typedef struct
{
    int unused;
} SPI_HandleTypeDef;

uint32_t HAL_GetTick(void);

#endif
