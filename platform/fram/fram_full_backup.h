#ifndef FRAM_FULL_BACKUP_H
#define FRAM_FULL_BACKUP_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FRAM_FULL_BACKUP_WINDOW_SIZE 0x1000u
#define FRAM_FULL_BACKUP_REGION_SIZE 0x8000u
#define FRAM_FULL_BACKUP_REGION_COUNT 4u
#define FRAM_FULL_BACKUP_SAMPLE_SIZE 0x40u

extern uint8_t g_fram_full_backup_window[FRAM_FULL_BACKUP_WINDOW_SIZE];
extern uint8_t g_fram_full_backup_alias_low[FRAM_FULL_BACKUP_SAMPLE_SIZE];
extern uint8_t g_fram_full_backup_alias_high[FRAM_FULL_BACKUP_SAMPLE_SIZE];
extern uint8_t g_fram_full_backup_schema_raw[FRAM_FULL_BACKUP_SAMPLE_SIZE];

extern volatile uint32_t g_fram_full_backup_region_crc32[FRAM_FULL_BACKUP_REGION_COUNT];
extern volatile uint32_t g_fram_full_backup_crc32;
extern volatile uint32_t g_fram_full_backup_bytes_read;
extern volatile uint32_t g_fram_full_backup_error_address;
extern volatile uint32_t g_fram_full_backup_window_address;
extern volatile uint8_t g_fram_full_backup_bank0_ready;
extern volatile uint8_t g_fram_full_backup_bank1_ready;
extern volatile uint8_t g_fram_full_backup_alias_low_ok;
extern volatile uint8_t g_fram_full_backup_alias_high_ok;
extern volatile uint8_t g_fram_full_backup_ok;
extern volatile uint8_t g_fram_full_backup_done;
extern volatile uint8_t g_fram_full_backup_window_ok;

void FramFullBackup_Run(void);
uint8_t FramFullBackup_ReadWindow(uint32_t address);

#ifdef __cplusplus
}
#endif

#endif