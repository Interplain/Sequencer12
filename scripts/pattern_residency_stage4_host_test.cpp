#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>

#define PATTERN_STORE_HOST_TEST 1
#include "devices/sequencer/sequencer_device.h"
#include "platform/fram/pattern_residency.h"
#include "platform/fram/pattern_store.h"
#include "stm32/sequencer_output_snapshot.h"
#include "ui/sequencer_clipboard.hpp"
#include "ui/ui_sequencer_clipboard.h"
#include "ui/ui_timing_commit.h"

namespace
{

std::array<uint8_t, 0x20000u> s_fram;
uint32_t s_read_count = 0u;
uint32_t s_slot_read_count[sequencer::kPatternCount] = {};
uint32_t s_write_count = 0u;
uint32_t s_fail_read_once_at = 0xFFFFFFFFu;
int s_failures = 0;

struct OutputRecorder
{
    SequencerDevice* device = nullptr;
    uint16_t values[4] = {0u, 0u, 0u, 0u};
    uint32_t write_count = 0u;
    bool gate_active_during_write = false;
};

uint16_t RecordPitchCode(void*, uint8_t lane, float volts)
{
    const int32_t note = (int32_t)(volts * 12.0f + 36.5f);
    return (uint16_t)((uint16_t)note | ((uint16_t)lane << 8u));
}

void RecordOutput(void* context, const uint16_t values[4])
{
    OutputRecorder* recorder = static_cast<OutputRecorder*>(context);
    memcpy(recorder->values, values, sizeof(recorder->values));
    ++recorder->write_count;
    recorder->gate_active_during_write = recorder->device->IsGateActive();
}

bool ServiceOutput(SequencerDevice* device,
                   SequencerOutputSnapshotState* state,
                   OutputRecorder* recorder,
                   bool* forced = nullptr)
{
    bool force_refresh = false;
    if (!device->ConsumeOutputSnapshotRequest(&force_refresh)) return false;
    const uint16_t zero_codes[4] = {900u, 901u, 902u, 903u};
    const SequencerOutputSnapshotOps ops = {recorder, RecordPitchCode, RecordOutput};
    const bool wrote = SequencerOutput_WriteSnapshot(*device, *state, ops, zero_codes,
                                                     false, force_refresh);
    if (forced) *forced = force_refresh;
    return wrote;
}

void Check(bool condition, const char* name)
{
    std::printf("[%s] %s\n", condition ? "PASS" : "FAIL", name);
    if (!condition) ++s_failures;
}

sequencer::Pattern MakePattern(uint8_t index)
{
    sequencer::Pattern pattern{};
    pattern.step_count = 1u;
    pattern.step_division = 1u;
    pattern.playback_mode = sequencer::PlaybackMode::Loop;
    pattern.repeat_count = 1u;
    pattern.arp_mode = sequencer::ArpMode::Off;
    pattern.steps[0].bar_state = sequencer::BarState::Active;
    pattern.steps[0].note_ledger[0].notes[0] = (uint8_t)(48u + index);
    pattern.steps[0].event_length_packed[0] = 0u;
    return pattern;
}

sequencer::Pattern MakeOutputPattern(uint8_t note0, uint8_t note1,
                                     uint8_t lane0, uint8_t lane1)
{
    sequencer::Pattern pattern = MakePattern(0u);
    sequencer::LedgerSlotClear(pattern.steps[0].note_ledger[0]);
    pattern.steps[0].note_ledger[0].notes[0] = note0;
    pattern.steps[0].note_ledger[0].notes[1] = note1;
    sequencer::LedgerLaneSet(pattern.steps[0].lane_slots[0], 0u, lane0);
    sequencer::LedgerLaneSet(pattern.steps[0].lane_slots[0], 1u, lane1);
    sequencer::StepSlotSetExplicitLaneFlag(pattern.steps[0], 0u, true);
    return pattern;
}

bool PrepareStore(void)
{
    s_fram.fill(0xFFu);
    s_read_count = 0u;
    s_write_count = 0u;
    s_fail_read_once_at = 0xFFFFFFFFu;
    PatternStore_ResetRuntimeStateForTest();
    PatternResidency_ResetForTest();
    for (uint8_t index = 0u; index < sequencer::kPatternCount; ++index)
    {
        const sequencer::Pattern pattern = MakePattern(index);
        if (!PatternStore_WriteGeneration(index, &pattern, (uint32_t)index + 1u)) return false;
    }
    return true;
}

bool InitializeDevice(SequencerDevice* device)
{
    if (!PatternResidency_RebuildDirectory()) return false;
    device->Init();
    return device->InitializePatternResidency();
}

bool Prepare(SequencerDevice* device)
{
    return PrepareStore() && InitializeDevice(device);
}

bool PrepareTimingDraftDevice(SequencerDevice* device)
{
    if (!PrepareStore()) return false;
    sequencer::Pattern initial = MakePattern(0u);
    initial.step_count = 2u;
    if (!PatternStore_WriteGeneration(0u, &initial, 100u)) return false;
    return InitializeDevice(device);
}

bool StepSlotsEqual(const sequencer::StepSlot& lhs, const sequencer::StepSlot& rhs)
{
    return lhs.type == rhs.type &&
         lhs.bar_state == rhs.bar_state &&
         lhs.duration_multiplier == rhs.duration_multiplier &&
         lhs.repeat_count == rhs.repeat_count &&
         lhs.note_mask == rhs.note_mask &&
         memcmp(lhs.note_ledger.data(), rhs.note_ledger.data(), sizeof(lhs.note_ledger)) == 0 &&
         lhs.event_length_packed == rhs.event_length_packed &&
         lhs.lane_slots == rhs.lane_slots &&
         lhs.lane_override_mask == rhs.lane_override_mask &&
         memcmp(lhs.custom_chord_name, rhs.custom_chord_name, sizeof(lhs.custom_chord_name)) == 0 &&
         lhs.velocity == rhs.velocity &&
         lhs.probability == rhs.probability;
}

bool CanonicalStepSlotsEqual(const sequencer::StepSlot& lhs, const sequencer::StepSlot& rhs)
{
    return lhs.bar_state == rhs.bar_state &&
         lhs.duration_multiplier == rhs.duration_multiplier &&
         memcmp(lhs.note_ledger.data(), rhs.note_ledger.data(), sizeof(lhs.note_ledger)) == 0 &&
         lhs.event_length_packed == rhs.event_length_packed &&
         lhs.lane_slots == rhs.lane_slots &&
         lhs.lane_override_mask == rhs.lane_override_mask &&
         memcmp(lhs.custom_chord_name, rhs.custom_chord_name, sizeof(lhs.custom_chord_name)) == 0 &&
         lhs.velocity == rhs.velocity &&
         lhs.probability == rhs.probability;
}

bool PatternsEqual(const sequencer::Pattern& lhs, const sequencer::Pattern& rhs)
{
    if (lhs.tempo_multiplier != rhs.tempo_multiplier ||
        lhs.step_count != rhs.step_count ||
        lhs.step_division != rhs.step_division ||
        lhs.playback_mode != rhs.playback_mode ||
        lhs.repeat_count != rhs.repeat_count ||
        lhs.arp_mode != rhs.arp_mode || lhs.arp_rate != rhs.arp_rate) return false;
    for (uint8_t step = 0u; step < sequencer::kStepCount; ++step)
    {
        if (!StepSlotsEqual(lhs.steps[step], rhs.steps[step])) return false;
    }
    return true;
}

void ServiceUntilIdle(SequencerDevice* device);

void TestPatternLoopPasteCase(uint8_t initial_step_count,
                              uint8_t destination_bar,
                              uint8_t expected_step_count,
                              const char* check_name)
{
    Check(PrepareStore(), "Pattern Loop case FRAM prepared");
    sequencer::Pattern source_pattern = MakePattern(0u);
    source_pattern.step_count = initial_step_count;
    source_pattern.repeat_count = 5u;
    for (uint8_t bar = 0u; bar < sequencer::kStepCount; ++bar)
    {
        source_pattern.steps[bar].note_ledger[bar].notes[0] = (uint8_t)(40u + bar);
        source_pattern.steps[bar].duration_multiplier = (uint32_t)(bar + 1u);
        source_pattern.steps[bar].custom_chord_name[0] = (char)('A' + bar);
    }
    Check(PatternStore_WriteGeneration(0u, &source_pattern, 100u),
          "Pattern Loop case source committed");

    SequencerDevice device;
    Check(InitializeDevice(&device), "Pattern Loop case EDIT initialized");
    UI_SequencerClipboard_Init();
    Check(sequencer_clipboard::CopyEditBar(device, 0u), "Pattern Loop case source Bar copied");

    sequencer::StepSlot before[sequencer::kStepCount]{};
    for (uint8_t bar = 0u; bar < sequencer::kStepCount; ++bar)
        Check(device.GetEditStepSlot(bar, &before[bar]), "Pattern Loop case Bar captured before Paste");

    const int8_t edit_index = PatternResidency_GetRoleEntry(PatternCacheRole::Edit);
    const PatternCacheEntry* edit = PatternResidency_GetCacheEntry((uint8_t)edit_index);
    const uint32_t generation_before = edit->current_generation;
    const uint32_t persisted_before = edit->persisted_generation;
    const uint8_t repeat_before = device.GetPatternRepeatCount();
        Check(device.GetPatternStepCount() == initial_step_count,
            "Pattern Loop starts at requested test boundary");

    Check(sequencer_clipboard::PasteEditBar(device, (uint8_t)(destination_bar - 1u)), check_name);
        Check(device.GetPatternStepCount() == expected_step_count &&
            device.GetPatternStepCount() >= initial_step_count,
            "Pattern Loop Paste extends to destination when needed and never shrinks");
    Check(device.GetPatternRepeatCount() == repeat_before && repeat_before == 5u,
          "Pattern Loop extension leaves Pattern repeat_count unchanged");
    Check(edit->current_generation == generation_before + 1u &&
          edit->persisted_generation == persisted_before &&
          PatternResidency_IsDirty((uint8_t)edit_index),
          "Bar replacement and loop extension advance generation exactly once");

    for (uint8_t bar = 0u; bar < sequencer::kStepCount; ++bar)
    {
        if (bar == (uint8_t)(destination_bar - 1u)) continue;
        sequencer::StepSlot after{};
        Check(device.GetEditStepSlot(bar, &after) && StepSlotsEqual(after, before[bar]),
              "Pattern Loop extension leaves every other Bar unchanged");
    }
}

void TestPatternLoopPasteExtension(void)
{
    TestPatternLoopPasteCase(2u, 1u, 2u, "Paste into B1 leaves Pattern Loop at 2");
    TestPatternLoopPasteCase(2u, 2u, 2u, "Paste into B2 leaves Pattern Loop at 2");
    TestPatternLoopPasteCase(2u, 3u, 3u, "Paste into B3 extends Pattern Loop to 3");
    TestPatternLoopPasteCase(2u, 7u, 7u, "Paste into B7 extends Pattern Loop to 7");
    TestPatternLoopPasteCase(2u, 12u, 12u, "Paste into B12 extends Pattern Loop to 12");
    TestPatternLoopPasteCase(9u, 3u, 9u, "Paste into an earlier Bar never shrinks Pattern Loop");
}

void TestStage1BarClipboard(void)
{
    Check(PrepareStore(), "Stage 1 clipboard store prepared");
    sequencer::Pattern source = MakePattern(0u);
    sequencer::StepSlot& bar_six = source.steps[5];
    bar_six.type = sequencer::StepType::Chord;
    bar_six.bar_state = sequencer::BarState::Skip;
    bar_six.duration_multiplier = 7u;
    bar_six.repeat_count = 23u;
    bar_six.note_mask = 0x05A5u;
    bar_six.note_ledger[3].notes = {{48u, 55u, 64u, 0xFFu}};
    bar_six.event_length_packed.fill(0xA5u);
    bar_six.lane_slots.fill(0x39u);
    bar_six.lane_override_mask.fill(0xC3u);
    memcpy(bar_six.custom_chord_name, "BAR SIX SNAPSHOT", 16u);
    bar_six.velocity = 91u;
    bar_six.probability = 37u;
    source.steps[6].note_ledger[2].notes = {{60u, 67u, 0xFFu, 0xFFu}};
    source.steps[6].custom_chord_name[0] = 'S';
    Check(PatternStore_WriteGeneration(0u, &source, 100u),
        "Stage 1 source Pattern committed");

    SequencerDevice device;
    Check(InitializeDevice(&device), "Stage 1 source Pattern loaded to EDIT");
    UI_SequencerClipboard_Init();
    Check(sequencer_clipboard::GetBarClipboard().valid == 0u,
        "Bar clipboard starts invalid after UI clipboard initialization");
    Check(sequencer_clipboard::GetPatternClipboard().valid == 0u,
        "Pattern clipboard starts invalid after UI clipboard initialization");

    Check(UI_SequencerClipboard_IsBarCopyPress(1u, 0u, 6u) == 1u,
        "unshifted M6 is copy input in Bar editor context");
    Check(UI_SequencerClipboard_IsBarCopyPress(1u, 1u, 6u) == 0u &&
        UI_SequencerClipboard_IsBarCopyPress(1u, 0u, 7u) == 0u,
        "shifted M6 and unshifted M7 are not Bar Copy inputs");
    Check(UI_SequencerClipboard_IsBarCopyPress(0u, 0u, 6u) == 0u &&
        UI_SequencerClipboard_IsBarCopyPress(0u, 0u, 7u) == 0u,
        "Main-screen M6/M7 bypass Bar Copy interception and continue through existing Bar-selection routing");

    const sequencer_clipboard::PatternClipboard pattern_clipboard_before =
        sequencer_clipboard::GetPatternClipboard();
    const PatternRoleState roles_before = *PatternResidency_GetRoles();
    const int8_t edit_entry_index = PatternResidency_GetRoleEntry(PatternCacheRole::Edit);
    const PatternCacheEntry* edit_entry = PatternResidency_GetCacheEntry((uint8_t)edit_entry_index);
    const uint32_t generation_before = edit_entry->current_generation;
    const uint32_t persisted_before = edit_entry->persisted_generation;
    const uint32_t writes_before = s_write_count;
    sequencer::StepSlot expected_six{};
    Check(device.GetEditStepSlot(5u, &expected_six), "read-only EDIT Bar 6 accessor succeeds");

    Check(sequencer_clipboard::CopyEditBar(device, 5u), "Bar 6 snapshot copy succeeds while stopped");
    const sequencer_clipboard::BarClipboard first_copy = sequencer_clipboard::GetBarClipboard();
    Check(first_copy.valid == 1u && first_copy.source_pattern == 0u && first_copy.source_bar == 6u,
        "Bar clipboard records source Pattern 0 and one-based Bar 6");
    Check(StepSlotsEqual(first_copy.snapshot, expected_six),
        "Bar clipboard contains the complete StepSlot value");
    const sequencer_clipboard::PatternClipboard pattern_clipboard_after =
        sequencer_clipboard::GetPatternClipboard();
    Check(pattern_clipboard_after.valid == pattern_clipboard_before.valid &&
          pattern_clipboard_after.source_pattern == pattern_clipboard_before.source_pattern &&
          PatternsEqual(pattern_clipboard_after.snapshot, pattern_clipboard_before.snapshot),
          "Bar Copy leaves the complete Pattern clipboard state unchanged");
    Check(edit_entry->current_generation == generation_before &&
        edit_entry->persisted_generation == persisted_before &&
        !PatternResidency_IsDirty((uint8_t)edit_entry_index),
        "Bar Copy does not advance generation or dirty the EDIT cache entry");
        const PatternRoleState* roles_after = PatternResidency_GetRoles();
        Check(s_write_count == writes_before &&
            roles_after->play_entry == roles_before.play_entry &&
            roles_after->next_entry == roles_before.next_entry &&
            roles_after->edit_entry == roles_before.edit_entry,
        "Bar Copy does not write FRAM or alter PLAY/NEXT/EDIT roles");

    Check(sequencer_clipboard::CopyEditBar(device, 6u), "second Bar Copy succeeds");
    const sequencer_clipboard::BarClipboard second_copy = sequencer_clipboard::GetBarClipboard();
    sequencer::StepSlot expected_seven{};
    Check(device.GetEditStepSlot(6u, &expected_seven), "read-only EDIT Bar 7 accessor succeeds");
    Check(second_copy.source_bar == 7u && StepSlotsEqual(second_copy.snapshot, expected_seven),
        "second Bar Copy replaces the prior snapshot and metadata");
        Check(sequencer_clipboard::GetPatternClipboard().valid == pattern_clipboard_before.valid &&
            sequencer_clipboard::GetPatternClipboard().source_pattern == pattern_clipboard_before.source_pattern &&
            PatternsEqual(sequencer_clipboard::GetPatternClipboard().snapshot,
                    pattern_clipboard_before.snapshot),
            "second Bar Copy still leaves Pattern clipboard unchanged");

    device.Start();
    Check(sequencer_clipboard::CopyEditBar(device, 5u) && device.IsPlaying(),
        "Bar Copy remains available while transport is playing");
    Check(edit_entry->current_generation == generation_before &&
        !PatternResidency_IsDirty((uint8_t)edit_entry_index) && s_write_count == writes_before,
        "playing Bar Copy remains read-only with no persistence operation");
    device.Stop();
}

