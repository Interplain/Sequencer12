#include "sequencer_bridge.h"
#include "devices/sequencer/sequencer_device.h"
#include "devices/sequencer/chords/chord_library.h"
#include "platform/fram/mb85rc256.h"
#include "platform/fram/fram_layout.h"
#include "platform/fram/fram_settings.h"
#include "platform/dac8564/dac8564.h"
#include "uClock.h"
#include <cstdio>
#include <cstring>

static SequencerDevice g_sequencer;
static uint8_t s_persist_depth = 0u;
static uint8_t s_persist_dirty = 0u;
static uint8_t s_tick_enabled = 1u;
static uint8_t s_last_substep_index = 0xFFu;

extern "C" void uClock_PC1_ClockOut_Toggle(uint32_t tick);

enum CvRouterMode : uint8_t
{
    kCvRouterSplit  = 0u,
    kCvRouterUnison = 1u,
};

namespace {

static constexpr uint8_t kSongMagic[4] = { 0x53, 0x31, 0x32, 0x53 }; // "S12S"
static constexpr uint32_t kSongBlobVersion = 1u;
static constexpr float kCvPitchBaseVolts = 1.0f;
static constexpr float kVoltsPerSemitone = (1.0f / 12.0f);
/* Logical lanes 0..3 correspond to CV1..CV4 in UI/gate routing. */
static constexpr Dac8564Channel kLaneToDac[4] = {
    DAC8564_CH_A, /* lane 0 (CV1 logical) */
    DAC8564_CH_B, /* lane 1 (CV2 logical) */
    DAC8564_CH_C, /* lane 2 (CV3 logical) */
    DAC8564_CH_D  /* lane 3 (CV4 logical) */
};
static uint32_t s_last_step_index = 0xFFFFFFFFu;
static uint16_t s_last_step_mask = 0xFFFFu;
static uint8_t s_last_arp_note = 0xFFu;
static bool s_cv_init_done = false;
static uint16_t s_cv_zero_code[4] = {40363u, 40451u, 40330u, 40221u};
static uint8_t s_cv_router_mode = kCvRouterSplit;
static volatile uint8_t s_gate_channel_mask = 0u;
static uint8_t s_gate_hold_active = 0u;
static uint8_t s_gate_step_pulsed = 0u;
static uint8_t s_gate_block_ticks = 0u;
static uint32_t s_gate_prev_step = 0xFFFFFFFFu;
static uint32_t s_gate_prev_loops = 0xFFFFFFFFu;
static uint8_t s_gate_prev_substep = 0xFFu;

static void Bridge_UClockMusicalCallback(uint32_t tick)
{
    (void)tick;
    g_sequencer.NotifyUClockMusicalCallback();
}

typedef struct
{
    uint8_t  magic[4];
    uint32_t version;
    uint32_t payload_size;
    uint32_t checksum;
} SongBlobHeader;

static uint32_t SongChecksum(const uint8_t* data, uint32_t len)
{
    uint32_t h = 2166136261u;
    for (uint32_t i = 0; i < len; ++i)
    {
        h ^= data[i];
        h *= 16777619u;
    }
    return h;
}

static uint8_t SaveSongToFram(void)
{
    sequencer::Song song{};
    g_sequencer.ExportSong(&song);

    SongBlobHeader hdr{};
    memcpy(hdr.magic, kSongMagic, sizeof(kSongMagic));
    hdr.version = kSongBlobVersion;
    hdr.payload_size = (uint32_t)sizeof(song);
    hdr.checksum = SongChecksum(reinterpret_cast<const uint8_t*>(&song), hdr.payload_size);

    const uint16_t hdr_size = (uint16_t)sizeof(SongBlobHeader);
    const uint16_t payload_size = (uint16_t)sizeof(song);
    if ((uint32_t)hdr_size + (uint32_t)payload_size > FRAM_SONGDATA_SIZE)
    {
        return 0u;
    }

    if (!MB85RC256_WriteAndVerify(FRAM_SONGDATA_ADDR, reinterpret_cast<const uint8_t*>(&hdr), hdr_size))
    {
        return 0u;
    }
    if (!MB85RC256_WriteAndVerify((uint16_t)(FRAM_SONGDATA_ADDR + hdr_size),
                                  reinterpret_cast<const uint8_t*>(&song),
                                  payload_size))
    {
        return 0u;
    }
    return 1u;
}

static uint8_t __attribute__((unused)) LoadSongFromFram(void)
{
    SongBlobHeader hdr{};
    if (!MB85RC256_IsReady()) return 0u;
    if (!MB85RC256_Read(FRAM_SONGDATA_ADDR, reinterpret_cast<uint8_t*>(&hdr), (uint16_t)sizeof(hdr))) return 0u;

    if (memcmp(hdr.magic, kSongMagic, sizeof(kSongMagic)) != 0) return 0u;
    if (hdr.version != kSongBlobVersion) return 0u;
    if (hdr.payload_size != sizeof(sequencer::Song)) return 0u;
    if ((uint32_t)sizeof(SongBlobHeader) + hdr.payload_size > FRAM_SONGDATA_SIZE) return 0u;

    sequencer::Song song{};
    if (!MB85RC256_Read((uint16_t)(FRAM_SONGDATA_ADDR + (uint16_t)sizeof(SongBlobHeader)),
                        reinterpret_cast<uint8_t*>(&song),
                        (uint16_t)sizeof(song)))
    {
        return 0u;
    }

    const uint32_t checksum = SongChecksum(reinterpret_cast<const uint8_t*>(&song), (uint32_t)sizeof(song));
    if (checksum != hdr.checksum) return 0u;

    g_sequencer.ImportSong(song);
    return 1u;
}

static void PersistSong(void)
{
    if (s_persist_depth > 0u)
    {
        s_persist_dirty = 1u;
        return;
    }
    (void)SaveSongToFram();
}

static void LoadCvRouterModeSetting(void)
{
    FramSettingsBlob settings{};
    if (!FramSettings_Read(&settings) || settings.payload_size < 1u)
    {
        s_cv_router_mode = kCvRouterSplit;
        return;
    }

    const uint8_t mode = settings.reserved[0];
    s_cv_router_mode = (mode == kCvRouterUnison) ? kCvRouterUnison : kCvRouterSplit;
}

static void SaveCvRouterModeSetting(void)
{
    FramSettingsBlob settings{};
    if (!FramSettings_Read(&settings))
    {
        FramSettings_Default(&settings);
    }

    settings.payload_size = 1u;
    settings.reserved[0] = s_cv_router_mode;
    settings.checksum = FramSettings_CalcChecksum(&settings);
    (void)FramSettings_Write(&settings);
}

static float NoteToPitchVolts(uint8_t note)
{
    if (note > 11u) note = 11u;
    return kCvPitchBaseVolts + ((float)note * kVoltsPerSemitone);
}

static uint16_t ComputeZeroCodeFromCalibration(Dac8564Channel channel, uint16_t fallback_code)
{
    uint16_t code_vlow = 0u;
    uint16_t code_vhigh = 0u;
    DAC8564_GetPitchCalibrationForChannel(channel, &code_vlow, &code_vhigh);
    if (code_vlow == code_vhigh)
    {
        return fallback_code;
    }

    /* Wizard low-point is 0V; use it directly for idle zero output. */
    return code_vlow;
}

static void Bridge_WriteLogicalLanes(const uint16_t logical_out[4])
{
    uint16_t dac_out[4] = {0u, 0u, 0u, 0u};
    for (uint8_t lane = 0u; lane < 4u; ++lane)
    {
        const uint8_t dac_index = (uint8_t)kLaneToDac[lane];
        dac_out[dac_index] = logical_out[lane];
    }
    DAC8564_SetAllRaw(dac_out[0], dac_out[1], dac_out[2], dac_out[3]);
}

static void RefreshCvZeroCodes(void)
{
    static const uint16_t kFallback[4] = {40363u, 40451u, 40330u, 40221u};
    for (uint8_t lane = 0u; lane < 4u; ++lane)
    {
        s_cv_zero_code[lane] = ComputeZeroCodeFromCalibration(kLaneToDac[lane], kFallback[lane]);
    }
}

} // namespace

