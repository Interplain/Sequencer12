# S12 Copy/Paste Pre-Implementation Architecture Audit

Read-only audit of the repository state at the time of review. No Copy/Paste implementation is present. No source files were changed for this audit; this report is the only new file.

## Executive Summary

- A Pattern is `sequencer::Pattern`; its 12 Bar/step records are `sequencer::StepSlot`. Compiler-measured sizes are 2,604 and 216 bytes respectively.
- A `StepSlot` contains canonical musical data plus derived compatibility projections. It is a self-contained value with no pointers, but a paste must refresh the derived projections.
- The runtime has three `PatternCacheEntry` objects with PLAY/NEXT/EDIT roles represented by indices. Roles may alias one entry. Dirty and active-I/O entries are pinned against eviction.
- Existing edits go through `SequencerDevice` and C bridge setters; `PersistPattern()` marks the current EDIT cache entry mutated. A dedicated whole-Bar or whole-Pattern replacement API does not exist.
- A Pattern copy should live in independent SRAM, not in a cache entry. A Bar snapshot plus a Pattern snapshot and source metadata costs 2,828 bytes with the proposed naturally aligned layouts.
- Main-grid mode is known by `s_ui_mode` plus `s_main_mode`; Bar editor mode is `UI_MODE_STEP_PIANO`. Input provides individual matrix press events, not reliable multi-key chord events. The scanner collapses simultaneous press events; the supplied schematic additionally confirms M6/M7 share a row without per-switch isolation diodes, so no simultaneous-key clipboard-clear gesture is included in V1.
- **V1 button mapping:** on the Main/current-Pattern screen, unshifted M1–M12 select Bars 1–12; SHIFT+M6 copies the current Pattern and SHIFT+M7 requests Pattern paste. Inside the Bar editor, unshifted M6 copies the current Bar and M7 requests Bar paste. Do not implement SHIFT+M6+M7: the actual schematic confirms M6/M7 share a matrix row with no per-switch isolation diodes. Both clipboards start invalid at UI initialization; each is replaced only by a new Copy of its type; Paste never consumes them; the two clipboards are independent.
- The current route table distinguishes `UI_MAIN_MODE_STEP` (Bar selection) from `UI_MAIN_MODE_PATTERN` (Pattern selection). The clarified behavior is authoritative; implementation must bind clipboard actions in the current-Pattern/Main context while preserving unshifted Bar selection. Do not globally bind M6/M7.
- The current Yes/No UI is renderer plus UI-owned action state, not a callback API. The example source/destination prompts do not fit its existing single-line text area as written.
- Copy needs no FRAM write. Paste should be one normal EDIT Pattern mutation, marked dirty through the residency generation mechanism and persisted by existing asynchronous PatternStore servicing.

## 1. Pattern And Bar Data Model

Definitions are in [devices/sequencer/sequencer_types.h](devices/sequencer/sequencer_types.h).

- `sequencer::LedgerSlot`: four `uint8_t` absolute MIDI notes; `0xFF` denotes an empty note. Size: 4 bytes.
- `sequencer::StepSlot`: the stored unit presented by the UI as a Bar. `sequencer::kStepCount` is 12, so every Pattern physically has 12 Bar slots, even if `Pattern::step_count` limits the active range.
- `sequencer::Pattern`: 12 `StepSlot`s plus Pattern-level playback settings. Size: 2,604 bytes.
- Compiler-measured `sizeof(StepSlot)` is 216 bytes. `pattern_store.cpp` also asserts the persisted `Pattern` ABI is 2,604 bytes. The source comment calls Pattern a “12-step block”; the piano-roll UI labels individual `StepSlot`s as BARs.

Fields in one `StepSlot`:

