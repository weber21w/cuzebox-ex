#include "cu_esp.h"
#include "cu_uart.h"
#include "cu_avr.h"
#include <stdio.h>
#include <string.h>

static cu_state_esp_t test_state;
static unsigned at_calls;
static unsigned timer_calls;
static unsigned ipd_calls;
static unsigned host_calls;
static char at_command[64];

cu_state_esp_t* cu_esp_get_state(void){ return &test_state; }

void cu_esp_host_serial_write(uint8 c){ (void)c; }
uint8 cu_esp_host_serial_read(void){ return 0u; }
auint cu_esp_host_serial_rx_bytes_ready(void){ return 0u; }
void cu_esp_tcp_serial_write(uint8 c){ (void)c; }
uint8 cu_esp_tcp_serial_read(void){ return 0u; }
auint cu_esp_tcp_serial_rx_bytes_ready(void){ return 0u; }
void cu_esp_serial_midi_write(uint8 c){ (void)c; }
uint8 cu_esp_serial_midi_read(void){ return 0u; }
auint cu_esp_serial_midi_rx_bytes_ready(void){ return 0u; }
void cu_esp_txi(sint32 v){ (void)v; }
void cu_esp_txl(const char *s, auint n){ (void)s; (void)n; }
void cu_esp_txp(const char *s){ (void)s; }
void cu_esp_txp_error(void){}
void cu_esp_txp_ok(void){}
sint32 cu_esp_mqtt_publish(const char *topic, const uint8 *data, uint32 len, uint8 qos, uint8 retain){
    (void)topic; (void)data; (void)len; (void)qos; (void)retain; return -1;
}
sint32 cu_esp_net_send(uint32 s, sint8 *b, sint32 l, sint32 f){ (void)s; (void)b; (void)l; (void)f; return -1; }
void cu_esp_process_at(sint8 *b){
    at_calls++;
    snprintf(at_command, sizeof(at_command), "%s", (char const*)b);
}
sint32 cu_esp_process_ipd(void){ ipd_calls++; return 0; }
auint cu_esp_update_timer_counts(auint cycle){
    auint delta=WRAP32(cycle-test_state.last_cycle_tick);test_state.last_cycle_tick=cycle;timer_calls++;
    for(unsigned i=0;i<sizeof(test_state.delay_len)/sizeof(test_state.delay_len[0]);i++){
#ifdef CU_ESP_AT_CAPTURE_PROFILE
        if(test_state.delay_pos[i]&CU_ESP_CAPTURE_RELATIVE)continue;
#endif
        if(test_state.delay_len[i]<=delta)test_state.delay_len[i]=0;
        else test_state.delay_len[i]-=delta;
    }
    for(unsigned i=0;i<sizeof(test_state.delay_len)/sizeof(test_state.delay_len[0]);i++)
        if(test_state.delay_len[i]&&test_state.delay_pos[i]==test_state.read_buf_pos_out)return test_state.read_buf_pos_out;
    return test_state.read_buf_pos_in;
}
void cu_esp_service_host_state(void){ host_calls++; }

static void init_esp_uart(void){
    memset(&test_state, 0, sizeof(test_state));
    at_calls = timer_calls = ipd_calls = host_calls = 0u;
    at_command[0] = '\0';
    test_state.serial_route = CU_ESP_SERIAL_ESP_MODULE;
    test_state.emulation_model = 1u;
    test_state.user_input_mode = ESP_USER_MODE_AT;
    test_state.uart_profile = CU_UART_PROFILE_FAST;
    test_state.uart_baud_bits = 185u;
    test_state.uart_baud_bits_module = 185u;
    test_state.uart_baud_bits_module_default = 185u;
    test_state.uart_data_bits = 8u;
    test_state.uart_stop_bits = 1u;
    test_state.uart_parity = 0u;
    test_state.uart_synchronous = 0u;
    test_state.uart_double_speed = 0u;
    test_state.uart_rx_enabled = 1u;
    test_state.uart_tx_enabled = 1u;
    cu_esp_reset_uart();
    test_state.uart_baud_bits = 185u;
    test_state.uart_baud_bits_module = 185u;
    test_state.uart_baud_bits_module_default = 185u;
    test_state.uart_data_bits = 8u;
    test_state.uart_stop_bits = 1u;
    test_state.uart_rx_enabled = 1u;
    test_state.uart_tx_enabled = 1u;
}

