#define SDL_MAIN_HANDLED
#undef main
#include "../../cu_vfat.h"
#define main input_test_emulator_main
#include "../../main.c"
#undef main
#include "../../microui/mui_integration.c"

#define CHECK(c, msg) do { if (!(c)) { fprintf(stderr, "FAIL: %s (line %d)\n", msg, __LINE__); return 1; } } while (0)

static int width(mu_Font font, char const* str, int len)
{ (void)font; return (len < 0 ? (int)strlen(str) : len) * 8; }
static int height(mu_Font font){ (void)font; return 16; }
static void draw(void)
{
 mu_begin(mui_state.ctx);
 mui_build_tool_inputcap_window(mui_state.ctx);
 mu_end(mui_state.ctx);
}
static int click_text(char const* text)
{
 mu_Command* cmd = NULL;
 int x = -1, y = -1;
 draw();
 while (mu_next_command(mui_state.ctx, &cmd)){
  if (cmd->type == MU_COMMAND_TEXT && strcmp(cmd->text.str, text) == 0){
   x = cmd->text.pos.x + 4; y = cmd->text.pos.y + 8; break;
  }
 }
 CHECK(x >= 0, "GUI control is drawn");
 mu_input_mousemove(mui_state.ctx, x, y); draw(); draw();
 mu_input_mousedown(mui_state.ctx, x, y, MU_MOUSE_LEFT); draw();
 mu_input_mouseup(mui_state.ctx, x, y, MU_MOUSE_LEFT); draw();
 return 0;
}

