#include "platform/dac8564/dac8564.h"
#include "stm32/hw/hw_init.h"

/* ── Hardware ────────────────────────────────────────────────────────────
 * PB12 = SYNC (active-low frame sync)
 * PB13 = SCLK, PB15 = MOSI (SPI2, configured in hw_init)
 * PB14 = LDAC — tied LOW in hardware (C20 ground wire); firmware ignores it.
 * A0/A1 grounded on the chip → DB23=DB22=0 in every frame.
 * ──────────────────────────────────────────────────────────────────────── */
#define DAC_SYNC_PORT   GPIOB
#define DAC_SYNC_PIN    GPIO_PIN_12
#define DAC_LDAC_PORT   GPIOB
#define DAC_LDAC_PIN    GPIO_PIN_14

#ifndef DAC_FORCE_LDAC_LOW
#define DAC_FORCE_LDAC_LOW 1u
#endif

/* Single-channel update: DB=[A1 A0 LD1 LD0 0 DACsel1 DACsel0 PD0]
 *   A1=0 A0=0 LD1=0 LD0=1 (single-channel update) PD0=0
 *   → 0x10 | (channel << 1)  →  0x10/0x12/0x14/0x16 for A/B/C/D          */
#define DAC_CMD_UPDATE_CH(ch)  ((uint8_t)(0x10u | (((ch) & 0x03u) << 1)))

/* Calibration interpolation window */
/* Calibration points — MUST match the voltages the wizard asks for in
 * calibration.c. Wizard currently calibrates at -2.00V and +6.00V. */
#define DAC_CAL_VLOW    (-2.0f)
#define DAC_CAL_VHIGH   ( 6.0f)
#define DAC_CAL_SPAN    (DAC_CAL_VHIGH - DAC_CAL_VLOW)

static SPI_HandleTypeDef* s_spi = 0;
static uint16_t s_pitch_code_neg1v[4] = {0u, 0u, 0u, 0u};
static uint16_t s_pitch_code_pos2v[4] = {26214u, 26214u, 26214u, 26214u};

/* diagnostics kept for the debug UI */
static HAL_StatusTypeDef s_last_spi_status = HAL_OK;
static uint32_t s_write_count = 0u;
static uint8_t  s_last_tx0    = 0u;

/* ── One 24-bit write to a single channel ──────────────────────────────── */
static void Dac_WriteChannel(uint8_t ch, uint16_t value)
{
    uint8_t tx[3];

    if (s_spi == 0 || ch > 3u) return;

    tx[0] = DAC_CMD_UPDATE_CH(ch);
    tx[1] = (uint8_t)(value >> 8);
    tx[2] = (uint8_t)(value & 0xFFu);
    s_last_tx0 = tx[0];

    /* SYNC low, clock 24 bits, SYNC high. Update occurs on 24th falling
     * clock per datasheet — LDAC (hardwired low) needs no action. */
    HAL_GPIO_WritePin(DAC_SYNC_PORT, DAC_SYNC_PIN, GPIO_PIN_RESET);
    s_last_spi_status = HAL_SPI_Transmit(s_spi, tx, 3, 10);
    HAL_GPIO_WritePin(DAC_SYNC_PORT, DAC_SYNC_PIN, GPIO_PIN_SET);

    if (s_last_spi_status == HAL_OK) s_write_count++;
}

/* ── Public API ────────────────────────────────────────────────────────── */
void DAC8564_Init(SPI_HandleTypeDef* hspi)
{
    s_spi = hspi;
    HAL_GPIO_WritePin(DAC_SYNC_PORT, DAC_SYNC_PIN, GPIO_PIN_SET); /* SYNC idle high */
#if DAC_FORCE_LDAC_LOW
    HAL_GPIO_WritePin(DAC_LDAC_PORT, DAC_LDAC_PIN, GPIO_PIN_RESET);
#endif
    DAC8564_ClearOutputs();
}

