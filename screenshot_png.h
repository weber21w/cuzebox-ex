/* Small dependency-free RGB PNG encoder. PNG Up filtering and fixed-Huffman
 * DEFLATE with short-distance runs keep duplicated scanlines compact. This
 * runs only on screenshot requests, never in the emulation hot path. */
#ifndef SCREENSHOT_PNG_H
#define SCREENSHOT_PNG_H
#include "types.h"
#include "cu_ufile.h"
#include <stdlib.h>
#include <string.h>

static void shot_be32(uint8* p, uint32 n)
{ p[0]=n>>24;p[1]=n>>16;p[2]=n>>8;p[3]=n; }

typedef struct { uint8* data; auint size; uint32 bits; unsigned count; } shot_bits_t;
static void shot_bits(shot_bits_t* s, unsigned value, unsigned count)
{
 s->bits|=(uint32)value<<s->count;s->count+=count;
 while(s->count>=8U){s->data[s->size++]=(uint8)s->bits;s->bits>>=8;s->count-=8U;}
}
static unsigned shot_reverse(unsigned value, unsigned count)
{
 unsigned result=0U;
 while(count--){result=(result<<1)|(value&1U);value>>=1;}
 return result;
}
static void shot_symbol(shot_bits_t* s,unsigned symbol)
{
 unsigned code,count;
 if(symbol<144U){code=0x30U+symbol;count=8U;}
 else if(symbol<256U){code=0x190U+symbol-144U;count=9U;}
 else if(symbol<280U){code=symbol-256U;count=7U;}
 else{code=0xC0U+symbol-280U;count=8U;}
 shot_bits(s,shot_reverse(code,count),count);
}
static void shot_chunk(uint8* dst,char const* type,uint8 const* data,auint len)
{
 shot_be32(dst,len);memcpy(dst+4,type,4U);
 if(len&&dst+8!=data)memcpy(dst+8,data,len);
 shot_be32(dst+8U+len,cu_ufile_crc32(dst+4U,len+4U));
}

static boole screenshot_png_encode(uint32 const* pixels,auint pitch,auint w,auint h,
                                  unsigned rsh,unsigned gsh,unsigned bsh,
                                  uint8** result,auint* result_size)
{
 static unsigned const bases[]={3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258};
 static unsigned const extra[]={0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0};
 uint8 *raw=NULL,*z=NULL,*png=NULL;
 auint row,n,x,y,i,zlen,total;
 uint32 a=1U,b=0U;
 shot_bits_t s;
 if(!result||!result_size)return FALSE;
 *result=NULL;*result_size=0U;
 if(!pixels||!w||!h||pitch<w||w>8192U||h>8192U||rsh>24U||gsh>24U||bsh>24U)return FALSE;
 row=w*3U+1U;
 if(h>128U*1024U*1024U/row)return FALSE;
 n=row*h;
 raw=(uint8*)malloc(n);z=(uint8*)malloc((size_t)n+n/8U+64U);
 if(!raw||!z)goto fail;
 for(y=0U;y<h;y++){
  uint8* dst=raw+y*row;*dst++=2U; /* PNG Up: preserves RGB exactly. */
  for(x=0U;x<w;x++){
   uint32 c=pixels[y*pitch+x],prev=y?pixels[(y-1U)*pitch+x]:0U;
   *dst++=(uint8)((c>>rsh)-(prev>>rsh));
   *dst++=(uint8)((c>>gsh)-(prev>>gsh));
   *dst++=(uint8)((c>>bsh)-(prev>>bsh));
  }
 }
 z[0]=0x78U;z[1]=0x01U;
 s=(shot_bits_t){z,2U,0U,0U};shot_bits(&s,3U,3U); /* final fixed-Huffman block */
 for(i=0U;i<n;){
  unsigned best=0U,distance=0U,d;
  for(d=1U;d<=3U;d+=2U){
   unsigned length=0U;
   if(i<d)continue;
   while(length<258U&&i+length<n&&raw[i+length]==raw[i+length-d])length++;
   if(length>best){best=length;distance=d;}
  }
  if(best>=3U){
   unsigned code=0U;
   while(code<28U&&bases[code+1U]<=best)code++;
   shot_symbol(&s,257U+code);shot_bits(&s,best-bases[code],extra[code]);
   shot_bits(&s,shot_reverse(distance==1U?0U:2U,5U),5U);
  }else{best=1U;shot_symbol(&s,raw[i]);}
  for(x=0U;x<best;x++){
   a+=raw[i+x];if(a>=65521U)a-=65521U;
   b+=a;if(b>=65521U)b-=65521U;
  }
  i+=best;
 }
 shot_symbol(&s,256U);
 if(s.count)shot_bits(&s,0U,8U-s.count);
 shot_be32(z+s.size,(b<<16)|a);zlen=s.size+4U;
 total=zlen+57U;png=(uint8*)malloc(total);
 if(!png)goto fail;
 memcpy(png,"\211PNG\r\n\032\n",8U);
 {
  uint8 header[13]={0};shot_be32(header,w);shot_be32(header+4,h);header[8]=8U;header[9]=2U;
  shot_chunk(png+8,"IHDR",header,13U);
 }
 shot_chunk(png+33,"IDAT",z,zlen);shot_chunk(png+45U+zlen,"IEND",NULL,0U);
 free(raw);free(z);*result=png;*result_size=total;return TRUE;
fail:
 free(raw);free(z);free(png);return FALSE;
}
/* Store the same JSON geometry contract returned by SCREENSHOT_CAPTURE. */
static boole screenshot_png_annotate(uint8** png,auint* size,char const* json)
{
 static char const key[]="CUEX.Capture";
 auint len=(auint)strlen(json)+(auint)sizeof(key),extra=len+12U;
 uint8 *out=(uint8*)malloc(*size+extra),*data;
 if(!out)return FALSE;
 memcpy(out,*png,33U);data=out+41U;
 memcpy(data,key,sizeof(key));memcpy(data+sizeof(key),json,strlen(json));
 shot_chunk(out+33U,"tEXt",data,len);
 memcpy(out+33U+extra,*png+33U,*size-33U);
 free(*png);*png=out;*size+=extra;return TRUE;
}
#endif
