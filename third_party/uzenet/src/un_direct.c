#include "un_client.h"
#include "un_recovery.h"
#include <string.h>
void ud_reset_peer(UrClient *c,unsigned peer){
    if(peer>=4)return;
    uint8_t mask=c->peers[peer].known?c->peers[peer].mask:(uint8_t)(1u<<peer);
    memset(&c->peers[peer],0,sizeof c->peers[peer]);
    memset(&c->oob.peer[peer],0,sizeof c->oob.peer[peer]);
    memset(&c->service.peer[peer],0,sizeof c->service.peer[peer]);
    memset(&c->pace.peer[peer],0,sizeof c->pace.peer[peer]);
    c->oob.peer[peer].mask=c->service.peer[peer].mask=mask;
    c->oob.pending&=(uint8_t)~mask;c->service.pending&=(uint8_t)~mask;
    if(c->oob.selected==peer+1)c->oob.selected=0;
    if(c->service.selected==peer+1)c->service.selected=0;
}
/* Pair-specific keys arrive only over the existing authenticated TLS stream.
 * Direction domain (2+sender) and the match ID bind every peer packet. */
int ud_offer(UrClient *c,const uint8_t t[68],uint32_t now) {
    if(un_recovery_enabled(c->config))return 0;
    if(c->protocol==UR_PROFILE_LIVE || c->protocol<UR_PROFILE || c->state<UC_BINDING || c->state>UC_READY || memcmp(t,"UNCP",4) || t[4]!=1 || t[5] ||
       t[6]>=4 || t[6]==c->peer || t[7]!=c->peer || memcmp(t+8,c->match,8) || t[17]!=c->history.width ||
       !t[16] || (t[16]&~c->history.all) || (t[16]&c->history.local) || !ur_get32(t+18) || !(t[22]|t[23]))return 0;
    UrPeer *p=&c->peers[t[6]];uint32_t generation=ur_get32(t+30);
    for(unsigned i=0;i<4;i++)if(i!=t[6] && c->peers[i].known && (c->peers[i].mask&t[16]))return 0;
    if(p->known){
        if(p->mask!=t[16]){uc_fail(c,7);return 0;}
        if(memcmp(p->key,t+34,32)){
            if(c->protocol!=UR_PROFILE_DROPIN || c->config[17]!=4 || generation<=p->generation){uc_fail(c,7);return 0;}
            ud_reset_peer(c,t[6]);p=&c->peers[t[6]];
        }
        if(generation<=p->generation)return 1;
    }
    if(!p->known){memset(p,0,sizeof(*p));memcpy(p->key,t+34,32);p->mask=t[16];p->known=1;}
    p->generation=generation;memcpy(p->candidate,t+18,6);p->token=0;p->send_at=now-100;return 1;
}
uint8_t ud_mask(const UrClient *c,uint32_t now) {
    uint8_t mask=0;
    if(c->state!=UC_READY)return 0;
    for(unsigned i=0;i<4;i++)if(c->peers[i].known && c->peers[i].active && now-c->peers[i].last_ok<300)mask|=1u<<i;
    return mask;
}
static int ud_seal(UrClient *c,UrPeer *p,uint8_t packet[48],uint8_t type,uint32_t frame,uint8_t mask,uint8_t width,const uint8_t *pads) {
    if(p->seq==UINT32_MAX){uc_fail(c,9);return 0;}
    return ur_seal(packet,p->key,(uint8_t)(2+c->peer),c->match,c->peer,type,++p->seq,frame,mask,width,pads);
}
int ud_receive(UrClient *c,const uint8_t *packet,size_t n,const uint8_t source[6],uint32_t now) {
    if(c->state<UC_WAITING || c->state>UC_READY || n<48 || (n>48 && !UR_IS_OOB(packet[4]) && c->protocol!=UR_PROFILE_REDUNDANT && !((c->protocol==UR_PROFILE || c->protocol==UR_PROFILE_READYSTART || c->protocol==UR_PROFILE_DROPIN) && UR_IS_INPUT(packet[4]) && c->history.width>2 && !(packet[23]>>6))) ||
       packet[5]>=4 || (packet[23]&63)!=c->history.width)return 0;
    UrPeer *p=&c->peers[packet[5]];uint8_t type=packet[4];uint32_t token=ur_get32(packet+18);
    if(!p->known || (type!=UD_PUNCH && type!=UD_PONG && type!=UD_INPUT && type!=UD_PACE && type!=UD_PACE_REPLY && type!=UD_OOB && type!=UD_OOB_ACK))return 0;
    /* Unproven sources cannot consume valid data sequences or carry inputs. */
    if((type==UD_INPUT || type==UD_PACE || type==UD_PACE_REPLY || type==UD_OOB || type==UD_OOB_ACK) &&
       (!p->active || memcmp(source,p->address,6) || packet[22]!=p->mask))return 0;
    if(type==UD_PONG && (!p->token || token!=p->token || memcmp(source,p->candidate,6) || now-p->probe_at>300))return 0;
    if((type==UD_PUNCH || type==UD_PONG) && packet[22])return 0;
    if(!ur_open(packet,n,p->key,(uint8_t)((UR_IS_OOB(type)?8:2)+packet[5]),c->match,
        UR_IS_OOB(type)?&c->oob.peer[packet[5]].replay:&p->replay))return 0;
    if(type==UD_OOB || type==UD_OOB_ACK){
        return uo_receive(c,packet[5],p->mask,type==UD_OOB_ACK,token,packet+24,now);
    }else if(type==UD_PACE || type==UD_PACE_REPLY){
        return up_receive(c,packet[5],p->mask,type==UD_PACE_REPLY,token,packet+24,now);
    }else if(type==UD_PUNCH){
        /* At most one pending equal-size reply; no immediate network work in
         * receive context. Validate a changed endpoint before using it. */
        p->reply_pending=1;p->reply_token=token;memcpy(p->reply_address,source,6);
        if(memcmp(source,p->candidate,6)){memcpy(p->candidate,source,6);p->token=0;p->send_at=now-100;}
    }else if(type==UD_PONG){
        if(p->send_at==p->probe_at)uc_rtt(&p->link,now-p->probe_at);
        memcpy(p->address,source,6);p->active=1;p->last_ok=now;p->token=0;
    }else if(c->state==UC_READY){
        if(!ur_merge_packet(&c->history,packet)){uc_fail(c,7);return 0;}
        if(token>=c->history.floor && token-c->history.floor<UR_WINDOW && token>=p->link.next)p->link.next=token+1;
    }
    return 1;
}
int ud_next(UrClient *c,unsigned peer,uint32_t now,uint8_t packet[48],uint8_t destination[6]) {
    if(peer>=4 || c->state<UC_WAITING || c->state>UC_READY)return 0;
    UrPeer *p=&c->peers[peer];if(!p->known)return 0;
    if(p->reply_pending){
        p->reply_pending=0;memcpy(destination,p->reply_address,6);
        return ud_seal(c,p,packet,UD_PONG,p->reply_token,0,c->history.width,0);
    }
    if(now-p->send_at<100)return 0;
    if(!p->token || now-p->probe_at>=300){p->probe_at=now;p->token=p->seq+1;}
    p->send_at=now;memcpy(destination,p->candidate,6);
    return ud_seal(c,p,packet,UD_PUNCH,p->token,0,c->history.width,0);
}
int ud_input(UrClient *c,unsigned peer,const uint8_t relay[48],uint32_t now,uint8_t packet[48],uint8_t destination[6]) {
    if(peer>=4 || !(ud_mask(c,now)&(1u<<peer)) || (relay[4]!=UR_INPUT && relay[4]!=UR_INPUT_DIRECT))return 0;
    UrPeer *p=&c->peers[peer];memcpy(destination,p->address,6);
    return ud_seal(c,p,packet,UD_INPUT,ur_get32(relay+18),c->history.local,relay[23],relay+24);
}
