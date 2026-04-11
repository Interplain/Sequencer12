#ifndef STM32F4XX_HAL_MSP_H
#define STM32F4XX_HAL_MSP_H

#include "stm32f4xx_hal.h"

void HAL_MspInit(void);
void HAL_SPI_MspInit(SPI_HandleTypeDef* hspi);
void HAL_SPI_MspDeInit(SPI_HandleTypeDef* hspi);
void HAL_TIM_Encoder_MspInit(TIM_HandleTypeDef* htim);
void HAL_TIM_Encoder_MspDeInit(TIM_HandleTypeDef* htim);
void HAL_I2C_MspInit(I2C_HandleTypeDef* hi2c);
void HAL_I2C_MspDeInit(I2C_HandleTypeDef* hi2c);

#endif