static int req(int cond, int code, const char *msg){
    if(!cond){ fprintf(stderr, "FAIL %d: %s\n", code, msg); return code; }
    return 0;
}

static int test_tx_buffering(void){
    const auint frame = 29760u;
    auint st;
    int rc;

    init_esp_uart();
    cu_esp_uzebox_write('A', 0u);
    if((rc=req(test_state.write_buf_pos_in == 0u, 10, "ESP saw TX byte before frame completed"))) return rc;
    st = cu_esp_uzebox_status(1u);
    if((rc=req((st & (1u << UDRE0)) != 0u, 11, "UDRE did not reopen with shift register active"))) return rc;
    if((rc=req((st & (1u << TXC0)) == 0u, 12, "TXC asserted during first frame"))) return rc;

    cu_esp_uzebox_write('T', 1u);
    st = cu_esp_uzebox_status(2u);
    if((rc=req((st & (1u << UDRE0)) == 0u, 13, "UDRE asserted while UDR holding register occupied"))) return rc;

    st = cu_esp_uzebox_status(frame);
    if((rc=req(test_state.write_buf_pos_in == 1u && test_state.write_buf[0] == 'A', 14, "first byte not delivered at frame boundary"))) return rc;
    if((rc=req((st & (1u << UDRE0)) != 0u && (st & (1u << TXC0)) == 0u, 15, "UDR/TXC flags wrong after buffered launch"))) return rc;
    cu_esp_uzebox_write('\r', frame + 1u);

    (void)cu_esp_uzebox_status(frame * 2u);
    if((rc=req(test_state.write_buf_pos_in == 2u && test_state.write_buf[1] == 'T', 16, "second byte timing wrong"))) return rc;
    cu_esp_uzebox_write('\n', frame * 2u + 1u);

    (void)cu_esp_uzebox_status(frame * 3u);
    if((rc=req(at_calls == 0u && test_state.write_buf_pos_in == 3u, 17, "AT command completed before final UART frame"))) return rc;
    (void)cu_esp_uzebox_status(frame * 4u);
    if((rc=req(at_calls == 1u && strcmp(at_command, "AT\r\n") == 0, 18, "AT command not delivered after final frame"))) return rc;
    if((rc=req(test_state.last_cycle_tick==frame*4u,19,"AT delay clock was not anchored at command completion")))return rc;
    return 0;
}

static int test_rx_first_frame(void){
    const auint frame = 29760u;
    const auint base = 200000u;
    auint st;
    int rc;

    init_esp_uart();
    test_state.read_buf[0] = 'O';
    test_state.read_buf_pos_out = 0u;
    test_state.read_buf_pos_in = 1u;

    st = cu_esp_uzebox_status(base);
    if((rc=req((st & (1u << RXC0)) == 0u, 30, "ESP response byte became visible without UART frame time"))) return rc;
    st = cu_esp_uzebox_status(base + frame - 1u);
    if((rc=req((st & (1u << RXC0)) == 0u, 31, "RXC asserted before receive frame completed"))) return rc;
    st = cu_esp_uzebox_status(base + frame);
    if((rc=req((st & (1u << RXC0)) != 0u, 32, "RXC missing at receive frame boundary"))) return rc;
    if((rc=req((uint8)cu_esp_uzebox_read(base + frame) == 'O', 33, "received ESP byte mismatch"))) return rc;
    if((rc=req(test_state.read_ready_cycle == 0u, 34, "idle receiver kept stale ready deadline"))) return rc;
    return 0;
}

