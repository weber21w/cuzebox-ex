#include "un_client.h"
#include "un_recovery.h"
#include <string.h>
void uc_rtt(UrLink *link,uint32_t elapsed) {
    uint16_t value=elapsed>65535?65535:(uint16_t)elapsed;
    if(!link->samples){link->rtt=value;link->jitter=0;}
    else{
        int32_t delta=(int32_t)value-link->rtt;
        link->rtt+=delta/8;
        if(delta<0)delta=-delta;
        link->jitter+=(delta-link->jitter)/4;
    }
    if(link->samples<65535)link->samples++;
}
void uc_reset(UrClient *c) {volatile uint8_t *p=(volatile uint8_t*)c;size_t n=sizeof(*c);while(n--)*p++=0;}
void uc_restart(UrClient *c){UrFailure saved=c->failure;uc_reset(c);c->failure=saved;}
void uc_uart_metrics(UrClient *c,uint32_t now,unsigned rx,unsigned tx,unsigned queued,int paused){
    UrMetrics *m=&c->metrics;m->uart_rx+=rx;m->uart_tx+=tx;
    if(m->paused)m->paused_ms+=now-m->pause_at;
    m->pause_at=now;m->paused=!!paused;
    m->uart_queued=queued>255?255:queued;
    if(queued>m->uart_peak)m->uart_peak=queued>65535?65535:queued;
}
void uc_note_failure(UrClient *c,uint8_t source,uint8_t reason,uint8_t peers){
    if(c->failure.source)return;
    c->failure.source=source;c->failure.reason=reason;
    c->failure.stage=c->state;c->failure.peers=peers;
}
void uc_fail(UrClient *c,uint8_t error) {
    uc_note_failure(c,1,error,0);
    /* Retain status for the AVR, erase the traffic secret immediately. */
    volatile uint8_t *p=c->key;unsigned n=sizeof(c->key);while(n--)*p++=0;
    for(unsigned i=0;i<4;i++){p=c->peers[i].key;n=32;while(n--)*p++=0;c->peers[i].known=0;}
    c->state=UC_FAILED;c->error=error;
}
int uc_begin(UrClient *c,const uint8_t cfg[19],const uint8_t nonce[4],uint32_t now,uint8_t request[25]) {
    if(c->state!=UC_IDLE || !cfg[16] || cfg[16]>4 || cfg[17]<2 || cfg[17]>4 || cfg[16]>cfg[17] ||
        ((cfg[18]&~UR_WIDTH_FLAGS)<1 || (cfg[18]&~UR_WIDTH_FLAGS)>((cfg[18]&UR_REDUNDANT)?2:4)) ||
        (UR_IS_READYSTART(cfg[18]) ? (cfg[16]!=1 || cfg[17]!=4 || (cfg[18]&UR_WIDTH_FLAGS)!=UR_READYSTART) :
        (((cfg[18]&UR_LOBBY) && (cfg[16]!=1 || cfg[17]!=4 || (cfg[18]!=(UR_LOBBY|1) && !UR_IS_LIVE(cfg[18])))) ||
       ((cfg[18]&UR_DROPIN) && !UR_DROPIN_CONFIG(cfg)))))return 0;
    uc_restart(c);memcpy(c->config,cfg,19);memcpy(c->nonce,nonce,4);c->state=UC_TICKET;c->born=now;
    request[0]=UR_OPEN;request[1]=UR_IS_LIVE(cfg[18])?UR_PROFILE_LIVE:UR_IS_READYSTART(cfg[18])?UR_PROFILE_READYSTART:(cfg[18]&UR_DROPIN)?UR_PROFILE_DROPIN:(cfg[18]&UR_LOBBY)?UR_PROFILE_LOBBY:((cfg[18]&UR_REDUNDANT)?UR_PROFILE_REDUNDANT:UR_PROFILE);memcpy(request+2,cfg,19);memcpy(request+21,nonce,4);return 1;
}
int uc_ticket(UrClient *c,const uint8_t t[68],uint32_t now) {
    unsigned bits=0,i;
    if(c->state!=UC_TICKET || memcmp(t,"UNCT",4) || t[4]<UR_PROFILE || t[4]>UR_PROFILE_LIVE || memcmp(t+6,c->nonce,4))return 0;
    if(t[5]){uc_fail(c,t[5]);return 1;}
    for(i=0;i<4;i++)bits+=(t[23]>>i)&1;
    if((t[4]==UR_PROFILE_LIVE)!=UR_IS_LIVE(c->config[18]) ||
       (t[4]==UR_PROFILE_READYSTART)!=UR_IS_READYSTART(c->config[18]) ||
       ((t[4]==UR_PROFILE_LOBBY || t[4]==UR_PROFILE_READYSTART || t[4]==UR_PROFILE_LIVE)!=!!(c->config[18]&UR_LOBBY)) ||
       ((t[4]==UR_PROFILE_DROPIN || t[4]==UR_PROFILE_READYSTART)!=!!(c->config[18]&UR_DROPIN)) ||
       (t[4]==UR_PROFILE_REDUNDANT)!=!!(c->config[18]&UR_REDUNDANT) || t[22]>3 || bits!=c->config[16] ||
       t[24]!=((1u<<c->config[17])-1) || t[25]!=(c->config[18]&~UR_WIDTH_FLAGS) ||
       !ur_history_init(&c->history,t[24],t[23],t[25]) || !ur_get32(t+26) || !(t[30]|t[31]) ||
       !ur_get32(t+64) || ur_get32(t+64)>3600){uc_fail(c,6);return 1;}
    memcpy(c->match,t+10,8);memcpy(c->seed,t+18,4);memcpy(c->endpoint,t+26,6);memcpy(c->key,t+32,32);
    c->peer=t[22];c->protocol=t[4];c->lease_ms=ur_get32(t+64)*1000;c->born=c->last_rx=now;c->last_tx=now-250;
    if(c->protocol>=UR_PROFILE_LOBBY)for(i=0;i<4;i++)c->oob.peer[i].mask=c->service.peer[i].mask=1u<<i;
    c->state=UC_BINDING;return 1;
}
int uc_receive(UrClient *c,const uint8_t *p,size_t n,uint32_t now) {
    uint32_t frame;
    if(c->state<UC_BINDING || c->state>UC_READY || n<48 || (n>48 && !UR_IS_OOB(p[4]) && c->protocol!=UR_PROFILE_REDUNDANT && !((c->protocol==UR_PROFILE || c->protocol==UR_PROFILE_READYSTART || c->protocol==UR_PROFILE_DROPIN) && UR_IS_INPUT(p[4]) && c->history.width>2 && !(p[23]>>6))) ||
       p[5]!=c->peer || (p[23]&63)!=c->history.width ||
       !ur_open(p,n,c->key,UR_IS_OOB(p[4])?7:1,c->match,UR_IS_OOB(p[4])?&c->oob.replay:&c->replay))return 0;
    frame=ur_get32(p+18);c->last_rx=now;
    switch(p[4]){
    case UR_RECOVERY_STATUS:{
        uint16_t gen=(uint16_t)p[28]|((uint16_t)p[29]<<8);
        if(!un_recovery_enabled(c->config) || !gen || p[24]>15 || p[25]>15 ||
           (p[24]&p[25]) || (p[26]&~p[25]) || p[30]>3 || p[31] ||
           (p[24]?(p[27]>3 || !(p[24]&(1u<<p[27]))):p[27]!=255))return 0;
        if(gen<c->recovery.generation || ur_get32(p+14)<c->recovery.received)return 1;
        c->recovery.received=ur_get32(p+14);c->lobby.roster_seq=ur_get32(p+14);
        uint8_t changed=gen!=c->recovery.generation;
        if(changed){
            c->recovery.generation=gen;c->recovery.game_ack=c->recovery.ack=0;
            c->recovery.forwarded=0;c->recovery.dirty=1;
            memset(c->snapshots.received,0,sizeof c->snapshots.received);
            memset(c->snapshots.pending,0,sizeof c->snapshots.pending);
            c->snapshots.unread&=(uint16_t)~15u;
            /* Reset reliable consumption on both participants at this fence,
             * retaining outer anti-replay counters. Every message has gen16. */
            UrReplay replay=c->oob.replay;uint32_t seq=c->oob.seq;
            memset(&c->oob,0,sizeof c->oob);memset(&c->service,0,sizeof c->service);
            c->oob.replay=replay;c->oob.seq=seq;
            for(unsigned i=0;i<4;i++)c->oob.peer[i].mask=c->service.peer[i].mask=1u<<i;
            c->services.dirty|=c->history.local;
        }
        c->lobby.active=p[24];c->recovery.reserved=p[25];c->recovery.returning=p[26];
        c->lobby.host=p[27];c->recovery.countdown=p[30];
        /* A generation fence can discard an in-flight reliable transfer.
         * Retained setup/config records owned by us must be offered again. */
        for(unsigned i=4;changed && i<13;i++)if(c->snapshots.valid&(1u<<i)){
            unsigned owner=i>=8?(p[27]<4?1u<<p[27]:0):un_recovery_owner(p[24],p[27],i&3u);
            if(owner==c->history.local)c->snapshots.dirty|=1u<<i;
        }
        break;
    }
    case UR_ROSTER:
        if(c->protocol<UR_PROFILE_LOBBY || (!p[24] && !un_recovery_enabled(c->config)) || p[24]>15 || p[25]>15 ||
           (p[24]&p[25]) || p[26]>2 || (p[27]&~p[24]))return 0;
        if(ur_get32(p+14)<=c->lobby.roster_seq)return 1;
        c->lobby.roster_seq=ur_get32(p+14);
        if(c->protocol==UR_PROFILE_READYSTART){
            if(p[25] || (p[26]!=0 && p[26]!=2) || !(p[24]&c->history.local) ||
               (c->lobby.phase==2 && (p[24]!=c->lobby.active || p[26]!=2))){uc_fail(c,7);return 0;}
            c->history.all=p[24];
        }
        /* Membership and reliable name packets can cross in flight. Refresh
         * local metadata throughout initial setup, even if an early name was
         * acknowledged before its sender appeared in the receiver's roster. */
        if(c->lobby.active!=p[24] || (c->protocol==UR_PROFILE_READYSTART &&
           (p[26]!=2 || !c->released)))c->services.dirty|=c->history.local;
        if(c->protocol==UR_PROFILE_DROPIN){
            uint8_t capacity=c->config[17]==4?15:3;
            uint16_t epoch=(uint16_t)p[28]|((uint16_t)p[29]<<8);
            if((p[24]|p[25])&~capacity)return 0;
            if(p[31]&~capacity)return 0;
            if(p[26] && (!c->join.reset || (capacity==15 && epoch!=c->lobby.epoch))){
                uint8_t local=c->history.local,width=c->history.width;
                if(!(p[24]&local))break; /* An arrival after the cutoff waits. */
                if(capacity==15)for(unsigned peer=0;peer<4;peer++)
                    if(p[31]&(1u<<peer))ud_reset_peer(c,peer);
                if(!ur_history_init(&c->history,p[24],local,width))return 0;
                c->history.floor=c->released=c->acked=c->next=frame;
                memset(&c->join,0,sizeof c->join);
                c->join.base=frame;c->join.reset=1;
                c->wanted=0;memset(&c->pace,0,sizeof c->pace);
                memset(&c->services,0,sizeof c->services);
            }else if(!p[26] && !c->join.reset)c->history.all=p[24];
        }
        if(c->protocol==UR_PROFILE_LIVE){
            if(!(p[24]&c->history.local) || p[31]>3 || !(p[24]&(1u<<p[31])) || p[26]>1)return 0;
            uint16_t epoch=(uint16_t)p[28]|((uint16_t)p[29]<<8);
            if(epoch!=c->lobby.epoch){
                memset(c->live.sequence,0,sizeof c->live.sequence);
                memset(c->live.unread,0,sizeof c->live.unread);
            }
            c->lobby.host=p[31];
        }
        c->lobby.active=p[24];c->lobby.pending=p[25];c->lobby.phase=p[26];c->lobby.ready=p[27];
        c->lobby.epoch=(uint16_t)p[28]|((uint16_t)p[29]<<8);
        if(c->lobby.waiting && frame==c->lobby.request){c->lobby.waiting=0;c->lobby.ack=frame;c->lobby.error=p[30];}
        break;
    case UR_OOB_FROM:case UR_OOB_ACK_FROM:
        return uo_receive(c,p[22]>>4,p[22]&15,p[4]==UR_OOB_ACK_FROM,frame,p+24,now);
    case UR_PACE_FROM:case UR_PACE_REPLY_FROM:
        return up_receive(c,p[22]>>4,p[22]&15,p[4]==UR_PACE_REPLY_FROM,frame,p+24,now);
    case UR_CHALLENGE:
        memcpy(c->challenge,p+24,8);c->challenged=1;c->challenge_at=now;c->last_tx=now-250;break;
    case UR_BOUND:if(c->state==UC_BINDING)c->state=UC_WAITING;break;
    case UR_READY:c->state=UC_READY;break;
    case UR_FRAME:
        if(c->protocol==UR_PROFILE_LIVE){
            uint8_t mask=p[22],key=p[26];uint16_t epoch=(uint16_t)p[24]|((uint16_t)p[25]<<8);
            if(!mask || mask>8 || (mask&(mask-1)) || !(mask&c->lobby.active) || (mask&c->history.local) || key>=16 || epoch!=c->lobby.epoch)return 0;
            unsigned seat=0;while(!(mask&(1u<<seat)))seat++;
            unsigned index=seat*16+key;
            if(frame>c->live.sequence[index]){
                c->live.sequence[index]=frame;memcpy(c->live.value[index],p+27,4);c->live.unread[seat]|=1u<<key;
            }
            break;
        }
        if((p[22]&c->history.local)==c->history.local){
            UrFrame *f=ur_frame(&c->history,frame,0);
            if(f && f->pending && f->transmitted==1)uc_rtt(&c->relay,now-f->sent);
        }
        if(p[22]>15 || !ur_merge_packet(&c->history,p))uc_fail(c,7);
        else if(frame>=c->history.floor && frame-c->history.floor<UR_WINDOW){
            if(frame>=c->relay.next)c->relay.next=frame+1;
            for(unsigned i=0;i<4;i++)if(c->peers[i].known && (p[22]&c->peers[i].mask)==c->peers[i].mask &&
                frame>=c->peers[i].link.next)c->peers[i].link.next=frame+1;
        }
        break;
    case UR_RELEASED:if(frame<=c->history.floor && frame>c->acked)c->acked=frame;break;
    case UR_ERROR:if(p[24]==2)uc_fail(c,7);break;
    case UR_END:uc_fail(c,8);break;
    default:return 0;
    }
    return 1;
}
int uc_submit(UrClient *c,uint32_t frame,const uint8_t pads[8]) {
    if(c->state!=UC_READY || frame<c->released || !ur_submit(&c->history,frame,pads))return 0;
    /* The game's own actual input is known immediately. Relay retention ACK
     * remains separate so direct play never waits on a server round trip. */
    ur_frame(&c->history,frame,0)->mask|=c->history.local;
    if(frame>=c->next){c->next=frame+1;c->metrics.input_frames++;}
    return 1;
}
UrFrame *uc_fetch(UrClient *c,uint32_t frame) {
    UrFrame *f;
    if(c->state!=UC_READY || frame<c->released || !(f=ur_frame(&c->history,frame,1)))return 0;
    if(!c->wanted || c->want_frame!=frame){c->want_frame=frame;c->wanted=1;c->fetch_at=0;}
    return f;
}
int uc_release(UrClient *c,uint32_t next) {
    if(c->state!=UC_READY || next<c->released || next-c->history.floor>UR_WINDOW)return 0;
    for(uint32_t n=c->released;n<next;n++){
        UrFrame *f=ur_frame(&c->history,n,0);if(!f || f->mask!=c->history.all)return 0;
    }
    c->released=next;return 1;
}
static int uc_packet(UrClient *c,uint8_t p[48],uint8_t type,uint32_t frame,uint8_t mask,uint8_t width,const uint8_t pads[8],uint32_t now) {
    if(c->seq==UINT32_MAX){uc_fail(c,9);return 0;}
    c->last_tx=now;
    return ur_seal(p,c->key,0,c->match,c->peer,type,++c->seq,frame,mask,width,pads);
}
int uc_next(UrClient *c,uint32_t now,uint8_t p[48]) {
    unsigned i,pass;
    if(c->state==UC_TICKET){if(now-c->born>10000)uc_fail(c,10);return 0;}
    if(c->state<UC_BINDING || c->state>UC_READY)return 0;
    if(now-c->last_rx>(un_recovery_enabled(c->config)?25000u:15000u) || (c->protocol!=UR_PROFILE_LIVE && now-c->born>=c->lease_ms)){uc_fail(c,11);return 0;}
    if(now-c->last_tx<1)return 0;
    if(c->state>=UC_WAITING && now-c->last_rx>=1000){
        c->state=UC_BINDING;c->challenged=0;c->last_tx=now-250;
    }
    if(c->state==UC_BINDING){
        if(now-c->last_tx<250)return 0;
        if(c->challenged && now-c->challenge_at>1500)c->challenged=0;
        return uc_packet(c,p,c->challenged?UR_CONFIRM:UR_HELLO,0,0,c->history.width,c->challenged?c->challenge:0,now);
    }
    if(c->state==UC_READY){
        if(un_recovery_enabled(c->config) && c->recovery.ack==c->recovery.generation &&
           c->recovery.ack && (c->recovery.dirty || ((c->recovery.returning&c->history.local) &&
           !c->recovery.countdown && now-c->recovery.ack_at>=250))){
            uint8_t data[8]={1,c->recovery.ack,c->recovery.ack>>8};
            c->recovery.ack_at=now;c->recovery.dirty=0;
            return uc_packet(c,p,UR_RECOVERY_REQUEST,0,0,c->history.width,data,now);
        }
        if(c->protocol==UR_PROFILE_LIVE){
            if(c->lobby.waiting && now-c->release_at>=100){
                uint8_t data[8]={c->lobby.action,c->lobby.expected,c->lobby.expected>>8};c->release_at=now;
                return uc_packet(c,p,UR_LOBBY_REQUEST,c->lobby.request,0,c->history.width,data,now);
            }
            if(c->live.pending && c->lobby.phase && (c->lobby.active&c->history.local)){
                uint8_t data[8]={c->lobby.epoch,c->lobby.epoch>>8};memcpy(data+2,c->live.out,5);
                c->live.pending=0;if(c->live.next==UINT32_MAX){uc_fail(c,9);return 0;}
                return uc_packet(c,p,UR_INPUT,++c->live.next,c->history.local,c->history.width,data,now);
            }
            return now-c->last_tx>=250?uc_packet(c,p,UR_PING,0,0,c->history.width,0,now):0;
        }
        if(c->protocol==UR_PROFILE_DROPIN && (c->join.want || c->join.ready || !(c->lobby.active&c->history.local)) && now-c->join.polled>=100){
            c->join.polled=now;
            return uc_packet(c,p,UR_ADMIT,c->join.base,c->join.ready?c->history.local:0,c->history.width,0,now);
        }
        if(c->protocol==UR_PROFILE_LOBBY || (c->protocol==UR_PROFILE_READYSTART &&
           (c->lobby.phase!=2 || c->lobby.waiting))){
            if(now-c->last_tx<(c->lobby.waiting?100u:250u))return 0;
            uint8_t data[8]={c->lobby.waiting?c->lobby.action:0,c->lobby.expected,c->lobby.expected>>8};
            return uc_packet(c,p,UR_LOBBY_REQUEST,c->lobby.waiting?c->lobby.request:0,0,c->history.width,data,now);
        }
        if(c->protocol==UR_PROFILE_DROPIN && !(c->lobby.active&c->history.local))
            return now-c->last_tx>=250?uc_packet(c,p,UR_PING,0,0,c->history.width,0,now):0;
        /* Released game inputs remain in the outbox until the relay has its
         * recovery copy. ACK loss must never discard an unsent input. */
        while(c->history.floor<c->released){
            UrFrame *f=ur_frame(&c->history,c->history.floor,0);
            if(!f || f->pending)break;
            c->history.floor++;
        }
        /* Fresh samples before recovery/control traffic. At 60 Hz there is
         * ample space between samples; the 64-frame window also bounds a
         * burst, so an application cannot overwrite unacknowledged input. */
        for(pass=0;pass<2;pass++){
        if(pass && now-c->last_tx<5)return 0;
        if(pass && c->history.floor>c->acked && now-c->release_at>=40){
            c->release_at=now;return uc_packet(c,p,UR_RELEASE,c->history.floor,0,c->history.width,0,now);
        }
        if(pass && c->wanted && now-c->fetch_at>=40){
            UrFrame *f=ur_frame(&c->history,c->want_frame,0);
            c->fetch_at=now;
            if(f && f->mask!=c->history.all)return uc_packet(c,p,UR_FETCH,c->want_frame,0,c->history.width,0,now);
        }
        for(i=0;i<UR_WINDOW;i++){
            /* In redundant mode drain fresh input newest-first, so a UART
             * burst becomes one current packet instead of stale singles. */
            unsigned index=(c->scan+((!pass && c->protocol==UR_PROFILE_REDUNDANT)?UR_WINDOW-1-i:i))&(UR_WINDOW-1);
            UrFrame *f=&c->history.frames[index];uint8_t pads[32];
            if(!f->valid || !f->pending || f->frame<c->history.floor || f->frame-c->history.floor>=UR_WINDOW ||
               (pass==0?f->transmitted:(f->transmitted && now-f->sent<25)))continue;
            uint8_t width=ur_samples(&c->history,f->frame,c->history.local,pads,c->protocol==UR_PROFILE_REDUNDANT);
            if(f->transmitted && c->retries<65535)c->retries++;
            for(unsigned n=0;n<=(width>>6);n++){
                UrFrame *sent=&c->history.frames[(index-n)&(UR_WINDOW-1)];sent->sent=now;
                if(sent->transmitted<255)sent->transmitted++;
            }
            c->scan=(uint8_t)((index+1)&(UR_WINDOW-1));
            uint8_t direct=ud_mask(c,now);
            return uc_packet(c,p,direct?UR_INPUT_DIRECT:UR_INPUT,f->frame,
                             c->history.local|(direct<<4),width,pads,now);
        }
        }
    }
    return now-c->last_tx>=250?uc_packet(c,p,UR_PING,0,0,c->history.width,0,now):0;
}
