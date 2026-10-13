# S12 Persistence Architecture

## Status and Scope

The clean FRAM persistence architecture and production Pattern residency
migration are implemented. This document describes current firmware behavior,
not a future design proposal.

The clean schema intentionally does not migrate old development Pattern or V3
song data. The legacy V3 codec remains dormant source behind
`S12_ENABLE_LEGACY_V3_SONG_CODEC`; that flag is disabled in production, and V3
is not an active persistence authority.

## Persistence Authority

Current authority is divided by data ownership:

- PatternStore V2 is the sole durable authority for committed Pattern data.
- A newer dirty cache generation is temporary live Pattern authority until
  that generation is committed and validated in its destination slot.
- The 32-entry Pattern Directory is derived SRAM metadata. It is not
  persistent authority.
- Song-global state is durable in independent Song Header A/B records.
- Pattern generations are per Pattern; Song Header has its own generation.
- No global transaction spanning Patterns and Song Header is implemented or
  required by current behavior.

## Current FRAM Layout

The MB85RC1MT provides 128 KiB at logical addresses
`0x00000..0x1FFFF`. Address bit A16 is selected through I2C address `0x50` or
`0x51`.

| Address range | Size | Current allocation |
|---|---:|---|
| `0x00000..0x00003` | 4 bytes | User-chord magic |
| `0x00004..0x0001F` | 28 bytes | DAC calibration record |
| `0x00020..0x00B1F` | 2,816 bytes | 128 user-chord slots |
| `0x00B20..0x07DFF` | 29,408 bytes | Legacy Song region, retained but inactive in production |
| `0x07E00..0x07EFF` | 256 bytes | Settings partition |
| `0x07F00..0x07FFF` | 256 bytes | Reserved/calibration metadata |
| `0x08000..0x1C7FF` | 83,968 bytes | 32 PatternStore V2 slots |
| `0x1C800..0x1D25F` | 2,656 bytes | PatternStore transaction journal |
| `0x1D260..0x1D27F` | 32 bytes | Clean-schema record A |
| `0x1D280..0x1D29F` | 32 bytes | Clean-schema record B |
| `0x1D2A0..0x1D2DF` | 64 bytes | Song Header record A |
| `0x1D2E0..0x1D31F` | 64 bytes | Song Header record B |
| `0x1D320..0x1FFFF` | 11,488 bytes | Unallocated |

The legacy-region macros in `fram_layout.h` describe the retained lower
32 KiB layout. `mb85rc1mt.h` and the persistence/PatternStore constants
describe the complete 128 KiB device.

## PatternStore V2 Records

Each of the 32 authoritative slots has a fixed 2,624-byte (`0xA40`) format:

```text
offset  size   field
0       4      magic "S12P"
4       2      format version = 2
6       2      payload size = 2,604
8       4      per-Pattern generation
12      4      payload CRC32
16      4      header CRC32
20      2,604  raw Pattern payload
```

The header CRC covers the identifying and integrity fields through the
payload CRC. The payload CRC covers the complete Pattern. No multi-byte FRAM
write is assumed atomic, and a record is accepted only when its complete
header and payload validate.

## Transaction Journal

PatternStore serializes writes through one journal at `0x1C800`. Its reserved
stride is 2,656 bytes (`0xA60`):

```text
journal header       24 bytes
Pattern payload   2,604 bytes
reserved padding     28 bytes
```

The `S12J` header contains format version, payload size, target Pattern index,
generation, payload CRC32, and header CRC32.

### Commit Protocol

For cache generation `G`:

1. Copy generation `G` into the stable 2,604-byte SRAM transaction scratch.
2. Invalidate the previous journal commit marker.
3. Write the journal payload in bounded chunks.
4. Commit and validate the journal header.
5. Read back and CRC-validate the complete journal payload.
6. Invalidate the destination commit marker.
7. Write the destination payload in bounded chunks.
8. Commit and validate the destination header.
9. Read back and CRC-validate the complete destination payload.
10. Advance the cache entry's `persisted_generation` to `G`.
11. Retire and verify retirement of the journal when possible.

Successful complete destination readback and validation is the durability
point. Journal commit, destination writes, and destination header write
success alone do not make a cache generation durable.

If another edit produces `G+1` while `G` is being written, the immutable
scratch snapshot for `G` completes. Destination validation advances only
`persisted_generation` to `G`; the entry remains dirty and pinned because
`current_generation` is `G+1`.

### Boot Recovery

At boot, a fully validated journal is reconciled with its destination:

- journal newer than destination: replay the journal;
- valid journal with invalid destination: replay the journal;
- equal generation and equal payload CRC: destination is committed, retire
  the journal;
- destination newer than journal: journal is stale, retire it;
- equal generation with different payload CRC: explicit integrity fault;
- invalid or uncommitted journal header: it is not accepted as a committed
  transaction.

Replay writes the validated journal payload to the destination, commits the
destination header, validates the complete destination, and only then retires
the journal.

### Runtime Journal Reconciliation

The same safety decisions are implemented at runtime. A journal retirement
failure after destination validation does not roll back durability:

- the cache generation remains durably committed;
- `persisted_generation` advances to the validated destination generation;
- runtime recovery status records that cleanup failed; and
- the valid journal remains available for reconciliation.

Before a later Pattern transaction can reuse the journal, non-ISR service
performs chunked preflight reconciliation:

1. validate the existing journal header and complete payload;
2. validate the destination header and complete payload when present;
3. retire equal or stale journals only after the comparison is proven safe;
4. replay a newer journal or a journal whose destination is invalid;
5. report equal-generation/different-CRC as a sticky integrity fault; and
6. verify journal retirement before allowing journal reuse.

