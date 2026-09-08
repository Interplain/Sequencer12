#include "ui_screensaver.h"
#include "ui_input.h"
#include "st7789.h"
#include "s12_fonts.h"
#include "stm32f4xx_hal.h"

#define SCREEN_W ST7789_WIDTH
#define SCREEN_H ST7789_HEIGHT

#define SCREENSAVER_IDLE_MS      (3u * 60u * 1000u)
#define SCREENSAVER_TICK_MS      150u   /* ~6-7 Hz */
#define SCREENSAVER_STAR_COUNT   48u
#define SCREENSAVER_STARS_PER_TICK 6u
#define SCREENSAVER_TWINKLE_CYCLE 32u   /* frames per full brightness cycle */
#define LOGO_SPEED_PX 2   /* pixels moved per tick along each axis */

#define LOGO_S12_W ((uint16_t)(3u * 16u * 2u))
#define LOGO_S12_H ((uint16_t)(24u * 2u))
#define LOGO_SUB_W ((uint16_t)(9u * 12u))
#define LOGO_SUB_H ((uint16_t)20u)
#define LOGO_GAP   8u
#define LOGO_BLOCK_H ((uint16_t)(LOGO_S12_H + LOGO_GAP + LOGO_SUB_H))
#define LOGO_ERASE_W ((uint16_t)((LOGO_S12_W > LOGO_SUB_W ? LOGO_S12_W : LOGO_SUB_W) + 4u))
#define LOGO_ERASE_H ((uint16_t)(LOGO_BLOCK_H + 4u))

/* All state below is intentionally tiny — no per-star table, no framebuffer.
 * Star positions/brightness are re-derived every tick from a deterministic
 * hash of (star index, frame counter) instead of being stored. */
static uint8_t  s_active = 0;
static uint32_t s_last_tick_ms = 0;
static uint32_t s_activated_at_ms = 0;
static uint32_t s_prev_idle_ms = 0;
static uint16_t s_frame_counter = 0;
static uint8_t  s_next_star = 0;
static int16_t  s_logo_x = 0;
static int16_t  s_logo_y = 0;
static int8_t   s_logo_vx = LOGO_SPEED_PX;
static int8_t   s_logo_vy = LOGO_SPEED_PX;
static uint8_t  s_logo_drawn = 0;

