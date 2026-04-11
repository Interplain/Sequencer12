#ifndef MCP23017_H
#define MCP23017_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32f4xx_hal.h"

// ─────────────────────────────────────────────
// I2C address — A0, A1, A2 all GND
// ─────────────────────────────────────────────
#define MCP23017_ADDR   (0x20 << 1)   // HAL uses 8-bit address

// ─────────────────────────────────────────────
// Registers
// ─────────────────────────────────────────────
#define MCP_IODIRA      0x00   // Direction A (1=input, 0=output)
#define MCP_IODIRB      0x01   // Direction B
#define MCP_IPOLA       0x02   // Polarity A
#define MCP_IPOLB       0x03   // Polarity B
#define MCP_GPINTENA    0x04   // Interrupt enable A
#define MCP_GPINTENB    0x05   // Interrupt enable B
#define MCP_GPPUA       0x0C   // Pull-up A
#define MCP_GPPUB       0x0D   // Pull-up B
#define MCP_GPIOA       0x12   // GPIO A read
#define MCP_GPIOB       0x13   // GPIO B read
#define MCP_OLATA       0x14   // Output latch A
#define MCP_OLATB       0x15   // Output latch B

// ─────────────────────────────────────────────
// Button bits (active low — 0 = pressed)
// ─────────────────────────────────────────────
#define BTN_PLAY_BIT    (1u << 0)   // GPA0
#define BTN_REC_BIT     (1u << 1)   // GPA1
#define BTN_SHIFT_BIT   (1u << 7)   // GPA7

// ─────────────────────────────────────────────
// Functions
// ─────────────────────────────────────────────
void     MCP23017_Init(I2C_HandleTypeDef *hi2c);
uint16_t MCP23017_ReadGPIO(I2C_HandleTypeDef *hi2c);
uint8_t  MCP23017_ReadGPIOA(I2C_HandleTypeDef *hi2c);
uint8_t  MCP23017_ReadGPIOB(I2C_HandleTypeDef *hi2c);
void     MCP23017_WriteGPIOA(I2C_HandleTypeDef *hi2c, uint8_t val);
void     MCP23017_WriteGPIOB(I2C_HandleTypeDef *hi2c, uint8_t val);

#ifdef __cplusplus
}
#endif

#endif