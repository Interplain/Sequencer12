#include "platform/fram/mb85rc1mt.h"
#include "stm32/time_commit_probe.h"
#include <string.h>

static I2C_HandleTypeDef* s_i2c = 0;
static Mb85rc1mtResult s_last_result = MB85RC1MT_RESULT_NOT_INITIALIZED;
static HAL_StatusTypeDef s_last_hal_status = HAL_OK;

/* Reset the I2C peripheral if the HAL state machine is stuck. */
static void I2C_Recover(void)
{
    if (s_i2c == 0) return;
    if (s_i2c->State == HAL_I2C_STATE_READY) return;
    TimeCommitProbe_RecordI2cRecovery();
    HAL_I2C_DeInit(s_i2c);
    HAL_Delay(2);
    HAL_I2C_Init(s_i2c);
}

/* A16 is folded into the 7-bit slave address (bit0); A2=A1=0 on this board. */
static uint8_t BankAddr7Bit(uint32_t address)
{
    uint8_t bank = (uint8_t)((address >> 16) & 0x1u);
    return (uint8_t)(MB85RC1MT_ADDR_7BIT_BASE | bank);
}

/* Bytes remaining in the current 64 KiB bank before the slave address must change. */
static uint32_t BytesUntilBankBoundary(uint32_t address)
{
    uint32_t offset_in_bank = address & (MB85RC1MT_BANK_SIZE - 1u);
    return MB85RC1MT_BANK_SIZE - offset_in_bank;
}

static HAL_StatusTypeDef FramTransmit(uint8_t addr7, const uint8_t* data, uint16_t len, uint32_t timeout)
{
    HAL_StatusTypeDef status = HAL_ERROR;

    if (s_i2c == 0 || data == 0) return HAL_ERROR;

    for (uint8_t attempt = 0u; attempt < 2u; ++attempt)
    {
        status = HAL_I2C_Master_Transmit(s_i2c,
                                         (uint16_t)(addr7 << 1),
                                         (uint8_t*)data,
                                         len,
                                         timeout);
                        TimeCommitProbe_RecordHal(0u, len, timeout, status == HAL_OK ? 1u : 0u);
        s_last_hal_status = status;
        if (status == HAL_OK)
        {
            return HAL_OK;
        }

        I2C_Recover();
    }

    return status;
}

static HAL_StatusTypeDef FramReceive(uint8_t addr7, uint8_t* data, uint16_t len, uint32_t timeout)
{
    HAL_StatusTypeDef status = HAL_ERROR;

    if (s_i2c == 0 || data == 0) return HAL_ERROR;

    for (uint8_t attempt = 0u; attempt < 2u; ++attempt)
    {
        status = HAL_I2C_Master_Receive(s_i2c,
                                        (uint16_t)(addr7 << 1),
                                        data,
                                        len,
                                        timeout);
                        TimeCommitProbe_RecordHal(1u, len, timeout, status == HAL_OK ? 1u : 0u);
        s_last_hal_status = status;
        if (status == HAL_OK)
        {
            return HAL_OK;
        }

        I2C_Recover();
    }

    return status;
}

void MB85RC1MT_Init(I2C_HandleTypeDef* hi2c)
{
    s_i2c = hi2c;
    s_last_result = (hi2c != 0) ? MB85RC1MT_RESULT_OK : MB85RC1MT_RESULT_NOT_INITIALIZED;
    s_last_hal_status = HAL_OK;
}

uint8_t MB85RC1MT_IsReady(void)
{
    if (s_i2c == 0)
    {
        s_last_result = MB85RC1MT_RESULT_NOT_INITIALIZED;
        return 0;
    }

    /* Bank 0 address stands in for chip presence; bank 1 is the same physical
     * die and does not need a separate liveness probe. */
    s_last_hal_status = HAL_I2C_IsDeviceReady(s_i2c,
                                              (uint16_t)(MB85RC1MT_ADDR_7BIT_BASE << 1),
                                              2,
                                              10);
    if (s_last_hal_status == HAL_OK)
    {
        s_last_result = MB85RC1MT_RESULT_OK;
        return 1u;
    }

    s_last_result = MB85RC1MT_RESULT_NOT_READY;
    return 0u;
}

Mb85rc1mtResult MB85RC1MT_GetLastResult(void)
{
    return s_last_result;
}

uint8_t MB85RC1MT_GetAddr7BitBase(void)
{
    return MB85RC1MT_ADDR_7BIT_BASE;
}

HAL_StatusTypeDef MB85RC1MT_GetLastHalStatus(void)
{
    return s_last_hal_status;
}

