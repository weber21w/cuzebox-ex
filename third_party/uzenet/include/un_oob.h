#ifndef UN_OOB_H
#define UN_OOB_H
#include "un_relay.h"
/* Reliable messages are outside the simulation timeline. All storage is
 * bounded and lives on the ESP; no allocation, timer or AVR-sized message. */
#define UO_MESSAGE_MAX 128
#define UO_WRITE_MAX 18
#define UO_READ_MAX 14
#define UO_FRAGMENT 24
/* 120 updates/s to three peers plus their ACK/control
 * traffic needs up to 720 packets/s. This is an aggregate send budget,
 * not an update timer; idle clients generate no extra traffic. */
#define UO_INTERVAL_MS 1
typedef struct {
    UrReplay replay;
    uint32_t seq,tx_id,rx_id,consumed,ack_id,send_at;
    uint16_t retry_ms;
    uint8_t mask,tx_offset,tx_sent,rx_used,rx_total,rx_count,rx_channel,rx_dest;
    uint8_t ack_pending,ack_turn,ack[8],data[UO_MESSAGE_MAX];
} UoPeer;
typedef struct {
    UoPeer peer[4];
    UrReplay replay;
    uint32_t seq,last_tx;
    uint16_t id;
    uint8_t dest,channel,total,uploaded,pending,scan,selected,read_scan,data[UO_MESSAGE_MAX];
} UrOob;
struct UrClient;
uint8_t uo_pending(const struct UrClient *c);
/* WRITE returns 0 invalid, 1 accepted/retry, 2 busy (no mutation). */
uint8_t uo_write(struct UrClient *c,const uint8_t *p,uint8_t n,uint32_t now);
uint8_t uo_read(struct UrClient *c,const uint8_t request[3],uint8_t reply[24]);
int uo_ack(struct UrClient *c,unsigned peer,uint32_t id);
int uo_receive(struct UrClient *c,unsigned peer,uint8_t mask,int ack,
               uint32_t id,const uint8_t *payload,uint32_t now);
int uo_next(struct UrClient *c,uint32_t now,uint8_t packet[UR_PACKET_MAX],uint8_t destination[6]);
/* Internal lane: authenticated payload byte 5 is 1 (ordinary chat is 0).
 * Both lanes share packet replay counters and the profile's network budget,
 * but have independent message IDs, buffers, delivery/consumption ACKs. */
uint8_t uo_internal_pending(const struct UrClient *c);
int uo_internal_ack(struct UrClient *c,unsigned peer,uint32_t id);
uint8_t uo_internal_send(struct UrClient *c,const uint8_t *data,uint8_t n,uint32_t now);
#endif
