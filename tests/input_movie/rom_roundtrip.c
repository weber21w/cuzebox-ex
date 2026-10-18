#define SDL_MAIN_HANDLED
#undef main
#define main movie_emulator_main
#include "../../main.c"
#undef main
#define CHECK(c,m) do{if(!(c)){fprintf(stderr,"FAIL %s: %s\n",m,capture_get_status());return 1;}}while(0)
int main(int argc,char **argv){
 unsigned i;uint32 final_sram;uint8 expected[4096];
 if(argc!=2)return 2;
 appcfg_defaults(&main_cfg);main_headless=TRUE;main_unthrottled=TRUE;main_nolimit=TRUE;
 CHECK(guicore_init(GUICORE_HEADLESS,"ROM movie"),"offscreen init");audio_set_headless(TRUE);CHECK(audio_init(),"audio");
 main_cfg.sd_timing_preset=CU_SPISD_PRESET_FAST;
 CHECK(mainui_load_rom_file(argv[1]),"load ROM");
 CHECK(mainui_start_input_record("roundtrip.cic",TRUE),"record reset snapshot");
 for(i=0;i<2000;i++){
  if(i==1700)cu_ctr_setsnes(0,CU_CTR_SNES_M_START);
  if(i==1704)cu_ctr_setsnes(0,0);
  frame_run(FALSE,FALSE);CHECK(capture_frame_end(),"record frame");
 }
 memcpy(expected,cu_avr_get_state()->sram,sizeof(expected));final_sram=cu_avr_get_state()->pc;
 mainui_set_input_capture_active(FALSE);
 /* Playback must ignore a different bootloader preference and SD preset. */
 main_cfg.resident_bootloader_enable=FALSE;main_cfg.sd_timing_preset=CU_SPISD_PRESET_SLOW;main_sd_apply_policy();
 CHECK(mainui_start_input_replay("roundtrip.cic",TRUE,FALSE),"play snapshot");
 for(i=0;i<2000;i++){
  CHECK(!capture_replay(),"not EOF early");
  cu_ctr_setsnes(0,0xFFFF);frame_run(FALSE,FALSE);CHECK(capture_frame_end(),"replay frame");
 }
 CHECK(capture_replay(),"exact EOF");
 CHECK(!memcmp(expected,cu_avr_get_state()->sram,sizeof(expected)) && final_sram==cu_avr_get_state()->pc,"final SRAM and PC match");
 audio_quit();guicore_quit();puts("PASS: real ROM 2000-frame movie with bootloader, SD chainload, live-input isolation and exact final state");return 0;
}
