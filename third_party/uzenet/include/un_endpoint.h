#ifndef UN_ENDPOINT_H
#define UN_ENDPOINT_H
#include "un_client.h"
#include "un_game.h"
/* One game-facing UART API. Backend UG opcodes are internal test seams. */
#define UE_REQUEST 0x30
#define UE_REPLY 0xb1
#define UE_PREDICTION_SLOTS 16 /* max rollback 8 plus current granted frame */
enum { UE_START=1, UE_POLL, UE_EXCHANGE, UE_CHAT_BEGIN, UE_CHAT_WRITE,
       UE_CHAT_READ, UE_CHAT_NEXT, UE_CHAT_STATUS, UE_CREDIT, UE_REPLAY_READ, UE_REPLAY_NEXT,
       UE_SERVICE_INFO, UE_DELAY_POLICY, UE_NET_STATUS, UE_PEER_STATUS,
       UE_NAME_SET, UE_NAME_GET, UE_CHECK, UE_CHECK_STATUS, UE_PROFILE_SET, UE_PROFILE_GET,
       UE_BARRIER, UE_END, UE_LOBBY, UE_SNAPSHOT, UE_JOIN, UE_SETTLE,
       UE_NAME_FORMAT, UE_NAME_MAP, UE_DISPLAY_NAME, UE_COMPACT_STATUS,
       UE_INPUT_SEND, UE_INPUT_READ, UE_LIVE, UE_LAST_FAILURE, UE_BURST, UE_BURST_ACK,
       UE_SNAPSHOT_STREAM, UE_PERFORMANCE, UE_INPUT_STREAM, UE_CAPTURE_TIME, UE_SCORES };
enum { UE_BURST_REPLAY=1, UE_BURST_INPUT, UE_BURST_LIVE, UE_BURST_SNAPSHOT };
#define UE_JOINING 8
#define UE_OK 0x80
#define UE_OOB 0x40
#define UE_ERROR 0x10
#define UE_REPAIR 0x20
/* START width option: keep WAIT inside the ESP until a useful reply exists.
 * Clear for explicit polling/interleaving chat; wire framing is identical. */
#define UE_DEFER 0x40
enum { UE_BAD_REQUEST=1, UE_BAD_SEQUENCE, UE_NOT_READY, UE_BACKEND, UE_EXHAUSTED,
       UE_DESYNC, UE_NEGOTIATION, UE_METADATA, UE_ENDED, UE_SYNC_ERROR, UE_SESSION_TIMEOUT };
enum { UE_ASCII=0, UE_UPPERCASE, UE_CUSTOM_MAP };
enum { UE_STATUS_IDLE=0, UE_STATUS_CONNECTING, UE_STATUS_BINDING,
       UE_STATUS_WAIT_PLAYERS, UE_STATUS_WAIT_READY, UE_STATUS_SYNC,
       UE_STATUS_RUNNING, UE_STATUS_WAIT_INPUT, UE_STATUS_RECONNECTING,
       UE_STATUS_ENDING, UE_STATUS_FAILED };
/* Failure source: 0 none, 1 UrClient.error, 2 UrServices.failure,
 * 3 desync (UE_DESYNC), 4 session end code. */
typedef struct {
    uint8_t request[24],reply[24],request_n,reply_n,tag,valid;
    uint8_t configured,running,granted,sampled,defer,awaiting;
    uint8_t input_stream,burst_count,cancelling;
    uint8_t snapshot_stream,snapshot_error;
    uint16_t snapshot_epoch;
    uint16_t recovery_reported;
    uint32_t snapshot_at;
    uint32_t snapshot_roster_at,snapshot_roster;
    uint8_t input_duplex,input_error,input_dirty;
    uint32_t input_at,input_sent;
    uint16_t performance_serial;
    uint8_t performance[6][20];
    uint32_t frame;
    uint8_t delay,window,repair,baseline[UR_PAD_MAX],used[UE_PREDICTION_SLOTS][UR_PAD_MAX];
    uint8_t policy[4]; /* auto flag, minimum, maximum, milliseconds per tick */
    uint32_t repair_first,repair_cursor,repair_end;
    uint8_t chat_dest,chat_channel,chat_total,chat_writing;
    uint8_t read_peer,read_offset,read_count;
    uint32_t read_id;
    uint8_t name_mode,name_fallback,name_map[95];
    uint8_t name_read[US_NAME_MAX],name_seat,name_length,name_valid;
} UnEndpoint;
/* Optional asynchronous score backend. Returns up to 22 payload bytes, or
 * zero when unsupported. Never blocks or writes UART directly. */
uint8_t ue_scores(const uint8_t *p,uint8_t n,uint32_t now,uint8_t out[22]);
void ue_reset(UnEndpoint *e);
/* Out-of-band cancel: 0 busy, 1 idle/ready for a new tag-1 request, 2 failed. */
uint8_t ue_stop(UrClient *c,uint8_t begin,uint32_t now);
void ue_cancel_control(UnEndpoint *e,UrGame *g,UrClient *c,uint32_t now);
/* START hook begins saved-TLS matchmaking; zero = accepted, nonzero = error.
 * It must not send UART bytes. */
uint8_t ue_start(const uint8_t config[19],uint32_t now);
void ue_command(UnEndpoint *e,UrGame *g,UrClient *c,const uint8_t *p,uint8_t n,uint32_t now);
/* CRC-validated UART payload: epoch LE16 + one/two 11-byte car records.
 * No reply, tag advance, retry or allocation. Invalid batches commit nothing. */
void ue_snapshot_write(UnEndpoint *e,UrClient *c,const uint8_t *p,uint8_t n,uint32_t now);
void ue_input_write(UnEndpoint *e,UrClient *c,const uint8_t *p,uint8_t n,uint32_t now);
void ue_timed_write(UnEndpoint *e,UrClient *c,const uint8_t *p,uint8_t n,uint32_t now);
/* Call only when the UART reply slot is free. Returns 1 if a reply was sent. */
uint8_t ue_service(UnEndpoint *e,UrGame *g,UrClient *c,uint32_t now);
/* Network-worker service, including while UART is paused or a reply is held. */
void ue_tick(UnEndpoint *e,UrClient *c,uint32_t now);
#endif
