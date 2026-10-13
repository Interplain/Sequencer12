#include "stm32f4xx_hal.h"
#include "st7789.h"
#include "hw_init.h"
#include "mcp23017.h"
#include "platform/dac8564/dac8564.h"
#include "platform/fram/mb85rc1mt.h"
#include "calibration.h"
#include "sequencer_bridge.h"
#include "user_chord_bridge.h"
#include "ui/ui_display.h"
#include "ui/ui_sequencer.h"
#include "ui/ui_input.h"
#include <stdio.h>

#if defined(S12_FRAM_BACKUP_MODE)
#include "platform/fram/mb85rc256.h"
#include "platform/fram/fram_backup_dump.h"
#endif

#if defined(S12_FRAM_NEWCHIP_TEST_MODE)
#include "platform/fram/fram_newchip_test.h"
#endif

#if defined(S12_FRAM_RESTORE_MODE)
#include "platform/fram/fram_restore_migrate.h"
#endif

#if defined(S12_PATTERNSTORE_TEST_MODE)
#include "platform/fram/pattern_store_test.h"
#endif

#include "stm32/timing_probe.h"
#include "stm32/time_commit_probe.h"

#define UI_LOOP_MIN_MS 1
#define STORAGE_FACTORY_HOLD_MS 1500u

#ifndef MCP_MINIMAL_DEBUG
#define MCP_MINIMAL_DEBUG 0
#endif

static volatile uint8_t s_factory_phase = 0u;
static volatile uint8_t s_factory_index = 0u;
static volatile uint8_t s_factory_total = 0u;

static void DrawStorageText(const char* title,
                            const char* line1,
                            const char* line2,
                            const char* line3)
{
    ST7789_Fill_Color(BLACK);
    ST7789_DrawString(8, 16, title, &Font16x24, WHITE, BLACK);
    if (line1) ST7789_DrawString(8, 70, line1, &Font8x12, CYAN, BLACK);
    if (line2) ST7789_DrawString(8, 94, line2, &Font8x12, WHITE, BLACK);
    if (line3) ST7789_DrawString(8, 142, line3, &Font8x12, YELLOW, BLACK);
}

static const char* StoragePhaseName(uint8_t phase)
{
    switch (phase)
    {
        case 1u: return "Formatting marker";
        case 2u: return "Clearing journal";
        case 3u: return "Preparing Patterns";
        case 4u: return "Preparing Song";
        case 5u: return "Initializing marker";
        case 6u: return "Writing Patterns";
        case 7u: return "Writing Song Header";
        case 8u: return "Validating storage";
        case 9u: return "Committing READY";
        case 10u: return "Complete";
        default: return "Storage operation";
    }
}

static void StorageFactoryProgress(uint8_t phase, uint8_t index, uint8_t total)
{
    char progress[40];
    s_factory_phase = phase;
    s_factory_index = index;
    s_factory_total = total;
    if (phase == 6u && total != 0u)
        snprintf(progress, sizeof(progress), "Patterns: %u / %u",
                 (unsigned)(index + 1u), (unsigned)total);
    else if (total != 0u)
        snprintf(progress, sizeof(progress), "%s: %u / %u",
                 StoragePhaseName(phase), (unsigned)(index + 1u), (unsigned)total);
    else
        snprintf(progress, sizeof(progress), "%s", StoragePhaseName(phase));
    DrawStorageText("INITIALISING STORAGE", progress, NULL, "Do not power off");
}

static uint8_t WaitForFactoryConfirmation(void)
{
    uint32_t held_since = 0u;
    while (1)
    {
        UI_Input_Poll();
        if (UI_Input_IsShiftHeld() && UI_Input_IsRecHeld())
        {
            if (held_since == 0u) held_since = HAL_GetTick();
            if ((uint32_t)(HAL_GetTick() - held_since) >= STORAGE_FACTORY_HOLD_MS)
                return 1u;
        }
        else
        {
            held_since = 0u;
        }
        HAL_Delay(4u);
    }
}

