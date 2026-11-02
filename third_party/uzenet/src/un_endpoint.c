#include "un_endpoint.h"
#include "un_snapshot.h"
#include "un_recovery.h"
#include <string.h>
__attribute__((weak)) uint8_t ue_start(const uint8_t config[19],uint32_t now){(void)config;(void)now;return UE_NOT_READY;}
__attribute__((weak)) uint8_t ue_scores(const uint8_t *p,uint8_t n,uint32_t now,uint8_t out[22]){(void)p;(void)n;(void)now;(void)out;return 0;}
void ue_reset(UnEndpoint *e){memset(e,0,sizeof(*e));e->read_peer=4;e->policy[3]=17;e->name_fallback='?';}
__attribute__((weak)) uint8_t ue_stop(UrClient *c,uint8_t begin,uint32_t now){(void)now;if(begin)uc_restart(c);return 1;}
void ue_cancel_control(UnEndpoint *e,UrGame *g,UrClient *c,uint32_t now){
    uint8_t begin=!e->cancelling;
    if(begin){ue_reset(e);ug_reset(g);e->cancelling=1;}
    uint8_t result=e->cancelling==2?1:ue_stop(c,begin,now);
    if(result==1)e->cancelling=2;
    un_send(UN_CANCEL_REPLY,&result,1);
}
static void error(UnEndpoint *e,uint8_t code){e->reply[1]=UE_ERROR;e->reply[2]=code;e->reply_n=3;}
static void performance(UnEndpoint *e,UrClient *c,uint8_t page,uint32_t now){
    UrMetrics *m=&c->metrics;
    if(page==255){
        uint8_t paused=m->paused,queued=m->uart_queued;memset(m,0,sizeof *m);
        m->paused=paused;m->pause_at=now;m->uart_queued=queued;
        memset(c->snapshots.link_rx,0,sizeof c->snapshots.link_rx);
        memset(c->snapshots.link_base,0,sizeof c->snapshots.link_base);
        memset(c->snapshots.link_seen,0,sizeof c->snapshots.link_seen);
        e->performance_serial=0;e->reply[1]=UE_OK;return;
    }
    if(page>=6){error(e,UE_BAD_REQUEST);return;}
    if(!page){
        uint32_t values[6][5]={
            {now,now-c->born,m->uart_rx,m->uart_tx,m->paused_ms+(m->paused?now-m->pause_at:0)},
            {m->captures,m->delivered,m->replaced,m->stream_tx,m->stream_rx},
            {m->reordered,m->gaps,m->input_frames,m->input_events,m->uart_peak},
            {m->critical_packets,m->gameplay_packets,m->background_packets,c->next,c->released},
            {0,0,0,0,(uint32_t)m->uart_queued|((uint32_t)m->paused<<8)},
            {c->snapshots.captured[0],c->snapshots.captured[1],c->snapshots.captured[2],c->snapshots.captured[3],un_snapshot_timed(c->config)?c->snapshots.valid&15u:0}
        };
        for(unsigned car=0;car<4;car++){
            unsigned bit=1u<<car,owner=(c->lobby.active&bit)?bit:1u;
            values[4][car]=((c->snapshots.valid&bit) && owner!=c->history.local)?now-m->received_at[car]:UINT32_MAX;
        }
        for(unsigned a=0;a<6;a++)for(unsigned b=0;b<5;b++)ur_put32(e->performance[a]+4*b,values[a][b]);
        if(!++e->performance_serial)e->performance_serial=1;
    }
    if(!e->performance_serial){error(e,UE_NOT_READY);return;}
    e->reply[1]=UE_OK;e->reply[2]=e->performance_serial;e->reply[3]=e->performance_serial>>8;
    memcpy(e->reply+4,e->performance[page],20);e->reply_n=24;
}
static int ending(UnEndpoint *e,UrClient *c,uint32_t now){
    if(!c->services.end_code)return 0;
    uint8_t code=us_end_status(c,now);
    if(code)error(e,code);
    return 1;
}
/* Snapshot lobbies deliberately have no input timeline. OOB delivers bounded
 * owner records; retaining the latest record cannot stall an AVR on old pads. */
static int snapshot_index(const uint8_t *v){
    int car=v[0]&3u;unsigned kind=v[0]&0xc0u;
    if(v[0]&0x3cu)return -1;
    return kind==0x40u?(car?-1:12):kind==0xc0u?8+car:kind==0x80u?4+car:car;
}
static int snapshot_owner(const UrClient *c,const uint8_t *v,uint8_t source){
    int index=snapshot_index(v);uint8_t car=1u<<(v[0]&3u);
    if(index<0)return 0;
    if(un_snapshot_jnb(c->config))return index<4 && (source==1u || source==car);
    unsigned host=un_recovery_enabled(c->config)?(c->lobby.host<4?1u<<c->lobby.host:0):1u;
    return source==(index>=8?host:((c->lobby.active&car)?car:host));
}
static void snapshot_store(UrClient *c,const uint8_t *v,int local,uint32_t now){
    int index=snapshot_index(v);uint16_t bit=1u<<index;
    if(un_snapshot_jnb(c->config)){
        unsigned car=v[0]&3u;
        /* Host world records and client input records must never overwrite
         * each other's retained slots, even for the same controlled rabbit. */
        index=car+((local?(c->history.local!=1u):(c->history.local==1u))?4u:0u);
        bit=1u<<index;
        if(local){c->metrics.captures++;if(c->snapshots.pending[car])c->metrics.replaced++;}
        else{if(c->snapshots.unread&bit)c->metrics.replaced++;c->metrics.received_at[car]=now;}
        memcpy(c->snapshots.data[index],v,14);c->snapshots.valid|=bit;
        if(local)c->snapshots.pending[car]=c->history.local==1u?(c->lobby.active&14u):1u;
        else c->snapshots.unread|=bit;
        return;
    }
    if(index>=8 && (c->snapshots.valid&bit))return; /* immutable round setup */
    if(index<4){
        if(local){
            c->metrics.captures++;
            if(c->snapshots.pending[index] || (c->snapshots.dirty&bit))c->metrics.replaced++;
        }else{
            if(c->snapshots.unread&bit)c->metrics.replaced++;
            c->metrics.received_at[index]=now;
        }
    }
    memcpy(c->snapshots.data[index],v,14);c->snapshots.valid|=bit;
    if(local){
        if(index<4 && un_snapshot_enabled(c->config))c->snapshots.pending[index]=un_recovery_enabled(c->config)?128:c->lobby.active&~c->history.local;
        else c->snapshots.dirty|=bit;
    }else c->snapshots.unread|=bit;
}
static void snapshot_upload(UnEndpoint *e,UrClient *c,const uint8_t *p,uint8_t n,uint32_t now){
    (void)now;
    if(!e->snapshot_stream)return;
    if(un_recovery_enabled(c->config) && (c->recovery.game_ack!=c->recovery.generation || !(c->lobby.active&c->history.local)))return;
    if(c->state!=UC_READY || c->protocol!=UR_PROFILE_LOBBY || !un_snapshot_enabled(c->config) ||
       !e->running || c->lobby.phase!=2 || c->lobby.epoch!=e->snapshot_epoch)return;
    if(n!=13 && n!=24){e->snapshot_error=UE_BAD_REQUEST;return;}
    if(((uint16_t)p[0]|((uint16_t)p[1]<<8))!=e->snapshot_epoch)return; /* stale race */
    uint8_t record[14];unsigned seen=0;
    for(unsigned at=2;at<n;at+=11){
        if(!(un_recovery_enabled(c->config)?un_snapshot_recovery_unpack(record,p+at):un_snapshot_unpack(record,p+at)) || !snapshot_owner(c,record,c->history.local) ||
           (seen&(1u<<record[0]))){e->snapshot_error=UE_BAD_REQUEST;return;}
        seen|=1u<<record[0];
    }
    for(unsigned at=2;at<n;at+=11){
        if(un_recovery_enabled(c->config))un_snapshot_recovery_unpack(record,p+at);else un_snapshot_unpack(record,p+at);
        snapshot_store(c,record,1,now);
    }
}
void ue_snapshot_write(UnEndpoint *e,UrClient *c,const uint8_t *p,uint8_t n,uint32_t now){
    if(e->snapshot_stream==1)snapshot_upload(e,c,p,n,now);
}
void ue_timed_write(UnEndpoint *e,UrClient *c,const uint8_t *p,uint8_t n,uint32_t now){
    if(e->cancelling || e->snapshot_stream!=2)return;
    if(c->state!=UC_READY || !e->running || c->protocol!=UR_PROFILE_LOBBY ||
       !un_snapshot_timed(c->config) || c->lobby.phase!=2 || c->lobby.epoch!=e->snapshot_epoch)return;
    if(n!=17){e->snapshot_error=UE_BAD_REQUEST;return;}
    if(((uint16_t)p[0]|((uint16_t)p[1]<<8))!=e->snapshot_epoch)return;
    uint8_t legacy[13];memcpy(legacy,p,2);memcpy(legacy+2,p+6,11);
    uint8_t record[14];
    if(!un_snapshot_unpack(record,p+6) || !snapshot_owner(c,record,c->history.local)){
        e->snapshot_error=UE_BAD_REQUEST;return;
    }
    uint32_t tick=ur_get32(p+2);unsigned car=p[6]&3u;
    if((c->snapshots.valid&(1u<<car)) && (int32_t)(tick-c->snapshots.captured[car])<=0)return;
    snapshot_upload(e,c,legacy,sizeof legacy,now);
    if(!e->snapshot_error && c->state==UC_READY && c->lobby.phase==2 && c->lobby.epoch==e->snapshot_epoch)
        c->snapshots.captured[car]=tick;
}
void ue_input_write(UnEndpoint *e,UrClient *c,const uint8_t *p,uint8_t n,uint32_t now){
    (void)now;
    if(e->cancelling || !e->input_duplex)return;
    if(c->state!=UC_READY || !e->running || c->services.failure || c->services.desync || c->services.end_code){e->input_error=UE_BACKEND;return;}
    unsigned stride=__builtin_popcount(c->history.local)*c->history.width;
    if(n<9 || n>24 || p[8]>8 || n!=9+p[8]*stride){e->input_error=UE_BAD_REQUEST;return;}
    uint32_t consumed=ur_get32(p),first=ur_get32(p+4),last=first+p[8];
    if(last<first || last>=0xffffffbfUL){e->input_error=UE_EXHAUSTED;return;}
    if(consumed>e->input_sent){e->input_error=UE_BAD_SEQUENCE;return;}
    /* Validate all retained duplicates before committing any part of a batch. */
    for(uint32_t frame=first;frame<last && frame<c->next;frame++){
        if(frame<c->history.floor)continue;
        UrFrame *v=ur_frame(&c->history,frame,0);unsigned at=9+(frame-first)*stride;
        if(!v){e->input_error=UE_BAD_SEQUENCE;return;}
        for(unsigned seat=0;seat<4;seat++)if(c->history.local&(1u<<seat)){
            if(memcmp(v->pads+UR_STRIDE(c->history.width)*seat,p+at,c->history.width)){
                e->input_error=UE_BAD_SEQUENCE;return;
            }
            at+=c->history.width;
        }
    }
    if(consumed>e->frame){
        if(!uc_release(c,consumed)){e->input_error=UE_BAD_SEQUENCE;return;}
        e->frame=consumed;up_advance(c,consumed,now);e->input_dirty=1;
    }
    /* Receipt progress must survive upload backpressure: otherwise a full
     * input window can block the very consumption cursor that frees it. */
    if(first>c->next || (last>c->next && last-c->history.floor>UR_WINDOW)){e->input_dirty|=2;return;}
    for(uint32_t frame=c->next;frame<last;frame++){
        uint8_t pads[UR_PAD_MAX]={0};unsigned at=9+(frame-first)*stride;
        for(unsigned seat=0;seat<4;seat++)if(c->history.local&(1u<<seat)){
            memcpy(pads+UR_STRIDE(c->history.width)*seat,p+at,c->history.width);at+=c->history.width;
        }
        if(!uc_submit(c,frame,pads)){e->input_error=UE_BACKEND;return;}
        e->input_dirty=1;
    }
}
static uint8_t input_duplex_service(UnEndpoint *e,UrClient *c,uint32_t now){
    if(!e->input_duplex)return 0;
    uint8_t p[24]={UE_OK};unsigned n=10,count=0;
    if(c->state!=UC_READY || !e->running || c->services.failure || c->services.desync || c->services.end_code)
        e->input_error=UE_BACKEND;
    if(e->input_error){
        p[0]=UE_ERROR;p[1]=e->input_error;e->input_duplex=e->input_error=0;
        un_send(UN_INPUT_FRAME,p,2);return 1;
    }
    unsigned stride=__builtin_popcount(c->history.all&~c->history.local)*c->history.width;
    if(!stride)return 0;
    while(count<8 && n+stride<=sizeof p){
        UrFrame *v=uc_fetch(c,e->frame+count);if(!v || v->mask!=c->history.all)break;
        for(unsigned seat=0;seat<4;seat++)if((c->history.all&~c->history.local)&(1u<<seat)){
            memcpy(p+n,v->pads+UR_STRIDE(c->history.width)*seat,c->history.width);n+=c->history.width;
        }
        count++;
    }
    if(!e->input_dirty && e->frame+count<=e->input_sent && now-e->input_at<(count?100u:1000u))return 0;
    if(e->input_dirty&2)p[0]|=1; /* accepted watermark requests gap/window retry */
    ur_put32(p+1,c->next);ur_put32(p+5,e->frame);p[9]=count;
    if(e->frame+count>e->input_sent)e->input_sent=e->frame+count;
    e->input_dirty=0;e->input_at=now;c->metrics.input_events++;
    un_send(UN_INPUT_FRAME,p,n);return 1;
}
/* Called only with an empty platform TX slot. One bounded frame is staged;
 * fresh states replace pending values while PA7 holds that frame paused. */
