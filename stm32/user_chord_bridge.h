#pragma once
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char     name[17];       // 16 chars + null
    uint16_t note_mask;      // 12 bits (C to B)
} UserChordInfo;

// Initialize user chord system
void Bridge_UserChord_Init(void);
void Bridge_UserChord_EnsureLoaded(void);
uint8_t Bridge_UserChord_IsFramEnabled(void);
uint8_t Bridge_UserChord_IsFramReady(void);
uint8_t Bridge_UserChord_WasLoadedFromFram(void);
uint8_t Bridge_UserChord_LastIoOk(void);

// User chord access
uint8_t Bridge_UserChord_GetCount(void);
const UserChordInfo* Bridge_UserChord_Get(uint8_t index);
uint8_t Bridge_UserChord_Save(const char* name, uint16_t note_mask);
uint8_t Bridge_UserChord_Rename(uint8_t index, const char* name);
void Bridge_UserChord_Delete(uint8_t index);

#ifdef __cplusplus
}
#endif
