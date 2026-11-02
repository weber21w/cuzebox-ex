#define SDL_MAIN_HANDLED
#undef main
#define main video_messages_emulator_main
#include "../../main.c"
#undef main
#include "../../avconv.c"
#define CHECK(c,msg) do{if(!(c)){fprintf(stderr,"FAIL: %s (line %d)\n",msg,__LINE__);return 1;}}while(0)

static uint32 screen[640U*228U];
static int record_case(boole omit,char const* name)
{
 uint8 audio[262];auint frame,x,y;FILE* file;guicore_pixfmt_t fmt;
 uint32 const* pixels;
 mainui_set_video_dump_omit_messages(omit);avconv_set_filename(name);avconv_set_autoinc(FALSE);
 mainui_set_video_dump_active(TRUE);
 memset(audio,128,sizeof(audio));guicore_getpixfmt(&fmt);
 for(frame=0U;frame<3U;frame++){
  textgui_log_add("EMULATION RESUMED");
  frame_run(FALSE,FALSE);
  pixels=guicore_getpixbuf()+640U*20U;
  memcpy(screen,pixels,sizeof(screen));
  if(omit){
   CHECK(avconv_clean_valid,"frame pipeline stages clean pixels");
   CHECK(memcmp(screen,avconv_clean_pixels,sizeof(screen))!=0,"messages remain visible on screen while omitted from recording");
  }
  if(!frame){
   char path[64];snprintf(path,sizeof(path),"%s.rgb",name);
   file=fopen(path,"wb");CHECK(file,"RGB recording reference");
   pixels=omit?avconv_clean_pixels:screen;
   for(y=0U;y<228U;y++)for(x=10U;x<630U;x++){
    uint32 c=pixels[y*640U+x];uint8 rgb[3]={c>>fmt.rsh,c>>fmt.gsh,c>>fmt.bsh};
    CHECK(fwrite(rgb,1U,3U,file)==3U,"write expected recording pixels");
   }
   CHECK(fclose(file)==0,"close reference");
  }
  /* GUI events can change the option between render and encode. The pending
   * frame must retain its original policy and its audio/video frame count. */
  if(!frame)mainui_set_video_dump_omit_messages(!omit);
  avconv_push(audio,262U);
  if(!frame)mainui_set_video_dump_omit_messages(omit);
  CHECK(avconv_is_initialized()&&avconv_ok,"FFmpeg recording initialized");
  CHECK(avconv_afrac==((frame+1U)*8008U)%10000U,"every frame advances audio timing equally in both modes");
  CHECK(!memcmp(screen,guicore_getpixbuf()+640U*20U,sizeof(screen)),"encoder leaves visible messages unchanged");
 }
 mainui_set_video_dump_active(FALSE);
 CHECK(!avconv_is_initialized()&&!avconv_clean_valid,"finalization clears clean frame cache");
 file=fopen(name,"rb");CHECK(file&&fgetc(file)!=EOF,"encoded video exists");fclose(file);
 return 0;
}

int main(void)
{
 appcfg_defaults(&main_cfg);main_headless=TRUE;
 CHECK(mainui_get_video_dump_omit_messages()&&avconv_get_omit_messages(),"OMIT MESSAGES defaults on");
 SDL_setenv("SDL_VIDEODRIVER","dummy",1);SDL_SetHint(SDL_HINT_RENDER_DRIVER,"software");
 CHECK(guicore_init(GUICORE_NOVSYNC|GUICORE_GAMEONLY,"Video messages"),"dummy visible-surface renderer");
 CHECK(!guicore_is_headless(),"on-screen message drawing exercised");
 cu_avr_reset();cu_avr_get_state()->crom[0]=0xFFU;cu_avr_get_state()->crom[1]=0xCFU;
 cu_avr_crom_update(0U,2U);
 textgui_reset();textgui_log_set_enabled(TRUE);
 if(record_case(TRUE,"omit.MP4")||record_case(FALSE,"messages.MP4"))return 1;
 mainui_set_video_dump_omit_messages(TRUE);
 CHECK(!avconv_clean_valid&&!avconv_frame_staged,"finalization leaves no stale pending frame");
 {uint8 audio[262]={0};avconv_push(audio,262U);CHECK(!avconv_is_initialized(),"no unstaged frame leaks messages into a fresh recording");}
 guicore_quit();
 puts("Video messages: PASS (default omission, visible on-screen notices, opt-in messages, encoded video/audio and cache lifecycle)");
 return 0;
}