| Field | Audit classification |
|---|---|
| `bar_state` | Canonical: Active/Skip behavior. |
| `duration_multiplier` | Canonical per-Bar timing multiplier. |
| `note_ledger[32]` | Canonical absolute MIDI notes, four notes per canonical ledger position. |
| `event_length_packed[20]` | Canonical event lengths: packed 32 x 5-bit length-minus-one values; values at non-event-start positions are ignored. Copy the packed array as part of the snapshot. |
| `lane_slots[32]` | Canonical explicit lane assignment data, meaningful where the corresponding override bit is set. |
| `lane_override_mask[4]` | Canonical flags controlling explicit A/B/C/D lane assignments. |
| `custom_chord_name[17]` | Bar-associated custom chord display/name data; part of the stored value and should travel with the Bar. |
| `velocity`, `probability` | Musical parameters stored on the Bar; probability is used in playback. |
| `type` | Derived legacy playback projection, refreshed from Bar state/note summary. |
| `note_mask` | Derived/compatibility pitch-class summary of the absolute-note ledger when ledger data is present. |
| `repeat_count` | Derived projection of the time-signature/Pattern-division slot count in the current residency projection path. It is also retained for compatibility/UI APIs, so it should be regenerated for the destination rather than trusted from the source Bar. |

The current projection logic is in `DeriveStepProjection()` in [platform/fram/pattern_residency.cpp](platform/fram/pattern_residency.cpp), with related legacy projection helpers and mutation logic in [devices/sequencer/sequencer_device.cpp](devices/sequencer/sequencer_device.cpp). A Bar copy should copy all canonical fields and then refresh `type`, `note_mask`, and destination-dependent `repeat_count` using the same projection rules. No separate piano-roll note-mask or viewport state belongs in the Bar snapshot; `PianoRollContext` in [ui/screens/ui_screen_piano_roll.h](ui/screens/ui_screen_piano_roll.h) is UI presentation state.

Pattern-level fields, outside the Bar snapshots, are `tempo_multiplier`, `step_count`, `step_division`, `playback_mode`, Pattern `repeat_count`, `arp_mode`, and `arp_rate`. A complete Pattern clipboard should include these as well as all 12 `steps`.

`StepSlot` and `Pattern` are composed of scalar values, enums, fixed arrays, and character arrays; neither contains internal pointers, references, or cache indices. Ordinary C++ value assignment is structurally safe and creates an independent snapshot. It is still important to recompute derived/projection fields after pasting into a Pattern with potentially different timing settings.

## 2. Three-Entry Pattern Cache

The cache and role API are declared in [platform/fram/pattern_residency.h](platform/fram/pattern_residency.h) and implemented in [platform/fram/pattern_residency.cpp](platform/fram/pattern_residency.cpp). `PatternCacheEntry` contains a 2,604-byte Pattern plus cache/I/O metadata; its measured size is 2,624 bytes. The runtime allocates three entries.

- `PatternCacheRole::{Play, Next, Edit}` maps each role to an entry index in `PatternRoleState`. `0xFF` means no entry.
- The actual Pattern identity is `PatternCacheEntry::pattern_index`, paired with `current_generation`. `PatternResidency_FindResidentEntry()` reuses an entry only for the requested Pattern identity and generation.
- `SequencerDevice::EditPattern()` and `PlayPattern()` resolve the corresponding role’s Pattern. They are not dedicated copies.
- Role aliasing is direct: assigning EDIT or PLAY to an already resident entry points both roles at the same cache object. Startup intentionally loads Pattern index 0 as PLAY, then assigns EDIT to that entry.
- NEXT can similarly share a matching resident entry. A mutation of an aliased entry is therefore visible to every role that points at it.
- `PatternResidency_IsPinned()` returns pinned when any role refers to the entry, it is dirty (`current_generation != persisted_generation`), or I/O is active.
- Eviction first chooses unpinned EMPTY/FAULT entries. Otherwise it chooses a READY, clean, idle, unreferenced entry. It does not evict a referenced role, dirty edit, or active transaction.
- Pattern navigation enters through `SequencerDevice::SetCurrentPatternIndex()`. It requests EDIT for the target identity/generation. If already resident, it assigns immediately; otherwise a load is started and `pending_edit_pattern_index_` is installed only after I/O completes. Until completion, `GetCurrentPatternIndex()` still reports the old EDIT identity.
- Cache loads use the asynchronous `PatternStore_ReadAsync*` path and verify CRC and expected generation before a loaded entry becomes READY.