static int test_fast_poll_budget(void){
    unsigned i;
    int rc;

    init_esp_uart();
    timer_calls = 0u;
    for(i = 0u; i <= 100000u; i += 64u)
        (void)cu_esp_uzebox_status((auint)i);

    /* UART timers remain sampled at roughly 1/4 frame so exact emulated
     * deadlines stay cheap to discover, but host/network work is decoupled to
     * about once per 29,760-cycle character at 9600 baud. */
    if((rc=req(timer_calls >= 12u && timer_calls <= 16u, 40, "FAST UART timer cache cadence changed unexpectedly"))) return rc;
    if((rc=req(host_calls >= 3u && host_calls <= 5u, 41, "FAST host-network servicing is not decoupled to about one frame"))) return rc;
    if((rc=req(ipd_calls == 0u, 42, "IPD polling ran without internet-access state"))) return rc;

    host_calls = 0u;
    for(i = 0u; i < 20000u; i += 8u)
        (void)cu_esp_uzebox_write_ready((auint)(200000u + i));
    if((rc=req(host_calls == 0u, 43, "TX-only UDRE/TXC polling serviced host networking"))) return rc;
    return 0;
}
static int test_reply_deadline(void){
    const auint base=200000u,delay=1234u,frame=29760u;
    int rc;init_esp_uart();
    test_state.read_buf[0]='O';test_state.read_buf_pos_in=1u;
    test_state.last_cycle_tick=base;test_state.delay_len[0]=delay;test_state.delay_pos[0]=0u;
    (void)cu_esp_uzebox_status(base);
    (void)cu_esp_uzebox_status(base+delay-1u);
    (void)cu_esp_uzebox_status(base+delay);
    if((rc=req(test_state.read_ready_cycle==base+delay+frame,60,"host cache hid AT reply deadline")))return rc;
    if((rc=req((cu_esp_uzebox_status(base+delay+frame)&(1u<<RXC0))!=0u,61,"delayed reply missed frame boundary")))return rc;
    return 0;
}

static int test_reply_chunk_boundary(void){
    const auint base=200000u,frame=29760u,pause=12000u;
    int rc;init_esp_uart();
    test_state.read_buf[0]='A';test_state.read_buf[1]='O';test_state.read_buf_pos_in=2u;
    test_state.last_cycle_tick=base;test_state.delay_pos[0]=1u;
    test_state.delay_len[0]=frame+pause;
    (void)cu_esp_uzebox_status(base);
    (void)cu_esp_uzebox_status(base+frame);
    if((rc=req(cu_esp_uzebox_read(base+frame)=='A',62,"echo byte mismatch")))return rc;
    if((rc=req(test_state.read_ready_cycle==0u,63,"delayed reply started shifting before chunk readiness")))return rc;
    (void)cu_esp_uzebox_status(base+frame);
    (void)cu_esp_uzebox_status(base+frame+pause);
    if((rc=req(test_state.read_ready_cycle==base+frame+pause+frame,64,"reply did not receive a full frame after pause")))return rc;
    if((rc=req(!(cu_esp_uzebox_status(base+frame+pause+frame-1u)&(1u<<RXC0)),65,"reply RXC arrived early")))return rc;
    if((rc=req(cu_esp_uzebox_status(base+frame+pause+frame)&(1u<<RXC0),66,"reply RXC missing after pause and frame")))return rc;
    return 0;
}

