# S12 Memory Architecture

## Status and Scope

This document describes the current implemented firmware after completion of
the FRAM-backed Pattern residency migration and output-coherency stabilization.
Historical measurements are retained only to explain why the migration was
performed. Remaining work is identified explicitly and is not current
firmware behavior.

The measured production build is:

```text
Flash: 182,720 / 1,048,576 bytes
SRAM:   40,220 /   131,072 bytes (30.7%)
Free:   90,852 bytes
```

## Historical Baseline

Before Pattern residency, `Song` permanently owned all 32 complete Patterns.
On the target ARM ABI, `Song`/`PatternBank` occupied approximately 83,376
bytes and the global `SequencerDevice` approximately 83,548 bytes. The
measured pre-migration firmware used:

```text
SRAM: 115,252 / 131,072 bytes (87.9%)
```

A temporary Stage 3 build retained both the old all-Pattern `Song` ownership
and the new cache infrastructure and measured 123,464 bytes. These figures
are historical; they are not the current memory footprint.

## Current Ownership Model

`Song` now contains only resident song-global metadata and `PatternChain`:

```text
global_bpm
key_root
key_scale
time_sig.numerator
time_sig.denominator
swing
transpose
PatternChain.pattern_indices[32]
PatternChain.length
PatternChain.position
```

`Song::patterns[32]` no longer exists. Complete Pattern payload ownership is:

- PatternStore V2 is the durable authority for committed Patterns.
- A newer dirty cache generation is temporary live authority until that
  generation is durably committed.
- Exactly three physical SRAM cache entries own resident Pattern payloads.
- Complete nonresident Pattern payloads exist only in FRAM.
- The Pattern Directory is derived metadata, not persistent authority.

Production playback and editing do not use arbitrary all-Pattern access.
`SequencerDevice::PlayPattern()` and `EditPattern()` resolve the PLAY and EDIT
cache roles.

## Current SRAM Structures

The following sizes are enforced by current target-oriented source assertions:

| Object or type | Size | Current role |
|---|---:|---|
| `LedgerSlot` | 4 bytes | Up to four absolute MIDI notes |
| `StepSlot` | 216 bytes | One step with canonical ledger, event, and lane data |
| `Pattern` | 2,604 bytes | One complete 12-step Pattern |
| `PatternChain` | 34 bytes | 32 indexes, length, and runtime position |
| `PatternDirectoryEntry` | 8 bytes | Derived unloaded-Pattern metadata |
| 32-entry Pattern Directory | 256 bytes | Derived directory for all Patterns |
| `PatternCacheEntry` | 2,624 bytes | Pattern plus generation/I/O metadata |
| Three Pattern cache entries | 7,872 bytes | Complete resident Pattern working set |
| Pattern role references | 3 bytes | PLAY, NEXT, and EDIT entry indexes |
| PatternStore transaction scratch | 2,604 bytes | Stable snapshot/recovery payload buffer |

Other major production allocations, including display scratch, user-chord
state, UI caches, stacks, and globals, are included in the measured 40,220
byte ELF total. The table above identifies the objects controlled directly by
the Pattern residency architecture; it is not an additive linker map.

The old 83 KB all-Pattern `Song` allocation is absent. The current measured
total is authoritative and replaces earlier architectural projections.

## Pattern Directory

The directory contains 32 entries and is rebuilt at boot from validated
PatternStore V2 records. Each 8-byte entry contains:

```text
committed/newest-live generation    4 bytes
playable-step mask                  2 bytes
packed division/mode/repeat         1 byte
validity/readiness status           1 byte
```

It supports chain traversal, unusable/all-skipped detection, timing metadata,
playback mode, repeat behavior, and generation checks without loading every
Pattern. It does not contain note, chord, event, lane, name, tempo, or arp
payload data.

When a resident Pattern is edited, its directory projection is updated from
the newest live cache generation. On reboot, the directory is reconstructed
from the newest validated durable records.

## Three-Entry Pattern Cache

The cache has exactly three physical `PatternCacheEntry` objects. Logical
roles refer to entries:

- `PLAY`: Pattern currently used by playback.
- `NEXT`: validated successor prepared for a boundary.
- `EDIT`: Pattern currently exposed to editing and synchronous UI getters.

Roles may alias any compatible entry:

- PLAY = EDIT shares one physical Pattern object, so live edits are visible
  to playback immediately.
- PLAY = NEXT supports repeats and single-Pattern playback.
- NEXT = EDIT allows editing the prepared successor.
- PLAY = NEXT = EDIT is valid.
- PLAY, NEXT, and EDIT may all differ.

Changing a role changes an entry index; it does not copy a Pattern payload.

Each entry tracks `current_generation`, `persisted_generation`,
`io_generation`, Pattern/request indexes, residency state, I/O state, and
fault state. Dirty state is generation-based:

