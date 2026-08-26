#ifndef S12_FONTS_H
#define S12_FONTS_H

#include <stdint.h>

typedef struct {
    uint8_t        width;
    uint8_t        height;
    uint8_t        first;
    uint8_t        last;
    const uint8_t *data;
} Font_t;

extern const Font_t Font8x12;
extern const Font_t Font16x24;
extern const Font_t Font12x20;

#define SYM_PLAY        ((char)127)
#define SYM_STOP        ((char)128)
#define SYM_RECORD      ((char)129)
#define SYM_NOTE_QTR    ((char)130)
#define SYM_NOTE_8TH    ((char)131)
#define SYM_NOTE_BEAM   ((char)132)
#define SYM_SHARP       ((char)133)
#define SYM_FLAT        ((char)134)
#define SYM_STEP_EMPTY  ((char)135)
#define SYM_STEP_FILLED ((char)136)
#define SYM_ARP_UP      ((char)137)
#define SYM_ARP_DOWN    ((char)138)
#define SYM_ARP_UPDOWN  ((char)139)
#define SYM_ARP_RANDOM  ((char)140)
#define SYM_DIV_4       ((char)141)
#define SYM_DIV_8       ((char)142)
#define SYM_DIV_16      ((char)143)
#define SYM_DIV_32      ((char)144)
#define SYM_MULT_2      ((char)145)
#define SYM_MULT_4      ((char)146)
#define SYM_HALF        ((char)147)
#define SYM_QUARTER     ((char)148)
#define SYM_ACTIVE      ((char)149)
#define SYM_INACTIVE    ((char)150)
#define SYM_CHAIN       ((char)151)
#define SYM_SEPARATOR   ((char)152)
#define SYM_BLOCK       ((char)153)
#define SYM_TICK        ((char)154)
#define SYM_CROSS       ((char)155)
#define SYM_LOOP        ((char)156)
#define SYM_ADD         ((char)157)
#define SYM_REMOVE      ((char)158)
#define SYM_INFINITE    ((char)159)

#define SYM16_PLAY      ((char)91)
#define SYM16_STOP      ((char)92)

#endif // S12_FONTS_H