int main(void){
    int rc;
#ifdef CU_ESP_AT_CAPTURE_PROFILE
    /* The host completes late. The ESP's internal chunk pause must still
     * follow actual reply transmission, rather than expiring beforehand. */
    init_esp_uart();
    test_state.last_cycle_tick=200000u;
    test_state.delay_pos[0]=CU_ESP_CAPTURE_RELATIVE|1u;
    test_state.delay_len[0]=29760u+1234u;
    (void)cu_esp_uzebox_status(200000u);
    (void)cu_esp_uzebox_status(400000u);
    if((rc=req(test_state.delay_len[0]==30994u,70,"internal pause expired while host connect was pending")))return rc;
    test_state.read_buf[0]='A';test_state.read_buf[1]='B';test_state.read_buf_pos_in=2u;
    (void)cu_esp_uzebox_status(500000u);
    if((rc=req(test_state.delay_pos[0]==1u,71,"relative pause did not anchor at actual first frame")))return rc;
    (void)cu_esp_uzebox_status(529760u);(void)cu_esp_uzebox_read(529760u);
    (void)cu_esp_uzebox_status(530994u);
    if((rc=req(test_state.read_ready_cycle==560754u,72,"late host result lost internal response gap")))return rc;
    init_esp_uart();
    test_state.at_firmware_profile=CU_ESP_AT_FW_ESPAT_230;
    test_state.baud_rate=115200u;test_state.uart_baud_bits=30u;test_state.uart_double_speed=1u;
    test_state.read_buf[0]='A';test_state.read_buf[1]='B';test_state.read_buf_pos_in=2u;
    (void)cu_esp_uzebox_status(200000u);
    if((rc=req(test_state.read_ready_cycle==202484u,67,"ESP frame uses AVR rather than module divider")))return rc;
    (void)cu_esp_uzebox_status(202484u);
    (void)cu_esp_uzebox_read(202514u);
    if((rc=req(test_state.read_ready_cycle==204968u,68,"prompt UDR read restarted continuous module TX")))return rc;
    if((rc=req(cu_esp_uzebox_status(204968u)&(1u<<RXC0),69,"continuous module frame boundary missing")))return rc;
#endif
    if((rc=test_reply_deadline()))return rc;
    if((rc=test_reply_chunk_boundary()))return rc;
    init_esp_uart();
    test_state.uart_baud_rate_default = 9600u;
    test_state.baud_rate = 19200u;
    cu_esp_reset_uart();
    if((rc=req(test_state.baud_rate==9600u && test_state.uart_baud_bits_module==185u, 54, "reset did not restore saved 9600 baud"))) return rc;
    cu_esp_uzebox_modify(CU_IO_UCSR0A, 1u << U2X0, 0u);
    if((rc=req(test_state.uart_baud_bits_module==372u, 55, "U2X changed ESP physical baud"))) return rc;
    cu_esp_uzebox_modify(CU_IO_UBRR0H, 1u, 1u);
    cu_esp_uzebox_modify(CU_IO_UBRR0L, 116u, 2u);
    if((rc=req(!test_state.uart_scramble, 56, "valid double-speed 9600 baud was scrambled"))) return rc;
    init_esp_uart();
    test_state.uart_rx_enabled = 0u;
    test_state.read_buf[0] = 'X';
    test_state.read_buf_pos_in = 1u;
    if((rc=req(!(cu_esp_uzebox_status(200000u) & (1u << RXC0)), 50, "RXC asserted with receiver disabled"))) return rc;
    if((rc=req(test_state.read_buf_pos_out == 1u, 51, "disabled receiver retained startup bytes"))) return rc;
    init_esp_uart();
    test_state.soft_ap_enabled = 1u;
    test_state.wifi_mode = ESP_WIFI_MODE_SOFTAP;
    (void)cu_esp_uzebox_status(200000u);
    if((rc=req(ipd_calls == 1u, 52, "AP-only UART did not poll incoming UDP"))) return rc;
    rc = test_tx_buffering(); if(rc) return rc;
    init_esp_uart();
    test_state.uart_scramble = 1u;
    cu_esp_uzebox_write('A', 0u);
    (void)cu_esp_uzebox_status(29760u);
    if((rc=req(!test_state.write_buf_pos_in && !at_calls, 53, "bad-baud probe polluted AT parser"))) return rc;
    rc = test_rx_first_frame(); if(rc) return rc;
    rc = test_fast_poll_budget(); if(rc) return rc;
    puts("ESP UART hot path OK: buffered TX, frame-timed RX, decoupled host polling, TX-only status");
    return 0;
}
