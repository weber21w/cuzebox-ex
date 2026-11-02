#include "un_client.h"
#include "un_snapshot.h"
#include "un_recovery.h"
#include <string.h>
static uint8_t seat_mask(const UrClient *c,unsigned i){
    return c->peers[i].known?c->peers[i].mask:
        (c->pace.peer[i].mask?c->pace.peer[i].mask:
         (c->oob.peer[i].mask?c->oob.peer[i].mask:c->service.peer[i].mask));
}
static uint8_t pending(const UrOob *o){
    uint8_t mask=0;
    for(unsigned i=0;i<4;i++)if(o->peer[i].rx_used &&
        o->peer[i].rx_count==o->peer[i].rx_total)mask|=1u<<i;
    return mask;
}
uint8_t uo_pending(const UrClient *c){return pending(&c->oob);}
uint8_t uo_internal_pending(const UrClient *c){return pending(&c->service);}
static uint8_t write_lane(UrClient *c,UrOob *o,const uint8_t *p,uint8_t n,uint32_t now){
    if(c->state!=UC_READY || n<7 || n>24)return 0;
    uint16_t id=(uint16_t)p[0]|((uint16_t)p[1]<<8);
    unsigned count=n-6,offset=p[5];uint8_t peers=0,covered=0;
    if(!id || !p[2] || (p[2]&~c->history.all) || (p[2]&c->history.local) ||
       !p[4] || p[4]>UO_MESSAGE_MAX || offset+count>p[4])return 0;
    if(id==o->id){
        if(p[2]!=o->dest || p[3]!=o->channel || p[4]!=o->total)return 0;
        if(offset<o->uploaded)return offset+count<=o->uploaded && !memcmp(o->data+offset,p+6,count);
        if(offset!=o->uploaded || o->uploaded==o->total)return 0;
    }else{
        if(o->id==65535 || id!=o->id+1 || offset)return 0;
        if(o->pending || o->uploaded!=o->total)return 2;
        for(unsigned i=0;i<4;i++)if(i!=c->peer){
            uint8_t mask=seat_mask(c,i);
            if(mask&p[2]){peers|=1u<<i;covered|=mask;
                if(o->peer[i].tx_id==UINT32_MAX){uc_fail(c,9);return 0;}}
        }
        if((covered&p[2])!=p[2])return 2; /* membership still arriving */
        o->id=id;o->dest=p[2];o->channel=p[3];o->total=p[4];o->uploaded=0;o->pending=peers;
        for(unsigned i=0;i<4;i++)if(peers&(1u<<i)){
            UoPeer *v=&o->peer[i];v->tx_id++;v->tx_offset=v->tx_sent=0;
            v->retry_ms=100;v->send_at=now;
        }
    }
    memcpy(o->data+offset,p+6,count);o->uploaded+=count;return 1;
}
uint8_t uo_write(UrClient *c,const uint8_t *p,uint8_t n,uint32_t now){
    return write_lane(c,&c->oob,p,n,now);
}
uint8_t uo_internal_send(UrClient *c,const uint8_t *data,uint8_t n,uint32_t now){
    UrOob *o=&c->service;
    if(!n || n>UO_MESSAGE_MAX || o->id==65535)return 0;
    if(o->pending || o->uploaded!=o->total)return 2;
    uint8_t dest=(c->protocol==UR_PROFILE_LOBBY?c->lobby.active:c->history.all)&~c->history.local;
    if(!dest)return 1;
    uint16_t id=o->id+1;uint8_t p[24]={id,id>>8,dest,0,n,0};
    for(unsigned offset=0;offset<n;){
        unsigned count=n-offset;if(count>UO_WRITE_MAX)count=UO_WRITE_MAX;
        p[5]=offset;memcpy(p+6,data+offset,count);
        uint8_t result=write_lane(c,o,p,count+6,now);
        if(result!=1)return result;
        offset+=count;
    }
    return 1;
}
uint8_t uo_read(UrClient *c,const uint8_t p[3],uint8_t reply[24]){
    unsigned peer=p[0];uint8_t pending=uo_pending(c);
    if(peer>4 || !p[2] || p[2]>UO_READ_MAX)return 0;
    if(peer==4){
        if(p[1])return 0;
        if(c->oob.selected)peer=c->oob.selected-1;
        else for(unsigned k=0;k<4;k++){
            unsigned i=(c->oob.read_scan+k)&3;
            if(pending&(1u<<i)){peer=i;c->oob.selected=i+1;break;}
        }
    }
    if(peer==4 || !(pending&(1u<<peer))){reply[0]=4;return 1;}
    const UoPeer *v=&c->oob.peer[peer];
    if(p[1]>=v->rx_total)return 0;
    unsigned n=v->rx_total-p[1];if(n>p[2])n=p[2];
    reply[0]=peer;ur_put32(reply+1,v->rx_id);reply[5]=v->mask;
    reply[6]=v->rx_dest;reply[7]=v->rx_channel;reply[8]=v->rx_total;reply[9]=p[1];
    memcpy(reply+10,v->data+p[1],n);return 10+n;
}
static int ack_lane(UrOob *o,unsigned peer,uint32_t id){
    if(peer>=4 || !id)return 0;
    UoPeer *v=&o->peer[peer];
    if(id==v->consumed)return 1;
    if(!v->rx_used || id!=v->rx_id || v->rx_count!=v->rx_total)return 0;
    v->consumed=id;v->rx_used=0;o->read_scan=(peer+1)&3;
    if(o->selected==peer+1)o->selected=0;
    return 1;
}
int uo_ack(UrClient *c,unsigned peer,uint32_t id){return ack_lane(&c->oob,peer,id);}
int uo_internal_ack(UrClient *c,unsigned peer,uint32_t id){return ack_lane(&c->service,peer,id);}
static void acknowledge(UoPeer *v,uint32_t id,const uint8_t *p,uint8_t count){
    v->ack_pending=1;v->ack_id=id;memset(v->ack,0,8);
    v->ack[0]=p[0];v->ack[1]=p[1];v->ack[2]=count;v->ack[4]=p[4];v->ack[5]=p[5];
}
/* Snapshot stream lane 2: epoch in bytes 2/3, one or two complete 11-byte
 * car records in bytes 8..29. It shares authentication/replay protection
 * with OOB, but has no message ACK, fragmentation, or reliable-lane state.
 * Per-car sequence checks reject reordered state without rejecting a fresh
 * update for a different car. This lane is exclusive to snapshot lobbies. */
