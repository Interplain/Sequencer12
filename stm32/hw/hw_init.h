#ifndef HW_INIT_H
#define HW_INIT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32f4xx_hal.h"

// ─────────────────────────────────────────────
// Peripheral handles — accessible everywhere
// ─────────────────────────────────────────────
extern SPI_HandleTypeDef hspi1;
extern SPI_HandleTypeDef hspi2;
extern I2C_HandleTypeDef hi2c1;
extern TIM_HandleTypeDef htim2;

// ─────────────────────────────────────────────
// Init functions
// ─────────────────────────────────────────────
void HW_Init(void);
void SystemClock_Config(void);

#ifdef __cplusplus
}
#endif

#endif