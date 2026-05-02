#ifndef FRAM_LAYOUT_H
#define FRAM_LAYOUT_H

#include <stdint.h>

/* MB85RC256V total capacity: 32 KiB (0x0000..0x7FFF). */
#define FRAM_TOTAL_SIZE             0x8000u

/* Header / metadata region. */
#define FRAM_USERCHORDS_MAGIC_ADDR  0x0000u  /* 4 bytes */
#define FRAM_CALIBRATION_ADDR       0x0004u  /* 28-byte CalBlob: 0x0004..0x001F */

/* User chord library region.
 * Each slot is 22 bytes (name[17] + note_mask[2] + in_use[1] + padding[2])
 * 128 slots => 2816 bytes (0x0B00). */
#define FRAM_USERCHORDS_ADDR        0x0020u
#define FRAM_USERCHORDS_SIZE        0x0B00u

/* Tail partitions reserved for persistent settings and future expansion. */
#define FRAM_SETTINGS_SIZE          0x0100u
#define FRAM_FUTURE_SIZE            0x0100u
#define FRAM_SETTINGS_ADDR          (FRAM_TOTAL_SIZE - FRAM_SETTINGS_SIZE - FRAM_FUTURE_SIZE)
#define FRAM_FUTURE_ADDR            (FRAM_TOTAL_SIZE - FRAM_FUTURE_SIZE)

/* Settings partition blob header for future parameters. */
#define FRAM_SETTINGS_MAGIC         0x53313253u /* "S12S" */
#define FRAM_SETTINGS_VERSION       1u

typedef struct
{
	uint32_t magic;          /* FRAM_SETTINGS_MAGIC */
	uint16_t version;        /* schema version */
	uint16_t payload_size;   /* bytes used after this header */
	uint32_t checksum;       /* checksum of payload bytes */
	uint32_t flags;          /* reserved feature flags */
	uint8_t  reserved[32];   /* room for first settings fields */
} FramSettingsBlob;

/* Song/pattern persistence occupies the middle contiguous region. */
#define FRAM_SONGDATA_ADDR          (FRAM_USERCHORDS_ADDR + FRAM_USERCHORDS_SIZE)
#define FRAM_SONGDATA_SIZE          (FRAM_SETTINGS_ADDR - FRAM_SONGDATA_ADDR)

/* Backwards-compatible alias used by existing code. */
#define FRAM_MAGIC_ADDR             FRAM_USERCHORDS_MAGIC_ADDR

/* Compile-time sanity checks to prevent partition overlap. */
#if (FRAM_SONGDATA_SIZE <= 0u)
#error "FRAM layout invalid: FRAM_SONGDATA_SIZE must be positive"
#endif

#if ((FRAM_CALIBRATION_ADDR + 28u) > FRAM_USERCHORDS_ADDR)
#error "FRAM layout invalid: calibration overlaps user chords"
#endif

#if ((FRAM_USERCHORDS_ADDR + FRAM_USERCHORDS_SIZE) > FRAM_SETTINGS_ADDR)
#error "FRAM layout invalid: user chords overlap song/settings"
#endif

#if ((FRAM_SETTINGS_ADDR + FRAM_SETTINGS_SIZE) != FRAM_FUTURE_ADDR)
#error "FRAM layout invalid: settings/future gap mismatch"
#endif

#if ((FRAM_FUTURE_ADDR + FRAM_FUTURE_SIZE) != FRAM_TOTAL_SIZE)
#error "FRAM layout invalid: end of FRAM not fully accounted for"
#endif

#ifdef __cplusplus
static_assert(sizeof(FramSettingsBlob) <= FRAM_SETTINGS_SIZE,
			  "FRAM settings blob exceeds FRAM_SETTINGS_SIZE");
#else
_Static_assert(sizeof(FramSettingsBlob) <= FRAM_SETTINGS_SIZE,
			   "FRAM settings blob exceeds FRAM_SETTINGS_SIZE");
#endif

#endif // FRAM_LAYOUT_H