static uint8_t snapshot_stream_service(UnEndpoint *e,UrClient *c,uint32_t now){
    if(!e->snapshot_stream)return 0;
    uint8_t type=e->snapshot_stream==2?UN_TIMED_FRAME:UN_SNAPSHOT_FRAME;
    uint8_t payload[24]={e->snapshot_epoch,e->snapshot_epoch>>8};
    if(un_snapshot_jnb(c->config) && c->state==UC_READY){
        uint32_t roster=c->lobby.active|((uint32_t)c->lobby.pending<<8)|((uint32_t)c->lobby.phase<<16)|((uint32_t)c->lobby.ready<<24);
        if(e->snapshot_epoch!=c->lobby.epoch || roster!=e->snapshot_roster || now-e->snapshot_roster_at>=1000u){
            e->snapshot_epoch=c->lobby.epoch;e->snapshot_roster=roster;e->snapshot_roster_at=now;
            payload[0]=c->lobby.epoch;payload[1]=c->lobby.epoch>>8;payload[2]=255;
            payload[3]=c->lobby.active;payload[4]=c->lobby.pending;payload[5]=c->lobby.phase;payload[6]=c->lobby.ready;
            un_send(type,payload,13);return 1;
        }
        if(c->lobby.phase!=2)return 0;
    }
    if(un_recovery_enabled(c->config) && c->state>=UC_BINDING && c->state<UC_READY){
        if(now-e->snapshot_at>=1000){e->snapshot_at=now;un_send(type,payload,2);return 1;}
        return 0;
    }
    if(c->state!=UC_READY || !e->running || c->protocol!=UR_PROFILE_LOBBY ||
       !un_snapshot_enabled(c->config))e->snapshot_error=UE_BACKEND;
    else if((c->lobby.phase!=2 && !(un_recovery_enabled(c->config) && !c->lobby.phase)) || c->lobby.epoch!=e->snapshot_epoch)e->snapshot_error=UE_NOT_READY;
    if(e->snapshot_error){
        if(un_recovery_enabled(c->config)){c->recovery.game_failed=1;c->recovery.ack=0;}
        payload[2]=e->snapshot_error;e->snapshot_error=e->snapshot_stream=0;
        un_send(type,payload,3);return 1;
    }
    unsigned n=2;
    for(unsigned k=0;k<4 && n<24;k++){
        unsigned car=(c->snapshots.read_scan+k)&3u;
        unsigned index=car+((un_snapshot_jnb(c->config) && c->history.local==1u)?4u:0u);
        if(!(c->snapshots.unread&(1u<<index)))continue;
        if(e->snapshot_stream==2){
            ur_put32(payload+2,c->snapshots.captured[car]);
            uint32_t age=now-c->metrics.received_at[car];if(age>65535)age=65535;
            payload[6]=age;payload[7]=age>>8;
            un_snapshot_pack(payload+8,c->snapshots.data[car]);n=19;
        }else{
            if(un_recovery_enabled(c->config))un_snapshot_recovery_pack(payload+n,c->snapshots.data[car]);
            else un_snapshot_pack(payload+n,c->snapshots.data[index]);
            n+=11;
        }
        c->metrics.delivered++;
        if(un_recovery_enabled(c->config))c->recovery.forwarded|=1u<<car;
        c->snapshots.unread&=(uint16_t)~(1u<<index);
        if(e->snapshot_stream==2)break;
    }
    if(n==2 && (uint32_t)(now-e->snapshot_at)<1000u)return 0;
    c->snapshots.read_scan=(c->snapshots.read_scan+1u)&3u;e->snapshot_at=now;
    un_send(type,payload,n);return 1;
}
static void snapshot_tick(UrClient *c,uint32_t now){
    if(c->snapshots.epoch!=c->lobby.epoch){
        memset(&c->snapshots,0,sizeof c->snapshots);c->snapshots.epoch=c->lobby.epoch;
    }
    for(unsigned peer=0;peer<4;peer++)if(uo_pending(c)&(1u<<peer)){
        UoPeer *v=c->oob.peer+peer;
        /* Game-owned OOB channels are consumed through CHAT_READ/NEXT.
         * Snapshot maintenance must not acknowledge and discard them. */
        if(v->rx_channel!=0xce)continue;
        if(v->rx_channel==0xce && v->rx_total==16){
            uint16_t epoch=(uint16_t)v->data[0]|((uint16_t)v->data[1]<<8);
            /* Peer traffic can beat Room's new-round roster. Keep that
             * complete record pending until the roster catches up: consuming
             * it now loses immutable CONFIG/setup data permanently. */
            if((int16_t)(epoch-c->lobby.epoch)>0)continue;
            if(epoch==c->lobby.epoch && snapshot_owner(c,v->data+2,v->mask))
                snapshot_store(c,v->data+2,0,now);
        }
        (void)uo_ack(c,peer,v->rx_id);
    }
    uint8_t dest=c->lobby.active&~c->history.local;
    if(!dest || !c->lobby.phase || c->oob.pending || c->oob.uploaded!=c->oob.total)return;
    /* Reliable configuration/setup is independent of live position traffic. */
    unsigned index=12;
    if(!(c->snapshots.dirty&(1u<<index))){
        for(unsigned i=0;i<12;i++){index=(c->snapshots.scan+i)%12;if(c->snapshots.dirty&(1u<<index))break;}
        if(!(c->snapshots.dirty&(1u<<index)))return;
    }
    uint16_t id=c->oob.id+1u;if(!id){uc_fail(c,9);return;}
    uint8_t message[22]={id,id>>8,dest,0xce,16,0,c->lobby.epoch,c->lobby.epoch>>8};
    memcpy(message+8,c->snapshots.data[index],14);
    if(uo_write(c,message,sizeof message,now)==1){c->snapshots.dirty&=(uint16_t)~(1u<<index);c->snapshots.scan=(index+1)%12;}
}
/* Drop-in snapshots live on the ESP. UART moves at most 16 bytes per request;
 * reliable OOB moves bounded records, independently of the input timeline. */
