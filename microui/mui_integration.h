#ifndef MUI_INTEGRATION_H
#define MUI_INTEGRATION_H

#include <SDL2/SDL.h>
#include "microui.h"
#include "../types.h"

int   mui_init(void);
void  mui_shutdown(void);
int   mui_handle_event(SDL_Event const* ev);
void  mui_render_overlay(uint32* dest, auint pitch, auint texw, auint texh);
boole mui_is_visible(void);
boole mui_is_open(void);
boole mui_wants_mouse(void);
boole mui_wants_keyboard(void);
void  mui_open_rom_load_window(void);
boole mui_get_style_snapshot(mu_Style* out);
void  mui_apply_style_snapshot(mu_Style const* in);
boole mui_netplay_ui_active(void);
boole mui_tools_ui_active(void);
boole mui_debugger_ui_active(void);
boole mui_gamepad_capture_active(void);
int   mui_textbox_with_vkbd(mu_Context* ctx, char* buf, int bufsz, int opt);
#ifdef ENABLE_DEBUGGER
void  mui_debug_value_watches_reset_defaults(void);
boole mui_debug_value_watch_get(auint idx, boole* out_enable, auint* out_region, char* out_addr, auint out_size);
void  mui_debug_value_watch_set(auint idx, boole enable, auint region, char const* addr);
#endif

#endif
