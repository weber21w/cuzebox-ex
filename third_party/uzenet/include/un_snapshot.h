#ifndef UN_SNAPSHOT_H
#define UN_SNAPSHOT_H
#include <stdint.h>
#include <string.h>
/* Lossless compact RCF1 state: car/lap, 10-bit X/Y, heading, speed,
 * waypoint, total time and best lap. Two records fit one UART/UDP batch. */
#define UN_SNAPSHOT_BYTES 11u
/* Separate match options fence the Jump'n Bump owner/input stream from its
 * older chat-based rooms. RCF1's codec and session behavior are unchanged. */
static inline uint8_t un_snapshot_jnb(const uint8_t *config){
    return !memcmp(config,"JNBBOT01",8) && config[12]=='J' && config[13]=='B' && config[14]==1 && !config[15];
}
static inline uint8_t un_snapshot_enabled(const uint8_t *config){return !memcmp(config,"RCF1SNP2",8) || un_snapshot_jnb(config);}
/* These match options isolate the new timed format from existing v1 rooms. */
static inline uint8_t un_snapshot_timed(const uint8_t *config){
    return un_snapshot_enabled(config) && config[12]=='T' && config[13]=='S' && config[14]==1 && !config[15];
}
static inline void un_snapshot_pack(uint8_t *out,const uint8_t *v){
    uint16_t x=(uint16_t)v[2]|((uint16_t)v[3]<<8),y=(uint16_t)v[4]|((uint16_t)v[5]<<8);
    out[0]=v[0]|(v[9]<<2);out[1]=x;out[2]=(x>>8)|(y<<2);out[3]=y>>6;
    out[4]=v[6];out[5]=v[7];out[6]=v[8];memcpy(out+7,v+10,4);
}
static inline uint8_t un_snapshot_unpack(uint8_t *out,const uint8_t *v){
    if((v[0]&0xe0u) || (v[0]>>2)>4u || v[3]>15u || v[6]>=237u)return 0;
    uint16_t x=v[1]|((uint16_t)(v[2]&3u)<<8),y=(v[2]>>2)|((uint16_t)v[3]<<6);
    out[0]=v[0]&3u;out[1]=0;out[2]=x;out[3]=x>>8;out[4]=y;out[5]=y>>8;
    out[6]=v[4];out[7]=v[5];out[8]=v[6];out[9]=v[0]>>2;memcpy(out+10,v+7,4);
    return 1;
}
/* Recovery keeps the original lap-start clock for unfinished cars. Bit 5
 * distinguishes it from a finished total. Ordinary codecs stay strict. */
static inline void un_snapshot_recovery_pack(uint8_t *out,const uint8_t *v){
    un_snapshot_pack(out,v);if(v[1])out[0]|=32;
}
static inline uint8_t un_snapshot_recovery_unpack(uint8_t *out,const uint8_t *v){
    if((v[0]&0xc0u) || ((v[0]>>2)&7u)>4u || v[3]>15u || v[6]>=237u)return 0;
    uint16_t x=v[1]|((uint16_t)(v[2]&3u)<<8),y=(v[2]>>2)|((uint16_t)v[3]<<6);
    out[0]=v[0]&3u;out[1]=!!(v[0]&32u);out[2]=x;out[3]=x>>8;out[4]=y;out[5]=y>>8;
    out[6]=v[4];out[7]=v[5];out[8]=v[6];out[9]=(v[0]>>2)&7u;memcpy(out+10,v+7,4);return 1;
}
#endif
