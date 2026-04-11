#include "mcp23017.h"

// ─────────────────────────────────────────────
// Write one register
// ─────────────────────────────────────────────
static void WriteReg(I2C_HandleTypeDef *hi2c,
                     uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = {reg, val};
    HAL_I2C_Master_Transmit(hi2c, MCP23017_ADDR,
                            buf, 2, HAL_MAX_DELAY);
}

// ─────────────────────────────────────────────
// Read one register
// ─────────────────────────────────────────────
static uint8_t ReadReg(I2C_HandleTypeDef *hi2c, uint8_t reg)
{
    uint8_t val = 0;
    HAL_I2C_Master_Transmit(hi2c, MCP23017_ADDR,
                            &reg, 1, HAL_MAX_DELAY);
    HAL_I2C_Master_Receive(hi2c, MCP23017_ADDR,
                           &val, 1, HAL_MAX_DELAY);
    return val;
}

// ─────────────────────────────────────────────
// Init — all pins as inputs with pull-ups
// ─────────────────────────────────────────────
void MCP23017_Init(I2C_HandleTypeDef *hi2c)
{
    // All GPA pins = inputs
    WriteReg(hi2c, MCP_IODIRA, 0xFF);

    // All GPB pins = inputs
    WriteReg(hi2c, MCP_IODIRB, 0xFF);

    // Enable pull-ups on GPA
    WriteReg(hi2c, MCP_GPPUA, 0xFF);

    // Enable pull-ups on GPB
    WriteReg(hi2c, MCP_GPPUB, 0xFF);

    // Invert polarity — so 1 = pressed, 0 = released
    WriteReg(hi2c, MCP_IPOLA, 0xFF);
    WriteReg(hi2c, MCP_IPOLB, 0xFF);
}

// ─────────────────────────────────────────────
// Read all 16 pins — GPA in low byte, GPB in high byte
// With polarity inversion: 1 = pressed, 0 = released
// ─────────────────────────────────────────────
uint16_t MCP23017_ReadGPIO(I2C_HandleTypeDef *hi2c)
{
    uint8_t a = ReadReg(hi2c, MCP_GPIOA);
    uint8_t b = ReadReg(hi2c, MCP_GPIOB);
    return (uint16_t)a | ((uint16_t)b << 8);
}

uint8_t MCP23017_ReadGPIOA(I2C_HandleTypeDef *hi2c)
{
    return ReadReg(hi2c, MCP_GPIOA);
}

uint8_t MCP23017_ReadGPIOB(I2C_HandleTypeDef *hi2c)
{
    return ReadReg(hi2c, MCP_GPIOB);
}

void MCP23017_WriteGPIOA(I2C_HandleTypeDef *hi2c, uint8_t val)
{
    WriteReg(hi2c, MCP_OLATA, val);
}

void MCP23017_WriteGPIOB(I2C_HandleTypeDef *hi2c, uint8_t val)
{
    WriteReg(hi2c, MCP_OLATB, val);
}