    void TestStage2BarPaste(void)
    {
        Check(PrepareStore(), "Stage 2 paste store prepared");
        sequencer::Pattern source_pattern = MakePattern(0u);
        sequencer::StepSlot& source_bar = source_pattern.steps[0];
        source_bar.bar_state = sequencer::BarState::Active;
        source_bar.duration_multiplier = 9u;
        source_bar.note_ledger[0].notes = {{48u, 55u, 64u, 0xFFu}};
        source_bar.event_length_packed.fill(0xA6u);
        source_bar.lane_slots.fill(0x39u);
        source_bar.lane_override_mask.fill(0xC3u);
        memcpy(source_bar.custom_chord_name, "PASTE SOURCE BAR", 17u);
        source_bar.velocity = 87u;
        source_bar.probability = 43u;

        sequencer::Pattern destination_pattern = MakePattern(1u);
        destination_pattern.step_division = 4u;
        destination_pattern.steps[0].note_ledger[0].notes = {{72u, 0xFFu, 0xFFu, 0xFFu}};
        Check(PatternStore_WriteGeneration(0u, &source_pattern, 100u) &&
            PatternStore_WriteGeneration(1u, &destination_pattern, 101u),
            "Stage 2 source and destination Patterns committed");

        SequencerDevice device;
        Check(InitializeDevice(&device), "Stage 2 source Pattern loaded to EDIT");
        UI_SequencerClipboard_Init();
        Check(UI_SequencerClipboard_IsBarPastePress(1u, 0u, 7u) == 1u &&
            UI_SequencerClipboard_IsBarPastePress(1u, 1u, 7u) == 0u &&
            UI_SequencerClipboard_IsBarPastePress(0u, 0u, 7u) == 0u,
            "only unshifted M7 in Bar editor requests Bar Paste");

        sequencer::StepSlot original_destination{};
        Check(device.GetEditStepSlot(2u, &original_destination),
            "same-Pattern destination Bar is readable before Paste");
        const int8_t source_entry_index = PatternResidency_GetRoleEntry(PatternCacheRole::Edit);
        const PatternCacheEntry* source_entry = PatternResidency_GetCacheEntry((uint8_t)source_entry_index);
        const uint32_t source_generation = source_entry->current_generation;
        Check(!sequencer_clipboard::CanPasteEditBar(device, 2u) &&
            !sequencer_clipboard::PasteEditBar(device, 2u),
            "invalid clipboard cannot Paste");
        sequencer::StepSlot unchanged_destination{};
        device.GetEditStepSlot(2u, &unchanged_destination);
        Check(StepSlotsEqual(unchanged_destination, original_destination) &&
            source_entry->current_generation == source_generation,
            "invalid-clipboard rejection leaves destination and generation unchanged");

        Check(sequencer_clipboard::CopyEditBar(device, 0u), "copy source P1/B1 for Paste tests");
        const sequencer_clipboard::BarClipboard clipboard_before = sequencer_clipboard::GetBarClipboard();
        uint16_t expected_note_mask = 0u;
        for (uint8_t position = 0u; position < sequencer::kStepLedgerMax; ++position)
            expected_note_mask |= sequencer::LedgerSlotToPitchClassMask(
                clipboard_before.snapshot.note_ledger[position]);
        const uint32_t writes_before_paste = s_write_count;
        Check(sequencer_clipboard::CanPasteEditBar(device, 2u),
            "M7 Paste preflight succeeds for stopped READY EDIT destination");
        device.GetEditStepSlot(2u, &unchanged_destination);
        Check(StepSlotsEqual(unchanged_destination, original_destination) &&
            source_entry->current_generation == source_generation,
            "Paste request/preflight does not mutate before YES");

        Check(sequencer_clipboard::PasteEditBar(device, 2u), "YES applies same-Pattern Bar Paste");
        const uint32_t same_pattern_generation = source_entry->current_generation;
        Check(same_pattern_generation == source_generation + 1u &&
            source_entry->persisted_generation == source_generation &&
            PatternResidency_IsDirty((uint8_t)source_entry_index),
            "successful Paste advances EDIT generation once and leaves Pattern dirty");
        sequencer::StepSlot pasted_same_pattern{};
        device.GetEditStepSlot(2u, &pasted_same_pattern);
        const uint8_t pasted_same_pattern_count = device.GetPatternStepCount();
        Check(CanonicalStepSlotsEqual(pasted_same_pattern, clipboard_before.snapshot),
            "same-Pattern Paste copies all canonical Bar fields");
        Check(pasted_same_pattern.type == sequencer::StepType::Chord &&
            pasted_same_pattern.note_mask == expected_note_mask &&
            pasted_same_pattern.repeat_count == 4u,
            "Paste regenerates type/note-mask projections and destination repeat count");
        Check(s_write_count == writes_before_paste,
            "Paste itself does not start a PatternStore write");

        const sequencer_clipboard::PatternClipboard pattern_clipboard_before =
            sequencer_clipboard::GetPatternClipboard();
        Check(sequencer_clipboard::GetBarClipboard().valid == 1u &&
            StepSlotsEqual(sequencer_clipboard::GetBarClipboard().snapshot, clipboard_before.snapshot) &&
            sequencer_clipboard::GetBarClipboard().source_pattern == clipboard_before.source_pattern &&
            sequencer_clipboard::GetBarClipboard().source_bar == clipboard_before.source_bar,
            "successful Paste retains the original Bar clipboard");
        Check(sequencer_clipboard::GetPatternClipboard().valid == pattern_clipboard_before.valid &&
            sequencer_clipboard::GetPatternClipboard().source_pattern == pattern_clipboard_before.source_pattern &&
            PatternsEqual(sequencer_clipboard::GetPatternClipboard().snapshot,
                    pattern_clipboard_before.snapshot),
            "Bar Paste does not change Pattern clipboard");

        sequencer::StepSlot source_after_same_paste{};
        const sequencer::Pattern* play_pattern = PatternResidency_GetRolePatternConst(PatternCacheRole::Play);
        Check(play_pattern && StepSlotsEqual(play_pattern->steps[0], clipboard_before.snapshot),
            "same-Pattern Paste leaves the copied source Bar unchanged");
        source_after_same_paste = play_pattern->steps[0];

        Check(sequencer_clipboard::CanPasteEditBar(device, 2u),
            "NO path can dismiss a valid Paste request");
        device.GetEditStepSlot(2u, &unchanged_destination);
        Check(StepSlotsEqual(unchanged_destination, pasted_same_pattern) &&
            source_entry->current_generation == same_pattern_generation &&
            device.GetPatternStepCount() == pasted_same_pattern_count,
            "dismissing Paste without YES leaves destination and Pattern Loop unchanged");

        Check(sequencer_clipboard::CopyEditBar(device, 0u), "re-copy source before playback rejection tests");
        const sequencer_clipboard::BarClipboard playing_clipboard = sequencer_clipboard::GetBarClipboard();
        device.Start();
        Check(!sequencer_clipboard::CanPasteEditBar(device, 2u) &&
            !sequencer_clipboard::PasteEditBar(device, 2u),
            "Paste request while playing is rejected");
        device.GetEditStepSlot(2u, &unchanged_destination);
        Check(StepSlotsEqual(unchanged_destination, pasted_same_pattern) &&
            source_entry->current_generation == same_pattern_generation &&
            device.GetPatternStepCount() == pasted_same_pattern_count &&
            sequencer_clipboard::GetBarClipboard().valid == playing_clipboard.valid &&
            StepSlotsEqual(sequencer_clipboard::GetBarClipboard().snapshot, playing_clipboard.snapshot),
            "playing request rejection leaves destination, Pattern Loop and clipboard unchanged");
        device.Stop();

        Check(sequencer_clipboard::CanPasteEditBar(device, 2u),
            "Paste confirmation opens while stopped");
        device.Start();
        Check(!sequencer_clipboard::PasteEditBar(device, 2u),
            "YES-time transport recheck rejects if playback starts while modal is open");
        device.GetEditStepSlot(2u, &unchanged_destination);
        Check(StepSlotsEqual(unchanged_destination, pasted_same_pattern) &&
            source_entry->current_generation == same_pattern_generation &&
            device.GetPatternStepCount() == pasted_same_pattern_count &&
            sequencer_clipboard::GetBarClipboard().valid == playing_clipboard.valid &&
            StepSlotsEqual(sequencer_clipboard::GetBarClipboard().snapshot, playing_clipboard.snapshot),
            "YES-time playing rejection leaves destination, Pattern Loop and clipboard unchanged");
        device.Stop();

        device.SetCurrentPatternIndex(1u);
        ServiceUntilIdle(&device);
        Check(device.GetCurrentPatternIndex() == 1u,
            "cross-Pattern EDIT navigation completes before Paste");
        const int8_t destination_entry_index = PatternResidency_GetRoleEntry(PatternCacheRole::Edit);
        const PatternCacheEntry* destination_entry =
          PatternResidency_GetCacheEntry((uint8_t)destination_entry_index);
        const uint32_t destination_initial_generation = destination_entry->current_generation;
        const uint32_t destination_initial_persisted = destination_entry->persisted_generation;
        const uint32_t destination_writes_before = s_write_count;
        uint8_t destination_bar_count = 0u;
        for (uint8_t destination_bar = 0u; destination_bar < sequencer::kStepCount; ++destination_bar)
        {
          const uint32_t generation_before = destination_entry->current_generation;
          Check(sequencer_clipboard::CanPasteEditBar(device, destination_bar),
              "cross-Pattern destination Bar is ready for Paste");
          Check(sequencer_clipboard::PasteEditBar(device, destination_bar),
              "cross-Pattern Bar Paste succeeds for selected destination");
          ++destination_bar_count;
          Check(destination_entry->current_generation == generation_before + 1u &&
              destination_entry->persisted_generation == destination_initial_persisted &&
              PatternResidency_IsDirty((uint8_t)destination_entry_index),
              "each cross-Pattern Paste advances generation once and remains dirty");
          sequencer::StepSlot pasted{};
          Check(device.GetEditStepSlot(destination_bar, &pasted) &&
              CanonicalStepSlotsEqual(pasted, playing_clipboard.snapshot),
              "copied P1/B1 snapshot pastes into each selected destination Bar");
          Check(pasted.type == sequencer::StepType::Chord &&
              pasted.note_mask == expected_note_mask && pasted.repeat_count == 16u,
              "cross-Pattern Paste regenerates derived fields for destination division");
        }
        Check(destination_bar_count == sequencer::kStepCount &&
            destination_entry->current_generation ==
              destination_initial_generation + sequencer::kStepCount,
            "one copied Bar can be pasted into all twelve destination Bars");
        Check(device.GetPatternStepCount() == sequencer::kStepCount,
            "cross-Pattern paste into B12 extends Pattern Loop to twelve");
        Check(device.GetPatternRepeatCount() == destination_pattern.repeat_count,
            "cross-Pattern loop extension preserves destination Pattern repeat_count");
        Check(s_write_count == destination_writes_before,
            "repeated Paste operations defer all writes to the async service");
        Check(sequencer_clipboard::GetBarClipboard().valid == 1u &&
            StepSlotsEqual(sequencer_clipboard::GetBarClipboard().snapshot, playing_clipboard.snapshot),
            "repeated cross-Pattern Paste retains the original clipboard");

        sequencer::StepSlot replacement_source{};
        replacement_source.bar_state = sequencer::BarState::Active;
        replacement_source.duration_multiplier = 3u;
        replacement_source.note_ledger[4].notes = {{72u, 79u, 0xFFu, 0xFFu}};
        replacement_source.event_length_packed.fill(0x5Au);
        replacement_source.lane_slots.fill(0x96u);
        replacement_source.lane_override_mask.fill(0x69u);
        memcpy(replacement_source.custom_chord_name, "NEW COPY SOURCE", 16u);
        replacement_source.velocity = 111u;
        replacement_source.probability = 83u;
        Check(device.ReplaceEditStepSlot(11u, replacement_source),
            "seed a distinct source Bar for replacement-copy test");
        const uint32_t before_replacement_copy = destination_entry->current_generation;
        Check(sequencer_clipboard::CopyEditBar(device, 11u),
            "new M6 Copy replaces the old Bar clipboard");
        const sequencer_clipboard::BarClipboard new_clipboard = sequencer_clipboard::GetBarClipboard();
        Check(new_clipboard.source_pattern == 1u && new_clipboard.source_bar == 12u &&
            CanonicalStepSlotsEqual(new_clipboard.snapshot, replacement_source),
            "replacement Copy records new source metadata and complete snapshot");
        Check(destination_entry->current_generation == before_replacement_copy,
            "new Bar Copy does not add another generation mutation");
        Check(sequencer_clipboard::PasteEditBar(device, 3u),
            "Paste after replacement Copy uses the new clipboard");
        sequencer::StepSlot pasted_replacement{};
        device.GetEditStepSlot(3u, &pasted_replacement);
        Check(CanonicalStepSlotsEqual(pasted_replacement, new_clipboard.snapshot),
            "subsequent Paste uses the replacement clipboard snapshot");
        const PatternCacheEntry* play_entry = PatternResidency_GetCacheEntry(
          (uint8_t)PatternResidency_GetRoleEntry(PatternCacheRole::Play));
        Check(play_entry && device.GetCurrentPatternIndex() != device.GetPlayPatternIndex() &&
            StepSlotsEqual(play_entry->pattern.steps[0], source_after_same_paste),
            "cross-Pattern Paste leaves source PLAY Pattern independent");

        const uint32_t persisted_target_generation = destination_entry->current_generation;
        for (uint32_t guard = 0u; guard < 20000u &&
           PatternResidency_IsDirty((uint8_t)destination_entry_index); ++guard)
        {
          (void)PatternResidency_BeginAnyDirtyWriteback();
          device.ServicePatternResidency();
        }
        Check(!PatternResidency_IsDirty((uint8_t)destination_entry_index) &&
            destination_entry->persisted_generation == persisted_target_generation,
            "normal asynchronous PatternStore service eventually cleans pasted Pattern");
        sequencer::Pattern persisted_destination{};
        uint32_t persisted_generation = 0u;
        Check(PatternStore_ReadWithGeneration(1u, &persisted_destination, &persisted_generation) &&
            persisted_generation == persisted_target_generation &&
                    persisted_destination.step_count == sequencer::kStepCount &&
                    persisted_destination.repeat_count == destination_pattern.repeat_count &&
            CanonicalStepSlotsEqual(persisted_destination.steps[3], new_clipboard.snapshot),
                    "async PatternStore writeback persists destination Bar and extended Pattern Loop");

                    Check(PatternStore_ReadWithGeneration(1u, &persisted_destination, &persisted_generation) &&
                          persisted_destination.step_count == sequencer::kStepCount,
                          "async persistence stores the extended step_count field");

                PatternStore_ResetRuntimeStateForTest();
                PatternResidency_ResetForTest();
                SequencerDevice rebooted_device;
                Check(InitializeDevice(&rebooted_device) && rebooted_device.GetCurrentPatternIndex() == 0u,
                        "rebooted sequencer reloads the persisted destination Pattern");
                rebooted_device.SetCurrentPatternIndex(1u);
                ServiceUntilIdle(&rebooted_device);
                Check(rebooted_device.GetCurrentPatternIndex() == 1u &&
                            rebooted_device.GetPatternStepCount() == sequencer::kStepCount &&
                            rebooted_device.GetPatternRepeatCount() == destination_pattern.repeat_count,
                        "reloaded destination restores extended Pattern Loop without changing repeat_count");
    }

void ServiceUntilIdle(SequencerDevice* device)
{
    for (uint32_t guard = 0u; guard < 1000u; ++guard)
    {
        device->ServicePatternResidency();
        bool busy = false;
        for (uint8_t entry = 0u; entry < PATTERN_RESIDENCY_CACHE_ENTRY_COUNT; ++entry)
        {
            const PatternCacheEntry* cache = PatternResidency_GetCacheEntry(entry);
            if (cache && cache->io_state != PATTERN_CACHE_IO_IDLE) busy = true;
        }
        if (!busy) return;
    }
}

/* Host Bridge shim for UI_TimingCommit_Apply(); mirrors sequencer_bridge.cpp
 * PersistBegin/End coalescing and per-setter PersistPattern/PersistSong calls. */
struct TimingBridgeShim
{
    SequencerDevice* device = nullptr;
    uint8_t depth = 0u;
    uint8_t pattern_pending = 0u;
    uint8_t song_pending = 0u;
    uint32_t set_step_count_calls = 0u;
    uint32_t set_division_calls = 0u;
    uint32_t set_timing_calls = 0u;
    uint32_t set_swing_calls = 0u;
    uint32_t pattern_mutations = 0u;
    uint32_t song_persists = 0u;
};

TimingBridgeShim s_timing_shim;

void ShimPersistPattern()
{
    if (s_timing_shim.depth > 0u) { s_timing_shim.pattern_pending = 1u; return; }
    s_timing_shim.device->MarkEditPatternMutated();
    ++s_timing_shim.pattern_mutations;
}

void ShimPersistSong()
{
    if (s_timing_shim.depth > 0u) { s_timing_shim.song_pending = 1u; return; }
    ++s_timing_shim.song_persists;
}

struct TimingFields
{
    uint8_t step_count;
    uint8_t division;
    uint8_t numerator;
    uint8_t denominator;
    uint8_t swing;
};

struct TimingCommitExpectation
{
    bool accepted;
    uint32_t set_step_count_calls;
    uint32_t set_division_calls;
    uint32_t set_timing_calls;
    uint32_t set_swing_calls;
    uint32_t pattern_mutations;
    uint32_t song_persists;
    uint32_t directory_lookups;
};

void TestTimingCommitCase(const char* label,
                          const TimingFields& draft_delta_from_initial,
                          const TimingCommitExpectation& expect)
{
    SequencerDevice device;
    Check(PrepareTimingDraftDevice(&device), "timing commit store prepared");
    s_timing_shim = TimingBridgeShim{};
    s_timing_shim.device = &device;

    const TimingFields before = {device.GetPatternStepCount(), device.GetPatternStepDivision(),
                                 device.GetTimeSigNumerator(), device.GetTimeSigDenominator(),
                                 device.GetSwing()};
    const TimingFields& d = draft_delta_from_initial;
    const UiTimingValues draft = {
        d.step_count ? d.step_count : before.step_count,
        d.division ? d.division : before.division,
        d.numerator ? d.numerator : before.numerator,
        d.denominator ? d.denominator : before.denominator,
        d.swing ? d.swing : before.swing,
    };

    const int8_t edit_index = PatternResidency_GetRoleEntry(PatternCacheRole::Edit);
    const PatternCacheEntry* edit = PatternResidency_GetCacheEntry((uint8_t)edit_index);
    const sequencer::Pattern pattern_before = edit->pattern;
    const uint32_t generation_before = edit->current_generation;
    const uint32_t persisted_before = edit->persisted_generation;
    const uint32_t writes_before = s_write_count;
    const uint32_t reads_before = s_read_count;
    const uint32_t lookups_before = PatternResidency_GetDirectoryLookupCountForTest();

    const bool accepted = UI_TimingCommit_Apply(&draft) != 0u;

    std::printf("  -- %s\n", label);
    Check(accepted == expect.accepted, label);
    Check(s_timing_shim.set_step_count_calls == expect.set_step_count_calls &&
          s_timing_shim.set_division_calls == expect.set_division_calls &&
          s_timing_shim.set_timing_calls == expect.set_timing_calls &&
          s_timing_shim.set_swing_calls == expect.set_swing_calls,
          "TIME commit invokes only the correctly scoped setters");
    Check(s_timing_shim.depth == 0u && s_timing_shim.pattern_pending == 0u &&
          s_timing_shim.song_pending == 0u,
          "TIME commit closes its persistence bracket");
    Check(s_timing_shim.pattern_mutations == expect.pattern_mutations &&
          s_timing_shim.song_persists == expect.song_persists,
          "TIME commit produces the minimum Pattern/Song persistence requests");
    Check(PatternResidency_GetDirectoryLookupCountForTest() - lookups_before ==
              expect.directory_lookups,
          "TIME commit performs only its required Pattern directory lookups");
    Check(s_read_count == reads_before && s_write_count == writes_before,
          "TIME commit performs no synchronous PatternStore read or write");
    Check(edit->current_generation == generation_before + expect.pattern_mutations &&
          edit->persisted_generation == persisted_before &&
          (PatternResidency_IsDirty((uint8_t)edit_index) != 0u) == (expect.pattern_mutations != 0u),
          "TIME commit advances EDIT generation once only when Pattern data changed");

    const TimingFields expected = expect.accepted
        ? TimingFields{draft.step_count, draft.step_division, draft.ts_num, draft.ts_den, draft.swing}
        : before;
    Check(device.GetPatternStepCount() == expected.step_count &&
          device.GetPatternStepDivision() == expected.division &&
          device.GetTimeSigNumerator() == expected.numerator &&
          device.GetTimeSigDenominator() == expected.denominator &&
          device.GetSwing() == expected.swing,
          expect.accepted ? "TIME commit applies exactly the drafted fields"
                          : "rejected TIME commit leaves every field unchanged");

    const bool lazy_projection_changes =
        expected.division != before.division || expected.numerator != before.numerator ||
        expected.denominator != before.denominator;
    if (!lazy_projection_changes)
    {
        sequencer::Pattern expected_pattern = pattern_before;
        expected_pattern.step_count = expected.step_count;
        Check(PatternsEqual(edit->pattern, expected_pattern),
              "Pattern contents are otherwise unchanged");
    }
    else
    {
        bool canonical_equal = edit->pattern.repeat_count == pattern_before.repeat_count &&
                               edit->pattern.playback_mode == pattern_before.playback_mode;
        for (uint8_t step = 0u; step < sequencer::kStepCount; ++step)
            canonical_equal = canonical_equal &&
                CanonicalStepSlotsEqual(edit->pattern.steps[step], pattern_before.steps[step]);
        Check(canonical_equal, "grid/time-signature commit leaves canonical Pattern events unchanged");
    }

    if (expect.pattern_mutations != 0u)
    {
        const uint32_t target_generation = edit->current_generation;
        Check(PatternResidency_BeginAnyDirtyWriteback(),
              "TIME Pattern mutation starts asynchronous writeback");
        ServiceUntilIdle(&device);
        sequencer::Pattern persisted{};
        uint32_t persisted_generation = 0u;
        Check(!PatternResidency_IsDirty((uint8_t)edit_index) &&
              edit->persisted_generation == target_generation &&
              PatternStore_ReadWithGeneration(device.GetCurrentPatternIndex(), &persisted,
                                              &persisted_generation) &&
              persisted_generation == target_generation &&
              persisted.step_count == expected.step_count &&
              persisted.step_division == expected.division,
              "TIME Pattern mutation completes through asynchronous writeback");
    }
}

void TestTimingDraftScoping(void)
{
    SequencerDevice defaults;
    Check(PrepareTimingDraftDevice(&defaults), "timing draft defaults prepared");
    Check(defaults.GetPatternStepCount() == 2u && defaults.GetPatternStepDivision() == 1u &&
          defaults.GetTimeSigNumerator() == 4u && defaults.GetTimeSigDenominator() == 4u &&
          defaults.GetSwing() != 7u,
          "timing draft fixture starts at Loop 2, 1/4 grid, 4/4");
    const uint32_t all = sequencer::kPatternCount;

    /*                                    loop div num den swing */
    TestTimingCommitCase("Loop-only 2-to-3 commit accepted", {3u, 0u, 0u, 0u, 0u},
                         {true, 1u, 0u, 0u, 0u, 1u, 0u, 0u});
    TestTimingCommitCase("Swing-only commit accepted", {0u, 0u, 0u, 0u, 7u},
                         {true, 0u, 0u, 0u, 1u, 0u, 1u, 0u});
    TestTimingCommitCase("Division-only commit accepted", {0u, 2u, 0u, 0u, 0u},
                         {true, 0u, 1u, 0u, 0u, 1u, 0u, 0u});
    TestTimingCommitCase("Time-signature-only commit accepted", {0u, 0u, 3u, 4u, 0u},
                         {true, 0u, 0u, 1u, 0u, 1u, 1u, all});
    TestTimingCommitCase("No-change commit accepted", {0u, 0u, 0u, 0u, 0u},
                         {true, 0u, 0u, 0u, 0u, 0u, 0u, 0u});
    TestTimingCommitCase("Loop plus division commit accepted", {3u, 2u, 0u, 0u, 0u},
                         {true, 1u, 1u, 0u, 0u, 1u, 0u, 0u});
    TestTimingCommitCase("Loop plus time-signature commit accepted", {3u, 0u, 3u, 4u, 0u},
                         {true, 1u, 0u, 1u, 0u, 1u, 1u, all});
    TestTimingCommitCase("Loop plus division plus swing commit accepted", {3u, 2u, 0u, 0u, 7u},
                         {true, 1u, 1u, 0u, 1u, 1u, 1u, 0u});
    TestTimingCommitCase("Loop plus invalid division rejected atomically", {3u, 3u, 0u, 0u, 7u},
                         {false, 0u, 1u, 0u, 0u, 0u, 0u, 0u});
    TestTimingCommitCase("Loop plus invalid time signature rejected atomically", {3u, 0u, 7u, 8u, 7u},
                         {false, 0u, 0u, 1u, 0u, 0u, 0u, 0u});
}

void QueueOneMusicalStep(SequencerDevice* device)
{
    for (uint32_t tick = 0u; tick < 96u; ++tick) device->NotifyUClockMusicalCallback();
}

void AdvanceToPatternBoundary(SequencerDevice* device)
{
    for (uint8_t position = 0u; position < 4u; ++position)
    {
        QueueOneMusicalStep(device);
        device->ServicePendingMusicalTicks();
        (void)device->ServiceOnePendingStep();
    }
}

void TestOwnershipAndLiveEditing(void)
{
    SequencerDevice device;
    Check(Prepare(&device), "initial PLAY load and validation succeeds");
    const PatternRoleState* roles = PatternResidency_GetRoles();
    Check(device.IsPatternResidencyReady() && roles->play_entry < 3u,
          "PLAY is assigned only after validated load");
    Check(PatternResidency_IsPinned(roles->play_entry), "production PLAY is pinned");
    Check(roles->play_entry == roles->edit_entry, "initial PLAY equals EDIT alias");

    sequencer::Pattern* shared = PatternResidency_GetRolePattern(PatternCacheRole::Play);
    Check(shared == PatternResidency_GetRolePattern(PatternCacheRole::Edit),
          "PLAY and EDIT reference the same physical Pattern object");

    device.SetStepCustomNoteMask(0u, sequencer::NOTE_C);
    device.MarkEditPatternMutated();
    uint8_t notes[4] = {0xFFu, 0xFFu, 0xFFu, 0xFFu};
    Check(device.GetCurrentStepNotesForPlayback(notes) == 1u && notes[0] == 60u,
          "live note edit is immediately visible to PLAY");

    device.SetStepChordParams(0u, 2u, 1u, 0u, 1u, 1u);
    device.MarkEditPatternMutated();
    Check(device.GetCurrentStepNotesForPlayback(notes) >= 3u && notes[0] == 62u,
          "live chord edit is immediately visible to PLAY");

    device.SetPatternArpMode(sequencer::ArpMode::Up);
    device.MarkEditPatternMutated();
    Check(device.GetCurrentArpMode() == sequencer::ArpMode::Up,
          "live arp edit is immediately visible to PLAY");

    const PatternCacheEntry* generation_entry = PatternResidency_GetCacheEntry(
        (uint8_t)PatternResidency_GetRoleEntry(PatternCacheRole::Edit));
    const uint32_t before_timing_edit = generation_entry->current_generation;
    Check(device.SetPatternTiming(2u, 4u, 4u) && device.GetPatternStepDivision() == 2u,
          "timing/division edit applies to resident EDIT Pattern");
    device.MarkEditPatternMutated();
    generation_entry = PatternResidency_GetCacheEntry(
        (uint8_t)PatternResidency_GetRoleEntry(PatternCacheRole::Edit));
    Check(generation_entry->current_generation == before_timing_edit + 1u,
          "authoritative timing edit increments current generation");

    const int8_t edit_entry = PatternResidency_GetRoleEntry(PatternCacheRole::Edit);
    const PatternCacheEntry* cache = PatternResidency_GetCacheEntry((uint8_t)edit_entry);
    const uint32_t before_projection = cache->current_generation;
    Check(PatternResidency_ApplyLazyProjections((uint8_t)edit_entry,
                                                &device.GetSongView().time_sig),
          "lazy projection derives resident fields");
    cache = PatternResidency_GetCacheEntry((uint8_t)edit_entry);
    Check(cache->current_generation == before_projection,
          "lazy projection does not increment generation");
}

void TestEditIsolationAndAliasing(void)
{
    SequencerDevice device;
    Check(Prepare(&device), "edit isolation store prepared");
    const uint8_t play_note = PatternResidency_GetRolePattern(PatternCacheRole::Play)
        ->steps[0].note_ledger[0].notes[0];
    device.SetCurrentPatternIndex(2u);
    ServiceUntilIdle(&device);
    const PatternRoleState* roles = PatternResidency_GetRoles();
    Check(roles->play_entry != roles->edit_entry && device.GetCurrentPatternIndex() == 2u,
          "PLAY not equal EDIT uses separate cache entries");
    device.SetStepCustomNoteMask(0u, sequencer::NOTE_G);
    device.MarkEditPatternMutated();
    Check(PatternResidency_GetRolePattern(PatternCacheRole::Play)
              ->steps[0].note_ledger[0].notes[0] == play_note,
          "EDIT mutation is isolated when PLAY differs");

    device.SetChainLength(2u);
    device.SetChainPatternAt(0u, 0u);
    device.SetChainPatternAt(1u, 2u);
    device.SetPlaybackSource(sequencer::PlaybackSource::SongChain);
    ServiceUntilIdle(&device);
    roles = PatternResidency_GetRoles();
    Check(roles->next_entry == roles->edit_entry,
          "EDIT equals NEXT aliases an already resident entry");
}

void TestEditWaitDuringNextIo(void)
{
    SequencerDevice device;
    Check(Prepare(&device), "EDIT wait store prepared");
    device.SetChainLength(2u);
    device.SetChainPatternAt(0u, 0u);
    device.SetChainPatternAt(1u, 6u);
    device.SetPlaybackSource(sequencer::PlaybackSource::SongChain);
    device.SetCurrentPatternIndex(7u);
    ServiceUntilIdle(&device);
    Check(device.GetCurrentPatternIndex() == 7u,
          "waiting EDIT request retries after NEXT I/O completes");
}

void TestStableEditHandoff(void)
{
    SequencerDevice device;
    Check(Prepare(&device), "stable EDIT handoff store prepared");
    const int8_t original_edit = PatternResidency_GetRoleEntry(PatternCacheRole::Edit);
    const uint8_t original_step_count = device.GetPatternStepCount();

    device.SetCurrentPatternIndex(8u);
    Check(PatternResidency_GetRoleEntry(PatternCacheRole::Edit) == original_edit,
        "old EDIT remains assigned while replacement loads");
    bool getters_stable = true;
    for (uint8_t pass = 0u; pass < 8u; ++pass)
    {
      getters_stable = getters_stable && device.GetPatternStepCount() == original_step_count;
      (void)device.GetStepNoteMask(0u);
      (void)device.GetPatternArpMode();
      device.ServicePatternResidency();
    }
    Check(getters_stable, "UI-style synchronous getters remain valid during EDIT load");
    ServiceUntilIdle(&device);
    Check(device.GetCurrentPatternIndex() == 8u &&
        PatternResidency_GetRoleEntry(PatternCacheRole::Edit) != original_edit,
        "EDIT reassigns only after validated replacement load");

    const int8_t retained_edit = PatternResidency_GetRoleEntry(PatternCacheRole::Edit);
    device.SetCurrentPatternIndex(9u);
    s_fail_read_once_at = FRAM_PATTERNSTORE_ADDR +
                          (9u * FRAM_PATTERNSTORE_SLOT_STRIDE) + 20u;
    for (uint32_t guard = 0u; guard < 1000u &&
         PatternResidency_GetLastRoleLoadError(PatternCacheRole::Edit) ==
             PATTERN_CACHE_ERROR_NONE; ++guard)
        device.ServicePatternResidency();
    Check(PatternResidency_GetRoleEntry(PatternCacheRole::Edit) == retained_edit &&
        device.GetCurrentPatternIndex() == 8u,
        "failed EDIT load retains previous valid EDIT role");
    Check(PatternResidency_GetLastRoleLoadError(PatternCacheRole::Edit) ==
        PATTERN_CACHE_ERROR_READ,
        "failed EDIT load exposes residency error state");

    device.SetCurrentPatternIndex(10u);
    device.SetCurrentPatternIndex(11u);
    ServiceUntilIdle(&device);
    Check(device.GetCurrentPatternIndex() == 11u,
          "superseded EDIT request becomes the selected index");
    Check(PatternResidency_GetRolePattern(PatternCacheRole::Edit)
              ->steps[0].note_ledger[0].notes[0] == 59u,
          "superseded EDIT load cannot install stale Pattern");
}

void TestBoundaryHoldAndPromotion(void)
{
    SequencerDevice device;
    Check(Prepare(&device), "boundary store prepared");
    device.SetChainLength(2u);
    device.SetChainPatternAt(0u, 0u);
    device.SetChainPatternAt(1u, 3u);
    device.SetPlaybackSource(sequencer::PlaybackSource::SongChain);
    device.Start();

    const sequencer::Pattern* loaded_object = nullptr;
    for (uint8_t entry = 0u; entry < PATTERN_RESIDENCY_CACHE_ENTRY_COUNT; ++entry)
    {
        const PatternCacheEntry* cache = PatternResidency_GetCacheEntry(entry);
        if (cache && cache->requested_pattern_index == 3u) loaded_object = &cache->pattern;
    }
    const uint32_t reads_before = s_read_count;
    const uint32_t writes_before = s_write_count;
    AdvanceToPatternBoundary(&device);
    Check(device.ConsumeOutputSnapshotRequest(),
          "full-step transition requests one output snapshot");
    Check(!device.ConsumeOutputSnapshotRequest(),
          "output snapshot requests coalesce until consumed");
    Check(device.IsPatternBoundaryHeld(), "NEXT-not-ready boundary enters HOLD");
    Check(!device.IsGateActive(), "gate is off during boundary HOLD");
    Check(s_read_count == reads_before && s_write_count == writes_before,
          "boundary performs no FRAM operation");
    Check(device.GetPlayPatternIndex() == 0u,
          "HOLD does not replay or enter successor Pattern");
        const uint32_t pending_before_hold_ticks = device.GetPendingStepEvents();
        QueueOneMusicalStep(&device);
        device.ServicePendingMusicalTicks();
        Check(device.GetPendingStepEvents() == pending_before_hold_ticks,
            "HOLD freezes musical event publication without backlog growth");

    for (uint32_t guard = 0u; guard < 1000u && device.IsPatternBoundaryHeld(); ++guard)
    {
        device.ServicePatternResidency();
    }
    Check(!device.IsPatternBoundaryHeld() && device.GetPlayPatternIndex() == 3u,
          "progression resumes after NEXT validates");
    Check(device.ConsumeOutputSnapshotRequest(),
          "HOLD resume requests refreshed CV and gate snapshot");
    Check(PatternResidency_GetRolePatternConst(PatternCacheRole::Play) == loaded_object,
          "NEXT promotion changes role without copying Pattern payload");
}

constexpr uint32_t kSlotReadsPerLoad =
    1u + (uint32_t)((sizeof(sequencer::Pattern) + PATTERNSTORE_CHUNK_BYTES - 1u) / PATTERNSTORE_CHUNK_BYTES);

bool PrepareColdDevice(SequencerDevice* device)
{
    if (!PrepareStore()) return false;
    memset(s_slot_read_count, 0, sizeof(s_slot_read_count));
    device->Init();
    return device->InitializePatternResidency();
}

uint32_t ServiceUntilEdit(SequencerDevice* device, uint8_t pattern)
{
    uint32_t passes = 0u;
    for (uint32_t guard = 0u; guard < 2000u && device->GetCurrentPatternIndex() != pattern; ++guard)
    {
        device->ServicePatternResidency();
        ++passes;
    }
    return passes;
}

uint32_t ServicePasses(SequencerDevice* device, uint32_t passes)
{
    for (uint32_t i = 0u; i < passes; ++i) device->ServicePatternResidency();
    return passes;
}

void TestColdDirectoryDiscovery(void)
{
    SequencerDevice device;
    Check(PrepareColdDevice(&device), "cold boot initializes PLAY/EDIT without a directory scan");
    s_fram[FRAM_PATTERNSTORE_ADDR + 30u * FRAM_PATTERNSTORE_SLOT_STRIDE + 20u] ^= 0x01u;
    const PatternDirectoryEntry* directory = PatternResidency_GetDirectory();
    uint8_t unknown = 0u;
    for (uint8_t i = 1u; i < sequencer::kPatternCount; ++i)
        if (directory[i].status == PATTERN_DIRECTORY_UNKNOWN) ++unknown;
    Check(directory[0].status == PATTERN_DIRECTORY_VALID && unknown == 31u,
          "cold boot resolves only Pattern 0");
    Check(s_slot_read_count[0] == kSlotReadsPerLoad,
          "cold boot reads Pattern 0 once (no separate directory validation pass)");

    const uint32_t reads_before = s_read_count;
    device.SetCurrentPatternIndex(3u);
    Check(s_read_count - reads_before <= 1u && s_slot_read_count[3] <= 1u,
          "SetCurrentPatternIndex(unknown) performs zero synchronous payload reads");
    Check(device.GetCurrentPatternIndex() == 0u &&
          PatternResidency_GetDirectoryEntry(3u)->status == PATTERN_DIRECTORY_UNKNOWN,
          "EDIT and directory stay unchanged until the async load validates");
    const uint32_t passes = ServiceUntilEdit(&device, 3u);
    Check(device.GetCurrentPatternIndex() == 3u && passes > 1u,
          "EDIT switches only after a load spread over multiple service passes");
    Check(directory[3].status == PATTERN_DIRECTORY_VALID && directory[3].generation == 4u &&
          PatternResidency_GetCacheEntry((uint8_t)PatternResidency_GetRoleEntry(
              PatternCacheRole::Edit))->current_generation == 4u,
          "discovery load preserves the stored generation in directory and cache");
    Check(s_slot_read_count[3] == kSlotReadsPerLoad,
          "unknown Pattern payload is read exactly once");

    uint32_t slot_reads = s_slot_read_count[0];
    device.SetCurrentPatternIndex(0u);
    Check(device.GetCurrentPatternIndex() == 0u && s_slot_read_count[0] == slot_reads,
          "already-resident Pattern switches immediately without FRAM");
    slot_reads = s_slot_read_count[3];
    device.SetCurrentPatternIndex(3u);
    Check(device.GetCurrentPatternIndex() == 3u && s_slot_read_count[3] == slot_reads,
          "already-known resident Pattern switches immediately without FRAM");

    device.SetCurrentPatternIndex(26u);
    device.ServicePatternResidency();
    device.SetCurrentPatternIndex(27u);
    (void)ServiceUntilEdit(&device, 27u);
    Check(device.GetCurrentPatternIndex() == 27u &&
          PatternResidency_GetRolePattern(PatternCacheRole::Edit)
              ->steps[0].note_ledger[0].notes[0] == 48u + 27u,
          "superseded unknown selection installs only the latest Pattern");

    (void)ServicePasses(&device, 400u);
    device.SetCurrentPatternIndex(30u);
    (void)ServicePasses(&device, 400u);
    Check(device.GetCurrentPatternIndex() == 27u &&
          PatternResidency_GetDirectoryEntry(30u)->status == PATTERN_DIRECTORY_INVALID,
          "corrupt Pattern becomes INVALID and never becomes EDIT");

    const int8_t dirty_entry = PatternResidency_GetRoleEntry(PatternCacheRole::Edit);
    device.SetStepCustomNoteMask(0u, sequencer::NOTE_D);
    device.MarkEditPatternMutated();
    Check(PatternResidency_IsDirty((uint8_t)dirty_entry) &&
          PatternResidency_ServiceDirectoryWarm() == 0u,
          "background directory warming is suspended while a Pattern is dirty");
    const sequencer::Pattern dirty_before =
        PatternResidency_GetCacheEntry((uint8_t)dirty_entry)->pattern;
    device.SetCurrentPatternIndex(28u);
    (void)ServiceUntilEdit(&device, 28u);
    const PatternCacheEntry* dirty_cache = PatternResidency_GetCacheEntry((uint8_t)dirty_entry);
    Check(device.GetCurrentPatternIndex() == 28u && dirty_cache->pattern_index == 27u &&
          PatternResidency_IsDirty((uint8_t)dirty_entry) &&
          PatternsEqual(dirty_cache->pattern, dirty_before),
          "dirty resident Pattern is never displaced by a discovery load");
}

void TestColdNextAndChainStart(void)
{
    SequencerDevice device;
    Check(PrepareColdDevice(&device), "cold NEXT store prepared");
    device.SetChainLength(2u);
    device.SetChainPatternAt(0u, 0u);
    device.SetChainPatternAt(1u, 20u);
    device.SetPlaybackSource(sequencer::PlaybackSource::SongChain);
    const uint32_t reads_before_start = s_slot_read_count[20];
    device.Start();
    Check(device.IsPlaying() && s_slot_read_count[20] <= 1u,
          "NEXT preparation with UNKNOWN directory does not read synchronously");
    for (uint32_t guard = 0u; guard < 1000u; ++guard)
    {
        const int8_t next = PatternResidency_GetRoleEntry(PatternCacheRole::Next);
        if (next >= 0 && PatternResidency_GetCacheEntry((uint8_t)next)->pattern_index == 20u) break;
        device.ServicePatternResidency();
    }
    const int8_t next_entry = PatternResidency_GetRoleEntry(PatternCacheRole::Next);
    Check(next_entry >= 0 &&
          PatternResidency_GetCacheEntry((uint8_t)next_entry)->pattern_index == 20u &&
          PatternResidency_GetDirectoryEntry(20u)->generation == 21u,
          "unknown NEXT is discovered asynchronously and prefetched with its generation");
    Check(s_slot_read_count[20] - reads_before_start == kSlotReadsPerLoad,
          "NEXT discovery and prefetch share one Pattern read");

    const uint32_t reads_before = s_read_count;
    const uint32_t writes_before = s_write_count;
    AdvanceToPatternBoundary(&device);
    Check(s_read_count == reads_before && s_write_count == writes_before,
          "Pattern boundary after prefetch performs no FRAM operation");
    Check(!device.IsPatternBoundaryHeld() && device.GetPlayPatternIndex() == 20u,
          "playback transitions to the prefetched Pattern");

    SequencerDevice chain_device;
    Check(PrepareColdDevice(&chain_device), "cold chain start store prepared");
    chain_device.SetChainLength(1u);
    chain_device.SetChainPatternAt(0u, 22u);
    chain_device.SetPlaybackSource(sequencer::PlaybackSource::SongChain);
    chain_device.Start();
    Check(chain_device.IsPlaying() && chain_device.GetPlayPatternIndex() == 22u &&
          PatternResidency_GetDirectoryEntry(22u)->status == PATTERN_DIRECTORY_VALID &&
          s_slot_read_count[22] == kSlotReadsPerLoad,
          "cold SongChain Start loads an unknown PLAY Pattern once");
}

void TestArpOnlyOutputRefresh(void)
{
    SequencerDevice device;
    Check(Prepare(&device), "arp output refresh store prepared");
    device.SetPatternArpMode(sequencer::ArpMode::Up);
    device.SetPatternArpRate(sequencer::ArpRate::ThirtySecond);
    device.Start();
    for (uint8_t tick = 0u; tick < 12u; ++tick) device.NotifyUClockMusicalCallback();
    device.ServicePendingMusicalTicks();
    Check(device.GetPendingStepEvents() == 0u,
          "arp refresh occurs before a full-step event");
    Check(device.ConsumeOutputSnapshotRequest(),
          "arp-only advance requests an output snapshot");
    Check(!device.ConsumeOutputSnapshotRequest(),
          "arp-only output request is consumed once");
}

