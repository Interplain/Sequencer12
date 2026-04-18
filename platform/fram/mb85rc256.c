#include "platform/fram/mb85rc256.h"

static I2C_HandleTypeDef* s_i2c = 0;
static uint8_t s_addr_7bit = MB85RC256_ADDR_7BIT;

void MB85RC256_Init(I2C_HandleTypeDef* hi2c)
{
    s_i2c = hi2c;

    /* Auto-scan valid MB85RC256 address range: 0x50..0x57 (A2/A1/A0). */
    for (uint8_t addr = 0x50u; addr <= 0x57u; addr++)
    {
        if (HAL_I2C_IsDeviceReady(s_i2c, (uint16_t)(addr << 1), 2, 20) == HAL_OK)
        {
            s_addr_7bit = addr;
            return;
        }
    }
}

uint8_t MB85RC256_GetAddress7bit(void)
{
    return s_addr_7bit;
}

/* Reset the I2C peripheral if the HAL state machine is stuck. */
static void I2C_Recover(void)
{
    if (s_i2c == 0) return;
    if (s_i2c->State == HAL_I2C_STATE_READY) return;
    HAL_I2C_DeInit(s_i2c);
    HAL_Delay(2);
    HAL_I2C_Init(s_i2c);
}

uint8_t MB85RC256_IsReady(void)
{
    if (s_i2c == 0) return 0;
    I2C_Recover();
    return (HAL_I2C_IsDeviceReady(s_i2c,
                                  (uint16_t)(s_addr_7bit << 1),
                                  2,
                                  20) == HAL_OK) ? 1u : 0u;
}

uint8_t MB85RC256_Read(uint16_t address, uint8_t* dst, uint16_t len)
{
    uint8_t addr[2];

    if (s_i2c == 0 || dst == 0) return 0;
    if ((uint32_t)address + (uint32_t)len > MB85RC256_SIZE_BYTES) return 0;
    I2C_Recover();

    addr[0] = (uint8_t)(address >> 8);
    addr[1] = (uint8_t)(address & 0xFFu);

    if (HAL_I2C_Master_Transmit(s_i2c,
                                (uint16_t)(s_addr_7bit << 1),
                                addr,
                                2,
                                20) != HAL_OK)
    {
        return 0;
    }

    if (HAL_I2C_Master_Receive(s_i2c,
                               (uint16_t)(s_addr_7bit << 1),
                               dst,
                               len,
                               (uint32_t)len + 50u) != HAL_OK)
    {
        return 0;
    }

    return 1;
}

uint8_t MB85RC256_Write(uint16_t address, const uint8_t* src, uint16_t len)
{
    uint16_t remaining;
    uint16_t offset;

    if (s_i2c == 0 || src == 0) return 0;
    if ((uint32_t)address + (uint32_t)len > MB85RC256_SIZE_BYTES) return 0;
    I2C_Recover();

    remaining = len;
    offset = 0;

    while (remaining > 0)
    {
        uint8_t frame[34];
        uint16_t chunk = (remaining > 32u) ? 32u : remaining;
        uint16_t addr_now = (uint16_t)(address + offset);

        frame[0] = (uint8_t)(addr_now >> 8);
        frame[1] = (uint8_t)(addr_now & 0xFFu);
        for (uint16_t i = 0; i < chunk; i++)
        {
            frame[2 + i] = src[offset + i];
        }

        if (HAL_I2C_Master_Transmit(s_i2c,
                                    (uint16_t)(s_addr_7bit << 1),
                                    frame,
                                    (uint16_t)(2u + chunk),
                                    50) != HAL_OK)
        {
            return 0;
        }

        offset = (uint16_t)(offset + chunk);
        remaining = (uint16_t)(remaining - chunk);
    }

    return 1;
}