void DAC8564_ClearOutputs(void)
{
    DAC8564_SetAllRaw(0, 0, 0, 0);
}

void DAC8564_SetChannelRaw(Dac8564Channel channel, uint16_t value)
{
    Dac_WriteChannel((uint8_t)channel, value);
}

void DAC8564_SetAllRaw(uint16_t a, uint16_t b, uint16_t c, uint16_t d)
{
    Dac_WriteChannel(0u, a);
    Dac_WriteChannel(1u, b);
    Dac_WriteChannel(2u, c);
    Dac_WriteChannel(3u, d);
}

uint16_t DAC8564_VoltsToCode(float volts, float full_scale_volts)
{
    if (full_scale_volts <= 0.0f) return 0;
    if (volts <= 0.0f) return 0;
    if (volts >= full_scale_volts) return 65535u;
    float codef = (volts / full_scale_volts) * 65535.0f;
    if (codef < 0.0f) codef = 0.0f;
    if (codef > 65535.0f) codef = 65535.0f;
    return (uint16_t)codef;
}

void DAC8564_SetChannelVolts(Dac8564Channel channel, float volts, float full_scale_volts)
{
    DAC8564_SetChannelRaw(channel, DAC8564_VoltsToCode(volts, full_scale_volts));
}

void DAC8564_SetPitchCalibrationForChannel(Dac8564Channel channel, uint16_t code_neg1v, uint16_t code_pos2v)
{
    uint8_t ch = (uint8_t)channel;
    if (ch > 3) return;
    if (code_pos2v == code_neg1v) code_pos2v = (uint16_t)(code_neg1v + 1u);
    s_pitch_code_neg1v[ch] = code_neg1v;
    s_pitch_code_pos2v[ch] = code_pos2v;
}

void DAC8564_GetPitchCalibrationForChannel(Dac8564Channel channel, uint16_t* code_neg1v, uint16_t* code_pos2v)
{
    uint8_t ch = (uint8_t)channel;
    if (ch > 3) return;
    if (code_neg1v) *code_neg1v = s_pitch_code_neg1v[ch];
    if (code_pos2v) *code_pos2v = s_pitch_code_pos2v[ch];
}

uint16_t DAC8564_PitchVoltsToCodeForChannel(Dac8564Channel channel, float volts)
{
    uint8_t ch = (uint8_t)channel;
    if (ch > 3) ch = 0;
    if (volts <= DAC_CAL_VLOW)  return s_pitch_code_neg1v[ch];
    if (volts >= DAC_CAL_VHIGH) return s_pitch_code_pos2v[ch];
    float t = (volts - DAC_CAL_VLOW) / DAC_CAL_SPAN;
    float c = (float)s_pitch_code_neg1v[ch] +
              ((float)((int32_t)s_pitch_code_pos2v[ch] - (int32_t)s_pitch_code_neg1v[ch]) * t);
    if (c < 0.0f) c = 0.0f;
    if (c > 65535.0f) c = 65535.0f;
    return (uint16_t)c;
}

void DAC8564_SetPitchCalibration(uint16_t code_neg1v, uint16_t code_pos2v)
{
    for (uint8_t ch = 0; ch < 4; ch++)
        DAC8564_SetPitchCalibrationForChannel((Dac8564Channel)ch, code_neg1v, code_pos2v);
}

void DAC8564_GetPitchCalibration(uint16_t* code_neg1v, uint16_t* code_pos2v)
{
    DAC8564_GetPitchCalibrationForChannel(DAC8564_CH_A, code_neg1v, code_pos2v);
}

uint16_t DAC8564_PitchVoltsToCode(float volts)
{
    return DAC8564_PitchVoltsToCodeForChannel(DAC8564_CH_A, volts);
}

HAL_StatusTypeDef DAC8564_GetLastSpiStatus(void) { return s_last_spi_status; }
uint8_t  DAC8564_GetLastTx0(void)   { return s_last_tx0; }
uint32_t DAC8564_GetWriteCount(void){ return s_write_count; }