    void TestProductionHoldOutputCoherency(void)
    {
        Check(PrepareStore(), "output coherency store prepared");
        const sequencer::Pattern pattern_a = MakeOutputPattern(48u, 52u, 0u, 1u);
        const sequencer::Pattern pattern_b = MakeOutputPattern(60u, 64u, 2u, 3u);
        Check(PatternStore_WriteGeneration(0u, &pattern_a, 100u) &&
            PatternStore_WriteGeneration(1u, &pattern_b, 101u),
            "same-pitch-class octave and lane Patterns committed");

        SequencerDevice device;
        Check(InitializeDevice(&device), "output coherency device initialized");
        device.SetChainLength(2u);
        device.SetChainPatternAt(0u, 0u);
        device.SetChainPatternAt(1u, 1u);
        device.SetPlaybackSource(sequencer::PlaybackSource::SongChain);
        device.Start();

        SequencerOutputSnapshotState state;
        OutputRecorder recorder;
        recorder.device = &device;
        Check(ServiceOutput(&device, &state, &recorder) &&
            recorder.values[0] == 48u && recorder.values[1] == (uint16_t)(52u | 0x0100u),
            "production snapshot writes Pattern A absolute notes and lanes");
        Check(device.IsGateActive(), "Pattern A gate activates only after initial snapshot");

        AdvanceToPatternBoundary(&device);
        Check(device.IsPatternBoundaryHeld() && !device.IsGateActive(),
            "HOLD keeps gate inactive while successor is unavailable");
        for (uint32_t guard = 0u; guard < 1000u && device.IsPatternBoundaryHeld(); ++guard)
          device.ServicePatternResidency();

        bool forced = false;
        Check(!device.IsGateActive() && device.IsGateActivationPending(),
            "successor gate remains pending before HOLD snapshot");
        Check(ServiceOutput(&device, &state, &recorder, &forced) && forced,
            "HOLD resume uses forced production snapshot semantics");
        Check(recorder.values[0] == 900u && recorder.values[1] == 901u &&
            recorder.values[2] == (uint16_t)(60u | 0x0200u) &&
            recorder.values[3] == (uint16_t)(64u | 0x0300u),
            "forced HOLD snapshot updates octave, exact notes, and lane assignment");
        Check(!recorder.gate_active_during_write && device.IsGateActive(),
            "HOLD snapshot writes successor CV before activating successor gate");

        const uint32_t writes_before = recorder.write_count;
        device.CommitOutputSnapshot();
        const uint16_t zero_codes[4] = {900u, 901u, 902u, 903u};
        const SequencerOutputSnapshotOps ops = {&recorder, RecordPitchCode, RecordOutput};
        Check(!SequencerOutput_WriteSnapshot(device, state, ops, zero_codes, false, false) &&
            recorder.write_count == writes_before,
            "genuinely unchanged ordinary output remains safely deduplicated");
    }

