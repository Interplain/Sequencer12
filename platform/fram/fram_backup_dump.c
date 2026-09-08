#include "platform/fram/fram_backup_dump.h"
#include "platform/fram/mb85rc256.h"
#include <string.h>

uint8_t g_fram_backup_buf[FRAM_BACKUP_DUMP_SIZE];
volatile uint32_t g_fram_backup_bytes_read = 0u;
volatile uint32_t g_fram_backup_crc32 = 0u;
volatile uint8_t  g_fram_backup_done = 0u;
volatile uint8_t  g_fram_backup_ok = 0u;

/* Standard zlib/ISO-hdlc CRC32 (poly 0xEDB88320, reflected), matching
 * Python's zlib.crc32 so the host verification script needs no special-casing. */
static uint32_t Crc32Compute(const uint8_t* data, uint32_t len)
{
    uint32_t crc = 0xFFFFFFFFu;
    for (uint32_t i = 0u; i < len; ++i)
    {
        crc ^= data[i];
        for (uint8_t bit = 0u; bit < 8u; ++bit)
        {
            uint32_t mask = (uint32_t)(-(int32_t)(crc & 1u));
            crc = (crc >> 1) ^ (0xEDB88320u & mask);
        }
    }
    return crc ^ 0xFFFFFFFFu;
}

void FramBackupDump_Run(void)
{
    g_fram_backup_bytes_read = 0u;
    g_fram_backup_crc32 = 0u;
    g_fram_backup_done = 0u;
    g_fram_backup_ok = 0u;

    memset(g_fram_backup_buf, 0, sizeof(g_fram_backup_buf));

    /* Conservative chunk size well within the driver's I2C transfer limits.
     * MB85RC256_Read never writes; this loop issues reads only. */
    const uint16_t kChunkSize = 64u;
    uint32_t offset = 0u;
    uint8_t ok = 1u;

    while (offset < FRAM_BACKUP_DUMP_SIZE)
    {
        uint32_t remaining = FRAM_BACKUP_DUMP_SIZE - offset;
        uint16_t len = (uint16_t)((remaining > kChunkSize) ? kChunkSize : remaining);

        if (!MB85RC256_Read((uint16_t)offset, &g_fram_backup_buf[offset], len))
        {
            ok = 0u;
            break;
        }

        offset += len;
        g_fram_backup_bytes_read = offset;
    }

    if (ok && offset == FRAM_BACKUP_DUMP_SIZE)
    {
        g_fram_backup_crc32 = Crc32Compute(g_fram_backup_buf, FRAM_BACKUP_DUMP_SIZE);
        g_fram_backup_ok = 1u;
    }

    g_fram_backup_done = 1u;
}
