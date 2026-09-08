#ifndef FRAM_RESTORE_MIGRATE_H
#define FRAM_RESTORE_MIGRATE_H

/* Temporary, removable MB85RC1MT restore-and-verify facility.
 * Only compiled when S12_FRAM_RESTORE_MODE is defined.
 *
 * Usage: flash this build, halt at reset (before main() runs), use OpenOCD
 * `load_image` to place the previously verified 32 KiB backup image into
 * g_fram_restore_buf, then resume. FramRestoreMigrate_Run() writes that
 * buffer into MB85RC1MT address 0x0000-0x7FFF (legacy 32 KiB range, entirely
 * within bank 0) via write+verify, then overwrites the SAME buffer with an
 * independent full re-read of that range (so a single 32 KiB SRAM buffer
 * serves as both the write source and the OpenOCD-retrievable readback
 * capture — RAM cannot hold two 32 KiB buffers at once here). Only
 * 0x0000-0x7FFF is ever touched; nothing above 0x7FFF is written. */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FRAM_RESTORE_SIZE 0x8000u /* 32768 bytes, matches legacy MB85RC256 image */

/* Run() performs no write at all unless this equals FRAM_RESTORE_ARM_MAGIC at
 * entry. Defaults to 0 (BSS zero-init), so a firmware auto-run immediately
 * after flashing is always a no-op — the operator must explicitly poke this
 * value via OpenOCD (e.g. `mww <addr> 0xA5C3ED17`) before resuming. */
#define FRAM_RESTORE_ARM_MAGIC 0xA5C3ED17u
extern volatile uint32_t g_fram_restore_arm_magic;
extern volatile uint8_t  g_fram_restore_armed; /* 1 if the magic matched at Run() entry */

/* Loaded externally via OpenOCD load_image (source image) before
 * FramRestoreMigrate_Run() is called; overwritten in place with the
 * post-write readback once the run completes. */
 extern uint8_t g_fram_restore_buf[FRAM_RESTORE_SIZE];

extern volatile uint8_t  g_fram_restore_write_verify_ok; /* 1 if MB85RC1MT_WriteAndVerify succeeded */
extern volatile uint32_t g_fram_restore_readback_crc32;  /* CRC32 of the independent full re-read of 0x0000-0x7FFF */
extern volatile uint8_t  g_fram_restore_done;

void FramRestoreMigrate_Run(void);

#ifdef __cplusplus
}
#endif

#endif