The current directory is a 32-entry static array. `PatternResidency_GetDirectoryEntry()` lazily reads and derives a requested entry into a separate 2,604-byte `s_directory_scratch`; the public full `PatternResidency_RebuildDirectory()` still scans every entry when explicitly called. This scratch buffer is reused and is not safe as a clipboard. A cache entry is also not a clipboard: navigating can change role aliases, and a cache entry has only cache-controlled lifetime/eviction semantics. An independent SRAM value snapshot is required for clipboard lifetime across navigation.

Current startup calls `Bridge_InitSafe()` and later `Bridge_ActivateStorageRuntime()` before `UI_Sequencer_Init()`. Runtime activation does not call `PersistenceStore_Bootstrap()` or rebuild all 32 directory entries. However, `InitializePatternResidency()` first asks for directory entry 0, whose lazy population performs a blocking complete Pattern read/CRC, and then requests Pattern 0 into the cache through the chunked reader loop. Thus the current boot path reads Pattern 1 twice (index 0), not the whole store; this is a separate residency inefficiency from the removed all-store validation scan.

## 3. Mutation Path, Generation, And Persistence

Representative note toggle path:

1. `UI_Sequencer_Update()` obtains normal/shifted matrix press events and calls `UI_Sequencer_HandleStepMatrixInput()` in [ui/ui_sequencer.c](ui/ui_sequencer.c).
2. In `UI_MODE_STEP_PIANO`, `UI_StepRoute_StepPianoEditor()` dispatches M9 to `UI_StepPiano_ActionToggleNote()`.
3. That operation reads the selected MIDI ledger slot, edits the note set, and calls `Bridge_SetStepLedgerSlotMidi()` (and the slot-length setter) for the selected zero-based Bar index.
4. The bridge wrapper in [stm32/sequencer_bridge.cpp](stm32/sequencer_bridge.cpp) calls `SequencerDevice::SetStepLedgerSlot()` and then `PersistPattern()`.
5. `PersistPattern()` calls `g_sequencer.MarkEditPatternMutated()`. That resolves the EDIT role entry and calls `PatternResidency_MarkMutated()`.
6. `PatternResidency_MarkMutated()` increments `current_generation` (skipping zero) and updates the Pattern directory projection. `persisted_generation` is unchanged, so dirty state is the generation inequality. Dirty entries remain pinned.
7. `Bridge_ServicePersistence()` runs after UI update in the main loop. Its `ServicePatternStoreAsync()` pumps residency I/O and calls `PatternResidency_BeginAnyDirtyWriteback()`. Writeback snapshots the resident Pattern and current generation into `PatternStore_WriteAsyncBeginGeneration()`, then writes bounded 32-byte chunks. When complete, `persisted_generation` advances to the generation that was written. If another edit occurs during a write, `current_generation` remains newer and the entry stays dirty for another write.

Accordingly, future Bar Paste must not assign through an untracked raw pointer or write FRAM. It should use an engine/bridge-owned replacement operation for the destination Bar, update its canonical data and derived fields, refresh live playback if applicable, and mark the EDIT entry mutated once for the completed paste. Future Pattern Paste should use an engine/bridge-owned complete EDIT Pattern replacement operation, run projection/directory updates, handle role/output coherency, and mark the entry mutated. Both then use the existing `Bridge_ServicePersistence()` path.

The lower-level `PatternResidency_GetMutablePattern()` pointer does not mark edits. `PatternResidency_MarkMutated()` exists, but calling it from UI code after direct replacement would still bypass engine-level projection and playback refresh. There is no sanctioned one-call whole-Pattern replacement API today.

## 4. Full Pattern Replacement And Playback Coherency

No existing API atomically replaces all fields of the current EDIT Pattern. `SequencerDevice::ImportSong()`/`FinalizeImportedSong()` replace Song-level data and do not provide a resident Pattern replacement operation. The right ownership boundary is a new `SequencerDevice` operation exposed through a C bridge function, not direct `PatternStore`/FRAM access and not UI access to a mutable cache pointer.