static uint8_t EnterStorageRecovery(uint8_t status)
{
    /* PersistenceBootStatus: FACTORY_REQUIRED=9, UNSUPPORTED_SCHEMA=5, SCHEMA_INTEGRITY_FAULT=6. */
    const uint8_t format_problem = (status == 9u || status == 5u || status == 6u) ? 1u : 0u;
    char error_line[40];
    snprintf(error_line, sizeof(error_line), "Storage error: %u", (unsigned)status);
    ST7789_Fill_Color(BLACK);
    ST7789_DrawString(8, 16, "STORAGE ERROR", &Font16x24, WHITE, BLACK);
    ST7789_DrawString(8, 62, format_problem ? "Sequencer storage is not" : "Sequencer storage could",
                      &Font8x12, CYAN, BLACK);
    ST7789_DrawString(8, 78, format_problem ? "in the current S12 format." : "not be recovered safely.",
                      &Font8x12, CYAN, BLACK);
    ST7789_DrawString(8, 110, "Hold SHIFT + REC", &Font8x12, WHITE, BLACK);
    ST7789_DrawString(8, 126, "to initialise storage.", &Font8x12, WHITE, BLACK);
    ST7789_DrawString(8, 142, "This erases Patterns/Song.", &Font8x12, YELLOW, BLACK);
    ST7789_DrawString(8, 158, "Calibration will be kept.", &Font8x12, YELLOW, BLACK);
    ST7789_DrawString(8, 190, error_line, &Font8x12, GRAY, BLACK);

    while (1)
    {
        (void)WaitForFactoryConfirmation();

        status = Bridge_FactoryInitializeStorage(StorageFactoryProgress);
        if (status == 1u)
        {
            DrawStorageText("STORAGE INITIALISED",
                            "Calibration preserved.",
                            "Starting S12...",
                            NULL);
            HAL_Delay(1200u);
            return status;
        }

        snprintf(error_line, sizeof(error_line), "Phase %u index %u error %u",
                 (unsigned)s_factory_phase,
                 (unsigned)s_factory_index,
                 (unsigned)status);
        DrawStorageText("STORAGE FAILED", error_line,
                        "Hold SHIFT + REC to retry.", NULL);
    }
}

