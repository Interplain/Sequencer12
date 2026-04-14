#include "platform/dac8564/dac8564.h"
#include "stm32/hw/hw_init.h"

/* DAC control pins from schematic */
#define DAC_CS_PORT    GPIOB
#define DAC_CS_PIN     GPIO_PIN_12
#define DAC_LDAC_PORT  GPIOB
#define DAC_LDAC_PIN   GPIO_PIN_14
#define DAC_CLR_PORT   GPIOC
#define DAC_CLR_PIN    GPIO_PIN_4

/* Command nibble (upper 4 bits of first byte) */
#define DAC8564_CMD_WRITE_UPDATE   0x3u

static SPI_HandleTypeDef* s_spi = 0;
static uint16_t s_pitch_code_neg1v[4] = {0u, 0u, 0u, 0u};
static uint16_t s_pitch_code_pos2v[4] = {26214u, 26214u, 26214u, 26214u}; /* ~2.0V at 5.0V full-scale */

static void DacSelect(void)
{
    HAL_GPIO_WritePin(DAC_CS_PORT, DAC_CS_PIN, GPIO_PIN_RESET);
}

static void DacDeselect(void)
{
    HAL_GPIO_WritePin(DAC_CS_PORT, DAC_CS_PIN, GPIO_PIN_SET);
}

static void DacPulseLdac(void)
{
    HAL_GPIO_WritePin(DAC_LDAC_PORT, DAC_LDAC_PIN, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(DAC_LDAC_PORT, DAC_LDAC_PIN, GPIO_PIN_SET);
}

static void DacWriteFrame(uint8_t cmd, uint8_t addr, uint16_t value)
{
    if (s_spi == 0) return;

    uint8_t tx[3];
    tx[0] = (uint8_t)((cmd << 4) | (addr & 0x0Fu));
    tx[1] = (uint8_t)(value >> 8);
    tx[2] = (uint8_t)(value & 0xFFu);

    DacSelect();
    (void)HAL_SPI_Transmit(s_spi, tx, 3, 10);
    DacDeselect();
}

void DAC8564_Init(SPI_HandleTypeDef* hspi)
{
    s_spi = hspi;

    /* Keep CLR inactive, keep LDAC high for explicit update edges. */
    HAL_GPIO_WritePin(DAC_CLR_PORT, DAC_CLR_PIN, GPIO_PIN_SET);
    HAL_GPIO_WritePin(DAC_LDAC_PORT, DAC_LDAC_PIN, GPIO_PIN_SET);

    DAC8564_ClearOutputs();
}

void DAC8564_ClearOutputs(void)
{
    DAC8564_SetAllRaw(0, 0, 0, 0);
}

void DAC8564_SetChannelRaw(Dac8564Channel channel, uint16_t value)
{
    DacWriteFrame(DAC8564_CMD_WRITE_UPDATE, (uint8_t)channel, value);
    DacPulseLdac();
}

void DAC8564_SetAllRaw(uint16_t a, uint16_t b, uint16_t c, uint16_t d)
{
    DAC8564_SetChannelRaw(DAC8564_CH_A, a);
    DAC8564_SetChannelRaw(DAC8564_CH_B, b);
    DAC8564_SetChannelRaw(DAC8564_CH_C, c);
    DAC8564_SetChannelRaw(DAC8564_CH_D, d);
}

uint16_t DAC8564_VoltsToCode(float volts, float full_scale_volts)
{
    if (full_scale_volts <= 0.0f) return 0;
    if (volts <= 0.0f) return 0;
    if (volts >= full_scale_volts) return 65535u;

    float ratio = volts / full_scale_volts;
    float codef = ratio * 65535.0f;
    if (codef < 0.0f) codef = 0.0f;
    if (codef > 65535.0f) codef = 65535.0f;
    return (uint16_t)codef;
}

void DAC8564_SetChannelVolts(Dac8564Channel channel, float volts, float full_scale_volts)
{
    DAC8564_SetChannelRaw(channel, DAC8564_VoltsToCode(volts, full_scale_volts));
}

void DAC8564_SetPitchCalibration(uint16_t code_neg1v, uint16_t code_pos2v)
{
    DAC8564_SetPitchCalibrationForChannel(DAC8564_CH_A, code_neg1v, code_pos2v);
    DAC8564_SetPitchCalibrationForChannel(DAC8564_CH_B, code_neg1v, code_pos2v);
    DAC8564_SetPitchCalibrationForChannel(DAC8564_CH_C, code_neg1v, code_pos2v);
    DAC8564_SetPitchCalibrationForChannel(DAC8564_CH_D, code_neg1v, code_pos2v);
}

void DAC8564_GetPitchCalibration(uint16_t* code_neg1v, uint16_t* code_pos2v)
{
    DAC8564_GetPitchCalibrationForChannel(DAC8564_CH_A, code_neg1v, code_pos2v);
}

uint16_t DAC8564_PitchVoltsToCode(float volts)
{
    return DAC8564_PitchVoltsToCodeForChannel(DAC8564_CH_A, volts);
}

void DAC8564_SetPitchCalibrationForChannel(Dac8564Channel channel, uint16_t code_neg1v, uint16_t code_pos2v)
{
    uint8_t ch = (uint8_t)channel;
    if (ch > 3) return;

    if (code_pos2v <= code_neg1v)
    {
        code_pos2v = (uint16_t)(code_neg1v + 1u);
    }

    s_pitch_code_neg1v[ch] = code_neg1v;
    s_pitch_code_pos2v[ch] = code_pos2v;
}

void DAC8564_GetPitchCalibrationForChannel(Dac8564Channel channel, uint16_t* code_neg1v, uint16_t* code_pos2v)
{
    uint8_t ch = (uint8_t)channel;
    if (ch > 3) return;

    if (code_neg1v != 0) *code_neg1v = s_pitch_code_neg1v[ch];
    if (code_pos2v != 0) *code_pos2v = s_pitch_code_pos2v[ch];
}

uint16_t DAC8564_PitchVoltsToCodeForChannel(Dac8564Channel channel, float volts)
{
    uint8_t ch = (uint8_t)channel;
    if (ch > 3) ch = 0;

    if (volts <= -1.0f) return s_pitch_code_neg1v[ch];
    if (volts >= 2.0f) return s_pitch_code_pos2v[ch];

    float t = (volts + 1.0f) / 3.0f; /* map [-1, +2] -> [0, 1] */
    float c = (float)s_pitch_code_neg1v[ch] +
              ((float)((int32_t)s_pitch_code_pos2v[ch] - (int32_t)s_pitch_code_neg1v[ch]) * t);

    if (c < 0.0f) c = 0.0f;
    if (c > 65535.0f) c = 65535.0f;
    return (uint16_t)c;
}
