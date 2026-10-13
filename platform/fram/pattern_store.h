#ifndef PATTERN_STORE_H
#define PATTERN_STORE_H

/* FRAM Pattern Store V2 transaction-safe storage foundation.
 *
 * Raw per-pattern record store in the unused upper region of the MB85RC1MT,
 * ABOVE the verified legacy 0x0000-0x7FFF image (untouched, unrelated).
 *
 * Current runtime use remains shadow persistence; SRAM-resident Song patterns
 * are still authoritative until the later residency migration.
 *
 * Layout (see pattern_store.cpp for the compile-time assertions that prove
 * these numbers):
 *   FRAM_PATTERNSTORE_ADDR        = 0x8000                (immediately after legacy region)
 *   per-slot header               = 20 bytes (magic, version, payload_size,
 *                                    generation, payload CRC32, header CRC32)
 *   per-slot payload               = sizeof(sequencer::Pattern) = 2604 bytes
 *   per-slot record                = 20 + 2604 = 2624 bytes
 *   FRAM_PATTERNSTORE_SLOT_STRIDE = 0xA40  (2624 bytes)
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
#define FRAM_PATTERNSTORE_JOURNAL_ADDR   FRAM_PATTERNSTORE_END
#define FRAM_PATTERNSTORE_JOURNAL_STRIDE 0xA60u
#define FRAM_PATTERNSTORE_JOURNAL_END    (FRAM_PATTERNSTORE_JOURNAL_ADDR + FRAM_PATTERNSTORE_JOURNAL_STRIDE)

/* Reads pattern slot `index` (0-31) into *out_pattern. Returns 1 only if the
 * slot's magic/version/payload_size header AND its CRC32 all check out; on
 * any failure returns 0 and *out_pattern is left completely untouched. */
uint8_t PatternStore_Read(uint8_t index, sequencer::Pattern* out_pattern);
uint8_t PatternStore_ReadWithGeneration(uint8_t index, sequencer::Pattern* out_pattern, uint32_t* out_generation);
uint8_t PatternStore_GetGeneration(uint8_t index, uint32_t* out_generation);

/* Writes *pattern into slot `index` (0-31) through the transaction journal,
 * validates the destination, and retires the journal when possible.
 * BLOCKS for the full payload transfer (~2604 bytes of I2C traffic, tens of
 * ms) -- diagnostic/test use only, must not be called from the live UI or
 * playback path. Use the async API below there instead. */
uint8_t PatternStore_Write(uint8_t index, const sequencer::Pattern* pattern);
uint8_t PatternStore_WriteGeneration(uint8_t index, const sequencer::Pattern* pattern, uint32_t generation);
uint8_t PatternStore_WriteDefaultGeneration(uint8_t index, uint32_t generation);

/* Clean-schema initialization maintenance. These invalidate only record
 * commit magic and verify that the target no longer parses as a committed
 * PatternStore V2 record. */
uint8_t PatternStore_InvalidateSlot(uint8_t index);
uint8_t PatternStore_DiscardJournal(void);

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
    PATTERNSTORE_ASYNC_DONE = 2,   /* destination fully read back and validated */
    PATTERNSTORE_ASYNC_FAILED = 3, /* an I2C write failed; write abandoned, slot left as before */
} PatternStoreAsyncStatus;

/* Snapshots *pattern immediately and (re)starts a chunked write into slot
 * `index`. If a write is already in progress it is NOT interrupted -- this
 * call is a no-op until PatternStore_WriteAsyncBusy() is false, so callers
 * must check that first (Bridge_ServicePersistence does). Returns 1 if the
 * request was accepted and will begin on the next Service call, 0 if a write
 * was already in progress and the request was ignored. */
uint8_t PatternStore_WriteAsyncBegin(uint8_t index, const sequencer::Pattern* pattern);
uint8_t PatternStore_WriteAsyncBeginGeneration(uint8_t index,
                                               const sequencer::Pattern* pattern,
                                               uint32_t generation);

/* Call once per main-loop pass. Performs one bounded transaction step, with
 * payload reads and writes capped at PATTERNSTORE_CHUNK_BYTES. */
PatternStoreAsyncStatus PatternStore_WriteAsyncService(void);

/* True while a chunked write is in progress (between Begin and Done/Failed). */
uint8_t PatternStore_WriteAsyncBusy(void);

/* ── Non-blocking chunked reader (real-time-safe path) ──────────────────────
 * Symmetric to the writer above: streams the payload directly into
 * *out_pattern in PATTERNSTORE_CHUNK_BYTES pieces (no extra scratch buffer --
 * writes straight into the caller's destination), validating the header and
 * checking the CRC32 only once every chunk has landed. *out_pattern is left
 * PARTIALLY WRITTEN between Begin and a DONE/FAILED result -- callers must
 * never treat it as valid (e.g. never expose it / flip a "ready" flag) until
 * PatternStore_ReadAsyncService() returns PATTERNSTORE_ASYNC_DONE. */

/* Validates the slot's V2 header and reads it up front (20 bytes, one small
 * transfer) before starting the chunked payload
 * read. Returns 0 immediately (no state changed) if the header is invalid or
 * a read is already in progress -- *out_pattern is untouched in that case. */
uint8_t PatternStore_ReadAsyncBegin(uint8_t index, sequencer::Pattern* out_pattern);
uint8_t PatternStore_ReadAsyncBeginGeneration(uint8_t index,
                                              sequencer::Pattern* out_pattern,
                                              uint32_t* out_generation);

/* Call once per main-loop pass. Reads at most one PATTERNSTORE_CHUNK_BYTES
 * chunk into the destination given to Begin(), then returns. */
PatternStoreAsyncStatus PatternStore_ReadAsyncService(void);

/* True while a chunked read is in progress. */
uint8_t PatternStore_ReadAsyncBusy(void);

typedef enum
{
    PATTERNSTORE_RECOVERY_NONE = 0,
    PATTERNSTORE_RECOVERY_REPLAYED,
    PATTERNSTORE_RECOVERY_RETIRED,
    PATTERNSTORE_RECOVERY_INTEGRITY_FAULT,
    PATTERNSTORE_RECOVERY_FAILED,
} PatternStoreRecoveryStatus;

/* Replays or retires one fully validated committed journal transaction.
 * Intended for boot/storage initialization context, never ISR context. */
PatternStoreRecoveryStatus PatternStore_RecoverJournal(void);

/* Last runtime preflight reconciliation result. An integrity fault is sticky
 * and blocks new writes until explicit recovery or reset. */
PatternStoreRecoveryStatus PatternStore_GetRuntimeRecoveryStatus(void);

#if defined(PATTERN_STORE_HOST_TEST)
void PatternStore_ResetRuntimeStateForTest(void);
#endif

#endif
