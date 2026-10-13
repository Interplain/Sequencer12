#if defined(S12_FRAM_FULL_BACKUP_MODE)

#include "platform/fram/fram_full_backup.h"
#include "platform/fram/mb85rc1mt.h"
#include <string.h>

_Static_assert(FRAM_FULL_BACKUP_WINDOW_SIZE <= 0x1000u,
               "full-backup extraction window exceeds 4 KiB");
_Static_assert((MB85RC1MT_SIZE_BYTES % FRAM_FULL_BACKUP_WINDOW_SIZE) == 0u,
               "full FRAM size must be divisible by extraction window");
_Static_assert((FRAM_FULL_BACKUP_REGION_SIZE * FRAM_FULL_BACKUP_REGION_COUNT) ==
                   MB85RC1MT_SIZE_BYTES,
               "CRC regions must cover the complete FRAM exactly");
_Static_assert((FRAM_FULL_BACKUP_REGION_SIZE % FRAM_FULL_BACKUP_WINDOW_SIZE) == 0u,
               "CRC region size must be divisible by extraction window");

uint8_t g_fram_full_backup_window[FRAM_FULL_BACKUP_WINDOW_SIZE];
uint8_t g_fram_full_backup_alias_low[FRAM_FULL_BACKUP_SAMPLE_SIZE];
uint8_t g_fram_full_backup_alias_high[FRAM_FULL_BACKUP_SAMPLE_SIZE];
uint8_t g_fram_full_backup_schema_raw[FRAM_FULL_BACKUP_SAMPLE_SIZE];

volatile uint32_t g_fram_full_backup_region_crc32[FRAM_FULL_BACKUP_REGION_COUNT];
volatile uint32_t g_fram_full_backup_crc32 = 0u;
volatile uint32_t g_fram_full_backup_bytes_read = 0u;
volatile uint32_t g_fram_full_backup_error_address = 0xFFFFFFFFu;
volatile uint32_t g_fram_full_backup_window_address = 0xFFFFFFFFu;
volatile uint8_t g_fram_full_backup_bank0_ready = 0u;
volatile uint8_t g_fram_full_backup_bank1_ready = 0u;
volatile uint8_t g_fram_full_backup_alias_low_ok = 0u;
volatile uint8_t g_fram_full_backup_alias_high_ok = 0u;
volatile uint8_t g_fram_full_backup_ok = 0u;
volatile uint8_t g_fram_full_backup_done = 0u;
volatile uint8_t g_fram_full_backup_window_ok = 0u;

static uint32_t Crc32Update(uint32_t crc, const uint8_t* data, uint32_t len)
{
    for (uint32_t i = 0u; i < len; ++i)
    {
        crc ^= data[i];
        for (uint8_t bit = 0u; bit < 8u; ++bit)
        {
            const uint32_t mask = (uint32_t)(-(int32_t)(crc & 1u));
            crc = (crc >> 1) ^ (0xEDB88320u & mask);
        }
    }
    return crc;
}

uint8_t FramFullBackup_ReadWindow(uint32_t address)
{
    g_fram_full_backup_window_ok = 0u;
    g_fram_full_backup_window_address = 0xFFFFFFFFu;
    if ((address & (FRAM_FULL_BACKUP_WINDOW_SIZE - 1u)) != 0u ||
        address > (MB85RC1MT_SIZE_BYTES - FRAM_FULL_BACKUP_WINDOW_SIZE))
    {
        return 0u;
    }

    if (!MB85RC1MT_Read(address,
                        g_fram_full_backup_window,
                        (uint16_t)FRAM_FULL_BACKUP_WINDOW_SIZE))
    {
        return 0u;
    }

    g_fram_full_backup_window_address = address;
    g_fram_full_backup_window_ok = 1u;
    return 1u;
}

