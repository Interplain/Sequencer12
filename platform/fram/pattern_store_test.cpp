#include "platform/fram/pattern_store_test.h"
#include "platform/fram/pattern_store.h"
#include "devices/sequencer/sequencer_types.h"
#include <cstdio>
#include <cstring>

volatile uint8_t g_patternstore_test_write_ok = 0u;
volatile uint8_t g_patternstore_test_valid_ok = 0u;
volatile uint8_t g_patternstore_test_read_ok = 0u;
volatile uint8_t g_patternstore_test_memcmp_ok = 0u;
volatile uint8_t g_patternstore_test_done = 0u;

static sequencer::Pattern s_known_pattern;
static sequencer::Pattern s_readback_pattern;

static void BuildKnownPattern(sequencer::Pattern& pat)
{
    pat = sequencer::Pattern{};
    pat.tempo_multiplier = 1.5f;
    pat.step_count = 7u;
    pat.step_division = 8u;
    pat.playback_mode = sequencer::PlaybackMode::PingPong;
    pat.repeat_count = 5u;
    pat.arp_mode = sequencer::ArpMode::Random;
    pat.arp_rate = sequencer::ArpRate::Eighth;

    for (uint8_t i = 0u; i < sequencer::kStepCount; ++i)
    {
        sequencer::StepSlot& step = pat.steps[i];
        step.type = sequencer::StepType::Chord;
        step.bar_state = (i % 2u) ? sequencer::BarState::Skip : sequencer::BarState::Active;
        step.duration_multiplier = (uint32_t)(i + 1u);
        step.repeat_count = (uint8_t)(i + 2u);
        step.velocity = (uint8_t)((100u + i) % 128u);
        step.probability = (uint8_t)((50u + i) % 101u);

        (void)sequencer::LedgerSlotAdd(step.note_ledger[0], (uint8_t)(i * 2u));

        for (uint8_t b = 0u; b < sequencer::kStepEventLengthPackedBytes; ++b)
        {
            step.event_length_packed[b] = (uint8_t)(i + b);
        }
        for (uint8_t b = 0u; b < sequencer::kStepLedgerMax; ++b)
        {
            step.lane_slots[b] = (uint8_t)(i ^ b);
        }
        for (uint8_t b = 0u; b < sequencer::kStepLaneOverrideMaskBytes; ++b)
        {
            step.lane_override_mask[b] = (uint8_t)(1u << (i % 8u));
        }

        char name[18];
        snprintf(name, sizeof(name), "T%u", (unsigned)i);
        memset(step.custom_chord_name, 0, sizeof(step.custom_chord_name));
        memcpy(step.custom_chord_name, name, strlen(name));
    }
}

void PatternStoreTest_Run(void)
{
    g_patternstore_test_write_ok = 0u;
    g_patternstore_test_valid_ok = 0u;
    g_patternstore_test_read_ok = 0u;
    g_patternstore_test_memcmp_ok = 0u;
    g_patternstore_test_done = 0u;

    BuildKnownPattern(s_known_pattern);

    g_patternstore_test_write_ok = PatternStore_Write(PATTERNSTORE_TEST_SLOT_INDEX, &s_known_pattern);
    g_patternstore_test_valid_ok = PatternStore_IsValid(PATTERNSTORE_TEST_SLOT_INDEX);

    memset(&s_readback_pattern, 0xAA, sizeof(s_readback_pattern));
    g_patternstore_test_read_ok = PatternStore_Read(PATTERNSTORE_TEST_SLOT_INDEX, &s_readback_pattern);

    g_patternstore_test_memcmp_ok =
        (memcmp(&s_known_pattern, &s_readback_pattern, sizeof(sequencer::Pattern)) == 0) ? 1u : 0u;

    g_patternstore_test_done = 1u;
}
