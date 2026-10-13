#ifndef STM32F4XX_HAL_H
#define STM32F4XX_HAL_H

#include <cstdint>

static inline uint32_t __get_PRIMASK(void) { return 0u; }
static inline void __disable_irq(void) {}
static inline void __set_PRIMASK(uint32_t) {}

#endif
