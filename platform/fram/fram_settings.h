#ifndef FRAM_SETTINGS_H
#define FRAM_SETTINGS_H

#include <stdint.h>
#include "platform/fram/fram_layout.h"

#ifdef __cplusplus
extern "C" {
#endif

void     FramSettings_Default(FramSettingsBlob* blob);
uint32_t FramSettings_CalcChecksum(const FramSettingsBlob* blob);
uint8_t  FramSettings_Read(FramSettingsBlob* out_blob);
uint8_t  FramSettings_Write(const FramSettingsBlob* blob);

#ifdef __cplusplus
}
#endif

#endif // FRAM_SETTINGS_H
