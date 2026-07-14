#include "mcp23017.h"

static I2C_HandleTypeDef *s_mcp_i2c = NULL;
static uint16_t s_prev_raw = 0u;
static uint16_t s_last_good = 0u;

/* ---------- low-level helpers ---------- */
static bool mcp_write_reg(I2C_HandleTypeDef *hi2c, uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = { reg, val };
    return HAL_I2C_Master_Transmit(hi2c, MCP23017_ADDR, buf, 2, 10) == HAL_OK;
}

static bool mcp_read_ab(I2C_HandleTypeDef *hi2c, uint8_t *a, uint8_t *b)
{
    uint8_t reg = MCP_GPIOA;
    uint8_t buf[2] = {0xFF, 0xFF};

    if (HAL_I2C_Master_Transmit(hi2c, MCP23017_ADDR, &reg, 1, 10) != HAL_OK)
        return false;
    if (HAL_I2C_Master_Receive(hi2c, MCP23017_ADDR, buf, 2, 10) != HAL_OK)
        return false;

    *a = buf[0];
    *b = buf[1];
    return true;
}

static uint8_t read_reg_compat(I2C_HandleTypeDef *hi2c, uint8_t reg)
{
    uint8_t val = 0xFFu;
    (void)HAL_I2C_Mem_Read(hi2c, MCP23017_ADDR, reg, I2C_MEMADD_SIZE_8BIT, &val, 1, 20);
    return val;
}

/* ---------- public API ---------- */
bool MCP_Init(I2C_HandleTypeDef *hi2c)
{
    s_mcp_i2c = hi2c;

    if (HAL_I2C_IsDeviceReady(s_mcp_i2c, MCP23017_ADDR, 3, 10) != HAL_OK)
        return false;

    bool ok = true;
    ok &= mcp_write_reg(s_mcp_i2c, MCP_IOCON, 0x00u);
    ok &= mcp_write_reg(s_mcp_i2c, MCP_IODIRA, 0xFFu);
    ok &= mcp_write_reg(s_mcp_i2c, MCP_IODIRB, 0xFFu);
    ok &= mcp_write_reg(s_mcp_i2c, MCP_GPPUA, 0xFFu);
    ok &= mcp_write_reg(s_mcp_i2c, MCP_GPPUB, 0xFFu);
    ok &= mcp_write_reg(s_mcp_i2c, MCP_IPOLA, 0xFFu);
    ok &= mcp_write_reg(s_mcp_i2c, MCP_IPOLB, 0xFFu);

    return ok;
}

uint16_t MCP_ReadGPIO(void)
{
    if (s_mcp_i2c == NULL)
        return s_last_good;

    uint8_t a = 0xFFu;
    uint8_t b = 0xFFu;

    if (!mcp_read_ab(s_mcp_i2c, &a, &b))
        return s_last_good;

    s_last_good = (uint16_t)b | ((uint16_t)a << 8);
    return s_last_good;
}

void PrimeButtons(void)
{
    s_prev_raw = MCP_ReadGPIO();
}

uint16_t MCP_ScanEdges(void)
{
    uint16_t raw = MCP_ReadGPIO();
    uint16_t rising = (uint16_t)(raw & (uint16_t)~s_prev_raw);
    s_prev_raw = raw;
    return (uint16_t)(rising & (BTN_PLAY | BTN_REC));
}

bool MCP_ShiftHeld(void)
{
    return (s_last_good & BTN_SHIFT) != 0u;
}

/* ---------- compatibility wrappers ---------- */
uint8_t MCP23017_ReadReg(I2C_HandleTypeDef *hi2c, uint8_t reg)
{
    return read_reg_compat(hi2c, reg);
}

void MCP23017_Init(I2C_HandleTypeDef *hi2c)
{
    (void)MCP_Init(hi2c);
    PrimeButtons();
}

void MCP23017_SetDirections(I2C_HandleTypeDef *hi2c, uint8_t iodira, uint8_t iodirb)
{
    (void)HAL_I2C_Mem_Write(hi2c, MCP23017_ADDR, MCP_IODIRA, I2C_MEMADD_SIZE_8BIT, &iodira, 1, 20);
    (void)HAL_I2C_Mem_Write(hi2c, MCP23017_ADDR, MCP_IODIRB, I2C_MEMADD_SIZE_8BIT, &iodirb, 1, 20);
}

void MCP23017_SetPullups(I2C_HandleTypeDef *hi2c, uint8_t gppua, uint8_t gppub)
{
    (void)HAL_I2C_Mem_Write(hi2c, MCP23017_ADDR, MCP_GPPUA, I2C_MEMADD_SIZE_8BIT, &gppua, 1, 20);
    (void)HAL_I2C_Mem_Write(hi2c, MCP23017_ADDR, MCP_GPPUB, I2C_MEMADD_SIZE_8BIT, &gppub, 1, 20);
}

uint16_t MCP23017_ReadGPIO(I2C_HandleTypeDef *hi2c)
{
    if (hi2c == s_mcp_i2c)
        return MCP_ReadGPIO();

    uint8_t a = read_reg_compat(hi2c, MCP_GPIOA);
    uint8_t b = read_reg_compat(hi2c, MCP_GPIOB);
    return (uint16_t)b | ((uint16_t)a << 8);
}

uint8_t MCP23017_ReadGPIOA(I2C_HandleTypeDef *hi2c)
{
    return read_reg_compat(hi2c, MCP_GPIOA);
}

uint8_t MCP23017_ReadGPIOB(I2C_HandleTypeDef *hi2c)
{
    return read_reg_compat(hi2c, MCP_GPIOB);
}

void MCP23017_WriteGPIOA(I2C_HandleTypeDef *hi2c, uint8_t val)
{
    (void)HAL_I2C_Mem_Write(hi2c, MCP23017_ADDR, MCP_OLATA, I2C_MEMADD_SIZE_8BIT, &val, 1, 20);
}

void MCP23017_WriteGPIOB(I2C_HandleTypeDef *hi2c, uint8_t val)
{
    (void)HAL_I2C_Mem_Write(hi2c, MCP23017_ADDR, MCP_OLATB, I2C_MEMADD_SIZE_8BIT, &val, 1, 20);
}