uint8_t MB85RC1MT_Read(uint32_t address, uint8_t* dst, uint16_t len)
{
    TimeCommitProbe_RecordPatternReadBegin(address, len);
    if (s_i2c == 0 || dst == 0)
    {
        s_last_result = MB85RC1MT_RESULT_NOT_INITIALIZED;
        TimeCommitProbe_RecordPatternReadEnd(0u);
        return 0;
    }
    if ((uint64_t)address + (uint64_t)len > MB85RC1MT_SIZE_BYTES)
    {
        s_last_result = MB85RC1MT_RESULT_RANGE;
        TimeCommitProbe_RecordPatternReadEnd(0u);
        return 0;
    }

    uint32_t remaining = len;
    uint32_t offset = 0u;

    while (remaining > 0u)
    {
        const uint32_t addr_now = address + offset;
        const uint32_t max_in_bank = BytesUntilBankBoundary(addr_now);
        const uint32_t chunk = (remaining > max_in_bank) ? max_in_bank : remaining;

        const uint8_t addr7 = BankAddr7Bit(addr_now);
        const uint16_t word_addr = (uint16_t)(addr_now & 0xFFFFu);
        uint8_t addr_bytes[2] = { (uint8_t)(word_addr >> 8), (uint8_t)(word_addr & 0xFFu) };

        if (FramTransmit(addr7, addr_bytes, 2u, 20u) != HAL_OK)
        {
            s_last_result = MB85RC1MT_RESULT_WRITE_FAILED;
            TimeCommitProbe_RecordPatternReadEnd(0u);
            return 0;
        }
        if (FramReceive(addr7, dst + offset, (uint16_t)chunk, chunk + 50u) != HAL_OK)
        {
            s_last_result = MB85RC1MT_RESULT_READ_FAILED;
            TimeCommitProbe_RecordPatternReadEnd(0u);
            return 0;
        }

        offset += chunk;
        remaining -= chunk;
    }

    s_last_result = MB85RC1MT_RESULT_OK;
    TimeCommitProbe_RecordPatternReadEnd(1u);
    return 1;
}

uint8_t MB85RC1MT_Write(uint32_t address, const uint8_t* src, uint16_t len)
{
    if (s_i2c == 0 || src == 0)
    {
        s_last_result = MB85RC1MT_RESULT_NOT_INITIALIZED;
        TimeCommitProbe_RecordPatternWrite(address, len, 0u);
        return 0;
    }
    if ((uint64_t)address + (uint64_t)len > MB85RC1MT_SIZE_BYTES)
    {
        s_last_result = MB85RC1MT_RESULT_RANGE;
        TimeCommitProbe_RecordPatternWrite(address, len, 0u);
        return 0;
    }

    uint32_t remaining = len;
    uint32_t offset = 0u;

    while (remaining > 0u)
    {
        const uint32_t addr_now = address + offset;
        const uint32_t max_in_bank = BytesUntilBankBoundary(addr_now);
        uint32_t chunk = (remaining > 32u) ? 32u : remaining;
        if (chunk > max_in_bank)
        {
            chunk = max_in_bank;
        }

        const uint8_t addr7 = BankAddr7Bit(addr_now);
        const uint16_t word_addr = (uint16_t)(addr_now & 0xFFFFu);
        uint8_t frame[34];
        frame[0] = (uint8_t)(word_addr >> 8);
        frame[1] = (uint8_t)(word_addr & 0xFFu);
        memcpy(&frame[2], src + offset, chunk);

        if (FramTransmit(addr7, frame, (uint16_t)(2u + chunk), 50u) != HAL_OK)
        {
            s_last_result = MB85RC1MT_RESULT_WRITE_FAILED;
            TimeCommitProbe_RecordPatternWrite(address, len, 0u);
            return 0;
        }

        offset += chunk;
        remaining -= chunk;
    }

    s_last_result = MB85RC1MT_RESULT_OK;
    TimeCommitProbe_RecordPatternWrite(address, len, 1u);
    return 1;
}

uint8_t MB85RC1MT_WriteAndVerify(uint32_t address, const uint8_t* src, uint16_t len)
{
    uint8_t verify_buf[64];

    for (uint8_t attempt = 0u; attempt < 3u; ++attempt)
    {
        uint32_t offset = 0u;

        if (!MB85RC1MT_Write(address, src, len))
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

            if (!MB85RC1MT_Read(address + offset, verify_buf, chunk))
            {
                s_last_result = MB85RC1MT_RESULT_READ_FAILED;
                break;
            }

            if (memcmp(src + offset, verify_buf, chunk) != 0)
            {
                s_last_result = MB85RC1MT_RESULT_VERIFY_MISMATCH;
                break;
            }

            offset += chunk;
        }

        if (offset == len)
        {
            s_last_result = MB85RC1MT_RESULT_OK;
            return 1;
        }

        I2C_Recover();
    }

    return 0;
}