int main(void)
{
    HW_Init();
#if defined(S12_TIMING_PROBE_ENABLE)
    TimingProbe_Init();
#endif

#if defined(S12_FRAM_BACKUP_MODE)
    /* Isolated, read-only backup path: read FRAM, compute CRC32, halt.
     * Deliberately returns before any calibration/display/MCP23017/DAC/UI
     * init below, so none of that code (including the calibration future-
     * partition write/restore self-test) ever runs in this build. */
    MB85RC256_Init(&hi2c3);
    FramBackupDump_Run();
    while (1)
    {
        __NOP();
    }
#endif

#if defined(S12_FRAM_NEWCHIP_TEST_MODE)
    /* Isolated, read-only MB85RC1MT probe: read-only on both banks, halt.
     * Returns before calibration/display/MCP23017/DAC/UI init, so nothing
     * else in the boot path can touch FRAM before this probe runs. */
    MB85RC1MT_Init(&hi2c3);
    FramNewchipTest_Run();
    while (1)
    {
        __NOP();
    }
#endif

#if defined(S12_FRAM_RESTORE_MODE)
    /* Isolated MB85RC1MT restore path: writes/verifies only 0x0000-0x7FFF
     * from a buffer pre-loaded by OpenOCD, then halts. Returns before
     * calibration/display/MCP23017/DAC/UI init, so nothing else in the boot
     * path can touch FRAM before this explicit, operator-triggered restore. */
    MB85RC1MT_Init(&hi2c3);
    FramRestoreMigrate_Run();
    while (1)
    {
        __NOP();
    }
#endif

#if defined(S12_PATTERNSTORE_TEST_MODE)
    /* Isolated pattern-store round-trip test: writes/reads/verifies ONE slot
     * in the new 0x8000+ region only, never touches the legacy 0x0000-0x7FFF
     * image. Returns before calibration/display/MCP23017/DAC/UI init. */
    MB85RC1MT_Init(&hi2c3);
    PatternStoreTest_Run();
    while (1)
    {
        __NOP();
    }
#endif

    /* Assert display RST low immediately and hold it throughout all early init.
     * The panel controller stays in hardware reset — its backlight may still
     * power up from VCC, but no image is visible because GRAM is not driven.
     * RST is released only just before ST7789_Init() below. */
    GPIOA->BSRR = (1U << (9 + 16));  /* RST LOW */

#if !MCP_MINIMAL_DEBUG
    /* Latch calibration-entry hold as early as possible in boot. */
    uint8_t enter_cal = Calibration_ShouldEnterOnBoot();
#endif

    /* Init FRAM on dedicated I2C3 bus (PA8=SCL / PC9=SDA) */
#if !MCP_MINIMAL_DEBUG
    MB85RC1MT_Init(&hi2c3);
#endif

    /* Init DAC8564 */
#if !MCP_MINIMAL_DEBUG
    DAC8564_Init(&hspi2);

    /* Apply saved calibration and clamp CVs to true 0V immediately. */
    Calibration_ApplySaved();
    Bridge_InitSafe();
#endif

    /* Release RST and initialise display as late as possible */
    /* A brief low-to-high transition is all the panel needs.
     * The controller was held in reset so there was nothing to display until now. */
    HAL_Delay(20);
    GPIOA->BSRR = (1U << 9);         /* RST HIGH */
    HAL_Delay(120);
    ST7789_Init();

    /* Init MCP23017 after display and other hardware are ready */
    MCP23017_Init(&hi2c1);

    UI_Input_Init();
    ST7789_Fill_Color(BLACK);
    ST7789_DisplayOn();

#if !MCP_MINIMAL_DEBUG
    if (enter_cal)
    {
        ST7789_Fill_Color(BLACK);
        ST7789_DrawStringScaled(10, 48, "DAC CAL", &Font16x24, 1, CYAN, BLACK);
        ST7789_DrawStringScaled(10, 86, "STARTING", &Font16x24, 1, WHITE, BLACK);
        ST7789_DrawStringScaled(10, 116, "CALIBRATION", &Font16x24, 1, WHITE, BLACK);

        Calibration_RunWizard();
    }

    {
        uint8_t storage_ready = Bridge_ActivateStorageRuntime();
        while (!storage_ready)
        {
            (void)EnterStorageRecovery(Bridge_GetPersistenceBootStatus());
            storage_ready = Bridge_ActivateStorageRuntime();
        }
    }

    Bridge_UserChord_Init();

    /* Re-apply zero codes (wizard may have updated calibration). */
    Bridge_ApplyZeroOutputCodes();

    /* UI_Sequencer_Init performs the first full UI draw. */
    UI_Sequencer_Init();

    while (1)
    {
        /* Keep display reset line asserted high to avoid transient blanking. */
        GPIOA->BSRR = (1U << 9);

        uint32_t t0 = HAL_GetTick();
#if defined(S12_TIME_COMMIT_PROBE)
        Bridge_TimeCommitProbeLoopSample();
        uint32_t probe_t = t0;
#endif
        Bridge_ServiceMusicalEvents();
#if defined(S12_TIME_COMMIT_PROBE)
        const uint32_t probe_musical_ms = HAL_GetTick() - probe_t;
        probe_t = HAL_GetTick();
        TimeCommitProbe_LoopMark(LONG_PASS_PHASE_INPUT);
#endif
        UI_Input_Poll();
#if defined(S12_TIME_COMMIT_PROBE)
        const uint32_t probe_input_ms = HAL_GetTick() - probe_t;
        probe_t = HAL_GetTick();
        TimeCommitProbe_LoopMark(LONG_PASS_PHASE_UI);
#endif
        {
            uint32_t _tp_ui = TIMING_PROBE_BEGIN();
            UI_Sequencer_Update();
            TIMING_PROBE_END(g_timing_max_ui_update_us, _tp_ui);
        }
#if defined(S12_TIME_COMMIT_PROBE)
        const uint32_t probe_ui_ms = HAL_GetTick() - probe_t;
        probe_t = HAL_GetTick();
        TimeCommitProbe_LoopMark(LONG_PASS_PHASE_PERSIST);
#endif
        /* After the screen has already redrawn: any deferred FRAM write
         * (V3 save + Pattern Store shadow-write) happens here, not inside
         * the modal-confirm call path itself. */
        {
            uint32_t _tp_persist = TIMING_PROBE_BEGIN();
            Bridge_ServicePersistence();
            TIMING_PROBE_END(g_timing_max_service_persistence_us, _tp_persist);
        }
        TimeCommitProbe_LoopMark(LONG_PASS_PHASE_DISPLAY);
        UI_Sequencer_ServiceDisplay();
        TimeCommitProbe_LoopMark(LONG_PASS_PHASE_OTHER);
#if defined(S12_TIME_COMMIT_PROBE)
        Bridge_TimeCommitProbeLoopPass(HAL_GetTick() - t0,
                                       probe_musical_ms,
                                       probe_input_ms,
                                       probe_ui_ms,
                                       HAL_GetTick() - probe_t);
#endif
#if defined(S12_PATTERNSTORE_VERIFYALL_ENABLE)
        /* Diagnostic/read-only, armed by writing g_ps_verify_trigger=1 over SWD;
         * never runs otherwise, never writes FRAM, never alters SRAM contents. */
        extern volatile uint8_t g_ps_verify_trigger;
        if (g_ps_verify_trigger)
        {
            g_ps_verify_trigger = 0u;
            Bridge_PatternStoreVerifyAll();
        }
#endif
#if defined(S12_PATTERNCACHE_PILOT_ENABLE)
        /* Diagnostic/shadow current+next Pattern cache: at most one small
         * chunked PatternStore read per pass, never affects playback. */
        Bridge_ServicePatternCache();
#endif
        uint32_t elapsed = HAL_GetTick() - t0;
        if (elapsed < UI_LOOP_MIN_MS)
            HAL_Delay(UI_LOOP_MIN_MS - elapsed);
    }
#endif

    return 0;
}