/* Large frozen joins use the existing 1536-byte storage as a bounded ring.
 * UART consumers acknowledge each 16-byte read before its storage is reused.
 * Small joins retain the original whole-snapshot contract. */
#define UE_JOIN_STREAM_MAX 65520u
static void join_ring_read(const UrClient *c,unsigned offset,uint8_t *dst,unsigned count){
    unsigned at=offset%sizeof c->join.data,n=sizeof c->join.data-at;
    if(n>count)n=count;
    memcpy(dst,c->join.data+at,n);memcpy(dst+n,c->join.data,count-n);
}
static void join_ring_write(UrClient *c,unsigned offset,const uint8_t *src,unsigned count){
    unsigned at=offset%sizeof c->join.data,n=sizeof c->join.data-at;
    if(n>count)n=count;
    memcpy(c->join.data+at,src,n);memcpy(c->join.data,src+n,count-n);
}
static void join_tick(UnEndpoint *e,UrClient *c,uint32_t now){
    if(c->join.reset && !c->join.primed){
        e->frame=c->join.base;e->granted=e->sampled=0;
        memset(e->baseline,0,sizeof e->baseline);c->join.primed=1;
    }
    for(unsigned peer=0;peer<4;peer++)if(uo_pending(c)&(1u<<peer)){
        UoPeer *v=c->oob.peer+peer;
        if(c->history.local!=1 && v->mask==1 &&
           v->rx_channel==0xcf && v->rx_total>6){
            unsigned offset=v->data[0]|((unsigned)v->data[1]<<8);
            unsigned total=v->data[2]|((unsigned)v->data[3]<<8);
            unsigned epoch=v->data[4]|((unsigned)v->data[5]<<8),n=v->rx_total-6;
            /* Roster and snapshot packets have independent delivery order. */
            if(epoch>c->lobby.epoch)continue;
            if(epoch<c->lobby.epoch || c->lobby.phase!=1){
                (void)uo_ack(c,peer,v->rx_id);continue;
            }
            if(!total || total>UE_JOIN_STREAM_MAX ||
               offset!=c->join.received || offset+n>total ||
               (c->join.total && c->join.total!=65535u && c->join.total!=total)){uc_fail(c,7);return;}
            /* Do not acknowledge a full ring. The immutable OOB record waits
             * here until AVR drains it, propagating backpressure to sender. */
            if(total>sizeof c->join.data && (!c->join.total || c->join.received-c->join.read+n>sizeof c->join.data))continue;
            c->join.total=total;join_ring_write(c,offset,v->data+6,n);c->join.received+=n;
        }
        (void)uo_ack(c,peer,v->rx_id);
    }
    if(c->history.local==1 && c->join.total && c->join.sent<c->join.total && c->join.sent<c->join.used &&
       !c->oob.pending && c->oob.uploaded==c->oob.total){
        unsigned offset=c->join.sent,n=c->join.used-offset;if(n>122)n=122;
        uint16_t id=c->oob.id+1;
        if(!id){uc_fail(c,9);return;}
        uint8_t message[128]={offset,offset>>8,c->join.total,c->join.total>>8,c->lobby.epoch,c->lobby.epoch>>8};
        join_ring_read(c,offset,message+6,n);
        uint8_t request[24]={id,id>>8,c->lobby.active&~1u,0xcf,n+6,0};
        for(unsigned pos=0;pos<n+6;){
            unsigned count=n+6-pos;if(count>18)count=18;
            request[5]=pos;memcpy(request+6,message+pos,count);
            if(uo_write(c,request,count+6,now)!=1){uc_fail(c,7);return;}
            pos+=count;
        }
        c->join.sent+=n;
    }
    if(c->lobby.phase==2 && c->join.ready && c->join.primed==1){
        uint8_t neutral[UR_PAD_MAX]={0};
        for(unsigned i=0;i<e->delay;i++)if(!uc_submit(c,e->frame+i,neutral)){uc_fail(c,7);return;}
        up_begin(c,e->frame,now);
        c->join.primed=2;c->join.want=c->join.ready=0;
    }
}
static void join_command(UnEndpoint *e,UrClient *c,const uint8_t *p,uint8_t n){
    if(!n){
        e->reply[1]=UE_OK;e->reply[2]=2;e->reply[3]=16;
        e->reply[4]=(uint8_t)UE_JOIN_STREAM_MAX;e->reply[5]=UE_JOIN_STREAM_MAX>>8;e->reply_n=6;return;
    }
    if(c->protocol!=UR_PROFILE_DROPIN || !c->join.reset || n<2){error(e,UE_BAD_REQUEST);return;}
    unsigned offset=p[0]|((unsigned)p[1]<<8);
    if(offset==65534u && n==4){
        unsigned total=p[2]|((unsigned)p[3]<<8);
        /* v2: receivers can opt into a bounded stream before its length is
         * known. The first authenticated host record fixes that length. */
        if(total==65535u && c->history.local!=1){
            if(!c->join.total)c->join.total=65535u;
        }else{
            if(!total || total>UE_JOIN_STREAM_MAX || (total&15) ||
               (c->join.total && c->join.total!=total)){error(e,UE_BAD_REQUEST);return;}
            c->join.total=total;
        }
        e->reply[1]=UE_OK;
    }else if(offset==65535u && n==4){
        unsigned total=p[2]|((unsigned)p[3]<<8);
        unsigned used=c->history.local==1?c->join.used:c->join.received;
        if(!total || total>UE_JOIN_STREAM_MAX || used!=total ||
           (c->join.total && c->join.total!=total) ||
           (total>sizeof c->join.data && c->history.local!=1 && c->join.read!=total)){error(e,UE_BAD_REQUEST);return;}
        c->join.total=total;
        if(c->lobby.phase==2 && c->join.primed==2)e->reply[1]=UE_OK;
        else c->join.ready=1;
    }else if(c->lobby.phase!=1)error(e,UE_BAD_REQUEST);
    else if(c->history.local==1 && n>2 && n<=18){
        unsigned count=n-2,stream=c->join.total>sizeof c->join.data;
        if(offset+count>(stream?c->join.total:sizeof c->join.data) || offset>c->join.used ||
           (offset<c->join.used && ((stream && offset<c->join.sent) || offset+count>c->join.used)))error(e,UE_BAD_REQUEST);
        else if(stream && offset+count-c->join.sent>sizeof c->join.data){} /* WAIT */
        else if(offset<c->join.used){
            uint8_t previous[16];join_ring_read(c,offset,previous,count);
            if(memcmp(previous,p+2,count))error(e,UE_BAD_REQUEST);else e->reply[1]=UE_OK;
        }else{join_ring_write(c,offset,p+2,count);c->join.used+=count;e->reply[1]=UE_OK;}
    }else if(c->history.local!=1 && n==2){
        unsigned stream=c->join.total>sizeof c->join.data;
        if(offset>(stream?c->join.total:sizeof c->join.data)-16 || (stream && offset!=c->join.read))error(e,UE_BAD_REQUEST);
        else if(c->join.received>=offset+16){
            join_ring_read(c,offset,e->reply+2,16);e->reply_n=18;e->reply[1]=UE_OK;
            if(stream)c->join.read+=16;
        }
    }else error(e,UE_BAD_REQUEST);
}
void ue_tick(UnEndpoint *e,UrClient *c,uint32_t now){
    if(e->cancelling)return;
    if(!e->configured || c->state!=UC_READY || c->protocol==UR_PROFILE_LIVE)return;
    if(c->protocol==UR_PROFILE_READYSTART && !c->lobby.active)return;
    if(c->protocol==UR_PROFILE_DROPIN)join_tick(e,c,now);
    if(!c->services.active){
        us_begin(c,e->policy,e->delay,e->window,now);
        if((c->protocol!=UR_PROFILE_READYSTART || c->lobby.phase==2) &&
           (c->protocol!=UR_PROFILE_DROPIN || (!c->lobby.phase && (c->lobby.active&c->history.local))))
            up_begin(c,c->released,now);
    }
    us_tick(c,now);
    if(c->protocol==UR_PROFILE_LOBBY)snapshot_tick(c,now);
}
static void ready(UnEndpoint *e,UrGame *g,UrClient *c,uint32_t now){
    if(c->state==UC_FAILED){error(e,UE_BACKEND);return;}
    if(c->state!=UC_READY)return;
    if(un_recovery_enabled(c->config) && !c->recovery.generation){
        if(now-c->born>=5000)error(e,UE_NEGOTIATION);
        return;
    }
    if(c->protocol==UR_PROFILE_READYSTART && c->lobby.phase!=2)return;
    if(c->protocol>=UR_PROFILE_LOBBY && !(c->lobby.active&c->history.local))return;
    if(c->protocol==UR_PROFILE_LIVE){e->running=1;goto ready_reply;}
    ue_tick(e,c,now);
    if(ending(e,c,now))return;
    if(c->services.failure){error(e,c->services.failure);return;}
    if(!c->services.ready)return;
    if(!e->running){
        if(c->protocol==UR_PROFILE_READYSTART)up_begin(c,c->released,now);
        e->delay=c->services.delay;
        ug_reset(g);g->active=1;g->base=c->released;
        e->frame=c->released;e->running=1;
        uint8_t neutral[UR_PAD_MAX]={0};
        if(!c->join.reset)for(unsigned i=0;i<e->delay;i++)if(!uc_submit(c,e->frame+i,neutral)){error(e,UE_BACKEND);return;}
    }
ready_reply:
    e->reply[1]=UE_OK;e->reply[2]=c->history.local;e->reply[3]=c->protocol>=UR_PROFILE_LOBBY?c->lobby.active:c->history.all;
    e->reply[4]=c->history.width;memcpy(e->reply+5,c->seed,4);e->reply_n=9;
}
/* Display formatting never modifies the stored profile or session name. */
static uint8_t display_char(const UnEndpoint *e,uint8_t ch){
    if(ch<32 || ch>126)return e->name_fallback;
    if(e->name_mode==UE_CUSTOM_MAP)return e->name_map[ch-32];
    if(e->name_mode==UE_UPPERCASE && ch>='a' && ch<='z')ch-='a'-'A';
    return ch;
}
static void compact_status(UnEndpoint *e,UrClient *c){
    UrServices *s=&c->services;
    uint8_t phase=UE_STATUS_IDLE,source=0,reason=0,missing=0;
    if(c->state==UC_FAILED){phase=UE_STATUS_FAILED;source=1;reason=c->error;}
    else if(s->failure){phase=UE_STATUS_FAILED;source=2;reason=s->failure;}
    else if(s->desync){phase=UE_STATUS_FAILED;source=3;reason=UE_DESYNC;}
    else if(s->end_code){phase=UE_STATUS_ENDING;source=4;reason=s->end_result?s->end_result:s->end_code;}
    else if(c->state==UC_TICKET)phase=UE_STATUS_CONNECTING;
    else if(c->state==UC_BINDING)phase=e->running?UE_STATUS_RECONNECTING:UE_STATUS_BINDING;
    else if(c->state==UC_WAITING)phase=UE_STATUS_WAIT_PLAYERS;
    else if(c->state==UC_READY){
        if(c->protocol==UR_PROFILE_READYSTART && c->lobby.phase!=2)
            phase=c->lobby.active?UE_STATUS_WAIT_READY:UE_STATUS_WAIT_PLAYERS;
        else if(!e->running || s->barrier_wait || e->repair)phase=UE_STATUS_SYNC;
        else{
            phase=UE_STATUS_RUNNING;
            if(e->sampled && !e->granted){
                UrFrame *f=ur_frame(&c->history,e->frame,0);
                missing=c->history.all&~(f?f->mask:0);
                phase=missing?UE_STATUS_WAIT_INPUT:UE_STATUS_SYNC;
            }
        }
    }
    e->reply[1]=UE_OK;e->reply[2]=phase;e->reply[3]=source;
    e->reply[4]=reason;e->reply[5]=missing;e->reply_n=6;
}
static void pads_at(UrClient *c,uint32_t f,uint8_t pads[UR_PAD_MAX]){
    UrFrame *v=ur_frame(&c->history,f,0);
    if(v)for(unsigned s=0;s<4;s++)if(v->mask&(1u<<s))memcpy(pads+UR_STRIDE(c->history.width)*s,v->pads+UR_STRIDE(c->history.width)*s,UR_STRIDE(c->history.width));
}
static void repair_notice(UnEndpoint *e){
    e->reply[1]=UE_REPAIR;e->reply[2]=(uint8_t)e->repair_first;
    e->reply[3]=(uint8_t)(e->repair_end-e->repair_first);e->reply_n=4;
}
static void reconcile(UnEndpoint *e,UrGame *g,UrClient *c,uint8_t pads[UR_PAD_MAX]){
    memcpy(pads,e->baseline,UR_PAD_BYTES(c->history.width));
    uint32_t release=c->released;
    for(uint32_t f=c->released;f<e->frame;f++){
        pads_at(c,f,pads);uint8_t *old=e->used[f&(UE_PREDICTION_SLOTS-1)];
        if(!e->repair && memcmp(pads,old,UR_PAD_BYTES(c->history.width))){
            e->repair=1;e->repair_first=e->repair_cursor=f;e->repair_end=e->frame;
        }
        if(e->repair)memcpy(old,pads,UR_PAD_BYTES(c->history.width)); /* immutable replay snapshot */
        else{
            UrFrame *v=ur_frame(&c->history,f,0);
            if(release==f && v && v->mask==c->history.all){
                release=f+1;memcpy(e->baseline,pads,UR_PAD_BYTES(c->history.width));g->delivered[f&(UR_WINDOW-1)]=0;
            }
        }
    }
    (void)uc_release(c,release);
    pads_at(c,e->frame,pads);
}
static void replay(UnEndpoint *e,UrClient *c){
    e->reply[1]=UE_OK;e->reply[2]=(uint8_t)e->repair_cursor;e->reply_n=3;
    const uint8_t *pads=e->used[e->repair_cursor&(UE_PREDICTION_SLOTS-1)];
    for(unsigned s=0;s<4;s++)if(c->history.all&(1u<<s)){
        memcpy(e->reply+e->reply_n,pads+UR_STRIDE(c->history.width)*s,c->history.width);e->reply_n+=c->history.width;
    }
}
/* One CRC-framed reply, bounded by both the caller and the existing codec.
 * Never wait to fill a burst. No extra UART queue or AVR receive buffer. */
