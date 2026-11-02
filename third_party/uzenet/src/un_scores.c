#include "un_scores.h"
#include <string.h>
#include <limits.h>
static uint32_t le32(const uint8_t *p){return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
static uint32_t be32(const uint8_t *p){return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3];}
static void put32(uint8_t *p,uint32_t v){for(unsigned i=0;i<4;i++){p[i]=(uint8_t)v;v>>=8;}}
static UnScoreJob *find(UnScores *s,uint32_t id){for(unsigned i=0;i<USCORE_JOBS;i++)if(id && s->job[i].id==id)return &s->job[i];return 0;}
static void finish(UnScores *s,UnScoreJob *j,uint8_t state){
    j->state=state;if(s->active==j->id)s->active=0;
    if(s->snapshot==j->id){s->state=state;if(state!=USCORE_OK)s->count=0;}
}
void uscores_tick(UnScores *s,uint32_t now){
    for(unsigned i=0;i<USCORE_JOBS;i++){
        UnScoreJob *j=&s->job[i];if(!j->id || j->state!=USCORE_PENDING)continue;
        if((s->active==j->id && (uint32_t)(now-s->sent)>=6000) ||
           (s->active!=j->id && (uint32_t)(now-j->born)>=15000))finish(s,j,USCORE_TIMEOUT);
    }
}
void uscores_disconnect(UnScores *s){
    for(unsigned i=0;i<USCORE_JOBS;i++)if(s->job[i].id && s->job[i].state==USCORE_PENDING)finish(s,&s->job[i],USCORE_ERROR);
    s->state=USCORE_ERROR;s->count=0;
}
uint8_t uscores_command(UnScores *s,const uint8_t *p,uint8_t n,uint32_t now,uint8_t out[22]){
    uscores_tick(s,now);out[0]=USCORE_INVALID;
    if(!n){out[0]=USCORE_VERSION;out[1]=USCORE_ROWS;out[2]=USCORE_NAME;out[3]=USCORE_JOBS;return 4;}
    if((p[0]&0xf8)==USCORE_OPEN_PART){
        unsigned at=p[0]&7,bytes=at==6?2:3;
        if((at!=0 && at!=3 && at!=6) || n!=bytes+1 || (at && s->staged!=at))return 1;
        for(unsigned i=1;i<n;i++)if(p[i]<33 || p[i]>126)return 1;
        if(!at)s->staged=0;
        memcpy(s->staging+at,p+1,bytes);s->staged=(uint8_t)(at+bytes);
        if(s->staged==8){uint8_t full[9]={USCORE_OPEN};memcpy(full+1,s->staging,8);return uscores_command(s,full,9,now,out);}
        out[0]=USCORE_OK;return 1;
    }
    if((p[0]&USCORE_SUBMIT24) && n==4){
        uint8_t full[6]={USCORE_SUBMIT,p[0]&127,p[1],p[2],p[3],0};
        return uscores_command(s,full,6,now,out);
    }
    if(p[0]==USCORE_OPEN && n==9){
        for(unsigned i=1;i<9;i++)if(p[i]<33 || p[i]>126)return 1;
        if(!s->bound || memcmp(s->rom,p+1,8)){s->snapshot=0;s->count=0;s->state=USCORE_EMPTY;}
        memcpy(s->rom,p+1,8);s->bound=1;out[0]=USCORE_OK;return 1;
    }
    if(p[0]==USCORE_STATUS && n==5){
        UnScoreJob *j=find(s,le32(p+1));if(!j){out[0]=USCORE_EMPTY;return 1;}
        out[0]=j->state;out[1]=j->board;put32(out+2,j->value);out[6]=(uint8_t)j->rank;out[7]=(uint8_t)(j->rank>>8);return 8;
    }
    if(p[0]==USCORE_READ && n==6){
        if(!s->snapshot || le32(p+1)!=s->snapshot){out[0]=USCORE_EMPTY;return 1;}
        out[0]=s->state;if(s->state!=USCORE_OK)return 1;
        out[1]=s->count;out[2]=p[5];if(p[5]>=s->count){out[0]=USCORE_EMPTY;return 3;}
        UnScoreRow *r=&s->row[p[5]];out[3]=r->rank;put32(out+4,r->score);out[8]=r->length;
        memcpy(out+9,r->name,r->length);return (uint8_t)(9+r->length);
    }
    unsigned kind=p[0]==USCORE_SUBMIT?1:p[0]==USCORE_TOP?2:0;
    if(!kind || !s->bound || (kind==1?n!=6:n!=3) || (kind==2 && (!p[2] || p[2]>USCORE_ROWS)))return 1;
    uint32_t value=kind==1?le32(p+2):p[2];UnScoreJob *slot=0;
    for(unsigned i=0;i<USCORE_JOBS;i++){
        UnScoreJob *j=&s->job[i];
        if(j->id && j->state==USCORE_PENDING && j->kind==kind && j->board==p[1] && j->value==value && !memcmp(j->rom,s->rom,8)){slot=j;break;}
    }
    if(!slot){
        for(unsigned i=0;i<USCORE_JOBS;i++){
            UnScoreJob *j=&s->job[i];if(j->id && j->state==USCORE_PENDING)continue;
            if(!slot || j->id<slot->id)slot=j;
        }
        if(!slot){out[0]=USCORE_BUSY;return 1;}
        if(s->serial==UINT32_MAX){out[0]=USCORE_ERROR;return 1;}
        memset(slot,0,sizeof(*slot));slot->id=++s->serial;slot->value=value;slot->born=now;
        slot->kind=(uint8_t)kind;slot->board=p[1];slot->state=USCORE_PENDING;memcpy(slot->rom,s->rom,8);
    }
    if(kind==2){s->snapshot=slot->id;s->state=USCORE_PENDING;s->count=0;}
    out[0]=USCORE_PENDING;put32(out+1,slot->id);return 5;
}
int uscores_next(UnScores *s,uint32_t now,uint8_t out[USCORE_REQUEST_BYTES]){
    uscores_tick(s,now);if(s->active)return 0;UnScoreJob *best=0;
    for(unsigned i=0;i<USCORE_JOBS;i++){
        UnScoreJob *j=&s->job[i];if(j->id && j->state==USCORE_PENDING && (!best || j->id<best->id))best=j;
    }
    if(!best)return 0;
    out[0]=USCORE_CONTROL;out[1]=best->kind;put32(out+2,best->id);memcpy(out+6,best->rom,8);out[14]=best->board;put32(out+15,best->value);
    s->active=best->id;s->sent=now;return 1;
}
int uscores_receive(UnScores *s,const uint8_t *p,unsigned n){
    if(n<7)return 0;
    UnScoreJob *j=find(s,le32(p));
    if(!j || s->active!=j->id || j->state!=USCORE_PENDING || p[5]>USCORE_ERROR || p[6]!=j->board)return 0;
    if(j->kind==1){
        if(n!=13 || p[4]!=0x90 || be32(p+7)!=j->value)return 0;
        j->rank=(uint16_t)((p[11]<<8)|p[12]);finish(s,j,p[5]);return 1;
    }
    if(p[4]!=0x91 || n<8 || p[7]>j->value || p[7]>USCORE_ROWS || (p[5]!=USCORE_OK && p[7]))return 0;
    UnScoreRow rows[USCORE_ROWS];memset(rows,0,sizeof rows);unsigned at=8;
    for(unsigned i=0;i<p[7];i++){
        if(at+7>n || p[at] || !p[at+1] || p[at+1]>i+1 || (!i && p[at+1]!=1) || p[at+6]>USCORE_NAME)return 0;
        UnScoreRow *r=&rows[i];r->rank=p[at+1];r->score=be32(p+at+2);r->length=p[at+6];at+=7;
        if(at+r->length>n)return 0;
        if(i && r->rank!=(r->score==rows[i-1].score?rows[i-1].rank:i+1))return 0;
        for(unsigned k=0;k<r->length;k++)if(p[at+k]<32 || p[at+k]>126)return 0;
        memcpy(r->name,p+at,r->length);at+=r->length;
    }
    if(at!=n)return 0;
    if(s->snapshot==j->id){memcpy(s->row,rows,sizeof rows);s->count=p[7];}
    finish(s,j,p[5]);return 1;
}