That operation should, as one logical edit:

1. Verify the EDIT entry is READY and resolve its Pattern identity.
2. Assign the clipboard Pattern value to the resident EDIT Pattern.
3. Recompute lazy Bar projections and directory metadata against the destination Song time signature and copied Pattern division.
4. Increment the EDIT cache generation exactly once and leave the entry dirty for normal asynchronous writeback.
5. Reconcile any NEXT preparation associated with the replaced identity/generation; cancel/reprepare stale NEXT work where needed.
6. If EDIT aliases PLAY, re-apply the current step and ensure a CV/gate output snapshot is refreshed. If the destination is not the currently playing identity, ordinary EDIT changes do not replace the separate PLAY entry.

EDIT may alias PLAY, particularly at startup. A Bar Paste into the currently playing Bar can therefore mutate the live playback data in place. Existing setters such as `SetStepLedgerSlot()` and `SetStepBarState()` call `ApplyCurrentStepBehavior()` when the edited Bar is `current_bar_` and EDIT equals PLAY; other setters also refresh timing/step state in their relevant cases. A simple `MarkMutated()` does not call `ApplyCurrentStepBehavior()` and is insufficient by itself for live edits.

Playback resolves the current Bar through `PlayPattern()` and `current_bar_`. `ApplyCurrentStepBehavior()` updates gate/arp state; output snapshots flow through `Bridge_Process()`/`WriteCurrentStepSnapshot()` or the pending-snapshot service in the uClock path. `SequencerOutput_WriteSnapshot()` calculates DAC lanes, while `Bridge_Tick1ms()` maintains gate GPIO state. A bulk replacement must explicitly follow the engine’s current-step refresh path. V1 uses stopped-only Paste: Copy is allowed while stopped or playing, while Bar and Pattern Paste check `Bridge_IsPlaying()` both when requested and again when YES is confirmed. If playing at either point, do not mutate the destination or change the clipboard.

## 5. Clipboard Storage And RAM Cost

Compiler measurement used the repository type declarations and the workspace C++ compiler:

| Object/layout | Bytes |
|---|---:|
| `sequencer::StepSlot` (one Bar) | 216 |
| `sequencer::Pattern` | 2,604 |
| `PatternCacheEntry` (Pattern plus cache metadata) | 2,624 |
| `PatternDirectoryEntry` | 8 |

For a concrete naturally aligned layout:

```cpp
struct BarClipboard {
    sequencer::StepSlot bar;
    uint8_t valid;
    uint8_t source_pattern;
    uint8_t source_bar;
};

struct PatternClipboard {
    sequencer::Pattern pattern;
    uint8_t valid;
    uint8_t source_pattern;
};
```

`sizeof(BarClipboard) == 220`; `sizeof(PatternClipboard) == 2,608`; both together are **2,828 bytes**. The five metadata bytes have three bytes of structure padding in this layout. Adding a source-Bar byte to Pattern metadata keeps its aligned struct size at 2,608 and the same 2,828-byte combined total. Indices fit in `uint8_t` (Pattern 0–31, Bar 0–11); the validity byte distinguishes an empty clipboard.

The three-entry cache occupies 3 x 2,624 = 7,872 bytes, and current lazy directory scratch occupies another 2,604 bytes. A full independent Pattern clipboard is roughly one cache entry in SRAM, but it avoids occupying or pinning one of the three operational cache entries. Existing step-piano rollback storage is scoped to a Bar-edit session and only snapshots ledger slots/lengths; it is not a complete, navigation-persistent Bar or Pattern clipboard.

Recommended storage: two static SRAM clipboard values in a UI-owned or narrowly scoped clipboard module, with valid flags initialized false in `UI_Sequencer_Init()` (also zero-initialized by static storage at reset). Do not reset them on Pattern/Bar navigation or after Paste. There is no manual clear gesture in V1. A new Bar Copy replaces only the Bar clipboard; a new Pattern Copy replaces only the Pattern clipboard. This gives independent snapshots and no Copy-time FRAM traffic.

