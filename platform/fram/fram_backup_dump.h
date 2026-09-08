#ifndef FRAM_BACKUP_DUMP_H
#define FRAM_BACKUP_DUMP_H

/* Temporary, removable read-only FRAM backup facility.
 * Only compiled when S12_FRAM_BACKUP_MODE is defined. Never issues any
 * MB85RC256_Write/WriteAndVerify/Format call — read-only by construction. */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FRAM_BACKUP_DUMP_SIZE 0x8000u /* 32768 bytes, 0x0000..0x7FFF */

/* Fixed-address SRAM buffer so it can be located from the ELF/map/nm output
 * and read back over SWD (e.g. via OpenOCD `dump_image`). */
extern uint8_t g_fram_backup_buf[FRAM_BACKUP_DUMP_SIZE];

extern volatile uint32_t g_fram_backup_bytes_read; /* bytes successfully read so far */
extern volatile uint32_t g_fram_backup_crc32;       /* zlib/ISO-hdlc CRC32 of the full buffer, valid only when g_fram_backup_done && g_fram_backup_ok */
extern volatile uint8_t  g_fram_backup_done;        /* 1 once the read pass has finished (success or failure) */
extern volatile uint8_t  g_fram_backup_ok;          /* 1 if exactly FRAM_BACKUP_DUMP_SIZE bytes were read without error */

/* Reads FRAM_BACKUP_DUMP_SIZE bytes starting at address 0x0000 into
 * g_fram_backup_buf, then computes CRC32 and updates the globals above. */
void FramBackupDump_Run(void);

#ifdef __cplusplus
}
#endif

#endif
