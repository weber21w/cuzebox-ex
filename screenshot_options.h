/* Shared screenshot choices. Widths refer to the complete 1440-cycle window. */
#ifndef SCREENSHOT_OPTIONS_H
#define SCREENSHOT_OPTIONS_H
#define SHOT_WIDTH_COUNT 15U
#define SHOT_PRESET_COUNT 7U
static inline unsigned shot_width(unsigned i)
{
 static unsigned const v[]={1440,2880,720,480,384,376,360,352,320,288,256,240,224,192,160};
 return v[i<SHOT_WIDTH_COUNT?i:0];
}
static inline char const* shot_width_name(unsigned i)
{
 static char const* const v[]={"1440 (1X)","2880 (2X)","720","480","384","376","360","352","320","288","256","240","224","192","160"};
 return i<SHOT_WIDTH_COUNT?v[i]:"CUSTOM";
}
static inline char const* shot_sampling_name(unsigned i)
{
 static char const* const v[]={"NEAREST","AREA","LINEAR"};
 return v[i<3U?i:0U];
}
static inline char const* shot_preset_name(unsigned i)
{
 static char const* const v[]={"CYCLE","RAW","WIDTH320","WIDTH256","WIDTH240","WIDTH192","WIDTH160"};
 return i<SHOT_PRESET_COUNT?v[i]:"CUSTOM";
}
static inline unsigned shot_preset_width(unsigned i)
{
 static unsigned const v[]={1440,1440,320,256,240,192,160};
 return v[i<SHOT_PRESET_COUNT?i:0];
}
static inline unsigned shot_preset_y(unsigned i){return i==0U?5U:1U;}
static inline unsigned shot_preset_index(unsigned w,unsigned y)
{
 unsigned i;for(i=0;i<SHOT_PRESET_COUNT;i++)if(w==shot_preset_width(i)&&y==shot_preset_y(i))break;
 return i;
}
#endif