static unsigned burst_limit(unsigned records,unsigned bytes,unsigned stride,unsigned header){
    unsigned fit=(UN_PAYLOAD_MAX-header)/stride, budget=bytes/stride;
    if(records>fit)records=fit;
    return records>budget?budget:records;
}
static void replay_burst(UnEndpoint *e,UrClient *c,unsigned records,unsigned bytes){
    if(!e->running || c->state!=UC_READY){error(e,UE_NOT_READY);return;}
    if(e->repair!=1){error(e,UE_BAD_REQUEST);return;}
    unsigned stride=__builtin_popcount(c->history.all)*c->history.width;
    if(!stride){error(e,UE_BAD_REQUEST);return;}
    records=burst_limit(records,bytes,stride,4);
    if(!records){error(e,UE_BAD_REQUEST);return;}
    if(records>e->repair_end-e->repair_cursor)records=e->repair_end-e->repair_cursor;
    e->reply[1]=UE_OK;e->reply[2]=(uint8_t)e->repair_cursor;e->reply[3]=records;e->reply_n=4;
    for(unsigned f=0;f<records;f++){
        const uint8_t *pads=e->used[(e->repair_cursor+f)&(UE_PREDICTION_SLOTS-1)];
        for(unsigned s=0;s<4;s++)if(c->history.all&(1u<<s)){
            memcpy(e->reply+e->reply_n,pads+UR_STRIDE(c->history.width)*s,c->history.width);
            e->reply_n+=c->history.width;
        }
    }
    e->burst_count=records;e->repair=3;
}
static void exchange(UnEndpoint *e,UrGame *g,UrClient *c,const uint8_t *p,uint8_t n,uint32_t now){
    if(e->input_stream){error(e,UE_BAD_REQUEST);return;}
    unsigned local=__builtin_popcount(c->history.local)*c->history.width;
    if(!e->running || c->state!=UC_READY){error(e,UE_NOT_READY);return;}
    if(n!=local){error(e,UE_BAD_REQUEST);return;}
    if(c->services.failure || c->services.desync){error(e,c->services.failure?c->services.failure:UE_DESYNC);return;}
    if(ending(e,c,now) || c->services.barrier_wait)return;
    if(e->repair){repair_notice(e);return;}
    /* Only a NEW EXCHANGE after RUN acknowledges a completed simulation step.
     * Chat/status transactions never advance the game. */
    if(e->granted){
        if(e->frame>=0xffffffbfUL){error(e,UE_EXHAUSTED);return;}
        e->frame++;up_advance(c,e->frame,now);e->granted=e->sampled=0;
    }
    if(c->protocol==UR_PROFILE_DROPIN){
        if(c->history.local==1 && c->lobby.pending && (!c->join.reset || (c->config[17]==4 && c->join.primed==2))){
            if(e->frame>=0xffffff80UL){error(e,UE_EXHAUSTED);return;}
            c->join.want=1;c->join.base=e->frame+64;
        }
        if(c->join.want || (c->join.reset && c->join.primed!=2)){
            if(c->join.reset && c->lobby.phase==1){
                e->reply[1]=UE_JOINING;e->reply[2]=c->history.local==1?1:2;e->reply_n=3;
                if(c->config[17]==4)e->reply[e->reply_n++]=c->lobby.active;
            }
            return;
        }
    }
    uint8_t predicted[UR_PAD_MAX];reconcile(e,g,c,predicted);
    if(e->repair){repair_notice(e);return;}
    if(!e->sampled){
        if(e->frame>=0xffffffc0UL-e->delay){error(e,UE_EXHAUSTED);return;}
        uint8_t pads[UR_PAD_MAX]={0};unsigned at=0;
        for(unsigned s=0;s<4;s++)if(c->history.local&(1u<<s)){
            memcpy(pads+UR_STRIDE(c->history.width)*s,p+at,c->history.width);at+=c->history.width;
        }
        /* Full recovery retention applies even if the game has consumed the
         * frame. Wait for Room acknowledgement rather than overwrite a slot. */
        if(e->frame+e->delay-c->history.floor>=UR_WINDOW)return;
        if(!uc_submit(c,e->frame+e->delay,pads)){error(e,UE_BACKEND);return;}
        e->sampled=1;
    }
    /* Once sampled, pads are immutable even if the user's buttons change
     * during WAIT. Echo all actual pads on RUN, including latched local ones. */
    UrFrame *v=uc_fetch(c,e->frame);
    e->reply[1]=uo_pending(c)?UE_OOB:0;
    if(!v)return;
    pads_at(c,e->frame,predicted);
    /* A newer all-actual frame cannot bypass an older unresolved gap and
     * exceed the game's snapshot window. */
    if((e->frame>c->released || v->mask!=c->history.all) && e->frame-c->released>=e->window)return;
    if(!up_run(c,now))return;
    e->granted=1;e->reply[1]|=UE_OK;
    memcpy(e->used[e->frame&(UE_PREDICTION_SLOTS-1)],predicted,UR_PAD_BYTES(c->history.width));
    /* Ready-start games keep four AVR slots; absent humans are neutral CPU
     * placeholders, never fabricated human samples in relay history. */
    for(unsigned s=0;s<4;s++)if((c->history.all&(1u<<s)) || c->protocol==UR_PROFILE_READYSTART || (c->protocol==UR_PROFILE_DROPIN && c->config[17]==4)){
        memcpy(e->reply+e->reply_n,predicted+UR_STRIDE(c->history.width)*s,c->history.width);e->reply_n+=c->history.width;
    }
    if(c->protocol==UR_PROFILE_DROPIN && c->config[17]!=4 && c->history.all==1){
        memset(e->reply+e->reply_n,0,c->history.width);e->reply_n+=c->history.width;
    }
}
/* Independent local submission and remote consumption use the existing
 * authenticated recovery window. SEND acknowledges ESP storage, not a round trip. */