void FramFullBackup_Run(void)
{
    uint32_t full_crc = 0xFFFFFFFFu;

    memset(g_fram_full_backup_window, 0, sizeof(g_fram_full_backup_window));
    memset(g_fram_full_backup_alias_low, 0, sizeof(g_fram_full_backup_alias_low));
    memset(g_fram_full_backup_alias_high, 0, sizeof(g_fram_full_backup_alias_high));
    memset(g_fram_full_backup_schema_raw, 0, sizeof(g_fram_full_backup_schema_raw));
    memset((void*)g_fram_full_backup_region_crc32, 0,
           sizeof(g_fram_full_backup_region_crc32));

    g_fram_full_backup_crc32 = 0u;
    g_fram_full_backup_bytes_read = 0u;
    g_fram_full_backup_error_address = 0xFFFFFFFFu;
    g_fram_full_backup_window_address = 0xFFFFFFFFu;
    g_fram_full_backup_bank0_ready = 0u;
    g_fram_full_backup_bank1_ready = 0u;
    g_fram_full_backup_alias_low_ok = 0u;
    g_fram_full_backup_alias_high_ok = 0u;
    g_fram_full_backup_ok = 0u;
    g_fram_full_backup_done = 0u;
    g_fram_full_backup_window_ok = 0u;

    g_fram_full_backup_bank0_ready = MB85RC1MT_IsReady() ? 1u : 0u;
    if (!g_fram_full_backup_bank0_ready)
    {
        g_fram_full_backup_error_address = 0u;
        g_fram_full_backup_done = 1u;
        return;
    }

    g_fram_full_backup_bank1_ready =
        MB85RC1MT_Read(0x10000u, g_fram_full_backup_window, 1u) ? 1u : 0u;
    if (!g_fram_full_backup_bank1_ready)
    {
        g_fram_full_backup_error_address = 0x10000u;
        g_fram_full_backup_done = 1u;
        return;
    }

    for (uint8_t region = 0u; region < FRAM_FULL_BACKUP_REGION_COUNT; ++region)
    {
        const uint32_t region_start = (uint32_t)region * FRAM_FULL_BACKUP_REGION_SIZE;
        const uint32_t region_end = region_start + FRAM_FULL_BACKUP_REGION_SIZE;
        uint32_t region_crc = 0xFFFFFFFFu;

        for (uint32_t address = region_start;
             address < region_end;
             address += FRAM_FULL_BACKUP_WINDOW_SIZE)
        {
            if (!FramFullBackup_ReadWindow(address))
            {
                g_fram_full_backup_error_address = address;
                g_fram_full_backup_done = 1u;
                return;
            }

            region_crc = Crc32Update(region_crc,
                                    g_fram_full_backup_window,
                                    FRAM_FULL_BACKUP_WINDOW_SIZE);
            full_crc = Crc32Update(full_crc,
                                  g_fram_full_backup_window,
                                  FRAM_FULL_BACKUP_WINDOW_SIZE);
            g_fram_full_backup_bytes_read += FRAM_FULL_BACKUP_WINDOW_SIZE;
        }

        g_fram_full_backup_region_crc32[region] = region_crc ^ 0xFFFFFFFFu;
    }

    g_fram_full_backup_alias_low_ok =
        MB85RC1MT_Read(0x0D260u,
                       g_fram_full_backup_alias_low,
                       FRAM_FULL_BACKUP_SAMPLE_SIZE) ? 1u : 0u;
    if (!g_fram_full_backup_alias_low_ok)
    {
        g_fram_full_backup_error_address = 0x0D260u;
        g_fram_full_backup_done = 1u;
        return;
    }

    g_fram_full_backup_alias_high_ok =
        MB85RC1MT_Read(0x1D260u,
                       g_fram_full_backup_alias_high,
                       FRAM_FULL_BACKUP_SAMPLE_SIZE) ? 1u : 0u;
    if (!g_fram_full_backup_alias_high_ok)
    {
        g_fram_full_backup_error_address = 0x1D260u;
        g_fram_full_backup_done = 1u;
        return;
    }

    memcpy(g_fram_full_backup_schema_raw,
           g_fram_full_backup_alias_high,
           sizeof(g_fram_full_backup_schema_raw));
    g_fram_full_backup_crc32 = full_crc ^ 0xFFFFFFFFu;
    g_fram_full_backup_ok = 1u;
    g_fram_full_backup_done = 1u;
}

#endif