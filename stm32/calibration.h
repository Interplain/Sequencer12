#ifndef CALIBRATION_H
#define CALIBRATION_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

uint8_t Calibration_ShouldEnterOnBoot(void);
uint8_t Calibration_EnterIfHeldOnBoot(void);
void    Calibration_ApplySaved(void);
void    Calibration_RunWizard(void);

#ifdef __cplusplus
}
#endif

#endif