## 6. Matrix Input And Hardware Constraints

The matrix path is implemented in [ui/ui_input.c](ui/ui_input.c), with pin definitions in [platform/mcp23017/mcp23017.h](platform/mcp23017/mcp23017.h).

- The MCP23017 is polled every 4 ms. The 3x4 matrix is scanned every 10 ms and debounced for 12 ms. Matrix index is `row * 4 + col`, with rows GPA0–GPA2 and columns GPA3–GPA6.
- M6 is index 5: zero-based row 1/column 1, i.e. physical matrix row 2/column 2, pins GPA1/GPA4. M7 is index 6: zero-based row 1/column 2, physical row 2/column 3, pins GPA1/GPA5. They share GPA1 and occupy adjacent columns.
- `ScanStepMatrix()` drives the selected column low and leaves the other column outputs high (`MCP_MATRIX_COL_MASK` except the selected bit), then reads row inputs GPA0–GPA2 for each column. While both same-row keys are down, they connect GPA1 to both GPA4 and GPA5 as the scanner drives one column low and the other high. The supplied schematic confirms there are no per-switch isolation diodes. The pair shares GPA1 and has no diode isolation, so V1 must not ask the user to hold M6 and M7 together.
- SHIFT is GPA7, configured as input with pull-up by `MCP23017_SetDirections(..., 0x87, 0xFF)` and `MCP23017_SetPullups(..., 0x87, 0xFF)`. The matrix scan only drives GPA3–GPA6 and tests GPA0–GPA2; SHIFT does not change matrix scanning electrically. Its debounced level is sampled separately and tags matrix events.
- The scanner stores a full debounced held-state bitmap internally in `s_matrix_debounced_pressed`, but exposes no held-mask/chord API. Events are falling-edge events; if multiple new keys appear in one debounce update, the code deliberately collapses them to the highest index. If M6 and M7 become pressed in separate updates, the first shifted event may already be queued before the second key arrives.

The existing public event queues cannot reliably identify a multi-key chord: they collapse simultaneous new keys and may dispatch staggered keys individually. Independently, the actual schematic has now confirmed M6 and M7 share a matrix row and have no per-switch isolation diodes. Therefore V1 must not implement SHIFT+M6+M7 or any manual clipboard-clear gesture. This is a deliberate hardware constraint, not an open feasibility question. Firmware establishes that SHIFT on GPA7 is electrically separate from column scanning, but it does not make the unisolated M6/M7 pair safe or dependable when held together.

## 7. UI Context And Button Routing

The UI context is represented by `s_ui_mode` (`UI_MODE_GRID`, `UI_MODE_STEP_PIANO`, chord/menu modes, etc.) plus `s_main_mode` (`UI_MAIN_MODE_STEP`, `CHORD`, `TIMING`, `PATTERN`, `QUAN`) in [ui/ui_sequencer.c](ui/ui_sequencer.c). The active screen object is separately tracked by `UI_ScreenRouter`.

The clarified control contract is:

- Main/current-Pattern view: unshifted M1–M12 select Bars 1–12 in the current Pattern. SHIFT+M6 copies that Pattern; SHIFT+M7 requests Pattern paste into it.
- Bar editor (`UI_MODE_STEP_PIANO`): unshifted M6 copies the current Bar; unshifted M7 requests Bar paste into it.
- Both clipboard-valid flags begin false at UI initialization. A Bar Copy replaces only the Bar snapshot; a Pattern Copy replaces only the Pattern snapshot. Paste never consumes either clipboard, and navigation never clears them. No manual clear gesture is part of V1.
- Copy snapshots the source. Paste is repeatable and does not consume the clipboard. Bar and Pattern clipboards are independent. Do not globally bind M6/M7.

In the current route table, `UI_MODE_GRID` + `UI_MAIN_MODE_STEP` routes unshifted M1–M12 through `UI_StepRoute_GridStep()` to `UI_Sequencer_SelectStep()`, selecting Bars 1–12. Its shifted route is `UI_StepRoute_ShiftGridStep()`, which only uses SHIFT+M11/M12 to navigate Patterns. SHIFT+M6/M7 are unused there and can be assigned the specified Pattern clipboard actions while retaining unshifted Bar selection.

