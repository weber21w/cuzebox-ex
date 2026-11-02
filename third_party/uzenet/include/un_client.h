#ifndef UN_CLIENT_H
#define UN_CLIENT_H
#include "un_relay.h"
#include "un_pace.h"
#include "un_oob.h"
#include "un_services.h"
enum {UC_IDLE,UC_TICKET,UC_BINDING,UC_WAITING,UC_READY,UC_FAILED};
typedef struct {uint32_t next;uint16_t rtt,jitter,samples;} UrLink;
typedef struct {
    UrReplay replay;
    uint8_t key[32],address[6],candidate[6],reply_address[6],mask,known,active,reply_pending;
    uint32_t seq,token,probe_at,send_at,last_ok,reply_token,generation;
    UrLink link;
} UrPeer;
/* First failure since explicit clear. stage is the pre-failure UC_* state;
 * source: 1 client, 2 services, 3 checksum, 4 abnormal end, 5 endpoint, 6 UART I/O (reason 1).
 * peers uses global seat bits, zero when the source cannot identify a peer. */
typedef struct {uint8_t source,reason,stage,peers;} UrFailure;
/* Cumulative transport counters. Read on demand; no telemetry packets/timer.
 * Counts/rate denominator avoid division in the packet and UART hot paths. */
typedef struct {
    uint32_t uart_rx,uart_tx,paused_ms,pause_at;
    uint32_t captures,delivered,replaced,stream_tx,stream_rx,reordered,gaps;
    uint32_t input_frames,input_events,critical_packets,gameplay_packets,background_packets;
    uint32_t received_at[4];
    uint16_t uart_peak;
    uint8_t paused,uart_queued;
} UrMetrics;

typedef struct UrClient {
    struct {
        uint16_t generation,ack,game_ack;
        uint8_t reserved,returning,countdown,forwarded,dirty,game_failed;
        uint32_t received,ack_at;
    } recovery;
    UrFailure failure;
    UrMetrics metrics;
    struct {
        uint32_t sequence[64],next;
        uint8_t value[64][4],out[5],pending,scan;
        uint16_t unread[4];
    } live;
    struct {
        uint8_t data[1536],want,ready,reset,primed;
        uint16_t used,total,sent,received,read;
        uint32_t base,polled;
    } join;
    struct {
        uint8_t data[13][14],scan;
        uint16_t valid,dirty,unread,epoch;
        /* Replaceable race state is independent of reliable OOB. Each car
         * retains just its newest state and the peers still needing it. */
        uint32_t sequence,received[4],sent_at[4];
        uint8_t pending[4],stream_scan,read_scan;
        uint32_t captured[4],link_tx[4],link_rx[4],link_base[4];
        uint64_t link_seen[4];
    } snapshots;
    struct {
        uint8_t active,pending,phase,ready,action,waiting,error,host;
        uint16_t epoch,expected;
        uint32_t request,ack,sent,roster_seq;
    } lobby;
    UrHistory history;
    UrPace pace;
    UrOob oob;
    /* Same authenticated message codec; independent bounded consumption.
     * An unread game chat message cannot block ESP-owned metadata. */
    UrOob service;
    UrServices services;
    uint8_t message_turn,message_streak;
    UrReplay replay;
    UrPeer peers[4];
    uint8_t key[32],match[8],config[19],nonce[4],seed[4],endpoint[6],challenge[8];
    uint8_t state,error,peer,challenged,wanted,scan,protocol;
    uint32_t seq,born,last_rx,last_tx,challenge_at,want_frame,fetch_at,release_at,acked,lease_ms;
    uint32_t released;
    uint32_t next;
    UrLink relay;
    uint16_t tx_errors,retries;
} UrClient;
void uc_reset(UrClient *c); /* cold initialization: clears diagnostics too */
void uc_restart(UrClient *c); /* initialized clients only: retain failure */
void uc_note_failure(UrClient *c,uint8_t source,uint8_t reason,uint8_t peers);
int uc_begin(UrClient *c,const uint8_t config[19],const uint8_t nonce[4],uint32_t now,uint8_t request[25]);
int uc_ticket(UrClient *c,const uint8_t ticket[68],uint32_t now);
int uc_receive(UrClient *c,const uint8_t *packet,size_t length,uint32_t now);
/* Call on UART/network activity or once per millisecond while active. Fresh
 * samples are limited to one datagram/ms, maintenance to one per 5 ms.
 * Returns one authenticated datagram, or zero.
 * All packet OUTPUT buffers below require UR_PACKET_MAX bytes. On success
 * transmit UR_SIZE(packet) bytes (48 without redundancy, up to 72 with it). */
int uc_next(UrClient *c,uint32_t now,uint8_t packet[48]);
int uc_submit(UrClient *c,uint32_t frame,const uint8_t pads[8]);
UrFrame *uc_fetch(UrClient *c,uint32_t frame);
int uc_release(UrClient *c,uint32_t next);
void uc_fail(UrClient *c,uint8_t error);
void uc_rtt(UrLink *link,uint32_t elapsed);
/* Platform supplies actual encoded bytes and PA7 pause state under its UART
 * lock. Pause time is charged once per interval, including timer rollover. */
void uc_uart_metrics(UrClient *c,uint32_t now,unsigned rx,unsigned tx,unsigned queued,int paused);
/* Transport-independent peer path: endpoints are IPv4[4], little-endian port16.
 * No key, endpoint, or Internet packet crosses the AVR interface. */
int ud_offer(UrClient *c,const uint8_t ticket[68],uint32_t now);
void ud_reset_peer(UrClient *c,unsigned peer);
int ud_receive(UrClient *c,const uint8_t *packet,size_t length,const uint8_t source[6],uint32_t now);
uint8_t ud_mask(const UrClient *c,uint32_t now);
int ud_next(UrClient *c,unsigned peer,uint32_t now,uint8_t packet[48],uint8_t destination[6]);
int ud_input(UrClient *c,unsigned peer,const uint8_t relay[48],uint32_t now,uint8_t packet[48],uint8_t destination[6]);
#endif
