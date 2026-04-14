#ifndef DAC8564_H
#define DAC8564_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "stm32f4xx_hal.h"

typedef enum
{
    DAC8564_CH_A = 0,
    DAC8564_CH_B = 1,
    DAC8564_CH_C = 2,
    DAC8564_CH_D = 3
} Dac8564Channel;

void     DAC8564_Init(SPI_HandleTypeDef* hspi);
void     DAC8564_ClearOutputs(void);
void     DAC8564_SetChannelRaw(Dac8564Channel channel, uint16_t value);
void     DAC8564_SetAllRaw(uint16_t a, uint16_t b, uint16_t c, uint16_t d);
uint16_t DAC8564_VoltsToCode(float volts, float full_scale_volts);
void     DAC8564_SetChannelVolts(Dac8564Channel channel, float volts, float full_scale_volts);
void     DAC8564_SetPitchCalibrationForChannel(Dac8564Channel channel, uint16_t code_neg1v, uint16_t code_pos2v);
void     DAC8564_GetPitchCalibrationForChannel(Dac8564Channel channel, uint16_t* code_neg1v, uint16_t* code_pos2v);
uint16_t DAC8564_PitchVoltsToCodeForChannel(Dac8564Channel channel, float volts);
void     DAC8564_SetPitchCalibration(uint16_t code_neg1v, uint16_t code_pos2v);
void     DAC8564_GetPitchCalibration(uint16_t* code_neg1v, uint16_t* code_pos2v);
uint16_t DAC8564_PitchVoltsToCode(float volts);

#ifdef __cplusplus
}
#endif

#endif