extern "C"
{
    void     Bridge_Init(void)
    {
        LoadCvRouterModeSetting();
        /* For current firmware behavior, enforce strict per-channel split routing. */
        if (s_cv_router_mode != kCvRouterSplit)
        {
            s_cv_router_mode = kCvRouterSplit;
            SaveCvRouterModeSetting();
        }
      g_sequencer.Init();

#if defined(S12_USE_UCLOCK_MUSICAL_STEP_CLOCK) && S12_USE_UCLOCK_MUSICAL_STEP_CLOCK
    /*
     * uClock is the musical transport clock.
     * Register the 96 PPQN callback before initialising the hardware timer.
     */
    uClock.setOnSync(
    umodular::clock::uClockClass::PPQN_96,
    Bridge_UClockMusicalCallback);
    uClock.setOnSync(
    umodular::clock::uClockClass::PPQN_24,
    uClock_PC1_ClockOut_Toggle);
    uClock.init();
    uClock.setTempo(120.0f);
    uClock.start();
#endif
        /* Keep runtime step state volatile: do not restore song step data on boot. */

        Bridge_ApplyZeroOutputCodes();
    }
    void     Bridge_ApplyZeroOutputCodes(void)
    {
        RefreshCvZeroCodes();
        Bridge_WriteLogicalLanes(s_cv_zero_code);
        s_cv_init_done = false;
        s_last_step_index = 0xFFFFFFFFu;
        s_last_step_mask = 0xFFFFu;
        s_last_arp_note = 0xFFu;
        s_last_substep_index = 0xFFu;
        s_gate_channel_mask = 0u;
        s_gate_hold_active = 0u;
        s_gate_step_pulsed = 0u;
        s_gate_block_ticks = 0u;
        s_gate_prev_step = 0xFFFFFFFFu;
        s_gate_prev_loops = 0xFFFFFFFFu;
        s_gate_prev_substep = 0xFFu;
    }
    void     Bridge_SetTickEnabled(uint8_t enabled)
    {
        s_tick_enabled = enabled ? 1u : 0u;
    }
    void     Bridge_SetCvRouterMode(uint8_t mode)
    {
        (void)mode;
        const uint8_t normalized = kCvRouterSplit;
        if (normalized == s_cv_router_mode)
        {
            return;
        }

        s_cv_router_mode = normalized;
        SaveCvRouterModeSetting();

        /* Force immediate recompute on next process pass. */
        s_last_step_index = 0xFFFFFFFFu;
        s_last_step_mask = 0xFFFFu;
        s_last_arp_note = 0xFFu;
        s_last_substep_index = 0xFFu;
    }
    uint8_t  Bridge_GetCvRouterMode(void)
    {
        return s_cv_router_mode;
    }
    void     Bridge_Tick1ms(void)
    {
        if (!s_tick_enabled)
        {
            return;
        }

        g_sequencer.Tick1ms();

        const uint32_t step_index = g_sequencer.GetCurrentStep();
        const uint32_t loops = g_sequencer.GetCompletedLoops();
        const uint8_t substep = g_sequencer.GetCurrentStepSubIndex();
        const bool position_changed =
            step_index != s_gate_prev_step ||
            loops != s_gate_prev_loops ||
            substep != s_gate_prev_substep;
        if (position_changed)
        {
            s_gate_prev_step = step_index;
            s_gate_prev_loops = loops;
            s_gate_prev_substep = substep;
            s_gate_step_pulsed = 0u;
            s_gate_hold_active = 0u;
            s_gate_block_ticks = 1u;
        }
        else if (s_gate_block_ticks > 0u)
        {
            --s_gate_block_ticks;
        }

        const bool gate_window_open = (s_gate_block_ticks == 0u) &&
                          g_sequencer.IsGateActive();
        uint8_t gate_mask = 0u;
        if (!gate_window_open)
        {
            s_gate_hold_active = 0u;
        }
        else
        {
            if (s_gate_hold_active)
            {
                gate_mask = s_gate_channel_mask;
            }
            else if (!s_gate_step_pulsed)
            {
                gate_mask = s_gate_channel_mask;
                s_gate_hold_active = 1u;
                s_gate_step_pulsed = 1u;
            }
        }

        uint32_t bsrr = 0u;
        bsrr |= (gate_mask & 0x01u) ? GPIO_PIN_5 : ((uint32_t)GPIO_PIN_5 << 16);
        bsrr |= (gate_mask & 0x02u) ? GPIO_PIN_6 : ((uint32_t)GPIO_PIN_6 << 16);
        bsrr |= (gate_mask & 0x04u) ? GPIO_PIN_7 : ((uint32_t)GPIO_PIN_7 << 16);
        bsrr |= (gate_mask & 0x08u) ? GPIO_PIN_8 : ((uint32_t)GPIO_PIN_8 << 16);
        GPIOC->BSRR = bsrr;
    }
    void     Bridge_TickMusical(void)
    {
        if (!s_tick_enabled)
        {
            return;
        }

        if (!g_sequencer.IsPlaying())
        {
            return;
        }

        g_sequencer.TickMusical();
    }

    void Bridge_ServiceMusicalEvents(void)
    {
        if (!g_sequencer.IsPlaying())
        {
            return;
        }

        if (!g_sequencer.ServiceOnePendingStep())
        {
            return;
        }

        Bridge_WriteCurrentStepSnapshot();
    }
    void Bridge_Start(void)
    {
    s_gate_hold_active = 0u;
    s_gate_step_pulsed = 0u;
    s_gate_block_ticks = 0u;
    s_gate_prev_step = 0xFFFFFFFFu;
    s_gate_prev_loops = 0xFFFFFFFFu;
    s_gate_prev_substep = 0xFFu;

    g_sequencer.Start();

    /* Load STEP 1 CV before the musical clock begins advancing. */
    Bridge_WriteCurrentStepSnapshot();

    GPIOC->BSRR = (GPIO_PIN_1 << 16);  /* force Clock OUT low until the first PPQN tick while playing */

    Bridge_Process();
    }

   void Bridge_Stop(void)
    {
    g_sequencer.Stop();
    GPIOC->BSRR = (GPIO_PIN_1 << 16);  /* force Clock OUT low when transport stops */

    s_gate_channel_mask = 0u;
    s_gate_hold_active = 0u;
    s_gate_step_pulsed = 0u;
    s_gate_block_ticks = 0u;
    s_gate_prev_step = 0xFFFFFFFFu;
    s_gate_prev_loops = 0xFFFFFFFFu;
    s_gate_prev_substep = 0xFFu;
    }

    void     Bridge_Reset(void)
    {
        g_sequencer.Reset();
        s_gate_channel_mask = 0u;
        s_gate_hold_active = 0u;
        s_gate_step_pulsed = 0u;
        s_gate_block_ticks = 0u;
        s_gate_prev_step = 0xFFFFFFFFu;
        s_gate_prev_loops = 0xFFFFFFFFu;
        s_gate_prev_substep = 0xFFu;
    }

    void Bridge_SetBpm(uint32_t bpm)
    {
    g_sequencer.SetBpm(bpm);

    #if defined(S12_USE_UCLOCK_MUSICAL_STEP_CLOCK) && S12_USE_UCLOCK_MUSICAL_STEP_CLOCK
    uClock.setTempo((float)bpm);
    #endif
    }

    void     Bridge_PersistBegin(void)
    {
        if (s_persist_depth < 255u) s_persist_depth++;
    }
    void     Bridge_PersistEnd(void)
    {
        if (s_persist_depth == 0u) return;
        s_persist_depth--;
        if (s_persist_depth == 0u && s_persist_dirty)
        {
            s_persist_dirty = 0u;
            (void)SaveSongToFram();
        }
    }
    void     Bridge_SetPatternStepCount(uint8_t step_count) { g_sequencer.SetPatternStepCount(step_count); PersistSong(); }
    uint8_t  Bridge_GetPatternStepCount(void) { return g_sequencer.GetPatternStepCount(); }
    uint8_t  Bridge_SetPatternTiming(uint8_t step_division, uint8_t numerator, uint8_t denominator)
    {
        if (!g_sequencer.SetPatternTiming(step_division, numerator, denominator)) return 0u;
        PersistSong();
        return 1u;
    }
    void     Bridge_SetPatternStepDivision(uint8_t step_division) { g_sequencer.SetPatternStepDivision(step_division); PersistSong(); }
    uint8_t  Bridge_GetPatternStepDivision(void) { return g_sequencer.GetPatternStepDivision(); }
    void     Bridge_SetTimeSignature(uint8_t numerator, uint8_t denominator) { g_sequencer.SetTimeSignature(numerator, denominator); PersistSong(); }
    uint8_t  Bridge_GetTimeSigNumerator(void) { return g_sequencer.GetTimeSigNumerator(); }
    uint8_t  Bridge_GetTimeSigDenominator(void) { return g_sequencer.GetTimeSigDenominator(); }
    void     Bridge_SetSwing(uint8_t swing) { g_sequencer.SetSwing(swing); PersistSong(); }
    uint8_t  Bridge_GetSwing(void) { return g_sequencer.GetSwing(); }
    void     Bridge_SetStepChordParams(uint8_t step_index,
                                       uint8_t root_key,
                                       uint8_t chord_type,
                                       uint8_t arp_pattern,
                                       uint8_t duration,
                                       uint8_t repeat_count)
    {
        g_sequencer.SetStepChordParams(step_index, root_key, chord_type, arp_pattern, duration, repeat_count);
        PersistSong();
    }
    void     Bridge_SetStepCustomNoteMask(uint8_t step_index, uint16_t note_mask)
    {
        g_sequencer.SetStepCustomNoteMask(step_index, note_mask);
        /* User-loaded/custom step masks are intentionally non-persistent. */
    }
    void     Bridge_SetStepCustomUserChord(uint8_t step_index, uint16_t note_mask, const char* name)
    {
        g_sequencer.SetStepCustomUserChord(step_index, note_mask, name);
        /* User-loaded/custom step masks are intentionally non-persistent. */
    }
    void     Bridge_SetStepLedgerLength(uint8_t step_index, uint8_t length)
    {
        g_sequencer.SetStepLedgerLength(step_index, length);
        /* Step-piano ledger edits are runtime-only for now. */
    }
    void     Bridge_SetStepLedgerSlot(uint8_t step_index, uint8_t slot_index, uint16_t note_mask)
    {
        g_sequencer.SetStepLedgerSlot(step_index, slot_index, note_mask);
        /* Step-piano ledger edits are runtime-only for now. */
    }
    uint8_t  Bridge_GetStepLedgerLength(uint8_t step_index)
    {
        return g_sequencer.GetStepLedgerLength(step_index);
    }
    uint16_t Bridge_GetStepLedgerSlot(uint8_t step_index, uint8_t slot_index)
    {
        return g_sequencer.GetStepLedgerSlot(step_index, slot_index);
    }
    void     Bridge_SetPatternRepeatCount(uint8_t repeat_count)
    {
        g_sequencer.SetPatternRepeatCount(repeat_count);
        PersistSong();
    }
    uint8_t  Bridge_GetPatternRepeatCount(void)
    {
        return g_sequencer.GetPatternRepeatCount();
    }
    void     Bridge_SetCurrentPattern(uint8_t pattern_index)
    {
        g_sequencer.SetCurrentPatternIndex(pattern_index);
        PersistSong();
    }
    void     Bridge_SetChainLength(uint8_t length)
    {
        g_sequencer.SetChainLength(length);
        PersistSong();
    }
    uint8_t  Bridge_GetChainLength(void)
    {
        return g_sequencer.GetChainLength();
    }
    void     Bridge_SetChainPatternAt(uint8_t pos, uint8_t pattern_index)
    {
        g_sequencer.SetChainPatternAt(pos, pattern_index);
        PersistSong();
    }
    uint8_t  Bridge_GetChainPatternAt(uint8_t pos)
    {
        return g_sequencer.GetChainPatternAt(pos);
    }
    uint8_t  Bridge_GetChainCurrentPosition(void)
    {
        return g_sequencer.GetChainCurrentPosition();
    }
    uint8_t  Bridge_GetCurrentPatternRepeatProgress(void)
    {
        return g_sequencer.GetCurrentPatternRepeatProgress();
    }
    uint16_t Bridge_GetStepNoteMask(uint8_t step_index)
    {
        return g_sequencer.GetStepNoteMask(step_index);
    }
    uint16_t Bridge_GetCurrentOutputNoteMask(void)
    {
        if (!g_sequencer.IsPlaying())
        {
            return 0u;
        }

        const sequencer::ArpMode arp_mode = g_sequencer.GetCurrentArpMode();
        if (arp_mode == sequencer::ArpMode::Off)
        {
            return g_sequencer.GetCurrentStepNoteMaskForPlayback();
        }

        const uint8_t note = g_sequencer.GetCurrentNote();
        return (note <= 11u) ? (uint16_t)(1u << note) : 0u;
    }
    int16_t Bridge_GetCurrentOutputPrimaryMilliVolts(void)
    {
        const uint16_t note_mask = Bridge_GetCurrentOutputNoteMask();
        if (note_mask == 0u)
        {
            return -1;
        }

        uint8_t note = 0u;
        while (note < 12u)
        {
            if ((note_mask & (uint16_t)(1u << note)) != 0u)
            {
                break;
            }
            note++;
        }
        if (note >= 12u)
        {
            return -1;
        }

        /* 1V base + one twelfth of a volt per semitone, rounded to mV. */
        return (int16_t)(1000 + ((int32_t)note * 1000 + 6) / 12);
    }

    void Bridge_WriteCurrentStepSnapshot(void)
    {
        const bool playing = g_sequencer.IsPlaying();

        if (!playing)
        {
            s_gate_channel_mask = 0u;
            return;
        }

        s_cv_init_done = true;

        const uint32_t step_index = g_sequencer.GetCurrentStep();
        const uint8_t substep = g_sequencer.GetCurrentStepSubIndex();
        const uint16_t note_mask = g_sequencer.GetCurrentStepNoteMaskForPlayback();
        const sequencer::ArpMode arp_mode = g_sequencer.GetCurrentArpMode();

        if (arp_mode == sequencer::ArpMode::Off)
        {
            if (step_index == s_last_step_index &&
            substep == s_last_substep_index &&
            note_mask == s_last_step_mask)
            {
                return;
            }

            uint8_t notes[4] = {0u, 0u, 0u, 0u};
            uint8_t note_count = 0u;

            for (uint8_t note = 0u; note < 12u && note_count < 4u; ++note)
            {
                if ((note_mask & (uint16_t)(1u << note)) != 0u)
                {
                    notes[note_count++] = note;
                }
            }

            uint16_t out[4] = {s_cv_zero_code[0], s_cv_zero_code[1], s_cv_zero_code[2], s_cv_zero_code[3]};
            if (s_cv_router_mode == kCvRouterUnison)
            {
                if (note_count > 0u)
                {
                    const float volts = NoteToPitchVolts(notes[0]);
                    for (uint8_t ch = 0u; ch < 4u; ++ch)
                    {
                        out[ch] = DAC8564_PitchVoltsToCodeForChannel(kLaneToDac[ch], volts);
                    }
                    Bridge_WriteLogicalLanes(out);
                }
                s_gate_channel_mask = (note_count > 0u) ? 0x0Fu : 0u;
            }
            else
            {
                if (note_count > 0u)
                {
                    for (uint8_t ch = 0u; ch < note_count; ++ch)
                    {
                        const float volts = NoteToPitchVolts(notes[ch]);
                        out[ch] = DAC8564_PitchVoltsToCodeForChannel(kLaneToDac[ch], volts);
                    }

                    Bridge_WriteLogicalLanes(out);
                }

                if (note_count >= 4u) s_gate_channel_mask = 0x0Fu;
                else if (note_count == 0u) s_gate_channel_mask = 0u;
                else s_gate_channel_mask = (uint8_t)((1u << note_count) - 1u);
            }
                s_last_step_index = step_index;
                s_last_substep_index = substep;
                s_last_step_mask = note_mask;
                s_last_arp_note = 0xFFu;
        }
        else
        {
            const uint8_t note = g_sequencer.GetCurrentNote();
            if (step_index == s_last_step_index && note == s_last_arp_note)
            {
                return;
            }

            if (note <= 11u)
            {
                const float volts = NoteToPitchVolts(note);
                uint16_t out[4] = {s_cv_zero_code[0], s_cv_zero_code[1], s_cv_zero_code[2], s_cv_zero_code[3]};
                out[0] = DAC8564_PitchVoltsToCodeForChannel(kLaneToDac[0], volts);
                Bridge_WriteLogicalLanes(out);
                s_gate_channel_mask = 0x01u;
            }
            else
            {
                Bridge_WriteLogicalLanes(s_cv_zero_code);
                s_gate_channel_mask = 0u;
            }

            s_last_step_index = step_index;
            s_last_step_mask = note_mask;
            s_last_arp_note = note;
        }
    }

    void Bridge_Process(void)
    {
        g_sequencer.Process();

#if defined(S12_USE_UCLOCK_MUSICAL_STEP_CLOCK) && S12_USE_UCLOCK_MUSICAL_STEP_CLOCK
        /* uClock musical step service writes the snapshot at the top of the loop. */
#else
        Bridge_WriteCurrentStepSnapshot();
#endif
    }

    const char* Bridge_GetStepChordDisplayName(uint8_t step_index, char* buf, uint8_t buf_len)
    {
        if (!buf || buf_len == 0) return "";
        if (g_sequencer.GetStepCustomChordName(step_index, buf, buf_len))
        {
            return buf;
        }
        return Bridge_FindChordName(g_sequencer.GetStepNoteMask(step_index), buf, buf_len);
    }

    const char* Bridge_FindChordName(uint16_t note_mask, char* buf, uint8_t buf_len)
    {
        if (!buf || buf_len == 0) return "";
        const sequencer::ChordPreset* presets = sequencer::ChordLibrary::GetPresets();
        uint32_t count = sequencer::ChordLibrary::GetPresetCount();
        for (uint32_t i = 0; i < count; ++i)
        {
            if (presets[i].note_mask == note_mask)
            {
                const char* root = sequencer::ChordLibrary::GetRootName(presets[i].root);
                const char* type = sequencer::ChordLibrary::GetTypeName(presets[i].type);
                snprintf(buf, buf_len, "%s %s", root, type);
                return buf;
            }
        }
        strncpy(buf, "Custom", buf_len);
        buf[buf_len - 1] = '\0';
        return buf;
    }
    uint8_t Bridge_GetStepChordUiParams(uint8_t step_index,
                                        uint8_t* root_key,
                                        uint8_t* chord_type,
                                        uint8_t* duration,
                                        uint8_t* repeat_count)
    {
        return g_sequencer.GetStepChordUiParams(step_index,
                                                root_key,
                                                chord_type,
                                                duration,
                                                repeat_count) ? 1u : 0u;
    }
    uint32_t Bridge_GetCurrentStep(void)          { return g_sequencer.GetCurrentStep(); }
    uint8_t  Bridge_GetCurrentStepSubIndex(void)  { return g_sequencer.GetCurrentStepSubIndex(); }
    uint8_t  Bridge_IsPlaying(void)               { return g_sequencer.IsPlaying() ? 1u : 0u; }
    uint32_t Bridge_GetElapsedMs(void)            { return g_sequencer.GetElapsedMs(); }


uint8_t Bridge_GetCurrentPattern(void)
{
    return static_cast<uint8_t>(g_sequencer.GetCurrentPatternIndex());
}

uint32_t Bridge_GetRunTimeMs(void)
{
    return g_sequencer.GetRunTimeMs();
}

uint32_t Bridge_GetCompletedLoops(void)
{
    return g_sequencer.GetCompletedLoops();
}

uint32_t Bridge_GetUClockMusicalCallbackCount(void)
{
    return g_sequencer.GetUClockMusicalCallbackCount();
}

uint32_t Bridge_GetTickMusicalCount(void)
{
    return g_sequencer.GetTickMusicalCount();
}

uint32_t Bridge_GetPendingStepEnqueueCount(void)
{
    return g_sequencer.GetPendingStepEnqueueCount();
}

uint32_t Bridge_GetServiceOneStepCount(void)
{
    return g_sequencer.GetServiceOneStepCount();
}
}