#define SDL_MAIN_HANDLED
#undef main
#include "../../cu_vfat.h"
#define main movie_emulator_main
#include "../../main.c"
#undef main
#define CHECK(c,m) do{if(!(c)){fprintf(stderr,"FAIL line %d: %s (%s)\n",__LINE__,m,capture_get_status());return 1;}}while(0)

static void latch_slot(unsigned slot,cu_state_ctr_t *out){
 cu_multitap_set_active_slot(0,slot);cu_multitap_set_active_slot(1,slot);
 cu_avr_get_state()->cycle+=100;
 (void)cu_ctr_process(0,4);
 cu_ctr_get_state(out);
}
static void wire_write(unsigned *prev,unsigned value){
 cu_avr_get_state()->cycle+=4;(void)cu_ctr_process(*prev,value);*prev=value;
}
static void wire_read_slot(unsigned slot){
 unsigned prev=0,i;
 wire_write(&prev,8);
 for(i=0;i<3;i++){wire_write(&prev,12);wire_write(&prev,8);}
 wire_write(&prev,11); /* Address both tap ports. */
 for(i=0;i<slot+2;i++){wire_write(&prev,15);wire_write(&prev,11);}
 wire_write(&prev,8);wire_write(&prev,0); /* Commit slot select. */
 wire_write(&prev,4);wire_write(&prev,0); /* Actual controller latch. */
 for(i=0;i<16;i++){wire_write(&prev,8);wire_write(&prev,0);}
}
static int copy_bad(char const *name,int truncate){
 FILE *in=fopen("movie.cic","rb"),*out=fopen(name,"wb");long len;int c;
 if(!in||!out)return 0;
 fseek(in,0,SEEK_END);len=ftell(in);rewind(in);
 for(long i=0;i<len-(truncate?1:0);i++){c=fgetc(in);if(!truncate&&i==48)c^=1;fputc(c,out);}
 fclose(in);fclose(out);return 1;
}
int main(int argc,char **argv){
 cu_state_ctr_t expected[8],actual;unsigned p,s,i;uint8 original_sd;
 auint ids[2][4], data_address=0;FILE *f;
 appcfg_defaults(&main_cfg);main_headless=TRUE;
 CHECK(guicore_init(GUICORE_HEADLESS,"Movie test"),"offscreen display");
 audio_set_headless(TRUE);CHECK(audio_init(),"offline audio");
 cu_avr_reset();cu_spir_set_size(2);cu_spisd_reset(0);cu_vfat_reset();cu_multitap_reset();cu_ctr_reset();cu_vdev_reset();
 if(argc>1 && !strcmp(argv[1],"--estimate")){
  for(unsigned reads=2;reads<=4;reads+=2){
   char name[40];long size;cu_state_ctr_t state;
   cu_multitap_set_tap_present(0,1);cu_multitap_set_tap_present(1,1);
   snprintf(name,sizeof(name),"ten-minutes-%u.cic",reads);
   CHECK(mainui_start_input_record(name,FALSE),"size recording");
   for(unsigned frame=0;frame<36000;frame++){
    for(unsigned slot=0;slot<reads;slot++){
     if(argc>2)wire_read_slot(slot);else latch_slot(slot,&state);
    }
    cu_avr_get_state()->cycle+=400000;
    if(frame%120==0)cu_kbd_debug_enqueue_port_byte(0,0x1C);
    (void)cu_kbd_response_for_port_command(0,KBD_SEND_KEY);
    CHECK(capture_frame_end(),"size frame");
   }
   mainui_set_input_capture_active(FALSE);
   f=fopen(name,"rb");CHECK(f,"size output");fseek(f,0,SEEK_END);size=ftell(f);fclose(f);
   printf("10 minutes, %u slot polls per port/frame, %s, keyboard poll each frame: %ld bytes (%.2f MiB)\n",reads,argc>2?"full UzeTap wire protocol":"payload latches only",size,size/1048576.0);
  }
  audio_quit();guicore_quit();return 0;
 }
 CHECK(main_mkdirs("sd-fixture"),"SD fixture directory");
 f=fopen("sd-fixture/DATA.BIN","wb");CHECK(f,"SD fixture file");
 for(i=0;i<512;i++)fputc(i&255U,f);
 fclose(f);
 filesys_setpath("sd-fixture/",NULL,0);cu_vfat_reset();
 for(i=0;i<CU_VFAT_ROOT_SIZE;i++){
  unsigned entry=0x40200U+i*32U;
  if(cu_vfat_read(entry)=='D'){
   unsigned cluster=cu_vfat_read(entry+26)|(cu_vfat_read(entry+27)<<8);
   data_address=CU_VFAT_SYS_SIZE+(cluster-2U)*0x8000U;break;
  }
 }
 CHECK(data_address!=0,"locate DATA.BIN backing sector");
 for(p=0;p<2;p++){
  cu_multitap_set_tap_present(p,1);
  for(s=0;s<4;s++){
   ids[p][s]=cu_vdev_create(CU_VDEV_TYPE_SUPERMOUSE32,"movie mouse");
   cu_multitap_set_slot_vdev(p,s,ids[p][s]);
   cu_vdev_set_packet_words(ids[p][s],1U<<(p*4+s),0x1234U+s+p*16);
  }
 }
 original_sd=(uint8)cu_vfat_read(data_address);
 cu_avr_get_state()->cycle=0xFFFFFFF0U; /* Cross the 32-bit cycle wrap. */
 CHECK(mainui_start_input_record("movie.cic",FALSE),"record from embedded state");
 for(i=0;i<8;i++){
  unsigned slot=i%4;
  /* Read and write the same private sector; no host mutation is allowed. */
  CHECK(cu_vfat_read(data_address)==(i?0xA0U+i-1:original_sd),"private SD read-after-write");
  cu_vfat_write(data_address,0xA0U+i);
  if(i==2)cu_multitap_set_probe_pending(0,1);
  latch_slot(slot,&expected[i]);
  if(i==2)CHECK((expected[i].blatch[0]&255U)==0xA2U && expected[i].probe_inflight[0],"multitap probe prefix captured with Super Mouse");
  cu_vdev_set_packet_words(ids[0][slot],0x55,0xAB00+i);
  if(i==3 || i==7)CHECK(capture_frame_end(),"record frame checksum");
 }
 mainui_set_input_capture_active(FALSE);
 CHECK(cu_vfat_read(data_address)==original_sd,"SD backing byte is unchanged after recording");
 f=fopen("sd-fixture/DATA.BIN","r+b");CHECK(f && fgetc(f)==0,"recording never wrote the host file");
 rewind(f);fputc(0xD5,f);fclose(f); /* Replay must use embedded data, not this edit. */
 cu_mouse_set_enabled(1);main_cfg.mouse_enable=TRUE;
 CHECK(mainui_start_input_replay("movie.cic",FALSE,FALSE),"restore starting state");
 main_cfg_capture_runtime();CHECK(main_cfg.mouse_enable,"temporary replay mouse disable does not overwrite preference");
 CHECK(!capture_replay(),"movie has events");
 for(i=0;i<8;i++){
  /* Live devices deliberately differ; they must not influence replay. */
  for(p=0;p<2;p++)for(s=0;s<4;s++)cu_vdev_set_packet_words(ids[p][s],0xFFFF,0xFFFF);
  CHECK(cu_vfat_read(data_address)==(i?0xA0U+i-1:original_sd),"recorded SD base sector replay");
  cu_vfat_write(data_address,0xA0U+i);
  if(i==2)cu_multitap_set_probe_pending(0,1);
  latch_slot(i%4,&actual);
  CHECK(!memcmp(expected[i].blatch,actual.blatch,sizeof(actual.blatch)),"all eight 32-bit slot packets round-trip");
  if(i==3 || i==7)CHECK(capture_frame_end(),"frame checksums match");
 }
 CHECK(capture_replay() && !capture_is_active(),"exact clean end");
 CHECK(cu_mouse_get_enabled(),"user legacy mouse preference restored");cu_mouse_set_enabled(0);
 f=fopen("sd-fixture/DATA.BIN","rb");CHECK(f && fgetc(f)==0xD5,"replay preserved changed host file");fclose(f);
 CHECK(copy_bad("corrupt.cic",0)&&copy_bad("truncated.cic",1),"bad fixtures");
 CHECK(!mainui_start_input_replay("corrupt.cic",FALSE,FALSE),"corrupt file rejected");
 CHECK(!mainui_start_input_replay("truncated.cic",FALSE,FALSE),"truncated file rejected");
 f=fopen("legacy.cap","wb");fputc(0,f);fputc(0,f);fclose(f);
 CHECK(!mainui_start_input_replay("legacy.cap",FALSE,FALSE),"legacy input rejected");
 /* ROM checksum validation precedes state restoration. */
 cu_avr_get_state()->crom[123]^=1;
 CHECK(!mainui_start_input_replay("movie.cic",FALSE,FALSE),"wrong ROM rejected");
 cu_avr_get_state()->crom[123]^=1;
 CHECK(mainui_start_input_replay("movie.cic",FALSE,FALSE),"restart valid movie");
 cu_avr_get_state()->cycle++;
 (void)cu_vfat_read(data_address);
 CHECK(!capture_frame_end() && !capture_is_active(),"cycle mismatch stops replay");
 CHECK(strstr(capture_get_status(),"desynchronized"),"desync reported");
 CHECK(mainui_start_input_replay("movie.cic",FALSE,FALSE),"restart after desync");
 mainui_set_input_capture_active(FALSE);
 cu_ctr_reset();cu_kbd_set_enabled(0);
 cu_multitap_set_tap_present(0,1);cu_multitap_set_tap_present(1,1);
 CHECK(mainui_start_input_record("wire.cic",FALSE),"record wire protocol");
 for(s=0;s<4;s++)wire_read_slot(s);
 CHECK(capture_frame_end(),"wire frame record");mainui_set_input_capture_active(FALSE);
 CHECK(mainui_start_input_replay("wire.cic",FALSE,FALSE),"play wire protocol");
 for(s=0;s<4;s++)wire_read_slot(s);
 CHECK(capture_frame_end()&&capture_replay(),"wire protocol exact replay");
 /* Keyboard responses are captured at emulated command time, not SDL time.
  * Mix a legacy P2 dongle with a keyboard routed to P1 tap slot 3. */
 cu_multitap_set_tap_present(1,0);cu_multitap_set_active_slot(0,3);
 {auint keyboard=cu_vdev_create(CU_VDEV_TYPE_KEYBOARD,"movie keyboard");cu_multitap_set_slot_vdev(0,3,keyboard);}
 cu_kbd_set_enabled(1);cu_kbd_reset_sessions();
 CHECK(mainui_start_input_record("keyboard.cic",FALSE),"record keyboard-enabled session");
 for(p=0;p<2;p++){
  cu_kbd_debug_enqueue_port_byte(p,0xE0);cu_kbd_debug_enqueue_port_byte(p,0xF0);cu_kbd_debug_enqueue_port_byte(p,0x75);
  for(i=0;i<4;i++){cu_avr_get_state()->cycle+=10;CHECK(cu_kbd_response_for_port_command(p,KBD_SEND_KEY)==(i==0?0xE0:i==1?0xF0:i==2?0x75:0),"keyboard make/break bytes and empty poll");}
  CHECK(cu_kbd_response_for_port_command(p,KBD_SEND_DEVICE_ID)==0xCC,"keyboard ID");
 }
 CHECK(capture_frame_end(),"keyboard frame record");mainui_set_input_capture_active(FALSE);
 cu_kbd_set_enabled(0);
 CHECK(mainui_start_input_replay("keyboard.cic",FALSE,FALSE),"restore keyboard topology");
 CHECK(cu_kbd_get_enabled(),"recorded legacy keyboard enable restored");
 for(p=0;p<2;p++){
  SDL_Event event={0};event.type=SDL_KEYDOWN;event.key.keysym.sym=SDLK_a;cu_kbd_handle_key(&event);
  cu_kbd_debug_enqueue_port_byte(p,0xFF); /* Deliberately conflicting host input. */
  for(i=0;i<4;i++){cu_avr_get_state()->cycle+=10;CHECK(cu_kbd_response_for_port_command(p,KBD_SEND_KEY)==(i==0?0xE0:i==1?0xF0:i==2?0x75:0),"recorded keyboard responses override live input");}
  CHECK(cu_kbd_response_for_port_command(p,KBD_SEND_DEVICE_ID)==0xCC,"replayed device ID");
 }
 CHECK(capture_frame_end() && capture_replay(),"keyboard exact EOF");
 CHECK(!cu_kbd_get_enabled(),"user keyboard enable preference restored");
 cu_kbd_set_enabled(1);
 CHECK(mainui_start_input_replay("keyboard.cic",FALSE,FALSE),"restart with live keyboard already enabled");
 cu_kbd_debug_enqueue_port_byte(0,0xFF);mainui_set_input_capture_active(FALSE);
 CHECK(cu_kbd_get_enabled() && cu_kbd_response_for_port_command(0,0)==0,"no queued replay/host keys leak into live keyboard");
 /* Hot plugging retains stable port/slot addresses and is replayed without
  * asking the host to repeat the device changes. */
 cu_ctr_reset();cu_kbd_set_enabled(0);
 CHECK(mainui_start_input_record("hotplug.cic",FALSE),"record hotplug session");
 latch_slot(0,&expected[0]);CHECK(capture_frame_end(),"normal direct ports");
 cu_multitap_reset(); /* GUI topology presets rebuild the protocol too. */
 cu_multitap_set_tap_present(0,1);cu_multitap_set_tap_present(1,1);
 {auint k=cu_vdev_create(CU_VDEV_TYPE_KEYBOARD,"plugged keyboard");auint m=cu_vdev_create(CU_VDEV_TYPE_SUPERMOUSE32,"plugged mouse");
  cu_multitap_set_slot_vdev(0,2,k);cu_multitap_set_slot_vdev(1,3,m);cu_vdev_set_packet_words(m,0x12,0x9876);}
 capture_inputs();cu_multitap_set_active_slot(0,2);cu_avr_get_state()->cycle+=10;
 cu_kbd_debug_enqueue_port_byte(0,0x75);CHECK(cu_kbd_response_for_port_command(0,0)==0x75,"hotplug keyboard response");
 latch_slot(3,&expected[1]);CHECK(capture_frame_end(),"taps and keyboard attached");
 {cu_state_multitap_t old;cu_multitap_get_state(&old);cu_multitap_reset();
  for(p=0;p<2;p++){cu_multitap_set_tap_present(p,1);for(s=0;s<4;s++)cu_multitap_set_slot_vdev(p,s,old.port[p].slot_vdev[s]);}}
 capture_inputs();latch_slot(0,&expected[2]);CHECK(capture_frame_end(),"same topology with protocol reset");
 cu_multitap_set_tap_present(1,0);cu_kbd_set_enabled(1);capture_inputs();
 cu_avr_get_state()->cycle+=10;cu_kbd_debug_enqueue_port_byte(1,0xF0);
 CHECK(cu_kbd_response_for_port_command(1,0)==0xF0,"legacy keyboard plugged after tap removal");
 latch_slot(0,&expected[3]);CHECK(capture_frame_end(),"direct keyboard frame");mainui_set_input_capture_active(FALSE);
 CHECK(mainui_start_input_replay("hotplug.cic",FALSE,FALSE),"replay hotplug session");
 CHECK(!cu_multitap_get_tap_present(0)&&!cu_multitap_get_tap_present(1),"starting direct ports restored");
 CHECK(!capture_replay(),"hotplug frame zero");latch_slot(0,&actual);CHECK(capture_frame_end(),"direct frame replay");
 CHECK(!capture_replay(),"apply recorded plug events");
 CHECK(cu_multitap_get_tap_present(0)&&cu_multitap_get_tap_present(1),"both tap attachments replayed");
 CHECK(cu_vdev_get(cu_multitap_get_slot_vdev(0,2))->type==CU_VDEV_TYPE_KEYBOARD,"keyboard attached to recorded slot");
 cu_multitap_set_active_slot(0,2);cu_avr_get_state()->cycle+=10;CHECK(cu_kbd_response_for_port_command(0,0)==0x75,"hotplug keyboard replay");
 latch_slot(3,&actual);CHECK(!memcmp(expected[1].blatch,actual.blatch,sizeof(actual.blatch)),"hotplug mouse slot packet");CHECK(capture_frame_end(),"hotplug frame replay");
 CHECK(!capture_replay()&&cu_multitap_get_active_slot(0)==0&&cu_multitap_get_active_slot(1)==0,"same-topology GUI reset restored selected slots");
 latch_slot(0,&actual);CHECK(capture_frame_end(),"protocol-reset frame replay");
 CHECK(!capture_replay()&&!cu_multitap_get_tap_present(1)&&cu_kbd_get_enabled(),"tap unplug and legacy keyboard enable replayed");
 cu_avr_get_state()->cycle+=10;CHECK(cu_kbd_response_for_port_command(1,0)==0xF0,"legacy hotplug response");
 latch_slot(0,&actual);CHECK(capture_frame_end()&&capture_replay(),"hotplug exact end");
 audio_quit();guicore_quit();
 puts("PASS: CZINPUT v1, eight Super Mouse slots, subframe latches, keyboard dongle/tap responses, hot swapping, cycle wrap, private SD, snapshot/ROM validation, CRC, truncation and cycle desync");
 return 0;
}
