#include "un_session.h"
#include <string.h>
__attribute__((weak)) uint8_t un_session_extension(uint8_t type,const uint8_t *p,uint8_t n) {
    (void)type;(void)p;(void)n;return 0;
}
__attribute__((weak)) uint8_t un_session_can_exit(void) {return 1;}
uint8_t un_trigger_sample(UnTrigger *t,uint8_t high,uint32_t now) {
    uint32_t elapsed=now-t->since;
    switch(t->phase) {
    case 0: if(high) {t->phase=1;t->since=now;} break;
    case 1:
        if(!high) t->phase=0;
        else if(elapsed>=100) t->phase=2;
        break;
    case 2: if(!high) {t->phase=3;t->since=now;} break;
    case 3:
        if(high) {
            t->phase=1;t->since=now;
            return elapsed>=20 && elapsed<=100;
        }
        if(elapsed>100) t->phase=0;
        break;
    default: t->phase=0; break;
    }
    return 0;
}
void un_session_rx_reset(UnSession *s) { memset(&s->rx,0,sizeof(s->rx)); }
void un_session_enter(UnSession *s) {
    uint8_t reply[10]={'U','N',UN_PROTOCOL_VERSION,2,0,0,0,0,0,UN_PA7_ACK_MS};
    if(!s->active) {
        s->active=1;s->source=2;s->generation++;
        un_session_rx_reset(s);
    }
    reply[3]=s->source;
    reply[4]=s->capabilities;
    reply[5]=(uint8_t)s->generation;reply[6]=(uint8_t)(s->generation>>8);
    reply[7]=s->tx_batch+1;reply[8]=s->tx_batch;
    un_send(UN_ENTERED,reply,sizeof(reply));
}
uint16_t un_defer(UnSession *s,const uint8_t *p,uint16_t n) {
    uint16_t used=0;
    while(used<n && s->count<UN_DEFERRED_BYTES) {
        s->deferred[(s->head+s->count)%UN_DEFERRED_BYTES]=p[used++];s->count++;
    }
    return used;
}
uint8_t un_deferred_pop(UnSession *s,uint8_t *out) {
    if(!s->count) return 0;
    *out=s->deferred[s->head];s->head=(s->head+1)%UN_DEFERRED_BYTES;s->count--;
    return 1;
}
void un_discard_ready_beacons(UnSession *s){
    uint16_t remaining=s->count;uint8_t bytes[4],used=0;
    while(remaining--){
        (void)un_deferred_pop(s,bytes+used++);
        if(used==4){
            if(bytes[0]=='!' && bytes[1]=='M' && !bytes[2] && !bytes[3])used=0;
            else{(void)un_defer(s,bytes,1);memmove(bytes,bytes+1,3);used=3;}
        }
    }
    (void)un_defer(s,bytes,used);
}
uint8_t un_session_byte(UnSession *s,uint8_t byte) {
    return un_session_dispatch(s,un_receive(&s->rx,byte));
}
uint8_t un_session_dispatch(UnSession *s,uint8_t n) {
    uint8_t reply[UN_PAYLOAD_MAX],type;
    if(!n || !s->active) return 0;
    type=s->rx.data[0];
    if(type==UN_CAPS && n==1) {
        reply[0]=UN_PROTOCOL_VERSION;reply[1]=s->capabilities;reply[2]=UN_PAYLOAD_MAX;reply[3]=4;
        un_send(0x81,reply,4);
    } else if(type==UN_STATUS && n==1) {
        reply[0]=s->active;reply[1]=s->source;
        reply[2]=(uint8_t)s->count;reply[3]=(uint8_t)(s->count>>8);
        un_send(0x82,reply,4);
    } else if(type==UN_FLOW_INFO && n==1) {
        reply[0]=2;reply[1]=s->tx_batch+1;
        reply[2]=s->tx_batch;reply[3]=UN_PA7_ACK_MS;
        un_send(0x84,reply,4);
    } else if(type==UN_EXIT && n==1) {
        if(un_session_can_exit()){un_send(0x83,0,0);return 1;}
        reply[0]=type;reply[1]=2;un_send(UN_ERROR,reply,2);
    } else if(type==UN_PULL && n==2 && s->rx.data[1]>=6) {
        /* Budget is encoded bytes, so escaping cannot exceed the grant. */
        uint8_t budget=(s->rx.data[1]-6)/2,used=0;
        if(budget>UN_PAYLOAD_MAX) budget=UN_PAYLOAD_MAX;
        while(used<budget && un_deferred_pop(s,&reply[used])) used++;
        un_send(0x91,reply,used);
    } else if(un_session_extension(type,s->rx.data+1,n-1)) {
        /* Backend has emitted its bounded reply. */
    } else {
        reply[0]=type;reply[1]=1; /* unsupported type/length/argument */
        un_send(UN_ERROR,reply,2);
    }
    return 0;
}
