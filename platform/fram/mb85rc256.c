
#include "platform/fram/mb85rc256.h"
#include <string.h>

static I2C_HandleTypeDef* s_i2c = 0;
static uint8_t s_addr_7bit = MB85RC256_ADDR_7BIT;
static uint8_t s_found = 0u;
static Mb85rc256Result s_last_result = MB85RC256_RESULT_NOT_INITIALIZED;
static HAL_StatusTypeDef s_last_hal_status = HAL_OK;

/* Reset the I2C peripheral if the HAL state machine is stuck. */
static void I2C_Recover(void)
{
    if (s_i2c == 0) return;
    if (s_i2c->State == HAL_I2C_STATE_READY) return;
    HAL_I2C_DeInit(s_i2c);
    HAL_Delay(2);
    HAL_I2C_Init(s_i2c);
}

static uint8_t ScanFramAddress(void)
{
    if (s_i2c == 0) return 0u;

    for (uint8_t addr = 0x50u; addr <= 0x57u; addr++)
    {
        if (HAL_I2C_IsDeviceReady(s_i2c, (uint16_t)(addr << 1), 2, 20) == HAL_OK)
        {
            s_addr_7bit = addr;
            s_found = 1u;
            return 1u;
        }
    }

    s_found = 0u;
    return 0u;
}

static HAL_StatusTypeDef FramTransmit(const uint8_t* data, uint16_t len, uint32_t timeout)
{
    HAL_StatusTypeDef status = HAL_ERROR;

    if (s_i2c == 0 || data == 0) return HAL_ERROR;

    for (uint8_t attempt = 0u; attempt < 2u; ++attempt)
    {
        status = HAL_I2C_Master_Transmit(s_i2c,
                                         (uint16_t)(s_addr_7bit << 1),
                                         (uint8_t*)data,
                                         len,
                                         timeout);
        s_last_hal_status = status;
        if (status == HAL_OK)
        {
            return HAL_OK;
        }

        s_found = 0u;
        I2C_Recover();
        (void)ScanFramAddress();
    }

    return status;
}

static HAL_StatusTypeDef FramReceive(uint8_t* data, uint16_t len, uint32_t timeout)
{
    HAL_StatusTypeDef status = HAL_ERROR;

    if (s_i2c == 0 || data == 0) return HAL_ERROR;

    for (uint8_t attempt = 0u; attempt < 2u; ++attempt)
    {
        status = HAL_I2C_Master_Receive(s_i2c,
                                        (uint16_t)(s_addr_7bit << 1),
                                        data,
                                        len,
                                        timeout);
        s_last_hal_status = status;
        if (status == HAL_OK)
        {
            return HAL_OK;
        }

        s_found = 0u;
        I2C_Recover();
        (void)ScanFramAddress();
    }

    return status;
}

void MB85RC256_Init(I2C_HandleTypeDef* hi2c)
{
    s_i2c = hi2c;
    s_last_result = (hi2c != 0) ? MB85RC256_RESULT_OK : MB85RC256_RESULT_NOT_INITIALIZED;
    s_last_hal_status = HAL_OK;
    (void)ScanFramAddress();
}

// Write, then read back and verify in chunks (safe for large payloads)
uint8_t MB85RC256_WriteAndVerify(uint16_t address, const uint8_t* src, uint16_t len)
{
    uint8_t verify_buf[64];
    for (uint8_t attempt = 0u; attempt < 3u; ++attempt)
    {
        uint16_t offset = 0u;

        if (!MB85RC256_Write(address, src, len))
        {
            continue;
        }

        while (offset < len)
        {
            uint16_t chunk = (uint16_t)(len - offset);
            if (chunk > (uint16_t)sizeof(verify_buf))
            {
                chunk = (uint16_t)sizeof(verify_buf);
            }

            if (!MB85RC256_Read((uint16_t)(address + offset), verify_buf, chunk))
            {
                s_last_result = MB85RC256_RESULT_READ_FAILED;
                break;
            }

            if (memcmp(src + offset, verify_buf, chunk) != 0)
            {
                s_last_result = MB85RC256_RESULT_VERIFY_MISMATCH;
                break;
            }

            offset = (uint16_t)(offset + chunk);
        }

        if (offset == len)
        {
            s_last_result = MB85RC256_RESULT_OK;
            return 1;
        }

        s_found = 0u;
        I2C_Recover();
        (void)ScanFramAddress();
    }


    return 0;   // all attempts exhausted → genuine failure
}


