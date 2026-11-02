#ifndef UN_SESSION_H
#define UN_SESSION_H
#include "un_wire.h"
#include "un_flow.h"
#define UN_DEFERRED_BYTES 512
#define UN_CAP_CONTROL 1
#define UN_CAP_LEGACY_RX 2
#define UN_CAP_GPIO_ENTRY 4
#define UN_CAP_RELAY 8
#define UN_CAP_DIRECT 16
#define UN_CAP_PA7_FLOW 32
#define UN_CAP_GAME 64
#define UN_CAP_OOB 128
#define UN_CAPABILITIES (UN_CAP_CONTROL|UN_CAP_LEGACY_RX|UN_CAP_GPIO_ENTRY|UN_CAP_PA7_FLOW)
#define UN_FLOW_INFO 0x04
#define UN_PA7_ACK_MS 40
#define UN_ENTERED 0x80
typedef struct {
    uint32_t since;
    uint8_t phase;
} UnTrigger;
typedef struct {
    UnRx rx;
    uint8_t deferred[UN_DEFERRED_BYTES];
    uint16_t head,count,generation;
    volatile uint8_t active; /* timer samples ownership without taking UART locks */
    uint8_t source,capabilities,tx_batch; /* platform sets batch before entry */
} UnSession;
/* Call with monotonically increasing milliseconds. Startup/stuck-low ignored.
 * 100 ms stable high arms; 20..100 ms low then release requests entry. */
uint8_t un_trigger_sample(UnTrigger *t,uint8_t high,uint32_t now);
void un_session_enter(UnSession *s);
/* 1 requests platform exit after sending reply. Platform releases ownership. */
uint8_t un_session_byte(UnSession *s,uint8_t byte);
/* Platform may hold a CRC-validated packet in rx.data until TX becomes free. */
uint8_t un_session_dispatch(UnSession *s,uint8_t length);
uint16_t un_defer(UnSession *s,const uint8_t *p,uint16_t length);
uint8_t un_deferred_pop(UnSession *s,uint8_t *out);
/* Drop-in START consumes passive Room !M/00/00 beacons. Preserve every other
 * byte, including partial records. Call while holding the UART TX lock. */
void un_discard_ready_beacons(UnSession *s);
void un_session_rx_reset(UnSession *s);
/* Optional transport backend. Requests and replies stay UART grant driven. */
uint8_t un_session_extension(uint8_t type,const uint8_t *payload,uint8_t length);
uint8_t un_session_can_exit(void);
#endif