static uint32_t Hash32(uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

static void StarPosition(uint8_t index, uint16_t* out_x, uint16_t* out_y, uint32_t* out_seed)
{
    uint32_t h = Hash32((uint32_t)index * 2654435761u + 0x9E3779B9u);
    *out_seed = h;
    *out_x = (uint16_t)(2u + (h % (uint32_t)(SCREEN_W - 4u)));
    *out_y = (uint16_t)(2u + ((h >> 16) % (uint32_t)(SCREEN_H - 4u)));
}

static uint16_t StarColorForLevel(uint8_t level)
{
    if (level < 3u)  return BLACK;
    if (level < 7u)  return 0x2104u; /* dim blue-gray */
    if (level < 12u) return 0x8410u; /* medium gray */
    return WHITE;
}

static void DrawStar(uint8_t index)
{
    uint16_t x, y;
    uint32_t seed;
    StarPosition(index, &x, &y, &seed);

    /* Always clear the max possible footprint first so a shrinking sparkle
     * never leaves stray bright pixels behind. */
    ST7789_FillRect((uint16_t)(x - 1u), (uint16_t)(y - 1u), 3u, 3u, BLACK);

    const uint8_t phase = (uint8_t)((s_frame_counter + (seed & 0xFFu)) % SCREENSAVER_TWINKLE_CYCLE);
    const uint8_t level = (uint8_t)(phase < (SCREENSAVER_TWINKLE_CYCLE / 2u)
                                     ? phase
                                     : (SCREENSAVER_TWINKLE_CYCLE - 1u - phase));
    const uint16_t color = StarColorForLevel(level);
    if (color == BLACK) return; /* fully twinkled out this cycle */

    const uint8_t sparkle_capable = ((seed % 11u) == 0u);
    if (sparkle_capable && level >= 14u)
    {
        /* Cross-shaped sparkle for a few stars at peak brightness. */
        ST7789_DrawPixel(x, y, color);
        ST7789_DrawPixel((uint16_t)(x - 1u), y, color);
        ST7789_DrawPixel((uint16_t)(x + 1u), y, color);
        ST7789_DrawPixel(x, (uint16_t)(y - 1u), color);
        ST7789_DrawPixel(x, (uint16_t)(y + 1u), color);
    }
    else
    {
        ST7789_DrawPixel(x, y, color);
    }
}

static void DrawLogoAt(int16_t box_x, int16_t box_y)
{
    const uint16_t s12_x = (uint16_t)(box_x + (int16_t)((LOGO_ERASE_W - LOGO_S12_W) / 2u));
    const uint16_t s12_y = (uint16_t)(box_y + 2);
    const uint16_t sub_x = (uint16_t)(box_x + (int16_t)((LOGO_ERASE_W - LOGO_SUB_W) / 2u));
    const uint16_t sub_y = (uint16_t)(box_y + 2 + LOGO_S12_H + LOGO_GAP);

    ST7789_DrawStringScaled(s12_x, s12_y, "S12", &Font16x24, 2u, WHITE, BLACK);
    ST7789_DrawString(sub_x, sub_y, "SEQUENCER", &Font12x20, GRAY, BLACK);
}

static void UpdateLogoBounce(void)
{
    int16_t new_x = (int16_t)(s_logo_x + s_logo_vx);
    int16_t new_y = (int16_t)(s_logo_y + s_logo_vy);
    const int16_t max_x = (int16_t)(SCREEN_W - LOGO_ERASE_W);
    const int16_t max_y = (int16_t)(SCREEN_H - LOGO_ERASE_H);

    if (new_x < 0) { new_x = 0; s_logo_vx = (int8_t)(-s_logo_vx); }
    else if (new_x > max_x) { new_x = max_x; s_logo_vx = (int8_t)(-s_logo_vx); }

    if (new_y < 0) { new_y = 0; s_logo_vy = (int8_t)(-s_logo_vy); }
    else if (new_y > max_y) { new_y = max_y; s_logo_vy = (int8_t)(-s_logo_vy); }

    if (s_logo_drawn)
    {
        ST7789_FillRect((uint16_t)s_logo_x, (uint16_t)s_logo_y, LOGO_ERASE_W, LOGO_ERASE_H, BLACK);
    }

    s_logo_x = new_x;
    s_logo_y = new_y;
    DrawLogoAt(s_logo_x, s_logo_y);
    s_logo_drawn = 1;
}

void UI_Screensaver_Init(void)
{
    s_active = 0;
    s_last_tick_ms = HAL_GetTick();
    s_prev_idle_ms = 0;
    s_frame_counter = 0;
    s_next_star = 0;
    s_logo_drawn = 0;
}

uint8_t UI_Screensaver_IsActive(void)
{
    return s_active;
}

static void Activate(uint32_t now_ms)
{
    s_active = 1;
    s_activated_at_ms = now_ms;
    s_frame_counter = 0;
    s_next_star = 0;
    s_logo_drawn = 0;
    s_logo_x = (int16_t)((SCREEN_W - LOGO_ERASE_W) / 2u);
    s_logo_y = (int16_t)((SCREEN_H - LOGO_ERASE_H) / 2u);
    s_logo_vx = (int8_t)((now_ms & 1u) ? LOGO_SPEED_PX : -LOGO_SPEED_PX);
    s_logo_vy = (int8_t)((now_ms & 2u) ? LOGO_SPEED_PX : -LOGO_SPEED_PX);

    ST7789_Fill_Color(BLACK);
    UpdateLogoBounce();
    UI_Input_ConsumeWakeEvent();
    s_prev_idle_ms = 0u;
}

UiScreensaverResult UI_Screensaver_Tick(void)
{
    const uint32_t now_ms = HAL_GetTick();

    if (!s_active)
    {
        if (UI_Input_GetIdleMs() >= SCREENSAVER_IDLE_MS)
        {
            Activate(now_ms);
            return UI_SCREENSAVER_ACTIVE;
        }
        return UI_SCREENSAVER_INACTIVE;
    }

    /* A genuine new input event resets the idle timer, so idle_ms will drop
     * below what it was on the previous tick; simple elapsed-time growth
     * never does that. This avoids a false wake right after Activate()
     * itself resets the idle timer to ~0. */
    const uint32_t idle_ms = UI_Input_GetIdleMs();
    if (idle_ms < s_prev_idle_ms)
    {
        s_active = 0;
        UI_Input_ConsumeWakeEvent();
        return UI_SCREENSAVER_JUST_WOKE;
    }
    s_prev_idle_ms = idle_ms;

    if ((uint32_t)(now_ms - s_last_tick_ms) < SCREENSAVER_TICK_MS)
    {
        return UI_SCREENSAVER_ACTIVE;
    }
    s_last_tick_ms = now_ms;
    ++s_frame_counter;

    UpdateLogoBounce();

    for (uint8_t i = 0u; i < SCREENSAVER_STARS_PER_TICK; ++i)
    {
        DrawStar(s_next_star);
        s_next_star = (uint8_t)((s_next_star + 1u) % SCREENSAVER_STAR_COUNT);
    }

    return UI_SCREENSAVER_ACTIVE;
}
