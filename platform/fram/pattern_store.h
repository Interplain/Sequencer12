#ifndef PATTERN_STORE_H
#define PATTERN_STORE_H

/* FRAM Pattern Store (Stage 1 plumbing only).
 *
 * Raw per-pattern record store in the unused upper region of the MB85RC1MT,
 * ABOVE the verified legacy 0x0000-0x7FFF image (untouched, unrelated).
 *
 * Not yet called by any production/playback code path -- these are isolated
 * helpers only, exercised by the Stage 1 diagnostic test mode.
 *
 * Layout (see pattern_store.cpp for the compile-time assertions that prove
 * these numbers):
 *   FRAM_PATTERNSTORE_ADDR        = 0x8000                (immediately after legacy region)
 *   per-slot header               = 12 bytes (magic, version, payload_size, crc32)
 *   per-slot payload               = sizeof(sequencer::Pattern) = 2604 bytes
 *   per-slot record                = 12 + 2604 = 2616 bytes
 *   FRAM_PATTERNSTORE_SLOT_STRIDE = 0xA40  (2624 bytes, 8 bytes slack over the record)
 *   FRAM_PATTERNSTORE_SLOT_COUNT  = 32
 *   FRAM_PATTERNSTORE_SIZE        = 32 * 0xA40 = 0x14800 (83968 bytes)
 *   FRAM_PATTERNSTORE_END         = 0x8000 + 0x14800 = 0x1C800
 *   Reserved/untouched beyond that, up to MB85RC1MT_SIZE_BYTES (0x20000).
 */

#include "devices/sequencer/sequencer_types.h"
#include <cstdint>

#define FRAM_PATTERNSTORE_ADDR        0x8000u
#define FRAM_PATTERNSTORE_SLOT_STRIDE 0xA40u
#define FRAM_PATTERNSTORE_SLOT_COUNT  32u
#define FRAM_PATTERNSTORE_SIZE        (FRAM_PATTERNSTORE_SLOT_STRIDE * FRAM_PATTERNSTORE_SLOT_COUNT)
#define FRAM_PATTERNSTORE_END         (FRAM_PATTERNSTORE_ADDR + FRAM_PATTERNSTORE_SIZE)

/* Reads pattern slot `index` (0-31) into *out_pattern. Returns 1 only if the
 * slot's magic/version/payload_size header AND its CRC32 all check out; on
 * any failure returns 0 and *out_pattern is left completely untouched. */
uint8_t PatternStore_Read(uint8_t index, sequencer::Pattern* out_pattern);

/* Writes *pattern into slot `index` (0-31): payload first, header (with magic)
 * last, so a slot only reads back as valid once the payload write completed.
 * BLOCKS for the full payload transfer (~2604 bytes of I2C traffic, tens of
 * ms) -- diagnostic/test use only, must not be called from the live UI or
 * playback path. Use the async API below there instead. */
uint8_t PatternStore_Write(uint8_t index, const sequencer::Pattern* pattern);

/* Header+CRC32 validity check only, without producing a Pattern. */
uint8_t PatternStore_IsValid(uint8_t index);

/* ── Non-blocking chunked writer (real-time-safe path) ─────────────────────
 * Spreads the same payload-first/header-last write PatternStore_Write() does
 * across many small I2C transactions, meant to be pumped once per main-loop
 * pass in between Bridge_ServiceMusicalEvents() calls so the musical clock
 * is never blocked longer than a single chunk's I2C transaction time. */
#define PATTERNSTORE_CHUNK_BYTES 32u

typedef enum
{
    PATTERNSTORE_ASYNC_IDLE = 0,   /* nothing queued or in progress */
    PATTERNSTORE_ASYNC_BUSY = 1,   /* one chunk serviced this call, more remain */
    PATTERNSTORE_ASYNC_DONE = 2,   /* just completed: header committed, slot now valid */
    PATTERNSTORE_ASYNC_FAILED = 3, /* an I2C write failed; write abandoned, slot left as before */
} PatternStoreAsyncStatus;

/* Snapshots *pattern immediately and (re)starts a chunked write into slot
 * `index`. If a write is already in progress it is NOT interrupted -- this
 * call is a no-op until PatternStore_WriteAsyncBusy() is false, so callers
 * must check that first (Bridge_ServicePersistence does). Returns 1 if the
 * request was accepted and will begin on the next Service call, 0 if a write
 * was already in progress and the request was ignored. */
uint8_t PatternStore_WriteAsyncBegin(uint8_t index, const sequencer::Pattern* pattern);

/* Call once per main-loop pass. Performs at most one PATTERNSTORE_CHUNK_BYTES
 * payload write, or the final 12-byte header write, then returns. */
PatternStoreAsyncStatus PatternStore_WriteAsyncService(void);

/* True while a chunked write is in progress (between Begin and Done/Failed). */
uint8_t PatternStore_WriteAsyncBusy(void);

#endif