static void input_stream(UnEndpoint *e,UrClient *c,uint8_t op,const uint8_t *p,uint8_t n,uint32_t now,
                         unsigned records,unsigned bytes,uint8_t burst){
    if(e->input_duplex){error(e,UE_BAD_REQUEST);return;}
    if(!e->running || c->state!=UC_READY){error(e,UE_NOT_READY);return;}
    if(e->delay || e->window || e->granted || e->sampled ||
       (c->protocol!=UR_PROFILE && c->protocol!=UR_PROFILE_REDUNDANT)){
        error(e,UE_BAD_REQUEST);return;
    }
    if(c->services.failure || c->services.desync){error(e,c->services.failure?c->services.failure:UE_DESYNC);return;}
    if(ending(e,c,now) || c->services.barrier_wait)return;
    unsigned stride=__builtin_popcount(c->history.all&~c->history.local)*c->history.width;
    if(burst){
        if(!stride || !(records=burst_limit(records,bytes,stride,7))){error(e,UE_BAD_REQUEST);return;}
    }
    if(op==UE_INPUT_SEND){
        unsigned local=__builtin_popcount(c->history.local)*c->history.width;
        if(n!=4+local){error(e,UE_BAD_REQUEST);return;}
        uint32_t frame=ur_get32(p);
        if(frame!=c->next){error(e,UE_BAD_SEQUENCE);return;}
        if(frame>=0xffffffbfUL){error(e,UE_EXHAUSTED);return;}
        if(frame-c->history.floor>=UR_WINDOW)return;
        uint8_t pads[UR_PAD_MAX]={0};unsigned at=4;
        for(unsigned s=0;s<4;s++)if(c->history.local&(1u<<s)){
            memcpy(pads+UR_STRIDE(c->history.width)*s,p+at,c->history.width);at+=c->history.width;
        }
        if(!uc_submit(c,frame,pads)){error(e,UE_BACKEND);return;}
        e->input_stream=1;e->reply[1]=UE_OK;
    }else if(n){error(e,UE_BAD_REQUEST);return;}
    {
        e->input_stream=1;
        unsigned count=0;
        while(count<records){
            UrFrame *v=uc_fetch(c,e->frame+count);
            if(!v || v->mask!=c->history.all)break;
            if(!count){ur_put32(e->reply+2,e->frame);e->reply_n=burst?7:6;}
            for(unsigned s=0;s<4;s++)if((c->history.all&~c->history.local)&(1u<<s)){
                memcpy(e->reply+e->reply_n,v->pads+UR_STRIDE(c->history.width)*s,c->history.width);
                e->reply_n+=c->history.width;
            }
            count++;
        }
        if(!count)return;
        if(!uc_release(c,e->frame+count)){error(e,UE_BACKEND);return;}
        c->metrics.input_events++;
        if(burst)e->reply[6]=count;
        e->frame+=count;up_advance(c,e->frame,now);e->reply[1]=UE_OK;
    }
}
static void snapshot(UnEndpoint *e,UrClient *c,const uint8_t *p,uint8_t n,uint32_t now,uint8_t burst){
    if(un_recovery_enabled(c->config) && e->running && c->state>=UC_BINDING && c->state<UC_READY)return;
    if((n!=0 && n!=11 && n!=14 && n!=22) ||
       ((n==11 || n==22) && !un_snapshot_enabled(c->config)) || c->protocol!=UR_PROFILE_LOBBY || !e->running ||
       c->state!=UC_READY || (!(c->lobby.active&c->history.local) && !(un_recovery_enabled(c->config) && !n))){error(e,UE_BAD_REQUEST);return;}
    if(un_snapshot_timed(c->config) && ((n==11 || n==22) || (n==14 && !(p[0]&0xc0u)))){
        error(e,UE_BAD_REQUEST);return;
    }
    snapshot_tick(c,now);
    if(n==11 || n==22){
        uint8_t record[14];unsigned seen=0;
        for(unsigned at=0;at<n;at+=11){
            if(!un_snapshot_unpack(record,p+at) || !snapshot_owner(c,record,c->history.local) ||
               (seen&(1u<<record[0]))){error(e,UE_BAD_REQUEST);return;}
            seen|=1u<<record[0];
        }
        for(unsigned at=0;at<n;at+=11){un_snapshot_unpack(record,p+at);snapshot_store(c,record,1,now);}
        /* Steady-state replies omit the redundant lobby header, freeing
         * room for two complete states inside the 24-byte UART payload.
         * Lobby polls still observe membership/epoch changes. */
        if(!burst && c->lobby.phase==2 && !(c->snapshots.unread&0x1ff0u)){
            e->reply[1]=UE_OK;e->reply_n=2;
            for(unsigned k=0;k<4 && e->reply_n<24;k++){
                unsigned car=(c->snapshots.read_scan+k)&3u;
                if(!(c->snapshots.unread&(1u<<car)))continue;
                un_snapshot_pack(e->reply+e->reply_n,c->snapshots.data[car]);e->reply_n+=11;
                c->metrics.delivered++;
                c->snapshots.unread&=(uint16_t)~(1u<<car);
            }
            c->snapshots.read_scan=(c->snapshots.read_scan+1u)&3u;
            return;
        }
    }else{
        if(n && !snapshot_owner(c,p,c->history.local)){error(e,UE_BAD_REQUEST);return;}
        if(n)snapshot_store(c,p,1,now);
    }
    e->reply[1]=UE_OK;e->reply[2]=c->lobby.active;e->reply[3]=c->lobby.phase;
    e->reply[4]=c->lobby.epoch;e->reply[5]=c->lobby.epoch>>8;e->reply_n=burst?7:6;
    if(burst)e->reply[6]=0;
    /* Immutable CONFIG and initial setup precede replaceable state. Rotate
     * mutable records so a constantly updated car cannot starve other cars
     * (or the initial setup records needed to leave the menu). */
    unsigned index=12;
    if(!(c->snapshots.unread&(1u<<12))){
        for(index=8;index<12;index++)if(c->snapshots.unread&(1u<<index))break;
        if(index==12)for(unsigned n=0;n<8;n++){
            index=(c->snapshots.read_scan+n)&7u;
            if(c->snapshots.unread&(1u<<index))break;
        }
    }
    if(c->lobby.phase && (c->snapshots.unread&(1u<<index))){
        memcpy(e->reply+e->reply_n,c->snapshots.data[index],14);e->reply_n+=14;
        if(index<4){c->metrics.delivered++;if(un_recovery_enabled(c->config))c->recovery.forwarded|=1u<<index;}
        c->snapshots.unread&=(uint16_t)~(1u<<index);
        if(index<8)c->snapshots.read_scan=(index+1u)&7u;
        if(burst)e->reply[6]=1;
    }
}
static void live(UnEndpoint *e,UrClient *c,const uint8_t *p,uint8_t n,unsigned records,uint8_t burst){
    if((n!=0 && n!=5) || c->protocol!=UR_PROFILE_LIVE || !e->running || c->state!=UC_READY ||
       (n && p[0]>=16)){error(e,UE_BAD_REQUEST);return;}
    e->reply[1]=UE_OK;
    if(n){
        if(c->live.pending || !c->lobby.phase)e->reply[1]=0;
        else{memcpy(c->live.out,p,5);c->live.pending=1;}
    }
    e->reply[2]=c->history.local;e->reply[3]=c->lobby.active;e->reply[4]=c->lobby.host;
    e->reply[5]=c->lobby.phase;e->reply[6]=c->lobby.epoch;e->reply[7]=c->lobby.epoch>>8;
    e->reply_n=burst?9:8;
    unsigned count=0;
    for(unsigned step=0;step<64 && count<records;step++){
        unsigned index=c->live.scan++;c->live.scan&=63;
        unsigned seat=index/16,key=index&15;
        if(c->live.unread[seat]&(1u<<key)){
            c->live.unread[seat]&=(uint16_t)~(1u<<key);
            e->reply[e->reply_n++]=seat;e->reply[e->reply_n++]=key;
            memcpy(e->reply+e->reply_n,c->live.value[index],4);e->reply_n+=4;count++;
        }
    }
    if(burst)e->reply[8]=count;
}
static void burst(UnEndpoint *e,UrClient *c,const uint8_t *p,uint8_t n,uint32_t now){
    if(!n){
        const uint8_t info[]={1,15,8,20}; /* version, sources, records, data bytes */
        e->reply[1]=UE_OK;memcpy(e->reply+2,info,sizeof info);e->reply_n=6;return;
    }
    if(n<3 || !p[1] || p[1]>8 || !p[2] || p[2]>20){error(e,UE_BAD_REQUEST);return;}
    unsigned source=p[0],records=p[1],bytes=p[2];p+=3;n-=3;
    if(source==UE_BURST_REPLAY && !n)replay_burst(e,c,records,bytes);
    else if(source==UE_BURST_INPUT)input_stream(e,c,n?UE_INPUT_SEND:UE_INPUT_READ,p,n,now,records,bytes,1);
    else if(source==UE_BURST_LIVE){
        records=burst_limit(records,bytes,6,9);
        if(!records)error(e,UE_BAD_REQUEST);else live(e,c,p,n,records,1);
    }else if(source==UE_BURST_SNAPSHOT && bytes>=14)snapshot(e,c,p,n,now,1);
    else error(e,UE_BAD_REQUEST);
}
static void chat_read(UnEndpoint *e,UrClient *c){
    uint8_t in[3]={e->read_peer,e->read_offset,14},out[24];
    unsigned n=uo_read(c,in,out);
    if(!n){error(e,UE_BACKEND);return;}
    if(out[0]==4){e->read_peer=4;e->read_count=0;return;}
    e->read_peer=out[0];e->read_id=ur_get32(out+1);e->read_count=n-10;
    e->reply[1]=UE_OK;
    /* source seats, destination seats, channel, total, offset, data */
    memcpy(e->reply+2,out+5,n-5);e->reply_n=n-3;
}
/* Commit completed simulation without granting another speculative tick.
 * A repair interrupts settling; after replay, a new SETTLE retries the drain.
 * Never submit input here: the peer already has each completed tick's local
 * sample. Retries/worker calls cannot acknowledge the same grant twice. */