int main(void)
{
 FILE* file;
 auint frames, reset_pc;
 uint8 audio_samples[262];
 int byte;
 main_headless = TRUE;
 main_unthrottled = TRUE;
 main_nolimit = TRUE;
 main_cfg.gui_pause_while_open = FALSE;
 main_cfg.frame_rate_limiter = FALSE;
 CHECK(guicore_init(GUICORE_HEADLESS, "Input test"), "offscreen initialization");
 audio_set_headless(TRUE);
 CHECK(audio_init(), "offline audio");
 cu_ctr_reset();
 cu_avr_reset();
 cu_spir_set_size(2); cu_vfat_reset(); cu_spisd_reset(0);
 /* Stable infinite loop ROM, with no host-dependent input. */
 cu_avr_get_state()->crom[0]=0xFF; cu_avr_get_state()->crom[1]=0xCF;
 cu_avr_autofuse(FALSE); cu_avr_crom_update(0,65536); cu_avr_reset();
 reset_pc = cu_avr_get_state()->pc; /* Reset may select the bootloader vector. */
 CHECK(!mainui_get_input_capture_active(), "no automatic recording or replay at startup");

 CHECK(mainui_start_input_record("recorded input.cic", FALSE), "runtime recording in replay-capable build");
 frame_run(FALSE,FALSE); CHECK(capture_frame_end(), "record first complete frame");
 frame_run(FALSE,FALSE); CHECK(capture_frame_end(), "record second complete frame");
 mainui_set_input_capture_active(FALSE);
 file = fopen("recorded input.cic", "rb");
 CHECK(file != NULL, "recording exists");
 {char magic[8];CHECK(fread(magic,1,8,file)==8 && !memcmp(magic,"CZINPUT\0",8),"versioned header");}
 fclose(file);
 CHECK(mainui_start_input_replay("recorded input.cic", FALSE, FALSE), "selected file playback");
 CHECK(!capture_replay(), "first replay frame");
 frame_run(FALSE,FALSE); CHECK(capture_frame_end(), "first frame state round-trip");
 CHECK(!capture_replay(), "second replay frame");
 frame_run(FALSE,FALSE); CHECK(capture_frame_end(), "second frame state round-trip");

 file = fopen("invalid.cic", "wb"); CHECK(file != NULL, "malformed fixture"); fputc(0, file); fclose(file);
 CHECK(!mainui_start_input_replay("invalid.cic", TRUE, TRUE), "odd-sized file rejected before reset/video");
 CHECK(capture_is_active() && capture_is_replaying(), "failed selection leaves current session intact");
 CHECK(!mainui_get_video_dump_active(), "invalid replay does not start a video");
 file = fopen("empty.cic", "wb"); CHECK(file != NULL, "empty fixture"); fclose(file);
 CHECK(!capture_start("empty.cic", TRUE), "empty file rejected");
 CHECK(!capture_start("missing.cic", TRUE), "missing file rejected");
 mainui_set_input_capture_active(FALSE);

 /* Exercise the real file-picker dispatch and real MicroUI buttons. */
 mui_state.ctx = calloc(1U, sizeof(*mui_state.ctx));
 CHECK(mui_state.ctx != NULL, "MicroUI allocation");
 mu_init(mui_state.ctx); mui_state.ctx->text_width = width; mui_state.ctx->text_height = height;
 mui_filedialog_init(&mui_rom_dialog); mui_filedialog_init(&mui_dir_dialog); mui_filedialog_init(&mui_cfg_file_dialog);
 mui_pending_file_target = MUI_FILE_TARGET_INPUT_REPLAY;
 strcpy(mui_pending_file_apply, "recorded input.cic");
 mui_apply_pending_file_change();
 CHECK(strcmp(mui_input_replay_file, "recorded input.cic") == 0, "picker selection reaches playback field");
 CHECK(mui_input_reset_first == 1, "ROM reset defaults on");
 mui_window_open(mui_state.ctx, MUI_WIN_INPUT_CAPTURE);
 CHECK(click_text("RECORD VIDEO") == 0 && mui_input_record_video, "video checkbox works");
 main_cfg.video_dump_reset_first = TRUE;
 avconv_set_autoinc(FALSE);
 avconv_set_filename("playback video.mp4");
 cu_avr_get_state()->pc = 100U;
 CHECK(click_text("PLAY FROM BEGINNING") == 0, "GUI playback action");
 CHECK(main_input_capture_active && capture_is_replaying(), "GUI starts replay");
 CHECK(cu_avr_get_state()->pc == reset_pc, "play action uses the ROM's reset vector before first input");
 CHECK(main_input_replay_video && mainui_get_video_dump_active(), "GUI starts owned video");
 CHECK(!mui_any_window_open(), "play closes windows so GUI pause does not block it");
 memset(audio_samples, 128, sizeof(audio_samples));
 CHECK(!capture_replay(), "video frame one input"); frame_run(FALSE,FALSE); CHECK(capture_frame_end(),"video frame one checksum"); avconv_push(audio_samples, 262U);
 CHECK(!capture_replay(), "video frame two input"); frame_run(FALSE,FALSE); CHECK(capture_frame_end(),"video frame two checksum"); avconv_push(audio_samples, 262U);
 CHECK(avconv_is_initialized(), "FFmpeg video stream initialized");
 frames = main_frame_counter;
 main_isadvfr = TRUE;
 main_ptick = SDL_GetTicks() - 17U;
 main_loop();
 CHECK(!main_input_capture_active && main_ispause, "EOF pauses and ends replay");
 CHECK(!main_isadvfr, "EOF cancels frame advance so pause persists");
 CHECK(main_frame_counter == frames, "EOF does not emulate an extra frame");
 CHECK(!main_input_replay_video && !mainui_get_video_dump_active() && !avconv_is_initialized(), "EOF finalizes video");
 file = fopen("playback video.mp4", "rb"); CHECK(file != NULL, "playback video output created");
 byte = fgetc(file); fclose(file); CHECK(byte != EOF, "video output is not empty");
 CHECK(!main_gui_pause_owned, "EOF pause survives closing the GUI");
 CHECK(mainui_start_input_replay("recorded input.cic", FALSE, TRUE), "restart from beginning");
 mainui_set_input_capture_active(FALSE);
 CHECK(!capture_is_active() && !mainui_get_video_dump_active(), "Stop also stops owned video");
 main_video_dump_set_active(TRUE, FALSE);
 CHECK(!mainui_start_input_replay("recorded input.cic", TRUE, FALSE), "unrelated video cannot be reset or joined");
 CHECK(mainui_get_video_dump_active(), "unrelated video left active");
 main_video_dump_set_active(FALSE, FALSE);
 CHECK(mainui_start_input_replay("recorded input.cic", FALSE, TRUE), "owned video restarted");
 mainui_set_video_dump_active(FALSE);
 mainui_set_video_dump_active(TRUE);
 CHECK(!main_input_replay_video, "manual video restart takes ownership away from replay");
 mainui_set_input_capture_active(FALSE);
 CHECK(mainui_get_video_dump_active(), "input Stop does not close a manually restarted video");
 mainui_set_video_dump_active(FALSE);
 main_np_session_active = TRUE;
 CHECK(!mainui_start_input_replay("recorded input.cic", TRUE, FALSE), "netplay prevents replay");
 CHECK(!mainui_start_input_record("blocked.cic", TRUE), "netplay prevents recording");
 file = fopen("blocked.cic", "rb"); CHECK(file == NULL, "netplay rejection does not create a file");
 main_np_session_active = FALSE;
 mui_filedialog_destroy(&mui_rom_dialog); mui_filedialog_destroy(&mui_dir_dialog); mui_filedialog_destroy(&mui_cfg_file_dialog);
 free(mui_state.ctx); mui_state.ctx = NULL;
 audio_quit(); guicore_quit();
 puts("PASS: recording/replay round-trip, file validation, GUI picker/play/video controls, reset, exact EOF, video finalization and Stop");
 return 0;
}
