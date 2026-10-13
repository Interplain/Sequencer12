#ifndef UI_SEQUENCER_CLIPBOARD_H
#define UI_SEQUENCER_CLIPBOARD_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void UI_SequencerClipboard_Init(void);
uint8_t UI_SequencerClipboard_IsBarCopyPress(uint8_t in_bar_editor,
                                            uint8_t shifted,
                                            uint8_t matrix_step);
uint8_t UI_SequencerClipboard_IsBarPastePress(uint8_t in_bar_editor,
                                             uint8_t shifted,
                                             uint8_t matrix_step);
uint8_t UI_SequencerClipboard_IsBarValid(void);
uint8_t UI_SequencerClipboard_GetBarSourcePattern(void);
uint8_t UI_SequencerClipboard_GetBarSourceBar(void);

#ifdef __cplusplus
}
#endif

#endif