`UI_MAIN_MODE_PATTERN` is a distinct Pattern-selector mode: current normal and shifted matrix events route to `UI_StepRoute_GridPattern()` and set current Pattern to `step - 1`. Keep this separate from the clarified Main/current-Pattern Bar-selection context; do not let Pattern clipboard actions steal the unshifted Bar-selection events.

In Bar editor context, M6/M7 are presently unassigned (the piano-roll footer says `M6 -- M7 --`). Existing M5, M8, M9, M10, M11, and M12 actions are skip, length, note, clear, octave-up, and octave-down. The Bar editor handler suppresses matrix input while its confirmation or length mode is active. Other menus have their own route tables; clipboard handling must be gated to the specified Main or Bar context and suppressed while an unrelated modal/menu owns input.

`UI_Sequencer_SelectStep()` stores the selected Bar as one-based `s_selected_step`; `UI_Sequencer_EnterStepPianoView()` enters `UI_MODE_STEP_PIANO` and retains that Bar selection. Engine/bridge Bar APIs use zero-based indices. `Bridge_GetCurrentPattern()` returns `SequencerDevice::GetCurrentPatternIndex()`, the EDIT Pattern identity; `Bridge_GetPlayPattern()` returns the distinct PLAY identity. `SetCurrentPatternIndex()` can retain the prior EDIT identity while a target load is pending, so a future paste must not assume a navigation request has completed before acting.

The intended control mapping is resolved. The source-level distinction between the Bar-selection route (`UI_MAIN_MODE_STEP`) and the separate Pattern-selector route (`UI_MAIN_MODE_PATTERN`) is an implementation constraint, not an unresolved product mapping question.

## 8. Existing Yes/No Confirmation UI

There are two existing render/state paths with the same visual style:

- The Bar editor clear/leave flow is owned by `s_step_piano_confirm_*` in [ui/ui_sequencer.c](ui/ui_sequencer.c). `UI_StepPiano_ActionClearBarRequest()` or `UI_StepPiano_ActionExitBarRequest()` supplies text into `s_step_piano_confirm_message[24]` and publishes it through `PianoRollContext.confirm_message`. The piano-roll renderer in [ui/screens/ui_screen_piano_roll.c](ui/screens/ui_screen_piano_roll.c) draws the local Yes/No overlay. Encoder delta changes `yes_selected`; encoder press calls `UI_StepPiano_ActionConfirmModal()`, which returns NO or switches on the UI-owned action code to perform the action.
- Other UI confirmations use `UI_Display_DrawYesNoQuestionModal(message, yes_selected)` in [ui/ui_display.c](ui/ui_display.c), invoked by `UI_Sequencer_BeginExitConfirm()` and redrawn when encoder delta changes selection. `UI_Sequencer_ConfirmExitConfirmSelection()` returns NO or dispatches the UI-owned `UiExitConfirmAction`. Direct-chord confirmation has another instance of the same state-and-renderer pattern.

Neither mechanism accepts callbacks. The existing design supplies a message string, tracks YES/NO and action in UI state, then dispatches from the input handler. Paste can follow that same pattern by adding a paste action/state and using the existing overlay/render path; do not create another modal subsystem.

Both message buffers are 24 bytes (23 visible characters plus NUL). The requested Bar text `PASTE BAR? P1 B1 -> P2 B7` is 25 characters and will be truncated. In addition, both existing overlays render a single `Font12x20` line: their usable width is about 12–13 characters, so even the shorter Pattern prompt `PASTE PATTERN? P1 -> P2` does not fit on one line. The modal needs a compact two-line/abbreviated message treatment and a larger buffer (or another existing multi-line text layout) to show source and destination; the Yes/No interaction itself is reusable.

## 9. Playback Mutation Behavior

