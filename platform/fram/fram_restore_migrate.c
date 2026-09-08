#include "platform/fram/fram_restore_migrate.h"
#include "platform/fram/mb85rc1mt.h"
#include <string.h>

uint8_t g_fram_restore_buf[FRAM_RESTORE_SIZE];
volatile uint32_t g_fram_restore_arm_magic = 0u;
volatile uint8_t  g_fram_restore_armed = 0u;
volatile uint8_t  g_fram_restore_write_verify_ok = 0u;
volatile uint32_t g_fram_restore_readback_crc32 = 0u;
volatile uint8_t  g_fram_restore_done = 0u;

/* Standard zlib/ISO-hdlc CRC32, matching the backup-dump implementation and
 * Python's zlib.crc32. */
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

void FramRestoreMigrate_Run(void)
{
    g_fram_restore_write_verify_ok = 0u;
    g_fram_restore_readback_crc32 = 0u;
    g_fram_restore_done = 0u;
    g_fram_restore_armed = (g_fram_restore_arm_magic == FRAM_RESTORE_ARM_MAGIC) ? 1u : 0u;

    if (!g_fram_restore_armed)
    {
        /* Not explicitly armed: ran, but deliberately touched nothing. */
        g_fram_restore_done = 1u;
        return;
    }

    /* g_fram_restore_buf must already contain the source image, loaded via
     * OpenOCD load_image before this call. Writes/verifies 0x0000-0x7FFF only. */
    uint8_t ok = MB85RC1MT_WriteAndVerify(0x00000u, g_fram_restore_buf, (uint16_t)FRAM_RESTORE_SIZE);
    g_fram_restore_write_verify_ok = ok;

    if (ok)
    {
        /* Independent re-read, overwriting the same buffer in place (RAM is too
         * tight to hold source and readback simultaneously) so it can be pulled
         * back out over SWD and diffed/decoded on the host exactly like a
         * fresh backup dump. */
        memset(g_fram_restore_buf, 0, sizeof(g_fram_restore_buf));
        if (MB85RC1MT_Read(0x00000u, g_fram_restore_buf, (uint16_t)FRAM_RESTORE_SIZE))
        {
            g_fram_restore_readback_crc32 = Crc32Compute(g_fram_restore_buf, FRAM_RESTORE_SIZE);
        }
    }

    g_fram_restore_done = 1u;
}