```text
dirty = current_generation != persisted_generation
```

### Pinning and Eviction

An entry is pinned when it is referenced by PLAY, NEXT, or EDIT, when it is
dirty, or while it participates in I/O. Only clean, idle, unreferenced entries
may be evicted. An idle unreferenced load-fault entry may be reused while its
fault remains observable until reuse.

If PLAY A, NEXT B, and dirty EDIT C occupy all entries, selecting a fourth
EDIT Pattern waits. Dirty data is not discarded and no fourth cache entry is
allocated.

### Asynchronous EDIT Handoff

EDIT replacement is asynchronous and CRC validated. The existing valid EDIT
role remains assigned while the replacement loads, so synchronous UI getters
continue to resolve a valid Pattern. A successful generation-matched load
changes EDIT ownership only after validation. Failed, stale, or superseded
loads cannot replace EDIT. A waiting selection retries when serialized I/O or
an evictable entry becomes available.

## NEXT Preparation and Boundary HOLD

`PatternChain` remains resident, and directory metadata allows unusable chain
entries to be skipped without loading their payloads. NEXT is selected and
loaded asynchronously outside interrupt context. Promotion requires the
requested Pattern index and generation to match a READY, validated entry.

At a normal Pattern boundary:

- no FRAM operation occurs;
- no Pattern payload is copied;
- validated NEXT becomes PLAY by role reassignment; and
- later non-ISR service prepares the following NEXT.

If required NEXT is not ready, the implemented HOLD policy freezes musical
event publication at the completed boundary, turns gate and arp state off,
and performs no FRAM access at the boundary. Main-loop service completes NEXT
loading and validation, promotes it, establishes the successor musical state,
and resumes.

## Musical Output Coherency

Musical output follows this implemented ordering invariant:

```text
establish new musical state
  -> calculate and write CV/DAC snapshot
  -> commit gate or retrigger activation
```

Gate activation may remain pending while an output snapshot is due. The
bridge commits that gate intent only after snapshot handling has established
the corresponding output state.

Ordinary snapshots retain bridge deduplication for repeated service of the
same step state. HOLD resume marks its request as a forced refresh. Forced
refresh bypasses deduplication, so equal pitch classes cannot hide changed
absolute notes, octaves, or lane assignments.

Arp-only note advancement requests and writes a snapshot without waiting for
a full sequencer-step event. Normal step changes use the same CV-before-gate
ordering. DAC work remains outside interrupt context.

## ISR and Main-Loop Responsibilities

The uClock callback runs in interrupt context and publishes bounded tick
state. It does not access FRAM, manage cache ownership, load or validate a
Pattern, calculate a complete output snapshot, or write the DAC.

The current split is:

```text
uClock ISR
  -> update clock counters
  -> publish pending musical ticks

main loop
  -> claim pending ticks and step events
  -> advance arp/step/Pattern state
  -> calculate and write output snapshot
  -> commit gate activation
  -> service cache reads, writeback, and persistence
```

FRAM and DAC operations occur from non-ISR service paths.

## Canonical and Derived Pattern State

Canonical Pattern data includes absolute `note_ledger[]` notes, packed event
lengths, explicit output lanes, `bar_state`, velocity, probability, duration,
custom names, step division, playback mode, Pattern repeat count, arp mode,
arp rate, and tempo multiplier.

The following are deterministic projections:

- `note_mask`, derived from absolute ledger notes;
- projected `StepType`, derived from bar state and note presence; and
- step-level `repeat_count`, derived from Song Header time signature and
  Pattern step division.

Projection derivation is lazy when appropriate and does not advance Pattern
generation or schedule writeback. Timing changes persist Song Header time
signature and the selected Pattern's authoritative step division separately;
there is no all-Pattern transaction.

## Transitional Surfaces

Metadata-only whole-`Song` accessors remain for current callers, but `Song`
contains no Pattern payload array. The dormant legacy V3 codec source is
guarded by `S12_ENABLE_LEGACY_V3_SONG_CODEC`, is disabled in production, and
is not a persistence authority.

Future bulk import/export, if required, must stream through PatternStore and
residency-aware APIs rather than recreate all 32 Patterns in SRAM.

## Remaining Future Work

The following remain open and are not implied to be implemented:

- user-facing persistence and cache fault reporting;
- cache-exhaustion timeout, cancellation, and waiting UX;
- external 24 PPQN behavior while NEXT is held;
- main-loop tick burst and jitter characterization on hardware;
- physical reset/power-loss and injected FRAM fault testing; and
- streaming import/export if dormant legacy V3 functionality is ever needed.

Exact degraded-mode product policy for corruption in an unreferenced Pattern
also remains open. These items do not change the implemented ownership,
durability, or ISR boundaries described above.