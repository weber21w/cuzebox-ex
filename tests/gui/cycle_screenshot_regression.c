#define SDL_MAIN_HANDLED
#undef main
#define main cycle_shot_emulator_main
#include "../../main.c"
#undef main
#include "../../frame.c"
#define CHECK(c,msg) do{if(!(c)){fprintf(stderr,"FAIL: %s (%d)\n",msg,__LINE__);return 1;}}while(0)

static cu_row_t fixture_row;
static uint8 color_at(auint x,auint y)
{return (y==0U||y==223U)?0U:(uint8)(x^(y*3U));}
static void fill_field(auint rows)
{
 auint x,y;
 for(y=0U;y<rows;y++){
  fixture_row.pno=RPULSE_START+y;
  memset(fixture_row.pixels,0xFF,sizeof(fixture_row.pixels));
  for(x=0U;x<ROW_ACTIVE_CYCLES;x++)fixture_row.pixels[ROW_BEGINCY+x]=color_at(x,y);
  frame_capture_row(&fixture_row);
 }
}
int main(void)
{
 uint32 *pixels,*again;auint w,h,x,y,rot,flip;
 uint32 const* pal;guicore_pixfmt_t fmt;
 uint8* png;auint size;FILE* file;
 static auint const rotations[]={0U,JAMMA_ROTATE_90,JAMMA_ROTATE_180,JAMMA_ROTATE_270};
 appcfg_defaults(&main_cfg);main_headless=TRUE;
 CHECK(guicore_init(GUICORE_HEADLESS,"Cycle screenshot"),"offscreen palette");
 guicore_getpixfmt(&fmt);pal=guicore_getpalette();frame_capture_reset();
 fill_field(224U);frame_capture_commit(FALSE,0U);
 CHECK(frame_capture_cycle_pixels(&pixels,&w,&h)&&w==1440U&&h==1120U,"224 captured lines become 1120 PNG rows");
 for(y=0U;y<h;y++)for(x=0U;x<w;x++)
  CHECK(pixels[y*w+x]==pal[color_at(x,y/5U)],"all 1440 cycles preserved; five identical rows; black game rows retained");
 file=fopen("cycles.rgb","wb");CHECK(file,"RGB reference");
 for(y=0U;y<h;y++)for(x=0U;x<w;x++){
  uint32 c=pixels[y*w+x];uint8 rgb[3]={c>>fmt.rsh,c>>fmt.gsh,c>>fmt.bsh};
  CHECK(fwrite(rgb,1U,3U,file)==3U,"RGB reference write");
 }
 fclose(file);
 CHECK(main_save_screenshot_to_path("cycles.png"),"cycle PNG export");
 {
  char automatic[512];
  CHECK(mainui_save_screenshot_auto(automatic,sizeof(automatic)),"shortcut automatic capture");
  CHECK(strrchr(automatic,'.')&&!strcmp(strrchr(automatic,'.'),".png"),"automatic filename defaults to PNG");
 }
 CHECK(mainui_capture_screenshot_png(&png,&size,NULL,NULL),"PNG API capture");
 file=fopen("cycles.png","rb");CHECK(file,"PNG file open");
 for(x=0U;x<size;x++)CHECK(fgetc(file)==png[x],"PNG file and API bytes agree");
 CHECK(fgetc(file)==EOF,"PNG file exact length");fclose(file);free(png);
 /* A partial next field must not leak into a screenshot while paused. */
 fixture_row.pno=RPULSE_START;memset(fixture_row.pixels,7U,sizeof(fixture_row.pixels));frame_capture_row(&fixture_row);
 frame_set_area_weighted(TRUE);guicore_set_filter_pre_mode(1U);guicore_set_filter_crt_mode(1U);
 CHECK(frame_capture_cycle_pixels(&again,&w,&h),"paused capture with filters enabled");
 CHECK(!memcmp(pixels,again,(size_t)w*h*sizeof(*pixels)),"partial frame and display filters do not change cycle screenshot");free(again);
 for(rot=0U;rot<4U;rot++)for(flip=0U;flip<4U;flip++){
  guicore_set_jamma(rotations[rot]|((flip&1U)?JAMMA_FLIP_H:0U)|((flip&2U)?JAMMA_FLIP_V:0U));
  CHECK(frame_capture_cycle_pixels(&again,&w,&h),"JAMMA capture");
  CHECK(w==((rot&1U)?1120U:1440U)&&h==((rot&1U)?1440U:1120U),"portrait screenshot rotates canvas without letterboxing");
  for(y=0U;y<1120U;y+=37U)for(x=0U;x<1440U;x+=31U){
   auint dx=x,dy=y;
   if(rot==1U){dx=1119U-y;dy=x;}else if(rot==2U){dx=1439U-x;dy=1119U-y;}else if(rot==3U){dx=y;dy=1439U-x;}
   if(flip&1U)dx=w-1U-dx;
   if(flip&2U)dy=h-1U-dy;
   CHECK(again[dy*w+dx]==pixels[y*1440U+x],"rotation/mirror preserves exact samples");
  }
  free(again);
 }
 free(pixels);
 /* Width conversion samples the whole cycle window, with independent Y. */
 guicore_set_jamma(0U);
 {
  auint widths[]={256U,240U,192U,160U,2880U},i;char settings[4096];
  for(i=0U;i<5U;i++){
   CHECK(frame_capture_cycle_pixels_ex(&pixels,&w,&h,widths[i],2U,0U),"resized cycle capture");
   CHECK(w==widths[i]&&h==448U,"independent dimensions");
   for(y=0U;y<h;y+=17U)for(x=0U;x<w;x++)CHECK(pixels[y*w+x]==pal[color_at(x*1440U/w,y/2U)],"nearest maps full width");
   free(pixels);
  }
  CHECK(!frame_capture_cycle_pixels_ex(&pixels,&w,&h,0U,5U,0U),"reject zero width");
  CHECK(!mainui_set_screenshot_scale(1440U,11U,0U),"reject invalid Y scale");
  CHECK(mainui_screenshot_settings("PRESET WIDTH256",settings,sizeof(settings))&&main_cfg.screenshot_x_width==256U&&main_cfg.screenshot_y_scale==1U,"API preset sets both axes");
  CHECK(mainui_screenshot_settings("SET Y_SCALE 3",settings,sizeof(settings))&&strstr(settings,"\"preset\":\"CUSTOM\""),"manual override leaves preset editable");
  CHECK(!mainui_screenshot_settings("SET X_WIDTH -1",settings,sizeof(settings))&&!mainui_screenshot_settings("SET Y_SCALE 3bad",settings,sizeof(settings)),"strict settings parsing");
  CHECK(mainui_screenshot_settings("PRESET CYCLE",settings,sizeof(settings)),"restore default preset");
  /* A two-color transition: box average is exact, while linear uses centers. */
  {
   uint8 samples[1440];auint c;uint32 a,b;
   memset(samples,0,sizeof(samples));memset(samples+720,255,720);
   a=frame_shot_sample(samples,0U,1U,1U,pal,&fmt);
   b=frame_shot_sample(samples,0U,1U,2U,pal,&fmt);
   for(c=0U;c<3U;c++){
    auint sh=c==0U?fmt.rsh:c==1U?fmt.gsh:fmt.bsh;
    auint expected=(((pal[0]>>sh)&255U)+((pal[255]>>sh)&255U)+1U)/2U;
    CHECK(((a>>sh)&255U)==expected&&((b>>sh)&255U)==expected,"area/linear independently expected midpoint");
   }
  }
 }
 mainui_set_screenshot_bypass_filters(FALSE);guicore_set_jamma(JAMMA_ROTATE_90);
 CHECK(main_capture_game_screenshot(&pixels,&w,&h)&&w<h&&h==456U,"filtered opt-in uses tight portrait content, not landscape letterboxing");free(pixels);
 CHECK(main_save_screenshot_to_path("filtered.png"),"filtered PNG export");
 mainui_set_screenshot_bypass_filters(TRUE);guicore_set_jamma(0U);frame_capture_reset();
 fill_field(160U);frame_capture_commit(FALSE,0U);
 CHECK(frame_capture_cycle_pixels(&pixels,&w,&h)&&h==800U,"height follows captured range instead of fixed 224");free(pixels);
 frame_capture_reset();
 memset(fixture_row.pixels,1U,sizeof(fixture_row.pixels));fixture_row.pno=RPULSE_START;frame_capture_row(&fixture_row);frame_capture_commit(TRUE,0U);
 memset(fixture_row.pixels,2U,sizeof(fixture_row.pixels));fixture_row.pno=RPULSE_START;frame_capture_row(&fixture_row);frame_capture_commit(TRUE,1U);
 CHECK(frame_capture_cycle_pixels(&pixels,&w,&h)&&h==5U,"woven fields retain progressive aspect instead of doubled height");
 CHECK(pixels[0]==pal[1]&&pixels[3U*w]==pal[2],"opposite fields use nearest-neighbor rows without blending");free(pixels);
 frame_capture_reset();CHECK(frame_capture_cycle_pixels(&pixels,&w,&h),"reset capture");
 CHECK(pixels[0]==pal[0],"reset never exports stale game pixels");free(pixels);
 /* Exercise Huffman literals, long runs, very short scanlines and arbitrary
  * native channel shifts. Independent Python/zlib decoding checks the files. */
 for(rot=0U;rot<8U;rot++){
  auint cw=rot<3U?rot+1U:(rot==3U?257U:1440U),ch=rot+1U;
  char name[48];uint32 seed=0x12345678U;
  pixels=malloc((size_t)cw*ch*sizeof(*pixels));CHECK(pixels,"codec test allocation");
  for(x=0U;x<cw*ch;x++){
   seed=seed*1664525U+1013904223U;
   pixels[x]=rot==4U?0U:rot==5U?0xFFFFFFU:rot==6U?0x123456U:seed&0xFFFFFFU;
  }
  CHECK(screenshot_png_encode(pixels,cw,cw,ch,0U,16U,8U,&png,&size),"PNG codec edge case");
  snprintf(name,sizeof(name),"codec-%u.png",(unsigned)rot);file=fopen(name,"wb");CHECK(file,"codec PNG file");
  CHECK(fwrite(png,1U,size,file)==size,"codec PNG write");fclose(file);free(png);
  snprintf(name,sizeof(name),"codec-%u.rgb",(unsigned)rot);file=fopen(name,"wb");CHECK(file,"codec RGB file");
  for(x=0U;x<cw*ch;x++){
   uint8 rgb[3]={pixels[x],pixels[x]>>16,pixels[x]>>8};
   CHECK(fwrite(rgb,1U,3U,file)==3U,"codec reference write");
  }
  fclose(file);free(pixels);
 }
 guicore_quit();
 puts("PASS: cycle screenshots, one-clock pixels, vertical duplication, filters, partial fields, JAMMA, interlace and reset");return 0;
}