    void TestProductionArpAndStepOrdering(void)
    {
        Check(PrepareStore(), "arp and normal-step output store prepared");
        sequencer::Pattern pattern = MakeOutputPattern(48u, 52u, 0u, 1u);
        pattern.arp_mode = sequencer::ArpMode::Up;
        pattern.arp_rate = sequencer::ArpRate::ThirtySecond;
        Check(PatternStore_WriteGeneration(0u, &pattern, 100u), "arp output Pattern committed");

        SequencerDevice arp_device;
        Check(InitializeDevice(&arp_device), "arp output device initialized");
        arp_device.Start();
        SequencerOutputSnapshotState arp_state;
        OutputRecorder arp_recorder;
        arp_recorder.device = &arp_device;
        Check(ServiceOutput(&arp_device, &arp_state, &arp_recorder) && arp_device.IsGateActive(),
            "initial arp CV is established before gate activation");

        for (uint8_t tick = 0u; tick < 12u; ++tick) arp_device.NotifyUClockMusicalCallback();
        arp_device.ServicePendingMusicalTicks();
        Check(!arp_device.IsGateActive() &&
            ServiceOutput(&arp_device, &arp_state, &arp_recorder) &&
            arp_recorder.values[0] == 52u && !arp_recorder.gate_active_during_write,
            "arp-only absolute note reaches DAC while retrigger gate is low");
        arp_device.Tick1ms();
        Check(arp_device.IsGateActivationPending() && !arp_device.IsGateActive(),
            "arp retrigger remains pending after its low-gap timer");
        Check(!ServiceOutput(&arp_device, &arp_state, &arp_recorder) && arp_device.IsGateActive(),
            "arp gate activates only after the established CV snapshot is confirmed");

        Check(PrepareStore(), "normal-step output store prepared");
        pattern = MakeOutputPattern(48u, sequencer::kMidiNoteNone, 0u, 1u);
        pattern.steps[0].note_ledger[8].notes[0] = 60u;
        Check(PatternStore_WriteGeneration(0u, &pattern, 100u), "normal-step output Pattern committed");
        SequencerDevice step_device;
        Check(InitializeDevice(&step_device), "normal-step output device initialized");
        step_device.Start();
        SequencerOutputSnapshotState step_state;
        OutputRecorder step_recorder;
        step_recorder.device = &step_device;
        Check(ServiceOutput(&step_device, &step_state, &step_recorder) && step_device.IsGateActive(),
            "normal-step initial CV precedes gate activation");
        QueueOneMusicalStep(&step_device);
        step_device.ServicePendingMusicalTicks();
        (void)step_device.ServiceOnePendingStep();
        Check(!step_device.IsGateActive() &&
            ServiceOutput(&step_device, &step_state, &step_recorder) &&
            step_recorder.values[0] == 60u && !step_recorder.gate_active_during_write,
            "normal pitch change writes CV before retrigger gate");
    }

void TestStaleNextAndOneShot(void)
{
    SequencerDevice device;
    Check(Prepare(&device), "stale NEXT store prepared");
    device.SetChainLength(2u);
    device.SetChainPatternAt(0u, 0u);
    device.SetChainPatternAt(1u, 4u);
    device.SetPlaybackSource(sequencer::PlaybackSource::SongChain);
    device.Start();
    device.SetChainPatternAt(1u, 5u);
    ServiceUntilIdle(&device);
    const int8_t next_entry = PatternResidency_GetRoleEntry(PatternCacheRole::Next);
    const PatternCacheEntry* next = next_entry >= 0 ?
        PatternResidency_GetCacheEntry((uint8_t)next_entry) : nullptr;
    Check(next && next->pattern_index == 5u,
          "chain change rejects stale NEXT and prepares current successor");

    SequencerDevice one_shot;
    Check(Prepare(&one_shot), "OneShot store prepared");
    PatternResidency_GetRolePattern(PatternCacheRole::Edit)->playback_mode =
        sequencer::PlaybackMode::OneShot;
    one_shot.MarkEditPatternMutated();
    one_shot.Start();
    AdvanceToPatternBoundary(&one_shot);
    Check(!one_shot.IsPlaying(), "OneShot termination semantics are preserved");
}

void TestSkippedTraversalAndRepeat(void)
{
    SequencerDevice device;
    Check(PrepareStore(), "skipped traversal store prepared");
    sequencer::Pattern skipped = MakePattern(1u);
    for (uint8_t step = 0u; step < skipped.step_count; ++step)
      skipped.steps[step].bar_state = sequencer::BarState::Skip;
    skipped.repeat_count = 3u;
    Check(PatternStore_WriteGeneration(1u, &skipped, 40u),
        "all-skipped repeated Pattern committed");
        Check(InitializeDevice(&device), "directory rebuilt with skipped Pattern metadata");

    device.SetChainLength(3u);
    device.SetChainPatternAt(0u, 0u);
    device.SetChainPatternAt(1u, 1u);
    device.SetChainPatternAt(2u, 2u);
    device.SetPlaybackSource(sequencer::PlaybackSource::SongChain);
    device.Start();
    ServiceUntilIdle(&device);
    AdvanceToPatternBoundary(&device);
    Check(device.GetPlayPatternIndex() == 2u,
        "NEXT traversal skips unusable Pattern without loading it as PLAY");
    Check(device.GetCompletedLoops() == 4u,
        "skipped Pattern repeat completions are preserved from directory metadata");
}

} // namespace