This allows later transactions to proceed without reboot after transient
retirement failure. Runtime recovery reuses the one PatternStore transaction
scratch; it does not allocate another Pattern-sized buffer.

An invalid/torn header is not treated as a committed journal. A valid header
with an invalid payload is an integrity fault and is not blindly erased.

## Clean Schema A/B Records

Schema identity uses two independently CRC-validated 32-byte records at
`0x1D260` and `0x1D280`:

```text
magic             4 bytes ("S12F")
schema version    2 bytes
record size       2 bytes
generation        4 bytes
state             1 byte
reserved         15 bytes
header CRC32      4 bytes
```

Implemented states are:

- `FORMATTING`: old PatternStore, journal, and Song Header commit markers are
  being invalidated;
- `INITIALIZING`: authoritative default records are being created or
  validated; and
- `READY`: required Pattern and Song Header records have validated.

Two validated READY records are established before initialization is
considered fully redundant. A valid single READY record after interruption is
accepted only after required records validate, and the missing redundant
record is restored.

After READY, corruption or unavailable FRAM produces an explicit boot fault.
Committed user data is not silently replaced with defaults.

## Resumable First Initialization

Initialization does not construct a 32-Pattern Song in SRAM. It proceeds one
Pattern at a time:

1. establish or resume schema `FORMATTING`/`INITIALIZING`;
2. recover or classify any journal transaction;
3. validate each PatternStore slot and create only missing defaults through
   the journal;
4. create or select a valid Song Header;
5. ensure both Song Header records exist;
6. validate all 32 Pattern records and Song Header state again;
7. write and validate two READY schema records; and
8. report persistence ready.

After interruption, already validated Pattern records are retained and
skipped. Initialization resumes from the remaining invalid records.

## Song Header A/B Persistence

Song-global durable state contains:

- global BPM;
- key root and scale;
- time-signature numerator and denominator;
- swing;
- transpose;
- 32 PatternChain indexes; and
- PatternChain length.

Runtime chain position is resident but is not persisted under current restart
semantics.

Each Song Header record is 64 bytes: a 20-byte integrity header followed by a
44-byte payload. Updates target the inactive A/B slot:

1. invalidate the target commit marker;
2. write the payload;
3. write the integrity header;
4. read back and validate the complete record; and
5. advance the in-memory Song Header generation only after success.

At boot, both records are validated and the newer generation is selected.
Equal generations with different payload CRCs are an explicit integrity
fault.

### Runtime Retry

Song-global mutation establishes explicit pending state and schedules a save
after the existing 1,500 ms idle interval while transport is stopped. Pending
state is independent of a zero timestamp.

If the A/B write or complete validation fails:

- pending state remains set;
- the next attempt is delayed by another 1,500 ms; and
- repeated failures therefore do not create a tight retry loop.

Pending state clears only after complete durable Song Header success.
`Bridge_FlushPersistence()` bypasses the idle wait, returns failure when the
save fails, and retains pending state for later retry.

PatternStore service and Song Header service run sequentially from the main
loop and write disjoint FRAM regions.

## Boot Ordering

The implemented production order is:

1. establish FRAM readiness;
2. inspect schema A/B state;
3. complete or resume first initialization when required;
4. recover a committed PatternStore journal;
5. validate all authoritative PatternStore records;
6. validate/select Song Header A/B and restore schema redundancy;
7. reconstruct the derived Pattern Directory;
8. load and CRC-validate initial PLAY outside interrupt context;
9. alias EDIT to PLAY;
10. resolve and prepare NEXT; and
11. expose persistence/playback readiness only after required state is valid.

No FRAM operation occurs in the uClock ISR or at a Pattern boundary.

## Pattern Directory and Cache Generations

The 32-entry, 256-byte Pattern Directory is reconstructed from validated
PatternStore records. It carries generation, playable-step mask, packed
division/mode/repeat metadata, and validity state. It is updated from newer
live cache generations but is never independently persisted.

Each cache entry tracks:

- `current_generation`: newest live Pattern content;
- `io_generation`: immutable generation captured for current I/O; and
- `persisted_generation`: generation validated in the authoritative
  destination.

Every authoritative edit advances `current_generation`. Deterministic
projection derivation does not. Failed writes do not advance
`persisted_generation`; dirty/faulted data remains pinned for retry or
explicit fault handling.

## Failure and Conflict Behavior

After successful schema initialization:

- missing or invalid required records cause explicit readiness/integrity
  faults;
- corrupt PLAY prevents playback readiness;
- unavailable required NEXT prevents promotion and invokes boundary HOLD;
- stale loads cannot acquire a logical role;
- dirty data remains pinned after write failure;
- equal-generation/different-CRC Pattern or Song Header records produce
  explicit integrity faults; and
- defaults are not silently substituted for committed user data.

## Legacy V3 Status

The lower legacy Song region remains reserved in the physical map, and the V3
codec source is retained for reference behind a disabled compile flag.
Production does not read or write V3 as active Song persistence. PatternStore
V2 and Song Header A/B are the current durable authorities.

If whole-Song import/export is needed later, it must stream Pattern payloads
through PatternStore/residency-aware operations instead of restoring permanent
`Song::patterns[32]` SRAM ownership.

## Remaining Open Work

The following are intentionally unresolved:

- user-facing persistence/cache fault reporting and recovery controls;
- cache-exhaustion timeout, cancellation, and waiting UX;
- external 24 PPQN behavior during NEXT HOLD;
- main-loop tick burst and jitter characterization on hardware;
- physical reset/power-loss and injected FRAM fault testing across durable
  transitions; and
- future streaming import/export if legacy whole-Song functionality is
  required.

Exact degraded-mode product policy for corruption in an unreferenced Pattern
also remains open. The current firmware fails closed for required boot and
playback state rather than inventing replacement user data.