Current ordinary Bar edits refresh playback selectively. For example, `SetStepLedgerSlot()` updates the ledger and lane assignments, summary/type projection, event boundary/length bookkeeping, and custom chord name; if the changed Bar is `current_bar_` and EDIT identity equals PLAY identity, it calls `ApplyCurrentStepBehavior()` and sets step-change state. `SetStepBarState()` follows the same current-Bar/current-Pattern condition. Non-current Bars do not alter the active playback event immediately.

Playback output is derived from the PLAY Pattern and current engine Bar. `ApplyCurrentStepBehavior()` selects gate/arp behavior; bridge snapshot code uses `SequencerOutput_WriteSnapshot()` to write DAC lanes, while the 1 ms bridge path applies gate output. In the non-uClock configuration `Bridge_Process()` is called from `UI_Sequencer_Update()` and writes the current step snapshot; the uClock path consumes output snapshot requests from musical-event servicing.

`PatternResidency_MarkMutated()` only increments generation and updates the directory; it does not refresh playback. A bulk Bar or Pattern replacement must explicitly follow the current-playing-Bar engine refresh rules and make the output snapshot/gate state coherent. For V1, avoid this live-paste problem entirely: COPY is allowed while stopped or playing; both Paste operations are stopped-only. Enforce this with `Bridge_IsPlaying()` both when Paste is requested and again when YES is confirmed. If playing at either point, do not mutate the destination or change the clipboard.

`Bridge_IsPlaying()` declared in [stm32/sequencer_bridge.h](stm32/sequencer_bridge.h) and implemented in [stm32/sequencer_bridge.cpp](stm32/sequencer_bridge.cpp) returns `g_sequencer.IsPlaying()` and is the existing authoritative transport-running query. Check it when Paste is requested and again when YES is confirmed. If playing at either point, do not mutate the destination and do not alter/consume the clipboard. The second check handles transport starting while the confirmation is open.

## 10. Persistence And Power/Lifetime

- Copy is read-only SRAM work and should perform no FRAM writes.
- Paste mutates the destination resident EDIT Pattern. The `current_generation`/`persisted_generation` mismatch causes the existing async PatternStore writeback to pick it up; no direct FRAM write belongs in Copy/Paste.
- Pattern writeback is chunked and pumped after UI updates through `Bridge_ServicePersistence()`. Dirty cache entries stay pinned during navigation and I/O, so changing Pattern does not itself discard an uncommitted Paste.
- Song-header persistence is a separate stopped/idle checkpoint with a 1,500 ms delay. `Bridge_FlushPersistence()` concerns that legacy Song snapshot; it is not a synchronous PatternStore flush.
- Pattern writes are asynchronous and there is no user-facing “flush this Pattern now” call in the audited UI path. Abrupt power removal before the async transaction commits can leave the latest paste unpersisted; transactional journal/recovery prevents treating a torn Pattern write as valid, but does not guarantee an edit that had not completed is durable. A reboot-persistence test must wait until the destination entry is clean/writeback-complete before resetting.
- Clipboard contents are volatile and need not be persisted. Static SRAM clipboard state begins zeroed at boot; explicitly initialize both valid flags false in `UI_Sequencer_Init()`. Do not clear them on navigation or Paste. A new Bar Copy replaces only the Bar clipboard; a new Pattern Copy replaces only the Pattern clipboard. The clipboards are independent. V1 has no manual clear gesture.

## 11. Integration Plan (No Implementation)

