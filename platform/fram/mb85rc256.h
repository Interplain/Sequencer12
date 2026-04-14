#ifndef MB85RC256_H
#define MB85RC256_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "stm32f4xx_hal.h"

/* MB85RC256V base 7-bit address is 0x50 when A2/A1/A0 are tied low. */
#define MB85RC256_ADDR_7BIT 0x50u
#define MB85RC256_SIZE_BYTES 32768u

void    MB85RC256_Init(I2C_HandleTypeDef* hi2c);
uint8_t MB85RC256_GetAddress7bit(void);
uint8_t MB85RC256_IsReady(void);
uint8_t MB85RC256_Read(uint16_t address, uint8_t* dst, uint16_t len);
uint8_t MB85RC256_Write(uint16_t address, const uint8_t* src, uint16_t len);

#ifdef __cplusplus
}
#endif

#endif
