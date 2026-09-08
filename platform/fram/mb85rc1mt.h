#ifndef MB85RC1MT_H
#define MB85RC1MT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "stm32f4xx_hal.h"

/* MB85RC1MTPNF-G-JNERE1: 1 Mbit (131072 x 8 bit = 128 KiB), SOP-8.
 *
 * Addressing scheme (per Fujitsu MB85RC1MT datasheet, NOT a 3-byte address):
 *  - Logical address range is 17 bits: 0x00000..0x1FFFF.
 *  - A16 (the top address bit) is folded into the 7-bit I2C slave address,
 *    not transmitted as part of the memory-address bytes.
 *  - A15..A0 are transmitted as the usual two memory-address bytes (MSB,LSB).
 *  - With A2=A1=0 (this board's strapping): bank 0 (0x00000-0x0FFFF) is
 *    accessed via slave address 0x50; bank 1 (0x10000-0x1FFFF) via 0x51.
 *
 * Consequence: a single I2C transaction can never cross the 0x10000 bank
 * boundary (the slave address itself would need to change mid-transaction).
 * MB85RC1MT_Read/Write split any request that would cross that boundary. */
#define MB85RC1MT_ADDR_7BIT_BASE   0x50u   /* A2=A1=0; bit0 = A16 (bank select) */
#define MB85RC1MT_SIZE_BYTES       0x20000u
#define MB85RC1MT_BANK_SIZE        0x10000u

typedef enum
{
	MB85RC1MT_RESULT_OK = 0,
	MB85RC1MT_RESULT_NOT_INITIALIZED,
	MB85RC1MT_RESULT_RANGE,
	MB85RC1MT_RESULT_NOT_READY,
	MB85RC1MT_RESULT_WRITE_FAILED,
	MB85RC1MT_RESULT_READ_FAILED,
	MB85RC1MT_RESULT_VERIFY_MISMATCH
} Mb85rc1mtResult;

void    MB85RC1MT_Init(I2C_HandleTypeDef* hi2c);
uint8_t MB85RC1MT_IsReady(void);
Mb85rc1mtResult MB85RC1MT_GetLastResult(void);
HAL_StatusTypeDef MB85RC1MT_GetLastHalStatus(void);
uint8_t MB85RC1MT_GetAddr7BitBase(void); /* fixed bank-0 base address (0x50); bank1 is base|1 */

uint8_t MB85RC1MT_Read(uint32_t address, uint8_t* dst, uint16_t len);
uint8_t MB85RC1MT_Write(uint32_t address, const uint8_t* src, uint16_t len);
uint8_t MB85RC1MT_WriteAndVerify(uint32_t address, const uint8_t* src, uint16_t len);

#ifdef __cplusplus
}
#endif

#endif
