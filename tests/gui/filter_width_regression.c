#define SDL_MAIN_HANDLED
#undef main
#define main filter_width_emulator_main
#include "../../main.c"
#undef main

/* The display filters on a hi-res picture: one-pixel white columns on blue,
** as a mode 80 text dot is about 1.3 pixels wide. Half width drops every one
** of them; full width keeps them all; the 12.5% scanlines darken odd rows by
** exactly 1/8. Also: the ROM's area-weighted hint and the user's setting
** select full width, and CRT mode 14 is accepted. */

#define CHECK(c, msg) do { if (!(c)) { fprintf(stderr, "FAIL: %s (line %d)\n", msg, __LINE__); return 1; } } while (0)

#define BLUE  0x000080U
#define WHITE 0xFFFFFFU

static uint32 bmp_rgb(uint8 const* bmp, auint w, auint h, auint x, auint y)
{
 auint row = ((w * 3U) + 3U) & ~3U;
 uint8 const* p = bmp + 54U + (h - 1U - y) * row + x * 3U;
 return ((uint32)p[2] << 16) | ((uint32)p[1] << 8) | p[0];
}

static uint32 dim(uint32 rgb)  /* one 12.5% scanline: each channel * 224 / 256 */
{
 return ((((rgb >> 16) & 255U) * 224U >> 8) << 16) |
        ((((rgb >>  8) & 255U) * 224U >> 8) <<  8) |
         (((rgb      ) & 255U) * 224U >> 8);
}

/* Captures the game-only 2x display; counts the white columns of output row y
** and checks every pixel of it is the column pattern or the background. */
static int capture_row(auint y, auint* whites, boole* dimmed_ok, boole dimmed)
{
 uint8* bmp;
 auint bytes, w, h, x;
 *whites = 0U;
 *dimmed_ok = TRUE;
 if (!mainui_capture_screenshot_bmp(&bmp, &bytes, &w, &h)){ return 0; }
 if (w != 620U || h != 456U){ free(bmp); return 0; }
 for (x = 20U; x < 600U; ++x){
  uint32 c = bmp_rgb(bmp, w, h, x, y);
  uint32 white = dimmed ? dim(WHITE) : WHITE;
  uint32 blue  = dimmed ? dim(BLUE)  : BLUE;
  if (c == white){ ++(*whites); }
  else if (c != blue){ *dimmed_ok = FALSE; }
 }
 free(bmp);
 return 1;
}

int main(void)
{
 auint x, y, whites;
 boole clean;
 uint32* raw;
 SDL_setenv("SDL_VIDEODRIVER", "dummy", 1);
 SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
 appcfg_defaults(&main_cfg); main_headless = TRUE;

 /* Policy: the ROM hint and the user's override select full width. */
 CHECK(!guicore_get_full_width(), "full width defaults off");
 main_rom_is_uze = TRUE; main_rom_head.video = VIDEO_AREA_WEIGHTED;
 main_interlace_apply_policy();
 CHECK(guicore_get_full_width(), "area-weighted hint selects full width");
 mainui_set_area_weighted_mode(1U);
 CHECK(!guicore_get_full_width(), "user's off overrides the hint");
 mainui_set_area_weighted_mode(2U);
 main_rom_head.video = 0U; main_interlace_apply_policy();
 CHECK(guicore_get_full_width(), "user's on applies without a hint");
 mainui_set_area_weighted_mode(0U);
 CHECK(!guicore_get_full_width(), "no hint, no override: off");

 /* CRT mode 14 (12.5%) is valid; past it is not. */
 mainui_set_filter_crt_mode(14U);
 CHECK(mainui_get_filter_crt_mode() == 14U, "12.5% scanlines accepted");
 mainui_set_filter_crt_mode(15U);
 CHECK(mainui_get_filter_crt_mode() == 0U, "past the last mode falls back to none");

 CHECK(guicore_init(GUICORE_NOVSYNC | GUICORE_GAMEONLY, "Filter width regression"), "initialize dummy display");
 guicore_set_filter_pre_mode(0U);
 guicore_set_filter_scale_mode(0U);
 guicore_set_filter_crt_mode(FILTER_CRT_SCANLINES_12);
 raw = guicore_getpixbuf();
 /* Odd pixels of each 640-pixel line white: the ones half width never reads. */
 for (y = 0U; y < 280U; ++y){
  for (x = 0U; x < 640U; ++x){
   uint32 rgb = (x & 1U) ? WHITE : BLUE;
   raw[y * 640U + x] = guicore_packrgb(rgb >> 16, (rgb >> 8) & 255U, rgb & 255U);
  }
 }

 guicore_set_full_width(FALSE);
 CHECK(capture_row(200U, &whites, &clean, FALSE), "capture, half width");
 CHECK(clean && whites == 0U, "half width drops every one-pixel column");

 guicore_set_full_width(TRUE);
 CHECK(capture_row(200U, &whites, &clean, FALSE), "capture, full width, bright row");
 CHECK(clean && whites == 290U, "full width keeps every one-pixel column");
 CHECK(capture_row(201U, &whites, &clean, TRUE), "capture, full width, scanline row");
 CHECK(clean && whites == 290U, "12.5% scanline darkens the odd row to 7/8");

 /* A pre-filter alone also goes through the full-width path, lines doubled. */
 guicore_set_filter_crt_mode(FILTER_CRT_NONE);
 guicore_set_filter_pre_mode(FILTER_PRE_BLACK_WHITE);
 {
  uint8* bmp; auint bytes, w, h;
  CHECK(mainui_capture_screenshot_bmp(&bmp, &bytes, &w, &h), "capture, pre-filter only");
  CHECK(w == 620U && h == 456U, "pre-filter only keeps the 2x size");
  CHECK(bmp_rgb(bmp, w, h, 301U, 200U) != bmp_rgb(bmp, w, h, 300U, 200U), "pre-filter only keeps the columns apart");
  CHECK(bmp_rgb(bmp, w, h, 301U, 200U) == bmp_rgb(bmp, w, h, 301U, 201U), "lines are doubled");
  free(bmp);
 }

 guicore_quit();
 puts("PASS: full width keeps one-pixel columns, 12.5% scanlines, hint and override select it");
 return 0;
}
