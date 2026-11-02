#include "un_wire.h"
#include "un_diagnostics.h"
#ifdef UN_WIRE_DIAGNOSTICS
#define RX_FAULT(reason) un_diag_rx_fault(reason)
#else
#define RX_FAULT(reason) ((void)0)
#endif
enum { RX_ACTIVE=1, RX_ESCAPED=2, RX_BAD=4 };
uint8_t un_crc8(uint8_t crc, uint8_t byte) {
    uint8_t i;
    crc ^= byte;
    for(i=0;i<8;i++) crc=(uint8_t)((crc<<1)^((crc&128)?7:0));
    return crc;
}
static void escaped(uint8_t byte) {
    if(byte==UN_DELIM || byte==UN_ESCAPE) {
        un_put(UN_ESCAPE); byte^=0x20;
    }
    un_put(byte);
}
uint8_t un_send(uint8_t type,const uint8_t *payload,uint8_t length) {
    uint8_t crc,i;
    if(length>UN_PAYLOAD_MAX) return 0;
    un_put(UN_DELIM); escaped(type); crc=un_crc8(0,type);
    for(i=0;i<length;i++) { escaped(payload[i]); crc=un_crc8(crc,payload[i]); }
    escaped(crc); un_put(UN_DELIM);
    return 1;
}
uint8_t un_encoded_size(uint8_t type,const uint8_t *p,uint8_t n) {
    uint8_t bytes=2,crc=0,b=type;
    if(n>UN_PAYLOAD_MAX)return 0;
    for(unsigned i=0;i<n+2u;i++){
        if(i==n+1u)b=crc;
        bytes+=1+(b==UN_DELIM || b==UN_ESCAPE);
        if(i<=n)crc=un_crc8(crc,b);
        if(i<n)b=p[i];
    }
    return bytes;
}
uint8_t un_receive(UnRx *rx,uint8_t byte) {
    uint8_t count=rx->count, i, crc=0;
    if(byte==UN_DELIM) {
        uint8_t prior=rx->flags;
        uint8_t valid=(prior==RX_ACTIVE && count>=2);
        (void)prior;
        rx->flags=RX_ACTIVE; rx->count=0;
        if(valid) {
            for(i=0;i<count;i++) crc=un_crc8(crc,rx->data[i]);
            if(!crc) return count-1;
            RX_FAULT(UN_RX_CRC);
        }
        if(!valid && (count || (prior & (RX_BAD|RX_ESCAPED)))) RX_FAULT(UN_RX_MALFORMED);
        return 0;
    }
    if(!(rx->flags&RX_ACTIVE) || (rx->flags&RX_BAD)) return 0;
    if(rx->flags&RX_ESCAPED) {
        rx->flags&=(uint8_t)~RX_ESCAPED;
        /* Reject noncanonical escape encodings, including repeated escape. */
        if(byte!=0x5e && byte!=0x5d) { rx->flags|=RX_BAD; return 0; }
        byte^=0x20;
    } else if(byte==UN_ESCAPE) { rx->flags|=RX_ESCAPED; return 0; }
    if(count==UN_BODY_MAX) rx->flags|=RX_BAD;
    else { rx->data[count]=byte; rx->count=count+1; }
    return 0;
}
