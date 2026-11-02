#define SDL_MAIN_HANDLED
#undef main
#define main screenshot_tool_emulator_main
#include "../../main.c"
#undef main
#include "../../microui/mui_integration.c"
#include "../../textgui.c"
#define CHECK(c, msg) do { if (!(c)) { fprintf(stderr, "FAIL: %s (line %d)\n", msg, __LINE__); return 1; } } while (0)
static uint32 canvas[640U * 560U];
static int png_size(char const* path,unsigned w,unsigned h)
{
 unsigned char hdr[24];FILE* f=fopen(path,"rb");size_t n;
 if(!f)return 0;
 n=fread(hdr,1U,sizeof(hdr),f);fclose(f);
 return n==sizeof(hdr)&&!memcmp(hdr,"\211PNG\r\n\032\n",8U)&&
  (((unsigned)hdr[16]<<24)|((unsigned)hdr[17]<<16)|((unsigned)hdr[18]<<8)|hdr[19])==w&&
  (((unsigned)hdr[20]<<24)|((unsigned)hdr[21]<<16)|((unsigned)hdr[22]<<8)|hdr[23])==h;
}
static int width(mu_Font font, char const* str, int len){ (void)font; return (len < 0 ? (int)strlen(str) : len) * 8; }
static int height(mu_Font font){ (void)font; return 16; }
static void draw(void){ memset(canvas, 0, sizeof(canvas)); mui_render_overlay(canvas, 640U, 640U, 560U); }
static int click(char const* text)
{
 mu_Command* cmd = NULL;
 int x = -1, y = -1;
 draw();
 while (mu_next_command(mui_state.ctx, &cmd)){
  if (cmd->type == MU_COMMAND_TEXT && strcmp(cmd->text.str, text) == 0){ x = cmd->text.pos.x + 4; y = cmd->text.pos.y + 8; break; }
 }
 CHECK(x >= 0, "control visible");
 mu_input_mousemove(mui_state.ctx, x, y); draw(); draw();
 mu_input_mousedown(mui_state.ctx, x, y, MU_MOUSE_LEFT); draw();
 mu_input_mouseup(mui_state.ctx, x, y, MU_MOUSE_LEFT); draw();
 return 0;
}
int main(void)
{
 char path[512];
 FILE* fixtures;
 SDL_Event event = {0};
 auint log_count;
 appcfg_defaults(&main_cfg);
 main_headless = TRUE;
 CHECK(guicore_init(GUICORE_HEADLESS, "Screenshot tool"), "offscreen init");
 mui_state.ctx = calloc(1U, sizeof(*mui_state.ctx));
 CHECK(mui_state.ctx != NULL, "allocate GUI");
 mu_init(mui_state.ctx); mui_state.ctx->text_width = width; mui_state.ctx->text_height = height;
 mui_filedialog_init(&mui_rom_dialog); mui_filedialog_init(&mui_dir_dialog); mui_filedialog_init(&mui_cfg_file_dialog);
 mui_window_open(mui_state.ctx,MUI_WIN_DUMP_VIDEO);mui_refresh_visibility();
 CHECK(mainui_get_video_dump_omit_messages(),"omit video messages default");
 CHECK(click("OMIT MESSAGES")==0&&!mainui_get_video_dump_omit_messages(),"video omit checkbox enables message recording");
 CHECK(click("OMIT MESSAGES")==0&&mainui_get_video_dump_omit_messages(),"video omit checkbox returns to clean recording");
 mui_window_close(mui_state.ctx,MUI_WIN_DUMP_VIDEO);
 mui_window_open(mui_state.ctx, MUI_WIN_SCREENSHOT); mui_refresh_visibility();
 CHECK(!mainui_get_screenshot_include_gui(), "game-only default");
 CHECK(mainui_get_screenshot_bypass_filters(),"bypass filters defaults on");
 CHECK(click("CYCLE")==0&&click("WIDTH256")==0,"preset dropdown selection");
 CHECK(mainui_get_screenshot_x_width()==256U&&mainui_get_screenshot_y_scale()==1U,"preset applies both axes");
 CHECK(click("1X")==0&&click("3X")==0&&mainui_get_screenshot_y_scale()==3U,"Y dropdown can override preset");
 CHECK(click("256")==0&&click("720")==0&&mainui_get_screenshot_x_width()==720U,"X dropdown can override preset");
 CHECK(click("NEAREST")==0&&click("AREA")==0&&mainui_get_screenshot_sampling()==1U,"sampling dropdown selects area");
 CHECK(click("CUSTOM")==0&&click("CYCLE")==0,"restore default preset through dropdown");
 CHECK(mainui_get_screenshot_sampling()==1U,"preset retains independent sampling");
 CHECK(click("BYPASS FILTERS")==0&&!mainui_get_screenshot_bypass_filters(),"real bypass checkbox toggles");
 CHECK(click("INCLUDE GUI") == 0 && mainui_get_screenshot_include_gui(), "real checkbox toggles");
 CHECK(appcfg_save(&main_cfg, "screenshot.cfg"), "save checkbox preference");
 main_cfg.screenshot_include_gui = FALSE;
 main_cfg.screenshot_bypass_filters = TRUE;
 CHECK(appcfg_load_or_create(&main_cfg, "screenshot.cfg") && main_cfg.screenshot_include_gui, "checkbox preference reloads");
 CHECK(!mainui_get_screenshot_bypass_filters(),"bypass preference reloads");
 CHECK(click("BYPASS FILTERS")==0&&mainui_get_screenshot_bypass_filters(),"enable cycle-domain capture");
 log_count = textgui_logcount;
 textgui_log_add("MENU NOTICE");
 CHECK(textgui_logcount == log_count, "GUI-time notices are not queued for later OSD");
 CHECK(click("SAVE NOW") == 0 && !mui_screenshot_pending, "GUI save finishes after overlay draw");
 CHECK(strncmp(mui_toolshot_status, "Saved: ", 7U) == 0, "GUI reports saved filename");
 snprintf(path, sizeof(path), "screenshots/%s", mui_toolshot_status + 7);
 CHECK(png_size(path,640U,560U),"include-GUI PNG uses completed rendered surface");
 fixtures=fopen("png-fixtures.txt","w");CHECK(fixtures,"PNG fixture index");
 fprintf(fixtures,"GUI %s\n",path);
 CHECK(textgui_logcount == log_count, "screenshot does not duplicate GUI status in OSD");
 mainui_set_screenshot_include_gui(FALSE);
 CHECK(mainui_save_screenshot_gui(NULL, 0U, 0U, 0U, path, sizeof(path)), "game-only screenshot without GUI buffer");
 CHECK(png_size(path,1440U,1125U),"cycle PNG has no GUI border and fivefold scanlines");
 fprintf(fixtures,"RAW %s\n",path);fclose(fixtures);
 event.type = SDL_KEYDOWN; event.key.keysym.sym = SDLK_a;
 cu_kbd_enabled = 1U;
 CHECK(mui_handle_event(&event) == 1, "GUI owns ordinary keys even with guest keyboard enabled");
 event.type = SDL_KEYUP; CHECK(mui_handle_event(&event) == 1, "GUI owns matching releases");
 CHECK(SDL_InitSubSystem(SDL_INIT_EVENTS) == 0, "host event queue without a window");
 audio_set_headless(TRUE); CHECK(audio_init(), "offline audio for paused event loop");
 event.type = SDL_KEYDOWN; event.key.keysym.sym = SDLK_LEFT; ginput_sendevent(&event);
 CHECK(ginput_host_get_keyboard_buttons() & CU_CTR_SNES_M_LEFT, "pre-menu held key fixture");
 event.type = SDL_KEYUP; CHECK(SDL_PushEvent(&event) == 1, "queue held-key release");
 event.type = SDL_KEYDOWN; event.key.keysym.sym = SDLK_ESCAPE; CHECK(SDL_PushEvent(&event) == 1, "queue GUI escape");
 main_headless = FALSE; main_ispause = TRUE; main_nolimit = TRUE; main_ptick = SDL_GetTicks() - 17U;
 main_loop();
 CHECK(!main_exit && !mui_any_window_open(), "GUI Escape does not also quit emulator");
 CHECK(!(ginput_host_get_keyboard_buttons() & CU_CTR_SNES_M_LEFT), "menu-time key release clears pre-menu held input");
 textgui_log_add("GAME NOTICE"); CHECK(textgui_logcount > log_count, "OSD notices resume after GUI closes");
 mui_filedialog_destroy(&mui_rom_dialog); mui_filedialog_destroy(&mui_dir_dialog); mui_filedialog_destroy(&mui_cfg_file_dialog);
 free(mui_state.ctx); mui_state.ctx = NULL; audio_quit(); guicore_quit();
 puts("PASS: screenshot checkbox/persistence, deferred GUI capture, game-only crop, OSD ownership, guest keyboard routing");
 return 0;
}