static int snapshot_receive(UrClient *c,unsigned peer,uint8_t source,uint32_t id,const uint8_t *p,uint32_t now){
    unsigned timed=un_snapshot_timed(c->config),offset=timed?4u:0u,bytes=timed?11u:p[1];
    if(c->protocol!=UR_PROFILE_LOBBY || !un_snapshot_enabled(c->config) || c->lobby.phase!=2 ||
       p[0]!=0xce || (timed?p[1]!=19:(p[1]!=11 && p[1]!=22)) || p[6] || p[7] ||
       ((uint16_t)p[2]|((uint16_t)p[3]<<8))!=c->lobby.epoch)return 0;
    unsigned seen=0;
    for(unsigned at=0;at<bytes;at+=11){
        const uint8_t *v=p+8+offset+at;unsigned car=v[0]&3u,bit=1u<<car;
        if((v[0]&0xe0) || (v[0]>>2)>4 || v[3]>15 || v[6]>=237 ||
           (seen&bit) || (un_snapshot_jnb(c->config)?
             (c->history.local==1u?(source!=bit || source==1u):source!=1u):
             source!=((c->lobby.active&bit)?bit:1u)))return 0;
        seen|=bit;
    }
    for(unsigned at=p[1];at<24;at++)if(p[8+at])return 0;
    uint32_t sequence=timed?ur_get32(p+23):0;
    if(timed && !sequence)return 0;
    if(c->snapshots.epoch!=c->lobby.epoch){
        memset(&c->snapshots,0,sizeof c->snapshots);c->snapshots.epoch=c->lobby.epoch;
    }
    if(timed){
        uint32_t old=c->snapshots.link_rx[peer];
        if(!old){c->snapshots.link_rx[peer]=c->snapshots.link_base[peer]=sequence;c->snapshots.link_seen[peer]=1;}
        else if(sequence>old){
            uint32_t delta=sequence-old;c->metrics.gaps+=delta-1;
            c->snapshots.link_seen[peer]=delta>=64?1:(c->snapshots.link_seen[peer]<<delta)|1;
            c->snapshots.link_rx[peer]=sequence;
        }else if(old-sequence<64){
            uint64_t bit=(uint64_t)1<<(old-sequence);
            if(sequence>=c->snapshots.link_base[peer] && !(c->snapshots.link_seen[peer]&bit) && c->metrics.gaps)c->metrics.gaps--;
            c->snapshots.link_seen[peer]|=bit;
        }
    }
    c->metrics.stream_rx++;
    for(unsigned at=0;at<bytes;at+=11){
        const uint8_t *v=p+8+offset+at;unsigned car=v[0]&3u;
        if(id<=c->snapshots.received[car]){c->metrics.reordered++;continue;}
        uint32_t tick=timed?ur_get32(p+8):0;
        if(timed && c->snapshots.received[car] && (int32_t)(tick-c->snapshots.captured[car])<=0){c->metrics.reordered++;continue;}
        unsigned index=car+((un_snapshot_jnb(c->config) && c->history.local==1u)?4u:0u);
        uint8_t *out=c->snapshots.data[index];
        uint16_t x=v[1]|((uint16_t)(v[2]&3u)<<8);
        uint16_t y=(v[2]>>2)|((uint16_t)v[3]<<6);
        out[0]=car;out[1]=0;out[2]=x;out[3]=x>>8;out[4]=y;out[5]=y>>8;
        out[6]=v[4];out[7]=v[5];out[8]=v[6];out[9]=v[0]>>2;
        memcpy(out+10,v+7,4);c->snapshots.received[car]=id;
        if(c->snapshots.unread&(1u<<index))c->metrics.replaced++;
        c->snapshots.captured[car]=tick;c->metrics.received_at[car]=now;
        c->snapshots.valid|=1u<<index;c->snapshots.unread|=1u<<index;
    }
    return 1;
}
int uo_receive(UrClient *c,unsigned peer,uint8_t mask,int ack,uint32_t id,const uint8_t *p,uint32_t now){
    if(un_recovery_enabled(c->config) && p[5]==3){
        uint16_t epoch=(uint16_t)p[2]|((uint16_t)p[3]<<8),gen=(uint16_t)p[6]|((uint16_t)p[7]<<8);
        uint8_t record[14];
        if(ack || c->state!=UC_READY || c->lobby.phase==1 || p[0]!=0xce || p[1]!=11 ||
           p[4]!=c->history.local || epoch!=c->lobby.epoch || gen!=c->recovery.generation ||
           !un_snapshot_recovery_unpack(record,p+8))return 0;
        for(unsigned i=19;i<32;i++)if(p[i])return 0;
        unsigned car=record[0],bit=1u<<car;
        if(id<=c->snapshots.received[car])return 1;
        if(c->snapshots.epoch!=epoch){memset(&c->snapshots,0,sizeof c->snapshots);c->snapshots.epoch=epoch;}
        memcpy(c->snapshots.data[car],record,14);c->snapshots.received[car]=id;
        c->snapshots.valid|=bit;c->snapshots.unread|=bit;
        c->metrics.received_at[car]=now;c->metrics.stream_rx++;return 1;
    }
    UrOob *o=p[5]?&c->service:&c->oob;
    if(c->state!=UC_READY || peer>=4 || peer==c->peer || !id || !mask ||
       (mask&~c->history.all) || (mask&c->history.local) ||
       !p[1] || p[1]>UO_MESSAGE_MAX || p[5]>2 ||
       (un_recovery_enabled(c->config)?((uint16_t)p[6]|((uint16_t)p[7]<<8))!=c->recovery.generation:(p[6] || p[7])))return 0;
    UoPeer *v=&o->peer[peer];uint8_t known=seat_mask(c,peer);
    if((known && known!=mask) || (v->mask && v->mask!=mask))return 0;
    for(unsigned i=0;i<4;i++)if(i!=peer && (seat_mask(c,i)&mask))return 0;
    if(p[5]==2){
        if(ack || !p[4] || (p[4]&~c->history.local) || (mask&c->lobby.active)!=mask)return 0;
        return snapshot_receive(c,peer,mask,id,p,now);
    }
    if(ack){
        if(p[3] || !(o->pending&(1u<<peer)) || o->uploaded!=o->total || id!=v->tx_id ||
           p[0]!=o->channel || p[1]!=o->total || p[4]!=(o->dest&mask) ||
           (p[2]!=v->tx_offset && p[2]!=v->tx_sent))return 0;
        if(p[2]>v->tx_offset){v->tx_offset=p[2];v->retry_ms=100;v->send_at=now-100;}
        if(v->tx_offset==o->total)o->pending&=(uint8_t)~(1u<<peer);
    }else{
        unsigned end=(unsigned)p[2]+p[3];
        if(!p[3] || p[3]>UO_FRAGMENT || end>p[1] || !p[4] || (p[4]&~c->history.local))return 0;
        for(unsigned i=p[3];i<UO_FRAGMENT;i++)if(p[8+i])return 0;
        if(id==v->rx_id){
            if(p[0]!=v->rx_channel || p[1]!=v->rx_total || p[4]!=v->rx_dest)return 0;
            if(p[2]<v->rx_count && (end>v->rx_count || memcmp(v->data+p[2],p+8,p[3])))return 0;
        }else{
            if(v->rx_id==UINT32_MAX || id!=v->rx_id+1)return 0;
            if(v->rx_used || p[2]){acknowledge(v,id,p,0);v->mask=mask;return 1;}
            v->rx_id=id;v->rx_total=p[1];v->rx_channel=p[0];v->rx_dest=p[4];v->rx_count=0;v->rx_used=1;
        }
        if(v->rx_used && p[2]==v->rx_count){memcpy(v->data+p[2],p+8,p[3]);v->rx_count=end;}
        acknowledge(v,id,p,v->rx_count);
    }
    v->mask=mask;return 1;
}
static int control_channel(const UrClient *c,uint8_t channel){
    return (c->protocol==UR_PROFILE_LOBBY && channel==0xce) ||
           (c->protocol==UR_PROFILE_DROPIN && channel==0xcf);
}
static int next_lane(UrClient *c,UrOob *o,uint32_t now,uint8_t packet[UR_PACKET_MAX],uint8_t destination[6],unsigned priority){
    if(c->state!=UC_READY || now-c->oob.last_tx<UO_INTERVAL_MS)return 0;
    /* The caller schedules OOB after gameplay/control. All profiles have
     * room for 120 Hz fan-out plus ACKs.
     * Reliable messages still use exponential retry backoff per peer. */
    for(unsigned k=0;k<4;k++){
        unsigned peer=(o->scan+k)&3;UoPeer *v=&o->peer[peer];
        int ack_class=o==&c->service?v->data[0]!=3:control_channel(c,v->ack[0]);
        int tx_class=o==&c->service?o->data[0]!=3:control_channel(c,o->channel);
        int ack_ready=v->ack_pending && (priority==1?ack_class:!ack_class);
        int tx_ready=(o->pending&(1u<<peer)) && o->uploaded==o->total &&
            (!v->tx_sent || now-v->send_at>=v->retry_ms) && (priority==1?tx_class:!tx_class);
        if(!ack_ready && !tx_ready)continue;
        /* An endless incoming setup stream must not starve this console's
         * own setup behind one consumption ACK on every scheduling turn. */
        int ack=ack_ready && (!tx_ready || !v->ack_turn);v->ack_turn=ack;
        uint8_t payload[32]={0},width=c->history.width;uint32_t id;
        if(ack){memcpy(payload,v->ack,8);id=v->ack_id;v->ack_pending=0;}
        else{
            unsigned n=o->total-v->tx_offset;if(n>UO_FRAGMENT)n=UO_FRAGMENT;
            payload[0]=o->channel;payload[1]=o->total;payload[2]=v->tx_offset;payload[3]=n;
            payload[4]=o->dest&seat_mask(c,peer);payload[5]=o==&c->service;
            memcpy(payload+8,o->data+v->tx_offset,n);
            id=v->tx_id;width|=0xc0;
            if(v->tx_sent>v->tx_offset && v->retry_ms<2000){v->retry_ms*=2;if(v->retry_ms>2000)v->retry_ms=2000;}
            v->tx_sent=v->tx_offset+n;v->send_at=now;
        }
        c->oob.last_tx=now;o->scan=(peer+1)&3;
        if(un_recovery_enabled(c->config)){payload[6]=c->recovery.generation;payload[7]=c->recovery.generation>>8;}
        if(ud_mask(c,now)&(1u<<peer)){
            UoPeer *wire=&c->oob.peer[peer];
            if(wire->seq==UINT32_MAX){uc_fail(c,9);return 0;}
            memcpy(destination,c->peers[peer].address,6);
            return ur_seal(packet,c->peers[peer].key,8+c->peer,c->match,c->peer,
                ack?UD_OOB_ACK:UD_OOB,++wire->seq,id,c->history.local,width,payload);
        }
        if(c->oob.seq==UINT32_MAX){uc_fail(c,9);return 0;}
        memcpy(destination,c->endpoint,6);
        return ur_seal(packet,c->key,6,c->match,c->peer,ack?UR_OOB_ACK:UR_OOB,
            ++c->oob.seq,id,1u<<peer,width,payload);
    }
    return 0;
}
static int snapshot_next(UrClient *c,uint32_t now,uint8_t packet[UR_PACKET_MAX],uint8_t destination[6]){
    if(c->protocol!=UR_PROFILE_LOBBY || !un_snapshot_enabled(c->config) || c->state!=UC_READY || c->lobby.phase!=2 ||
       c->snapshots.epoch!=c->lobby.epoch || now-c->oob.last_tx<UO_INTERVAL_MS)return 0;
    if(un_recovery_enabled(c->config)){
        if(!c->recovery.generation || c->recovery.game_ack!=c->recovery.generation)return 0;
        for(unsigned k=0;k<4;k++){
            unsigned car=(c->snapshots.stream_scan+k)&3u;
            if(!c->snapshots.pending[car])continue;
            uint8_t data[32]={0xce,11,c->lobby.epoch,c->lobby.epoch>>8,0,3,c->recovery.generation,c->recovery.generation>>8};
            un_snapshot_recovery_pack(data+8,c->snapshots.data[car]);
            if(c->snapshots.sequence==UINT32_MAX || c->oob.seq==UINT32_MAX){uc_fail(c,9);return 0;}
            memcpy(destination,c->endpoint,6);c->snapshots.pending[car]=0;
            c->snapshots.stream_scan=(car+1u)&3u;c->oob.last_tx=now;c->metrics.stream_tx++;
            return ur_seal(packet,c->key,6,c->match,c->peer,UR_OOB,++c->oob.seq,++c->snapshots.sequence,0,c->history.width|0xc0,data);
        }
        return 0;
    }
    for(unsigned k=0;k<4;k++){
        unsigned peer=(c->oob.scan+k)&3u;
        uint8_t mask=seat_mask(c,peer)&c->lobby.active&~c->history.local;
        /* JNB's host uploads two pairs every field. Permit the second pair
         * on the next service tick, including bursty browser emulation;
         * the global OOB limiter still bounds packet output. */
        if(!mask || now-c->snapshots.sent_at[peer]<(un_snapshot_jnb(c->config)?1u:un_snapshot_timed(c->config)?4u:8u))continue;
        uint8_t payload[32]={0xce,0,c->lobby.epoch,c->lobby.epoch>>8,mask,2,0,0};
        unsigned count=0,timed=un_snapshot_timed(c->config);
        for(unsigned n=0;n<4 && count<(timed?1u:2u);n++){
            unsigned car=(c->snapshots.stream_scan+n)&3u;
            if(!(c->snapshots.pending[car]&mask))continue;
            unsigned index=car+((un_snapshot_jnb(c->config) && c->history.local!=1u)?4u:0u);
            const uint8_t *v=c->snapshots.data[index];uint8_t *out=payload+8+(timed?4u:0u)+11*count++;
            if(timed)ur_put32(payload+8,c->snapshots.captured[car]);
            uint16_t x=(uint16_t)v[2]|((uint16_t)v[3]<<8),y=(uint16_t)v[4]|((uint16_t)v[5]<<8);
            out[0]=car|(v[9]<<2);out[1]=x;out[2]=(x>>8)|(y<<2);out[3]=y>>6;
            out[4]=v[6];out[5]=v[7];out[6]=v[8];memcpy(out+7,v+10,4);
            c->snapshots.pending[car]&=(uint8_t)~mask;
        }
        if(!count)continue;
        payload[1]=timed?19:11*count;c->snapshots.stream_scan=(c->snapshots.stream_scan+1u)&3u;
        if(timed){
            if(c->snapshots.link_tx[peer]==UINT32_MAX){uc_fail(c,9);return 0;}
            ur_put32(payload+23,++c->snapshots.link_tx[peer]);
        }
        c->metrics.stream_tx++;
        c->snapshots.sent_at[peer]=c->oob.last_tx=now;c->oob.scan=(peer+1u)&3u;
        if(c->snapshots.sequence==UINT32_MAX){uc_fail(c,9);return 0;}
        uint32_t id=++c->snapshots.sequence;
        if(ud_mask(c,now)&(1u<<peer)){
            UoPeer *wire=&c->oob.peer[peer];
            if(wire->seq==UINT32_MAX){uc_fail(c,9);return 0;}
            memcpy(destination,c->peers[peer].address,6);
            return ur_seal(packet,c->peers[peer].key,8+c->peer,c->match,c->peer,
                UD_OOB,++wire->seq,id,c->history.local,c->history.width|0xc0,payload);
        }
        if(c->oob.seq==UINT32_MAX){uc_fail(c,9);return 0;}
        memcpy(destination,c->endpoint,6);
        return ur_seal(packet,c->key,6,c->match,c->peer,UR_OOB,++c->oob.seq,id,
            1u<<peer,c->history.width|0xc0,payload);
    }
    return 0;
}
int uo_next(UrClient *c,uint32_t now,uint8_t packet[UR_PACKET_MAX],uint8_t destination[6]){
    /* Bounded fairness: at most seven foreground OOB packets before giving
     * pending names/chat one slot. Control precedes fresh state, which in
     * turn precedes background traffic and its consumption ACKs. */
    if(c->message_streak>=7)for(unsigned k=0;k<2;k++){
        unsigned lane=(c->message_turn+k)&1;
        if(next_lane(c,lane?&c->service:&c->oob,now,packet,destination,2)){
            c->message_turn=lane^1;c->message_streak=0;c->metrics.background_packets++;return 1;
        }
    }
    for(unsigned k=0;k<2;k++){
        unsigned lane=(c->message_turn+k)&1;
        if(next_lane(c,lane?&c->service:&c->oob,now,packet,destination,1)){
            if(c->message_streak<7)c->message_streak++;
            c->metrics.critical_packets++;return 1;
        }
    }
    if(snapshot_next(c,now,packet,destination)){
        if(c->message_streak<7)c->message_streak++;
        c->metrics.gameplay_packets++;return 1;
    }
    for(unsigned k=0;k<2;k++){
        unsigned lane=(c->message_turn+k)&1;
        if(next_lane(c,lane?&c->service:&c->oob,now,packet,destination,2)){
            c->message_turn=lane^1;c->message_streak=0;c->metrics.background_packets++;return 1;
        }
    }
    return 0;
}