1. **Clipboard data/state:** add independent SRAM `StepSlot` and `Pattern` snapshots with separate valid/source metadata. Initialize both valid flags false in `UI_Sequencer_Init()`; preserve them through navigation and paste. A new copy replaces only its matching clipboard.
2. **Bar Copy:** in `UI_MODE_STEP_PIANO`, route unshifted M6 to a new engine/bridge value-read operation for `EditPattern().steps[s_selected_step - 1]`. Store the `sequencer::StepSlot`, `Bridge_GetCurrentPattern()`, and one-based Bar index in the independent Bar clipboard. Copy is permitted while playing and does not mutate/dirty the source.
3. **Bar Paste + confirmation:** route unshifted M7 only in Bar-editor context. Reject immediately if `Bridge_IsPlaying()` is true; otherwise stage the source/destination prompt using the existing `s_step_piano_confirm_*` / `PianoRollContext.confirm_message` flow. On YES, call `Bridge_IsPlaying()` again; if now playing, dismiss without mutation and retain the clipboard. If stopped, invoke a new engine/bridge Bar replacement operation, then retain the clipboard.
4. **Pattern Copy:** in the Main/current-Pattern context, route SHIFT+M6 to snapshot the complete READY EDIT `sequencer::Pattern` by value and record its source identity from `Bridge_GetCurrentPattern()`. Copy is permitted while playing and does not dirty the source.
5. **Pattern Paste + confirmation:** route SHIFT+M7 only in that Main context. Reject immediately if `Bridge_IsPlaying()` is true; otherwise stage the prompt through the existing `UI_Sequencer_BeginExitConfirm()` / UI-owned action dispatch or reuse the Bar-editor confirmation state where context permits. On YES, check `Bridge_IsPlaying()` again and dismiss unchanged if playing. If stopped, call a new `SequencerDevice` complete-Pattern replacement operation exposed through `stm32/sequencer_bridge.cpp`; retain the clipboard.
6. **Dirty-generation/persistence:** both replacement operations update canonical fields and derived projections, then mark the EDIT cache entry mutated exactly once using the existing `MarkEditPatternMutated()` -> `PatternResidency_MarkMutated()` path. Let `Bridge_ServicePersistence()` and `PatternResidency_BeginAnyDirtyWriteback()` handle chunked PatternStore persistence; do not write FRAM directly.
7. **Playback coherency:** stopped-only Paste avoids modifying the live Pattern while transport runs. Account for EDIT=PLAY alias and stale NEXT generation in the engine replacement operations. Ensure the next start consumes the replaced Pattern; `Bridge_Start()` already writes the initial current-step output snapshot. Do not add live-paste behavior in V1.
8. **Tests:** implement the test matrix below, including explicit playing rejection at request time and if transport starts while a confirmation is open, no clipboard consumption, no source dirtying on Copy, and persistence after completed async writeback/reboot.

## Unresolved Questions

1. Is a “complete Pattern” definitively the full 12 stored Bars plus Pattern-level fields even when `step_count` is less than 12? That is the natural interpretation of the current `Pattern` value type, but product intent is not otherwise encoded in source.

## 12. Proposed Test Matrix

| Case | Expected assertion |
|---|---|
| Same-Pattern Bar copy/paste | Canonical Bar fields replace destination; derived summaries match copied data. |
| Cross-Pattern Bar copy/paste | Clipboard survives source navigation/cache eviction and pastes into any destination Bar. |
| Repeated Bar paste | Multiple destinations receive the snapshot; clipboard remains valid. |
| Complete Pattern copy/paste | All 12 Bars and Pattern-level settings copy; Song-level state is unchanged. |
| Repeated Pattern paste | Clipboard remains valid and can replace multiple destination Patterns. |
| Source/destination independence | Source edits after Copy do not alter clipboard; destination edits after Paste do not alias source or clipboard. |
| Clipboard replacement by Copy | Bar Copy replaces only Bar clipboard; Pattern Copy replaces only Pattern clipboard. |
| Clipboard independence/invalidation | Both start invalid at UI init; copying one type leaves the other unchanged; Paste preserves validity; navigation preserves both. |
| Paste while stopped | YES applies replacement, advances destination generation once, and asynchronous writeback completes. |
| Paste while playing | Request is rejected without prompt or mutation; if playback starts after prompt, YES-time recheck rejects without mutation. Clipboard remains valid. |
| Destination persistence after reboot | After writeback completes, reboot and verify pasted destination data survives; clipboard starts invalid. |
| Pending navigation | Paste cannot target the previous EDIT Pattern while a requested Pattern is still loading. |
| EDIT/PLAY alias after stopped Paste | Subsequent start uses replaced contents and writes the normal initial CV snapshot. |
| Corrupt requested Pattern | CRC failure rejects load/copy; clipboard is not marked valid. |