uint8_t MB85RC256_GetAddress7bit(void)
{
    return s_addr_7bit;
}

Mb85rc256Result MB85RC256_GetLastResult(void)
{
    return s_last_result;
}

HAL_StatusTypeDef MB85RC256_GetLastHalStatus(void)
{
    return s_last_hal_status;
}

uint8_t MB85RC256_IsReady(void)
{
    if (s_i2c == 0)
    {
        s_last_result = MB85RC256_RESULT_NOT_INITIALIZED;
        return 0;
    }

    /* Explicit readiness probe only. Do not recover/rescan here because this
     * call is used from time-sensitive UI paths and should fail fast. */
    s_last_hal_status = HAL_I2C_IsDeviceReady(s_i2c,
                                              (uint16_t)(s_addr_7bit << 1),
                                              2,
                                              10);
    if (s_last_hal_status == HAL_OK)
    {
        s_last_result = MB85RC256_RESULT_OK;
        return 1u;
    }

    s_last_result = MB85RC256_RESULT_NOT_READY;
    return 0u;
}

uint8_t MB85RC256_Read(uint16_t address, uint8_t* dst, uint16_t len)
{
    uint8_t addr[2];

    if (s_i2c == 0 || dst == 0)
    {
        s_last_result = MB85RC256_RESULT_NOT_INITIALIZED;
        return 0;
    }
    if ((uint32_t)address + (uint32_t)len > MB85RC256_SIZE_BYTES)
    {
        s_last_result = MB85RC256_RESULT_RANGE;
        return 0;
    }
    addr[0] = (uint8_t)(address >> 8);
    addr[1] = (uint8_t)(address & 0xFFu);

    if (FramTransmit(addr, 2u, 20u) != HAL_OK)
    {
        s_last_result = MB85RC256_RESULT_WRITE_FAILED;
        return 0;
    }

    if (FramReceive(dst, len, (uint32_t)len + 50u) != HAL_OK)
    {
        s_last_result = MB85RC256_RESULT_READ_FAILED;
        return 0;
    }

    s_last_result = MB85RC256_RESULT_OK;
    return 1;
}

uint8_t MB85RC256_Write(uint16_t address, const uint8_t* src, uint16_t len)
{
    uint16_t remaining;
    uint16_t offset;

    if (s_i2c == 0 || src == 0)
    {
        s_last_result = MB85RC256_RESULT_NOT_INITIALIZED;
        return 0;
    }
    if ((uint32_t)address + (uint32_t)len > MB85RC256_SIZE_BYTES)
    {
        s_last_result = MB85RC256_RESULT_RANGE;
        return 0;
    }
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

        if (FramTransmit(frame, (uint16_t)(2u + chunk), 50u) != HAL_OK)
        {
            s_last_result = MB85RC256_RESULT_WRITE_FAILED;
            return 0;
        }

        offset = (uint16_t)(offset + chunk);
        remaining = (uint16_t)(remaining - chunk);
    }

    s_last_result = MB85RC256_RESULT_OK;
    return 1;
}

// Format/erase entire FRAM by writing 0xFF to all addresses
uint8_t MB85RC256_Format(void)
{
    uint8_t erase_buf[32];
    memset(erase_buf, 0xFF, sizeof(erase_buf));

    // Write 0xFF in 32-byte chunks to all 32KB (1024 chunks total)
    for (uint16_t i = 0; i < 1024u; i++)
    {
        uint16_t address = (uint16_t)(i * 32u);
        if (!MB85RC256_Write(address, erase_buf, sizeof(erase_buf)))
        {
            return 0;  // Failed
        }
    }

    return 1;  // Success
}