extern "C" void Bridge_PersistBegin(void) { ++s_timing_shim.depth; }
extern "C" void Bridge_PersistEnd(void)
{
    if (s_timing_shim.depth == 0u || --s_timing_shim.depth != 0u) return;
    if (s_timing_shim.pattern_pending)
    {
        s_timing_shim.pattern_pending = 0u;
        ShimPersistPattern();
    }
    if (s_timing_shim.song_pending)
    {
        s_timing_shim.song_pending = 0u;
        ShimPersistSong();
    }
}
extern "C" uint8_t Bridge_GetPatternStepCount(void) { return s_timing_shim.device->GetPatternStepCount(); }
extern "C" uint8_t Bridge_GetPatternStepDivision(void) { return s_timing_shim.device->GetPatternStepDivision(); }
extern "C" uint8_t Bridge_GetTimeSigNumerator(void) { return s_timing_shim.device->GetTimeSigNumerator(); }
extern "C" uint8_t Bridge_GetTimeSigDenominator(void) { return s_timing_shim.device->GetTimeSigDenominator(); }
extern "C" uint8_t Bridge_GetSwing(void) { return s_timing_shim.device->GetSwing(); }
extern "C" void Bridge_SetPatternStepCount(uint8_t step_count)
{
    ++s_timing_shim.set_step_count_calls;
    s_timing_shim.device->SetPatternStepCount(step_count);
    ShimPersistPattern();
}
extern "C" uint8_t Bridge_SetPatternStepDivision(uint8_t step_division)
{
    ++s_timing_shim.set_division_calls;
    const uint8_t before = s_timing_shim.device->GetPatternStepDivision();
    if (!s_timing_shim.device->SetPatternStepDivision(step_division)) return 0u;
    if (s_timing_shim.device->GetPatternStepDivision() != before) ShimPersistPattern();
    return 1u;
}
extern "C" uint8_t Bridge_SetPatternTiming(uint8_t step_division, uint8_t numerator, uint8_t denominator)
{
    ++s_timing_shim.set_timing_calls;
    if (!s_timing_shim.device->SetPatternTiming(step_division, numerator, denominator)) return 0u;
    ShimPersistPattern();
    ShimPersistSong();
    return 1u;
}
extern "C" void Bridge_SetSwing(uint8_t swing)
{
    ++s_timing_shim.set_swing_calls;
    s_timing_shim.device->SetSwing(swing);
    ShimPersistSong();
}

