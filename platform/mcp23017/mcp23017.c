#include "mcp23017.h"

/* ── Write one register ─────────────────────────────────────────────────── */
static void WriteReg(I2C_HandleTypeDef *hi2c, uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = { reg, val };
    HAL_I2C_Master_Transmit(hi2c, MCP23017_ADDR, buf, 2, 100);
}

/* ── Read one register ──────────────────────────────────────────────────── */
static uint8_t ReadReg(I2C_HandleTypeDef *hi2c, uint8_t reg)
{
    uint8_t val = 0xFF;

    if (HAL_I2C_Master_Transmit(hi2c, MCP23017_ADDR, &reg, 1, 10) != HAL_OK)
        return 0xFF;

    if (HAL_I2C_Master_Receive(hi2c, MCP23017_ADDR, &val, 1, 10) != HAL_OK)
        return 0xFF;

    return val;
}

/* ── Init ───────────────────────────────────────────────────────────────── */
void MCP23017_Init(I2C_HandleTypeDef *hi2c)
{
    HAL_Delay(50);

    /* write config twice, to survive a marginal cold start */
    WriteReg(hi2c, MCP_IODIRA, 0xFF);
    WriteReg(hi2c, MCP_IODIRB, 0xFF);
    WriteReg(hi2c, MCP_GPPUA,  0xFF);
    WriteReg(hi2c, MCP_GPPUB,  0xFF);

    HAL_Delay(2);

    WriteReg(hi2c, MCP_IODIRA, 0xFF);
    WriteReg(hi2c, MCP_IODIRB, 0xFF);
    WriteReg(hi2c, MCP_GPPUA,  0xFF);
    WriteReg(hi2c, MCP_GPPUB,  0xFF);

    HAL_Delay(2);

    /* flush first stale reads */
    (void)ReadReg(hi2c, MCP_GPIOA);
    (void)ReadReg(hi2c, MCP_GPIOB);
    (void)ReadReg(hi2c, MCP_GPIOA);
    (void)ReadReg(hi2c, MCP_GPIOB);
}

/* ── Read all 16 pins ───────────────────────────────────────────────────── */
uint16_t MCP23017_ReadGPIO(I2C_HandleTypeDef *hi2c)
{
    uint8_t a = ReadReg(hi2c, MCP_GPIOA);
    uint8_t b = ReadReg(hi2c, MCP_GPIOB);
    return (uint16_t)b | ((uint16_t)a << 8);
}

/* ── Individual port reads ──────────────────────────────────────────────── */
uint8_t MCP23017_ReadGPIOA(I2C_HandleTypeDef *hi2c)
{
    return ReadReg(hi2c, MCP_GPIOA);
}

uint8_t MCP23017_ReadGPIOB(I2C_HandleTypeDef *hi2c)
{
    return ReadReg(hi2c, MCP_GPIOB);
}

/* ── Output writes ──────────────────────────────────────────────────────── */
void MCP23017_WriteGPIOA(I2C_HandleTypeDef *hi2c, uint8_t val)
{
    WriteReg(hi2c, MCP_OLATA, val);
}

void MCP23017_WriteGPIOB(I2C_HandleTypeDef *hi2c, uint8_t val)
{
    WriteReg(hi2c, MCP_OLATB, val);
}