static void settle(UnEndpoint *e,UrGame *g,UrClient *c,uint8_t n,uint32_t now){
    if(e->input_stream){error(e,UE_BAD_REQUEST);return;}
    if(!e->running || c->state!=UC_READY){error(e,UE_NOT_READY);return;}
    if(n){error(e,UE_BAD_REQUEST);return;}
    if(ending(e,c,now))return;
    if(c->services.failure || c->services.desync){error(e,c->services.failure?c->services.failure:UE_DESYNC);return;}
    if(e->repair){repair_notice(e);return;}
    if(e->granted){
        if(e->frame>=0xffffffbfUL){error(e,UE_EXHAUSTED);return;}
        e->frame++;up_advance(c,e->frame,now);e->granted=e->sampled=0;
    }
    uint8_t pads[UR_PAD_MAX];reconcile(e,g,c,pads);
    if(e->repair)repair_notice(e);
    else if(c->released==e->frame)e->reply[1]=UE_OK;
}
static void fence(UnEndpoint *e,UrClient *c,uint8_t op,const uint8_t *p,uint8_t n,uint32_t now){
    if(!e->running || c->state!=UC_READY){error(e,UE_NOT_READY);return;}
    if(op==UE_END && !n){
        us_end(c,US_ENDED,now);
        if(ending(e,c,now) && e->reply[1]==UE_ERROR && e->reply[2]==UE_ENDED){
            e->reply[1]=UE_OK;e->reply_n=2;
        }
        return;
    }
    if(op!=UE_BARRIER || n!=1 || e->input_stream){error(e,UE_BAD_REQUEST);return;}
    if(c->protocol==UR_PROFILE_DROPIN && c->lobby.phase==1 && p[0]==1){e->reply[1]=UE_OK;return;}
    if(ending(e,c,now))return;
    if(c->services.failure || c->services.desync){error(e,c->services.failure?c->services.failure:UE_DESYNC);return;}
    /* Barriers never acknowledge a RUN. Rollback callers must first settle
     * predictions: every completed step must already be actual and confirmed. */
    if(e->repair || c->released!=e->frame){error(e,UE_BAD_REQUEST);return;}
    if(e->granted){
        UrFrame *v=uc_fetch(c,e->frame);
        if(!v || v->mask!=c->history.all || memcmp(v->pads,e->used[e->frame&(UE_PREDICTION_SLOTS-1)],UR_PAD_BYTES(c->history.width))){
            error(e,UE_BAD_REQUEST);return;
        }
    }
    int result=us_barrier(c,p[0],e->frame+e->granted,now);
    if(!result)error(e,UE_BAD_REQUEST);
    else if(result==2){c->services.barrier_wait=0;e->reply[1]=UE_OK;}
}
uint8_t ue_service(UnEndpoint *e,UrGame *g,UrClient *c,uint32_t now){
    if(e->cancelling)return 0;
    ue_tick(e,c,now);
    if(!e->awaiting){
        if(input_duplex_service(e,c,now))return 1;
        return snapshot_stream_service(e,c,now);
    }
    e->reply[0]=e->tag;e->reply[1]=0;e->reply_n=2;
    if(e->request[1]==UE_EXCHANGE)
        exchange(e,g,c,e->request+2,e->request_n-2,now);
    else if(e->request[1]==UE_SETTLE)settle(e,g,c,e->request_n-2,now);
    else if(e->request[1]==UE_BARRIER || e->request[1]==UE_END)
        fence(e,c,e->request[1],e->request+2,e->request_n-2,now);
    else ready(e,g,c,now);
    if(!(e->reply[1]&(UE_OK|UE_ERROR|UE_REPAIR|UE_JOINING)))return 0;
    e->awaiting=0;un_send(UE_REPLY,e->reply,e->reply_n);return 1;
}
void ue_command(UnEndpoint *e,UrGame *g,UrClient *c,const uint8_t *p,uint8_t n,uint32_t now){
    uint8_t fail[3]={n?p[0]:0,UE_ERROR,UE_BAD_SEQUENCE};
    if(e->cancelling==1){fail[2]=UE_NOT_READY;un_send(UE_REPLY,fail,3);return;}
    if(e->cancelling==2)e->cancelling=0;
    if(n<2 || n>24){fail[2]=UE_BAD_REQUEST;un_send(UE_REPLY,fail,3);return;}
    if(e->valid && p[0]==e->tag){
        if(n==e->request_n && !memcmp(p,e->request,n)){
            if(!e->awaiting)un_send(UE_REPLY,e->reply,e->reply_n);
        }
        else un_send(UE_REPLY,fail,3);
        return;
    }
    if(e->awaiting || p[0]!=(uint8_t)(e->tag+1)){un_send(UE_REPLY,fail,3);return;}
    memcpy(e->request,p,n);e->request_n=n;e->valid=1;e->tag=p[0];
    e->reply[0]=e->tag;e->reply[1]=0;e->reply_n=2;
    uint8_t op=p[1];p+=2;n-=2;
    if(op==UE_START && n==21 && (p[19]&0x7f)<=8 && p[20]<=8 &&
       (!(p[19]&0x80) || e->policy[1]<=(p[19]&0x7f)) && !e->configured){
        uint8_t config[19];memcpy(config,p,sizeof config);config[18]&=(uint8_t)~UE_DEFER;
        uint8_t result=ue_start(config,now);
        if(result)error(e,result);else{
            e->defer=!!(p[18]&UE_DEFER);e->configured=1;e->read_peer=4;e->delay=p[19]&0x7f;e->window=p[20];
            e->policy[0]=(p[19]&0x80)?US_AUTO_DELAY:0;e->policy[2]=e->delay;ready(e,g,c,now);
        }
    }else if(op==UE_POLL && !n && e->configured)ready(e,g,c,now);
    else if(op==UE_JOIN)join_command(e,c,p,n);
    else if(op==UE_LOBBY && n==3 && un_recovery_enabled(c->config) && c->state>=UC_BINDING && c->state<UC_READY){
        c->recovery.game_ack=0;e->recovery_reported=0;
        if(p[0])return; /* WAIT: an offline command has not reached Room. */
        e->reply[1]=UE_OK;e->reply[2]=e->reply[3]=e->reply[5]=0;e->reply[4]=c->lobby.phase;
        e->reply[6]=c->lobby.epoch;e->reply[7]=c->lobby.epoch>>8;e->reply_n=8;
    }
    else if(op==UE_LOBBY && n==3 && UR_IS_READYSTART(c->config[18]) &&
            (c->state!=UC_READY || !c->lobby.active)){
        if(c->state==UC_FAILED)error(e,UE_BACKEND);
    }
    else if(op==UE_LOBBY && n==3 && (c->protocol==UR_PROFILE_LOBBY || c->protocol==UR_PROFILE_READYSTART || c->protocol==UR_PROFILE_LIVE) && c->state==UC_READY && p[0]<=3){
        uint16_t epoch=(uint16_t)p[1]|((uint16_t)p[2]<<8);
        uint8_t action=p[0];
        if(un_recovery_enabled(c->config) && !(c->lobby.active&c->history.local) && action){
            /* The CPU's finish already counts for this reserved driver. A
             * returning game may reach analysis before its countdown ends. */
            if(action!=3)return;
            action=0;
        }
        if(action && !c->lobby.waiting &&
           (c->lobby.action!=action || c->lobby.expected!=epoch || !c->lobby.ack)){
            if(c->lobby.request==UINT32_MAX)error(e,UE_EXHAUSTED);
            else{c->lobby.request++;c->lobby.action=action;c->lobby.expected=epoch;c->lobby.waiting=1;c->lobby.error=0;c->last_tx=now-250;}
        }
        if(!action || !c->lobby.waiting){
            if(c->lobby.error && action && c->protocol!=UR_PROFILE_READYSTART)error(e,UE_BAD_REQUEST);
            else{e->reply[1]=UE_OK;e->reply[2]=c->lobby.active;e->reply[3]=c->lobby.pending;
                e->reply[4]=c->lobby.phase;e->reply[5]=c->lobby.ready;
                e->reply[6]=c->lobby.epoch;e->reply[7]=c->lobby.epoch>>8;e->reply_n=8;
                if(un_recovery_enabled(c->config)){
                    e->reply[3]=c->lobby.host<4?1u<<c->lobby.host:0;e->reply[5]=c->recovery.countdown;
                    uint8_t complete=c->lobby.phase==1 || c->recovery.forwarded==15 ||
                        (!(c->snapshots.valid&15) && (c->lobby.active&c->history.local));
                    if(complete && c->recovery.generation && !c->recovery.game_failed){
                        if(e->recovery_reported==c->recovery.generation && c->recovery.game_ack!=c->recovery.generation){
                            c->recovery.game_ack=c->recovery.ack=c->recovery.generation;
                            c->recovery.dirty=1;c->last_tx=now-250;
                        }
                        e->recovery_reported=c->recovery.generation;
                    }
                }
                if(c->protocol==UR_PROFILE_READYSTART){e->reply[8]=c->history.local;e->reply_n=9;}}
        }
    }
    else if(op==UE_SNAPSHOT)snapshot(e,c,p,n,now,0);
    else if(op==UE_SNAPSHOT_STREAM && n<=1){
        if(!n){e->reply[1]=UE_OK;e->reply[2]=1;e->reply[3]=2;e->reply_n=4;}
        else if(!p[0]){e->snapshot_stream=e->snapshot_error=0;e->reply[1]=UE_OK;}
        else if(p[0]>2 || (p[0]==2)!=!!un_snapshot_timed(c->config) || e->input_duplex ||
                c->protocol!=UR_PROFILE_LOBBY || !un_snapshot_enabled(c->config))error(e,UE_BAD_REQUEST);
        else if(!e->running || c->state!=UC_READY || c->lobby.phase!=2)error(e,UE_NOT_READY);
        else{snapshot_tick(c,now);e->snapshot_stream=p[0];e->snapshot_error=0;e->snapshot_epoch=c->lobby.epoch;e->snapshot_at=now;e->reply[1]=UE_OK;}
    }
    else if(op==UE_PERFORMANCE && n<=1){
        if(!n){e->reply[1]=UE_OK;e->reply[2]=1;e->reply[3]=6;e->reply[4]=20;e->reply_n=5;}
        else performance(e,c,p[0],now);
    }
    else if(op==UE_SCORES){
        uint8_t count=ue_scores(p,n,now,e->reply+2);
        if(!count || count>22)error(e,UE_BAD_REQUEST);
        else{
            e->reply[1]=UE_OK;e->reply_n=2+count;
            if(n==6 && p[0]==4 && count>=9 && !e->reply[2]){
                unsigned length=e->reply[10];
                if(length>13 || count!=9+length)error(e,UE_BACKEND);
                else for(unsigned i=0;i<length;i++)e->reply[11+i]=display_char(e,e->reply[11+i]);
            }
        }
    }
    else if(op==UE_CAPTURE_TIME && !n){
        e->reply[1]=UE_OK;e->reply[2]=1;e->reply[3]=32;e->reply[4]=1;e->reply[5]=2;e->reply_n=6;
    }
    else if(op==UE_INPUT_STREAM && n<=1){
        if(!n){e->reply[1]=UE_OK;e->reply[2]=1;e->reply[3]=8;e->reply[4]=24;e->reply_n=5;}
        else if(!p[0]){e->input_duplex=e->input_error=0;e->reply[1]=UE_OK;}
        else if(p[0]!=1 || e->snapshot_stream || e->delay || e->window || e->granted || e->sampled ||
                (c->protocol!=UR_PROFILE && c->protocol!=UR_PROFILE_REDUNDANT) || c->history.all==c->history.local)error(e,UE_BAD_REQUEST);
        else if(!e->running || c->state!=UC_READY || c->services.barrier_wait)error(e,UE_NOT_READY);
        else{
            if(!e->input_duplex){e->input_stream=e->input_duplex=e->input_dirty=1;e->input_error=0;
                e->input_sent=e->frame;e->input_at=now;}
            e->reply[1]=UE_OK;
        }
    }
    else if(op==UE_LIVE)live(e,c,p,n,1,0);
    else if(op==UE_BURST)burst(e,c,p,n,now);
    else if(op==UE_BURST_ACK && n==1 && e->running && c->state==UC_READY &&
            e->repair==3 && e->burst_count && p[0]==(uint8_t)(e->repair_cursor+e->burst_count)){
        e->repair_cursor+=e->burst_count;e->burst_count=0;
        e->repair=e->repair_cursor==e->repair_end?0:1;e->reply[1]=UE_OK;
    }
    else if(op==UE_EXCHANGE)exchange(e,g,c,p,n,now);
    else if(op==UE_INPUT_SEND || op==UE_INPUT_READ)input_stream(e,c,op,p,n,now,1,24,0);
    else if(op==UE_SETTLE)settle(e,g,c,n,now);
    else if(op==UE_BARRIER || op==UE_END)fence(e,c,op,p,n,now);
    else if((op==UE_REPLAY_READ || op==UE_REPLAY_NEXT) && !n && e->repair && c->state==UC_READY){
        if(e->repair==3 || (op==UE_REPLAY_NEXT && e->repair!=2))error(e,UE_BAD_REQUEST);
        else if(op==UE_REPLAY_NEXT && ++e->repair_cursor==e->repair_end){e->repair=0;e->reply[1]=UE_OK;}
        else{replay(e,c);e->repair=2;}
    }
    else if(op==UE_CREDIT && n==1 && e->running && c->state==UC_READY && (unsigned)g->credit+p[0]<=255){
        g->mode=UG_PUSH;g->credit+=p[0];e->reply[1]=UE_OK;e->reply[2]=g->credit;e->reply_n=3;
    }else if(op>=UE_CHAT_BEGIN && op<=UE_CHAT_STATUS){
        if(!e->running || c->state!=UC_READY)error(e,UE_NOT_READY);
        else if(op==UE_CHAT_BEGIN && n==3 && p[0] && !(p[0]&~c->history.all) &&
                !(p[0]&c->history.local) && p[2] && p[2]<=128){
            if(e->chat_writing || c->oob.pending || c->oob.uploaded!=c->oob.total){}
            else if(c->oob.id==65535)error(e,UE_EXHAUSTED);
            else{e->chat_dest=p[0];e->chat_channel=p[1];e->chat_total=p[2];e->chat_writing=1;e->reply[1]=UE_OK;}
        }else if(op==UE_CHAT_WRITE && n && n<=18 && e->chat_writing){
            uint8_t in[24];unsigned offset=e->chat_writing==1?0:c->oob.uploaded;
            unsigned id=c->oob.id+(e->chat_writing==1);
            in[0]=id;in[1]=id>>8;in[2]=e->chat_dest;in[3]=e->chat_channel;in[4]=e->chat_total;in[5]=offset;
            memcpy(in+6,p,n);uint8_t result=uo_write(c,in,n+6,now);
            if(!result)error(e,UE_BAD_REQUEST);
            else if(result==1){e->chat_writing=c->oob.uploaded==c->oob.total?0:2;e->reply[1]=UE_OK;}
        }else if(op==UE_CHAT_READ && !n)chat_read(e,c);
        else if(op==UE_CHAT_NEXT && !n && e->read_count){
            /* Only NEXT acknowledges a displayed/stored chunk. Merely reading
             * another game reply cannot consume chat. Cached retries are safe. */
            unsigned next=e->read_offset+e->read_count;
            if(next==c->oob.peer[e->read_peer].rx_total){
                if(!uo_ack(c,e->read_peer,e->read_id))error(e,UE_BACKEND);
                else{e->read_peer=4;e->read_offset=e->read_count=0;e->reply[1]=UE_OK;}
            }else{e->read_offset=next;e->read_count=0;chat_read(e,c);}
        }else if(op==UE_CHAT_STATUS && !n){
            e->reply[1]=UE_OK|(uo_pending(c)?UE_OOB:0);e->reply[2]=e->chat_writing?1:c->oob.pending?2:0;e->reply_n=3;
        }else error(e,UE_BAD_REQUEST);
    }else if(op==UE_SERVICE_INFO && !n){
        /* Version, feature flags, max name bytes, checksum slots, max delay.
         * Feature bits: status, automatic delay, names, checksums, dual lane. */
        static const uint8_t info[]={5,255,US_NAME_MAX,US_CHECK_SLOTS,8};
        e->reply[1]=UE_OK;memcpy(e->reply+2,info,sizeof info);e->reply_n=2+sizeof info;
    }else if(op==UE_DELAY_POLICY && n==2 && !e->configured && p[0]<=8 && p[1]>=10 && p[1]<=50){
        e->policy[1]=p[0];e->policy[3]=p[1];e->reply[1]=UE_OK;
    }else if(op==UE_NAME_FORMAT && !e->configured && n==2 && p[0]<=UE_CUSTOM_MAP && p[1]>=32 && p[1]<=126){
        e->name_mode=p[0];e->name_fallback=p[1];
        if(p[0]==UE_CUSTOM_MAP)memset(e->name_map,p[1],sizeof e->name_map);
        e->reply[1]=UE_OK;
    }else if(op==UE_NAME_MAP && !e->configured && e->name_mode==UE_CUSTOM_MAP && n>=2 && p[0]<95 && n-1<=95-p[0]){
        unsigned i;for(i=1;i<n;i++)if(p[i]<32 || p[i]>126)break;
        if(i!=n)error(e,UE_BAD_REQUEST);
        else{memcpy(e->name_map+p[0],p+1,n-1);e->reply[1]=UE_OK;}
    }else if(op==UE_DISPLAY_NAME && n==2 && p[0]<4 && p[1] && p[1]<=20){
        unsigned seat=p[0];
        if(c->state!=UC_READY || !c->services.active || !(c->history.all&(1u<<seat)))error(e,UE_NOT_READY);
        else if(c->services.known&(1u<<seat)){
            unsigned count=c->services.length[seat];if(count>p[1])count=p[1];
            e->reply[1]=UE_OK;e->reply[2]=seat;e->reply[3]=count;e->reply_n=4+count;
            for(unsigned i=0;i<count;i++)e->reply[4+i]=display_char(e,c->services.names[seat][i]);
        }
    }else if(op==UE_LAST_FAILURE && n==1 && p[0]<=1){
        if(p[0])memset(&c->failure,0,sizeof c->failure);
        e->reply[1]=UE_OK;memcpy(e->reply+2,&c->failure,sizeof c->failure);e->reply_n=2+sizeof c->failure;
    }else if(op==UE_COMPACT_STATUS && !n){compact_status(e,c);
    }else if(op==UE_NET_STATUS && !n){
        ue_tick(e,c,now);e->reply[1]=UE_OK;
        e->reply_n=2+us_status(c,e->frame,e->running?e->delay:c->services.delay,now,e->reply+2);
    }else if(op==UE_PEER_STATUS && n==1){
        uint8_t count=us_peer_status(c,p[0],now,e->reply+2);
        if(!count)error(e,UE_BAD_REQUEST);else{e->reply[1]=UE_OK;e->reply_n=2+count;}
    }else if(op==UE_PROFILE_SET && !e->configured){
        int result=us_profile_set(c,p,n);
        if(result!=1)error(e,result<0?UE_BACKEND:UE_BAD_REQUEST);else e->reply[1]=UE_OK;
    }else if(op==UE_PROFILE_GET && !e->configured && n==3 && p[0]<4 && p[2] && p[2]<=16){
        if(!p[1]){
            int length=us_profile_read(p[0],e->name_read);
            e->name_valid=length>=-1 && length<=US_NAME_MAX;
            e->name_length=length<0?0:length;e->name_seat=p[0];
        }
        if(!e->name_valid || e->name_seat!=p[0] || p[1]>e->name_length)error(e,UE_BACKEND);
        else{
            unsigned count=e->name_length-p[1];if(count>p[2])count=p[2];
            e->reply[1]=UE_OK;e->reply[2]=p[0];e->reply[3]=e->name_length;e->reply[4]=p[1];
            memcpy(e->reply+5,e->name_read+p[1],count);e->reply_n=5+count;
        }
    }else if(op>=UE_NAME_SET && op<=UE_CHECK_STATUS){
        if((!e->running && !(op==UE_NAME_GET && c->protocol==UR_PROFILE_READYSTART && c->services.active)) || c->state!=UC_READY)error(e,UE_NOT_READY);
        else if(op==UE_NAME_SET){
            if(!us_name_set(c,p,n))error(e,UE_BAD_REQUEST);else e->reply[1]=UE_OK;
        }else if(op==UE_NAME_GET && n==3 && p[0]<4 && (c->history.all&(1u<<p[0])) && p[2] && p[2]<=16){
            unsigned seat=p[0];
            if(!p[1]){
                e->name_valid=!!(c->services.known&(1u<<seat));e->name_seat=seat;
                e->name_length=c->services.length[seat];
                memcpy(e->name_read,c->services.names[seat],US_NAME_MAX);
            }
            if(!e->name_valid && !p[1]){} /* unknown remote name: WAIT */
            else if(!e->name_valid || e->name_seat!=seat || p[1]>e->name_length)error(e,UE_BAD_REQUEST);
            else{
                unsigned count=e->name_length-p[1];if(count>p[2])count=p[2];
                e->reply[1]=UE_OK;e->reply[2]=seat;e->reply[3]=e->name_length;e->reply[4]=p[1];
                memcpy(e->reply+5,e->name_read+p[1],count);e->reply_n=5+count;
            }
        }else if(op==UE_CHECK && (n==8 || (n==4 && ((!e->window && e->granted) ||
                  (!e->granted && e->frame && c->released==e->frame))))){
            /* Compact lockstep form refers to the current RUN grant. Metadata
             * never acknowledges/advances it; retries are transaction-cached. */
            uint32_t frame=n==4?e->frame-!e->granted:ur_get32(p);
            if((!e->granted && !e->frame) || frame>=e->frame+e->granted ||
               e->frame-frame>UR_WINDOW || (e->window && frame>=c->released))error(e,UE_BAD_REQUEST);
            else{
                int result=us_check(c,frame,ur_get32(p+n-4));
                if(!result)error(e,UE_BAD_REQUEST);else if(result==1)e->reply[1]=UE_OK;
            }
        }else if(op==UE_CHECK_STATUS && !n){
            UrServices *s=&c->services;e->reply[1]=UE_OK;
            e->reply[2]=s->check_known|(s->desync?2:0);ur_put32(e->reply+3,s->check_frame);
            ur_put32(e->reply+7,s->check_hash);e->reply[11]=0;e->reply[12]=s->desync;
            for(unsigned i=0;i<US_CHECK_SLOTS;i++)if(s->check[i].mask && s->check[i].frame==s->check_frame)e->reply[11]=s->check[i].mask;
            e->reply_n=13;
        }else error(e,UE_BAD_REQUEST);
    }else error(e,UE_BAD_REQUEST);
    if(e->defer && !(op==UE_START && UR_IS_READYSTART(c->config[18])) && (op==UE_START || op==UE_POLL || op==UE_EXCHANGE || op==UE_SETTLE || op==UE_BARRIER || op==UE_END) &&
       !(e->reply[1]&(UE_OK|UE_ERROR|UE_REPAIR|UE_JOINING)))e->awaiting=1;
    else un_send(UE_REPLY,e->reply,e->reply_n);
}
