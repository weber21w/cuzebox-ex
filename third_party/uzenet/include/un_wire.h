/* Experimental UART framing. Not the Internet packet/authentication format. */
#ifndef UN_WIRE_H
#define UN_WIRE_H
#include <stdint.h>
#define UN_PAYLOAD_MAX 24
#define UN_BODY_MAX (UN_PAYLOAD_MAX + 2)
#define UN_ENCODED_MAX (2 * UN_BODY_MAX + 2)
#define UN_DELIM 0x7e
#define UN_ESCAPE 0x7d
#define UN_PROTOCOL_VERSION 4
#define UN_CANCEL 5
#define UN_CANCEL_REPLY 0x85
#define UN_DIAGNOSTIC 6
#define UN_DIAGNOSTIC_REPLY 0x86
#define UN_FAULT_CONFIG 7 /* test firmware only */
#define UN_FAULT_REPLY 0x87
#define UN_ECHO 8
#define UN_ECHO_REPLY 0x88
/* Opt-in full-duplex compact snapshots. Independent of request/reply tags. */
#define UN_SNAPSHOT_WRITE 0x31
#define UN_SNAPSHOT_FRAME 0xb2
#define UN_INPUT_WRITE 0x32
#define UN_TIMED_WRITE 0x33
#define UN_INPUT_FRAME 0xb3
#define UN_TIMED_FRAME 0xb4
#define UN_FRAME_RUN 0x80
#define UN_FRAME_OOB 0x40
#define UN_FRAME_ACTUAL_MASK 0x0f
/* Experimental predecessor INPUT/FETCH/RELEASE opcodes are not supported. */
enum { UN_CAPS=1, UN_STATUS=2, UN_EXIT=3, UN_PULL=17,
       UN_TICK=20, UN_READ=21, UN_CONFIG=32, UN_NET_STATUS=33,
       UN_PLAY=34, UN_CREDIT=35, UN_LINK=36, UN_OOB_WRITE=37, UN_OOB_READ=38,
       UN_OOB_ACK=39, UN_OOB_STATUS=40, UN_FRAME=0xb0, UN_ERROR=127 };
typedef struct {
    uint8_t data[UN_BODY_MAX];
    uint8_t count, flags;
} UnRx;
/* No hidden allocation or UART driver. Sink must accept every supplied byte.
 * Hardware integration must provide bounded service/flow control separately. */
void un_put(uint8_t byte);
uint8_t un_crc8(uint8_t crc, uint8_t byte);
uint8_t un_send(uint8_t type, const uint8_t *payload, uint8_t length);
uint8_t un_encoded_size(uint8_t type,const uint8_t *payload,uint8_t length);
/* Zero initialize before first use. Positive return = type + payload length;
 * valid bytes remain in data until the next call. Invalid packets return zero. */
uint8_t un_receive(UnRx *rx, uint8_t byte);
#endif