extern "C" uint8_t PatternStoreBackend_Read(uint32_t address, uint8_t* dst, uint16_t len)
{
    if (!dst || address + len > s_fram.size()) return 0u;
    if (address == s_fail_read_once_at)
    {
        s_fail_read_once_at = 0xFFFFFFFFu;
        return 0u;
    }
    ++s_read_count;
    if (address >= FRAM_PATTERNSTORE_ADDR &&
        address < FRAM_PATTERNSTORE_ADDR + sequencer::kPatternCount * FRAM_PATTERNSTORE_SLOT_STRIDE)
        ++s_slot_read_count[(address - FRAM_PATTERNSTORE_ADDR) / FRAM_PATTERNSTORE_SLOT_STRIDE];
    memcpy(dst, s_fram.data() + address, len);
    return 1u;
}

extern "C" uint8_t PatternStoreBackend_Write(uint32_t address, const uint8_t* src, uint16_t len)
{
    if (!src || address + len > s_fram.size()) return 0u;
    ++s_write_count;
    memcpy(s_fram.data() + address, src, len);
    return 1u;
}

int main()
{
    TestTimingDraftScoping();
    TestPatternLoopPasteExtension();
    TestStage1BarClipboard();
    TestStage2BarPaste();
    TestOwnershipAndLiveEditing();
    TestEditIsolationAndAliasing();
    TestEditWaitDuringNextIo();
    TestStableEditHandoff();
    TestBoundaryHoldAndPromotion();
    TestArpOnlyOutputRefresh();
    TestProductionHoldOutputCoherency();
    TestProductionArpAndStepOrdering();
    TestStaleNextAndOneShot();
    TestSkippedTraversalAndRepeat();
    TestColdDirectoryDiscovery();
    TestColdNextAndChainStart();
    std::printf("%s: %d failure(s)\n", s_failures == 0 ? "PASS" : "FAIL", s_failures);
    return s_failures == 0 ? 0 : 1;
}
