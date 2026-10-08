/*
 *  CUzeBox UART module (AVR-facing UART boundary)
 */

#include "cu_esp.h"
#include "cu_uart.h"
#include "midi.h"
#include "cu_types.h"
#if defined(ENABLE_DEBUGGER) && defined(ENABLE_API_SERVER)
#include "debug_timing.h"
#endif

#include <stdio.h>
#include <string.h>

#define CU_ESP_ESCAPE_GUARD_CYCLES    (ESP_UZEBOX_CORE_FREQUENCY)
#define CU_ESP_SERIAL_TRACE_CAP       8192u
#define CU_ESP_SERIAL_TRACE_GAP_CYCLES (ESP_UZEBOX_CORE_FREQUENCY / 200u)

#define CU_ESP_SERIAL_TRACE_EVT_TX    1u
#define CU_ESP_SERIAL_TRACE_EVT_RX    2u
#define CU_ESP_SERIAL_TRACE_EVT_CFG   3u
#define CU_ESP_SERIAL_TRACE_EVT_BTX   4u
#define CU_ESP_SERIAL_TRACE_EVT_BRX   5u

#define CU_ESP_SERIAL_TRACE_FLAG_SCRAMBLE    0x01u
#define CU_ESP_SERIAL_TRACE_FLAG_TX_DISABLED 0x02u
#define CU_ESP_SERIAL_TRACE_FLAG_RX_DISABLED 0x04u
#define CU_ESP_SERIAL_TRACE_FLAG_BAD_BAUD    0x08u
#define CU_ESP_SERIAL_TRACE_FLAG_BAD_FORMAT  0x10u
#define CU_ESP_SERIAL_TRACE_FLAG_SYNC        0x20u
#define CU_ESP_SERIAL_TRACE_FLAG_GOOD        0x40u
#define CU_ESP_SERIAL_TRACE_FLAG_OVERRUN     0x80u

#ifndef CU_UART_VERBOSE_LOG
#define CU_UART_VERBOSE_LOG 0
#endif
#if CU_UART_VERBOSE_LOG
#define CU_UART_LOG(...) print_trace(__VA_ARGS__)
#else
#define CU_UART_LOG(...) ((void)0)
#endif

#if !defined(ENABLE_ESP)

boole cu_uart_route_is_host_serial(void){ return FALSE; }
boole cu_uart_route_is_tcp_serial(void){ return FALSE; }
boole cu_uart_route_is_midi(void){ return FALSE; }
boole cu_uart_route_is_loopback(void){ return FALSE; }
boole cu_uart_runtime_is_inert(void){ return TRUE; }
auint cu_uart_get_bit_cycles(void){ return 1u; }
auint cu_uart_get_frame_bits(void){ return 10u; }
auint cu_uart_get_frame_cycles(void){ return 10u; }
void cu_uart_debug_logic_capture_set(boole tx_enable, boole rx_enable){ (void)tx_enable; (void)rx_enable; }
void cu_uart_debug_logic_capture_get(boole* tx_enable, boole* rx_enable){ if(tx_enable)*tx_enable=FALSE; if(rx_enable)*rx_enable=FALSE; }

void cu_esp_uzebox_write(uint8 val, auint cycle){ (void)val; (void)cycle; }
auint cu_esp_uzebox_read(auint cycle){ (void)cycle; return 0xFFu; }
void cu_esp_uzebox_modify(auint port, auint val, auint cycle){ (void)port; (void)val; (void)cycle; }
auint cu_esp_uzebox_status(auint cycle){ (void)cycle; return ((1u << UDRE0) | (1u << TXC0)); }
auint cu_esp_uzebox_read_ready(auint cycle){ return cu_esp_uzebox_status(cycle) & ((1u << RXC0) | (1u << FE0) | (1u << DOR0) | (1u << UPE0)); }
auint cu_esp_uzebox_write_ready(auint cycle){ return cu_esp_uzebox_status(cycle) & ((1u << UDRE0) | (1u << TXC0)); }
void cu_esp_reset_uart(void){}

boole cu_esp_serial_trace_get_enabled(void){ return FALSE; }
void cu_esp_serial_trace_set_enabled(boole enable){ (void)enable; }
void cu_esp_serial_trace_clear(void){}
auint cu_esp_serial_trace_get_event_count(void){ return 0u; }
auint cu_esp_serial_trace_get_line_count(void){ return 0u; }
auint cu_esp_serial_trace_get_capacity(void){ return 0u; }
auint cu_esp_serial_trace_get_drop_count(void){ return 0u; }
boole cu_esp_serial_trace_is_full(void){ return FALSE; }
void cu_esp_serial_trace_get_uart_status(char* out, auint out_size){ if(out != NULL && out_size != 0u){ out[0] = '\0'; } }
void cu_esp_serial_trace_format_line(auint idx, char* out, auint out_size){ (void)idx; if(out != NULL && out_size != 0u){ out[0] = '\0'; } }
boole cu_esp_serial_trace_export(char const* path, boole raw){ (void)path; (void)raw; return FALSE; }
void cu_esp_serial_trace_note_backend_tx(uint8 val, auint cycle){ (void)val; (void)cycle; }
void cu_esp_serial_trace_note_backend_rx(uint8 val, auint cycle){ (void)val; (void)cycle; }
void cu_esp_serial_trace_get_counts(auint* avr_tx, auint* avr_rx, auint* backend_tx, auint* backend_rx, auint* cfg){
	if(avr_tx != NULL) *avr_tx = 0u;
	if(avr_rx != NULL) *avr_rx = 0u;
	if(backend_tx != NULL) *backend_tx = 0u;
	if(backend_rx != NULL) *backend_rx = 0u;
	if(cfg != NULL) *cfg = 0u;
}

#else

#define esp_state (*cu_esp_get_state())

typedef struct{
	auint cycle;
	uint8 type;
	uint8 v0;
	uint8 v1;
	uint8 flags;
} cu_esp_serial_trace_evt_t;

static cu_esp_serial_trace_evt_t cu_esp_serial_trace_evts[CU_ESP_SERIAL_TRACE_CAP];
static auint cu_esp_serial_trace_evt_count = 0u;
static auint cu_esp_serial_trace_drop_count = 0u;
static boole cu_esp_serial_trace_enabled = FALSE;
static boole cu_esp_serial_trace_full = FALSE;
static auint cu_esp_serial_trace_line_starts[CU_ESP_SERIAL_TRACE_CAP];
static auint cu_esp_serial_trace_line_count = 0u;
static auint cu_esp_serial_trace_count_avr_tx = 0u;
static auint cu_esp_serial_trace_count_avr_rx = 0u;
static auint cu_esp_serial_trace_count_backend_tx = 0u;
static auint cu_esp_serial_trace_count_backend_rx = 0u;
static auint cu_esp_serial_trace_count_cfg = 0u;

typedef struct{
	boole valid;
	auint next_poll_cycle;
	auint next_host_cycle;
	uint8 route_kind;
	uint8 rx_ready;
	auint buf_next;
} cu_uart_poll_cache_t;

typedef struct{
	boole valid;
	uint32 signature;
	auint bit_cycles;
	auint frame_bits;
	auint frame_cycles;
} cu_uart_timing_cache_t;

static cu_uart_poll_cache_t cu_uart_poll_cache;
static cu_uart_timing_cache_t cu_uart_timing_cache;
/* The serial trace already has one disabled fast-path test per byte. Fold the
 * logic waveform capture into that same gate so normal UART traffic gains no
 * second disabled trace call/check. */
static boole cu_uart_logic_tx_capture;
static boole cu_uart_logic_rx_capture;
static boole cu_uart_any_trace_capture;
static void cu_uart_trace_gate_refresh(void){ cu_uart_any_trace_capture=(cu_esp_serial_trace_enabled || cu_uart_logic_tx_capture || cu_uart_logic_rx_capture)?TRUE:FALSE; }
void cu_uart_debug_logic_capture_set(boole tx_enable, boole rx_enable){ cu_uart_logic_tx_capture=tx_enable?TRUE:FALSE; cu_uart_logic_rx_capture=rx_enable?TRUE:FALSE; cu_uart_trace_gate_refresh(); }
void cu_uart_debug_logic_capture_get(boole* tx_enable, boole* rx_enable){ if(tx_enable)*tx_enable=cu_uart_logic_tx_capture; if(rx_enable)*rx_enable=cu_uart_logic_rx_capture; }

static boole cu_uart_route_is_passthrough(void);
static boole cu_uart_route_uses_esp_rules(void);

static boole cu_uart_profile_is_fast(void)
{
	return (esp_state.uart_profile == CU_UART_PROFILE_FAST) ? TRUE : FALSE;
}

static boole cu_uart_profile_is_debug(void)
{
	return (esp_state.uart_profile == CU_UART_PROFILE_DEBUG) ? TRUE : FALSE;
}

static boole cu_uart_profile_has_rich_flags(void)
{
	/* Raw cable endpoints model the ATmega644 USART itself, not an ESP8266
	 * performance profile. Keep the historical profile choices for the ESP
	 * module, but do not let FAST remove USART status flags from TCP/serial/
	 * MIDI/loopback endpoints. */
	if(cu_uart_route_is_passthrough())
		return TRUE;
	return cu_uart_profile_is_fast() ? FALSE : TRUE;
}

static boole cu_uart_profile_has_tx_double_buffer(void)
{
	/* The ATmega644 USART always has a transmit shift register plus the UDR
	 * holding register. FAST is allowed to reduce host-side polling/diagnostic
	 * work, but it must not change AVR-visible UART buffering. In particular,
	 * the ESP8266 route is timing-sensitive firmware's normal UART peer, so it
	 * uses the same UDR + shift-register model as raw serial endpoints. */
	if(cu_uart_route_is_passthrough() || cu_uart_route_uses_esp_rules())
		return TRUE;
	return cu_uart_profile_is_fast() ? FALSE : TRUE;
}

static boole cu_uart_tx_emit_at_frame_end(void)
{
	/* A real ESP8266 cannot receive a byte until its complete UART frame has
	 * crossed TXD. Keep the historical launch-time behavior for generic raw
	 * endpoints, but make the built-in ESP route observe bytes at the actual
	 * frame-complete boundary. */
	return cu_uart_route_uses_esp_rules();
}

boole cu_uart_route_is_host_serial(void)
{
	return (esp_state.serial_route == CU_ESP_SERIAL_HOST_SERIAL) ? TRUE : FALSE;
}

boole cu_uart_route_is_tcp_serial(void)
{
	return (esp_state.serial_route == CU_ESP_SERIAL_TCP_SERIAL) ? TRUE : FALSE;
}

boole cu_uart_route_is_midi(void)
{
	return ((esp_state.serial_route == CU_ESP_SERIAL_HOST_MIDI) ||
	        (esp_state.serial_route == CU_ESP_SERIAL_VIRTUAL_MIDI)) ? TRUE : FALSE;
}

boole cu_uart_route_is_loopback(void)
{
	return (esp_state.serial_route == CU_ESP_SERIAL_LOOPBACK) ? TRUE : FALSE;
}

static boole cu_uart_route_is_passthrough(void)
{
	return (cu_uart_route_is_host_serial() || cu_uart_route_is_tcp_serial() ||
	        cu_uart_route_is_midi() || cu_uart_route_is_loopback()) ? TRUE : FALSE;
}

static boole cu_uart_route_uses_esp_rules(void)
{
	return (esp_state.serial_route == CU_ESP_SERIAL_ESP_MODULE) ? TRUE : FALSE;
}

boole cu_uart_runtime_is_inert(void)
{
	if(cu_uart_route_is_passthrough())
		return FALSE;
	return (esp_state.emulation_model == 0u) ? TRUE : FALSE;
}

static auint cu_uart_loopback_rx_ready(void)
{
	return (esp_state.loopback_count != 0u) ? 1u : 0u;
}

static uint8 cu_uart_loopback_read(void)
{
	uint8 c;

	if(esp_state.loopback_count == 0u)
		return 0u;

	c = esp_state.loopback_buf[esp_state.loopback_head];
	esp_state.loopback_head = (esp_state.loopback_head + 1u) % (uint32)sizeof(esp_state.loopback_buf);
	esp_state.loopback_count--;
	return c;
}

static void cu_uart_loopback_write(uint8 c)
{
	if(esp_state.loopback_count >= (uint32)sizeof(esp_state.loopback_buf)){
		esp_state.loopback_drops++;
		esp_state.uart_overrun = 1u;
		return;
	}

	esp_state.loopback_buf[esp_state.loopback_tail] = c;
	esp_state.loopback_tail = (esp_state.loopback_tail + 1u) % (uint32)sizeof(esp_state.loopback_buf);
	esp_state.loopback_count++;
}

static uint32 cu_uart_timing_signature(void)
{
	return ((uint32)esp_state.uart_baud_bits & 0x0FFFu) |
	       (((uint32)esp_state.uart_double_speed & 1u) << 12) |
	       (((uint32)esp_state.uart_synchronous & 3u) << 13) |
	       (((uint32)esp_state.uart_data_bits & 0x0Fu) << 15) |
	       (((uint32)esp_state.uart_stop_bits & 3u) << 19) |
	       (((uint32)esp_state.uart_parity & 3u) << 21);
}

static void cu_uart_timing_cache_refresh(void)
{
	uint32 sig = cu_uart_timing_signature();
	auint ubrr;
	auint mult;
	auint bits;
	auint data_bits;
	auint stop_bits;

	if(cu_uart_timing_cache.valid && cu_uart_timing_cache.signature == sig)
		return;

	ubrr = esp_state.uart_baud_bits & 0x0FFFu;
	if(esp_state.uart_synchronous != 0u)
		mult = 2u;
	else if(esp_state.uart_double_speed != 0u)
		mult = 8u;
	else
		mult = 16u;

	data_bits = (esp_state.uart_data_bits >= 5u) ? (auint)esp_state.uart_data_bits : 8u;
	stop_bits = (esp_state.uart_stop_bits != 0u) ? (auint)esp_state.uart_stop_bits : 1u;
	bits = 1u + data_bits + stop_bits + ((esp_state.uart_parity != 0u) ? 1u : 0u);

	cu_uart_timing_cache.signature = sig;
	cu_uart_timing_cache.bit_cycles = (ubrr + 1u) * mult;
	cu_uart_timing_cache.frame_bits = bits;
	cu_uart_timing_cache.frame_cycles = cu_uart_timing_cache.bit_cycles * bits;
	if(cu_uart_timing_cache.bit_cycles == 0u) cu_uart_timing_cache.bit_cycles = 1u;
	if(cu_uart_timing_cache.frame_bits == 0u) cu_uart_timing_cache.frame_bits = 1u;
	if(cu_uart_timing_cache.frame_cycles == 0u) cu_uart_timing_cache.frame_cycles = 1u;
	cu_uart_timing_cache.valid = TRUE;
}

static void cu_uart_timing_cache_invalidate(void)
{
	cu_uart_timing_cache.valid = FALSE;
}

auint cu_uart_get_bit_cycles(void)
{
	cu_uart_timing_cache_refresh();
	return cu_uart_timing_cache.bit_cycles;
}

auint cu_uart_get_frame_bits(void)
{
	cu_uart_timing_cache_refresh();
	return cu_uart_timing_cache.frame_bits;
}

static auint cu_uart_get_fast_cycles(void)
{
	cu_uart_timing_cache_refresh();
	return cu_uart_timing_cache.frame_cycles;
}

auint cu_uart_get_frame_cycles(void)
{
	cu_uart_timing_cache_refresh();
	return cu_uart_timing_cache.frame_cycles;
}

static auint cu_uart_get_tx_ready_cycles(void)
{
	if(cu_uart_profile_is_fast())
		return cu_uart_get_fast_cycles();
	return cu_uart_get_bit_cycles();
}

static auint cu_uart_get_rx_ready_cycles(void)
{
	if(cu_uart_profile_is_fast())
		return cu_uart_get_fast_cycles();
	return cu_uart_get_frame_cycles();
}

static void cu_uart_poll_cache_invalidate(void)
{
	cu_uart_poll_cache.valid = FALSE;
	cu_uart_poll_cache.next_poll_cycle = 0u;
	cu_uart_poll_cache.next_host_cycle = 0u;
	cu_uart_poll_cache.route_kind = 0u;
	cu_uart_poll_cache.rx_ready = 0u;
	cu_uart_poll_cache.buf_next = 0u;
}

static auint cu_uart_poll_interval_cycles(void)
{
	if(cu_uart_profile_is_debug())
		return 128u;
	if(cu_uart_profile_is_fast()){
		/* ESP status polling can reach this path once per HSYNC. Its timer update
		 * also services nonblocking host networking, so doing that every 2048
		 * AVR cycles is needless at low baud rates. Poll no faster than the old
		 * 2048-cycle floor and no slower than roughly 1/4 UART frame (capped at
		 * 8192 cycles). Known UART-ready deadlines below are still honored exactly. */
		if(cu_uart_route_uses_esp_rules()){
			auint qframe = cu_uart_get_frame_cycles() >> 2;
			if(qframe < 2048u) qframe = 2048u;
			if(qframe > 8192u) qframe = 8192u;
			return qframe;
		}
		return 2048u;
	}
	return 512u;
}

static auint cu_uart_host_service_interval_cycles(void)
{
	if(cu_uart_profile_is_debug())
		return 512u;
	if(cu_uart_profile_is_fast() && cu_uart_route_uses_esp_rules()){
		auint frame = cu_uart_get_frame_cycles();
		if(frame < 8192u) frame = 8192u;
		if(frame > 32768u) frame = 32768u;
		return frame;
	}
	return 2048u;
}

static boole cu_uart_host_service_due(auint cycle)
{
	if(cu_uart_poll_cache.next_host_cycle == 0u)
		return TRUE;
	if((cycle < cu_uart_poll_cache.next_host_cycle) &&
	   ((cu_uart_poll_cache.next_host_cycle - cycle) < 0x80000000u))
		return FALSE;
	return TRUE;
}

static void cu_uart_host_service_mark(auint cycle)
{
	cu_uart_poll_cache.next_host_cycle = WRAP32(cycle + cu_uart_host_service_interval_cycles());
}

static auint cu_uart_poll_next_cycle(auint cycle)
{
	auint next = WRAP32(cycle + cu_uart_poll_interval_cycles());
	auint poll_delta = WRAP32(next - cycle);

	/* Do not let a host-work cache interval hide an emulated UART timing event.
	 * If RX is already shifting toward UDR, force the next refresh exactly at
	 * that frame-complete cycle. Use wrap-safe deltas because the AVR cycle
	 * counter naturally wraps during long emulator sessions. */
	if(esp_state.read_ready_cycle != 0u){
		auint ready_delta = WRAP32(esp_state.read_ready_cycle - cycle);
		if(ready_delta < 0x80000000u && ready_delta < poll_delta)
			next = esp_state.read_ready_cycle;
	}
	return next;
}

static boole cu_uart_poll_due(auint cycle)
{
	if(!cu_uart_poll_cache.valid)
		return TRUE;
	if(cu_uart_poll_cache.route_kind != (uint8)esp_state.serial_route)
		return TRUE;
	if((cycle < cu_uart_poll_cache.next_poll_cycle) &&
	   ((cu_uart_poll_cache.next_poll_cycle - cycle) < 0x80000000u))
		return FALSE;
	return TRUE;
}

static auint cu_uart_backend_rx_ready(void)
{
	if(cu_uart_route_is_host_serial())
		return cu_esp_host_serial_rx_bytes_ready();
	if(cu_uart_route_is_tcp_serial())
		return cu_esp_tcp_serial_rx_bytes_ready();
	if(cu_uart_route_is_midi())
		return cu_esp_serial_midi_rx_bytes_ready();
	if(cu_uart_route_is_loopback())
		return cu_uart_loopback_rx_ready();
	return 0u;
}

static void cu_uart_poll_cache_refresh_passthrough(auint cycle)
{
	cu_uart_poll_cache.rx_ready = (esp_state.uart_rx_enabled != 0u) ?
		((cu_uart_backend_rx_ready() != 0u) ? 1u : 0u) : 0u;
	cu_uart_poll_cache.buf_next = 0u;
	cu_uart_poll_cache.route_kind = (uint8)esp_state.serial_route;
	cu_uart_poll_cache.next_poll_cycle = cu_uart_poll_next_cycle(cycle);
	cu_uart_poll_cache.valid = TRUE;
}

static void cu_uart_poll_cache_refresh_esp(auint cycle)
{
	auint buf_next = esp_state.read_buf_pos_in;
	uint8 rx_ready = 0u;
	boole host_due = cu_uart_host_service_due(cycle);

	/* Keep exact AVR UART timing separate from comparatively expensive host
	 * networking. FAST services sockets at roughly one UART frame (bounded),
	 * while known UART-ready deadlines still override the polling cache. */
	if(host_due){
		cu_esp_service_host_state();
		cu_uart_host_service_mark(cycle);
	}

	if(cycle >= esp_state.read_ready_cycle){
		buf_next = cu_esp_update_timer_counts(cycle);
		if(esp_state.read_buf_pos_in != esp_state.read_buf_pos_out){
			if(buf_next != esp_state.read_buf_pos_out){
				if(esp_state.read_ready_cycle == 0u){
					auint frame_cycles = cu_uart_get_frame_cycles();
					esp_state.read_ready_cycle = WRAP32(cycle + frame_cycles);
				}else{
					rx_ready = 1u;
				}
			}
		}else if(host_due && (esp_state.state & ESP_INTERNET_ACCESS)){
			if(cu_esp_process_ipd() > 0){
				auint frame_cycles = cu_uart_get_frame_cycles();
				esp_state.read_ready_cycle = WRAP32(cycle + frame_cycles);
			}
		}
	}

	cu_uart_poll_cache.rx_ready = rx_ready;
	cu_uart_poll_cache.buf_next = buf_next;
	cu_uart_poll_cache.route_kind = (uint8)esp_state.serial_route;
	cu_uart_poll_cache.next_poll_cycle = cu_uart_poll_next_cycle(cycle);
	cu_uart_poll_cache.valid = TRUE;
}

static void cu_uart_poll_cache_refresh(auint cycle)
{
	if(!cu_uart_poll_due(cycle))
		return;
	if(cu_uart_route_is_passthrough())
		cu_uart_poll_cache_refresh_passthrough(cycle);
	else if(!cu_uart_runtime_is_inert())
		cu_uart_poll_cache_refresh_esp(cycle);
	else{
		cu_uart_poll_cache.rx_ready = 0u;
		cu_uart_poll_cache.buf_next = esp_state.read_buf_pos_in;
		cu_uart_poll_cache.route_kind = (uint8)esp_state.serial_route;
		cu_uart_poll_cache.next_poll_cycle = cu_uart_poll_next_cycle(cycle);
		cu_uart_poll_cache.valid = TRUE;
	}
}

static auint cu_uart_cached_rx_ready(auint cycle)
{
	cu_uart_poll_cache_refresh(cycle);
	return cu_uart_poll_cache.rx_ready;
}

static uint8 cu_uart_backend_read(auint cycle)
{
	uint8 val = 0u;

	if(cu_uart_route_is_host_serial())
		val = cu_esp_host_serial_read();
	else if(cu_uart_route_is_tcp_serial())
		val = cu_esp_tcp_serial_read();
	else if(cu_uart_route_is_midi())
		val = cu_esp_serial_midi_read();
	else if(cu_uart_route_is_loopback())
		val = cu_uart_loopback_read();

	cu_esp_serial_trace_note_backend_rx(val, cycle);
	return val;
}

static void cu_uart_backend_write(uint8 val, auint cycle)
{
	if(cu_uart_route_is_host_serial())
		cu_esp_host_serial_write(val);
	else if(cu_uart_route_is_tcp_serial())
		cu_esp_tcp_serial_write(val);
	else if(cu_uart_route_is_midi())
		cu_esp_serial_midi_write(val);
	else if(cu_uart_route_is_loopback())
		cu_uart_loopback_write(val);

	cu_esp_serial_trace_note_backend_tx(val, cycle);
}

static uint8 cu_uart_scramble_byte(uint8 val, auint cycle, uint8 salt)
{
	uint8 mix;

	mix  = (uint8)(cycle & 0xFFu);
	mix ^= (uint8)((cycle >> 8) & 0xFFu);
	mix ^= (uint8)(esp_state.uart_baud_bits & 0xFFu);
	mix ^= (uint8)(esp_state.uart_baud_bits_module & 0xFFu);
	mix ^= (uint8)((esp_state.uart_data_bits & 0x0Fu) << 1);
	mix ^= (uint8)((esp_state.uart_stop_bits & 0x03u) << 5);
	mix ^= (uint8)((esp_state.uart_parity & 0x03u) << 3);
	mix ^= salt;
	val ^= (uint8)(0xA5u ^ mix);
	val = (uint8)((val << 1) | (val >> 7));
	return val;
}

static uint8 cu_uart_current_rx_error_flags(void)
{
	uint8 flags = 0u;

	if(!cu_uart_profile_has_rich_flags())
		return 0u;

	if(esp_state.uart_overrun)
		flags |= (1u << DOR0);
	if(esp_state.uart_scramble){
		flags |= (1u << FE0);
		if(esp_state.uart_parity != 0u)
			flags |= (1u << UPE0);
	}
	return flags;
}

static uint8 cu_esp_serial_trace_health_flags(void)
{
	uint8 flags = 0u;
	boole esp_rules = cu_uart_route_uses_esp_rules();

	if(esp_state.uart_synchronous)
		flags |= CU_ESP_SERIAL_TRACE_FLAG_SYNC;
	if(!esp_state.uart_tx_enabled)
		flags |= CU_ESP_SERIAL_TRACE_FLAG_TX_DISABLED;
	if(!esp_state.uart_rx_enabled)
		flags |= CU_ESP_SERIAL_TRACE_FLAG_RX_DISABLED;
	if(esp_rules && (!esp_state.uart_baud_bits || (esp_state.uart_baud_bits != esp_state.uart_baud_bits_module)))
		flags |= CU_ESP_SERIAL_TRACE_FLAG_BAD_BAUD;
	if(esp_rules && ((esp_state.uart_data_bits != 8u) || (esp_state.uart_stop_bits != 1u) || (esp_state.uart_parity != 0u)))
		flags |= CU_ESP_SERIAL_TRACE_FLAG_BAD_FORMAT;
	if(esp_rules && esp_state.uart_scramble)
		flags |= CU_ESP_SERIAL_TRACE_FLAG_SCRAMBLE;
	if(esp_state.uart_overrun)
		flags |= CU_ESP_SERIAL_TRACE_FLAG_OVERRUN;
	if(flags == 0u)
		flags = CU_ESP_SERIAL_TRACE_FLAG_GOOD;
	return flags;
}

static void cu_esp_serial_trace_push(uint8 type, uint8 v0, uint8 v1, auint cycle)
{
	cu_esp_serial_trace_evt_t *evt;
	boole new_line = TRUE;

	if(!cu_uart_any_trace_capture)
		return;
#if defined(ENABLE_DEBUGGER) && defined(ENABLE_API_SERVER)
	if((type == CU_ESP_SERIAL_TRACE_EVT_TX && cu_uart_logic_tx_capture) ||
	   (type == CU_ESP_SERIAL_TRACE_EVT_RX && cu_uart_logic_rx_capture)){
		auint frame_cycles=cu_uart_get_frame_cycles();
		auint bit_cycles=cu_uart_get_bit_cycles();
		auint start_cycle=cycle;
		if(frame_cycles==0u)frame_cycles=1u;
		if(bit_cycles==0u)bit_cycles=1u;
		if(type == CU_ESP_SERIAL_TRACE_EVT_TX){
			/* cu_esp_uzebox_write() pumps the transmitter before calling us.
			 * If the shift register is still occupied, this UDR byte begins
			 * exactly when the current frame completes. */
			if(esp_state.uart_tx_enabled && cu_uart_profile_has_tx_double_buffer() && esp_state.uart_tx_shift_valid)
				start_cycle=esp_state.uart_tx_complete_cycle;
			if(esp_state.uart_tx_enabled)
				cu_debug_timing_trace_uart(CU_TIMING_EVT_UART_TX,v0,0u,bit_cycles,esp_state.uart_data_bits,esp_state.uart_stop_bits,esp_state.uart_parity,esp_state.uart_synchronous,esp_state.uart_double_speed,start_cycle,cycle);
		}else{
			/* The read path calls us before it advances read_ready_cycle to
			 * the next byte, so the old deadline is the modeled end of this
			 * received frame. If there was no deadline, conservatively anchor
			 * the frame at the CPU observation cycle. */
			auint end_cycle=esp_state.read_ready_cycle;
			if(end_cycle==0u || WRAP32(cycle-end_cycle)>0x7FFFFFFFu)end_cycle=cycle;
			start_cycle=WRAP32(end_cycle-frame_cycles);
			cu_debug_timing_trace_uart(CU_TIMING_EVT_UART_RX,v0,cu_uart_current_rx_error_flags(),bit_cycles,esp_state.uart_data_bits,esp_state.uart_stop_bits,esp_state.uart_parity,esp_state.uart_synchronous,esp_state.uart_double_speed,start_cycle,cycle);
		}
	}
#endif
	if(!cu_esp_serial_trace_enabled)
		return;

	if(cu_esp_serial_trace_evt_count >= CU_ESP_SERIAL_TRACE_CAP){
		cu_esp_serial_trace_enabled = FALSE;
		cu_uart_trace_gate_refresh();
		cu_esp_serial_trace_full = TRUE;
		cu_esp_serial_trace_drop_count++;
		return;
	}

	evt = &cu_esp_serial_trace_evts[cu_esp_serial_trace_evt_count];
	evt->cycle = cycle;
	evt->type  = type;
	evt->v0    = v0;
	evt->v1    = v1;
	evt->flags = cu_esp_serial_trace_health_flags();

	if(cu_esp_serial_trace_evt_count != 0u){
		cu_esp_serial_trace_evt_t const *prev = &cu_esp_serial_trace_evts[cu_esp_serial_trace_evt_count - 1u];
		if(((type == CU_ESP_SERIAL_TRACE_EVT_TX) || (type == CU_ESP_SERIAL_TRACE_EVT_RX) ||
		    (type == CU_ESP_SERIAL_TRACE_EVT_BTX) || (type == CU_ESP_SERIAL_TRACE_EVT_BRX)) &&
		   (prev->type == type) &&
		   (prev->flags == evt->flags) &&
		   (WRAP32(cycle - prev->cycle) <= CU_ESP_SERIAL_TRACE_GAP_CYCLES))
			new_line = FALSE;
	}

	if(new_line && (cu_esp_serial_trace_line_count < CU_ESP_SERIAL_TRACE_CAP))
		cu_esp_serial_trace_line_starts[cu_esp_serial_trace_line_count++] = cu_esp_serial_trace_evt_count;

	switch(type){
		case CU_ESP_SERIAL_TRACE_EVT_TX:  cu_esp_serial_trace_count_avr_tx++; break;
		case CU_ESP_SERIAL_TRACE_EVT_RX:  cu_esp_serial_trace_count_avr_rx++; break;
		case CU_ESP_SERIAL_TRACE_EVT_BTX: cu_esp_serial_trace_count_backend_tx++; break;
		case CU_ESP_SERIAL_TRACE_EVT_BRX: cu_esp_serial_trace_count_backend_rx++; break;
		case CU_ESP_SERIAL_TRACE_EVT_CFG: cu_esp_serial_trace_count_cfg++; break;
		default: break;
	}

	cu_esp_serial_trace_evt_count++;
}

static char const* cu_esp_serial_trace_port_name(uint8 port)
{
	switch(port){
		case CU_IO_UCSR0A: return "UCSR0A";
		case CU_IO_UCSR0B: return "UCSR0B";
		case CU_IO_UCSR0C: return "UCSR0C";
		case CU_IO_UBRR0L: return "UBRR0L";
		case CU_IO_UBRR0H: return "UBRR0H";
		default: return "UART?";
	}
}

static char const* cu_esp_serial_trace_type_name(uint8 type)
{
	switch(type){
		case CU_ESP_SERIAL_TRACE_EVT_TX:  return "TX";
		case CU_ESP_SERIAL_TRACE_EVT_RX:  return "RX";
		case CU_ESP_SERIAL_TRACE_EVT_BTX: return "LINK-TX";
		case CU_ESP_SERIAL_TRACE_EVT_BRX: return "LINK-RX";
		case CU_ESP_SERIAL_TRACE_EVT_CFG: return "CFG";
		default: return "?";
	}
}


static void cu_esp_serial_trace_append_flags(char *out, auint out_size, uint8 flags)
{
	char tmp[96];
	int pos = 0;

	if(out_size == 0u)
		return;
	out[0] = '\0';
	if(flags & CU_ESP_SERIAL_TRACE_FLAG_GOOD){
		snprintf(out, (size_t)out_size, "GOOD");
		return;
	}
	if(flags & CU_ESP_SERIAL_TRACE_FLAG_SYNC)
		pos += snprintf(tmp + pos, sizeof(tmp) - (size_t)pos, "%sSYNC", (pos != 0) ? " " : "");
	if(flags & CU_ESP_SERIAL_TRACE_FLAG_BAD_BAUD)
		pos += snprintf(tmp + pos, sizeof(tmp) - (size_t)pos, "%sBAD-BAUD", (pos != 0) ? " " : "");
	if(flags & CU_ESP_SERIAL_TRACE_FLAG_BAD_FORMAT)
		pos += snprintf(tmp + pos, sizeof(tmp) - (size_t)pos, "%sBAD-FMT", (pos != 0) ? " " : "");
	if(flags & CU_ESP_SERIAL_TRACE_FLAG_TX_DISABLED)
		pos += snprintf(tmp + pos, sizeof(tmp) - (size_t)pos, "%sTX-OFF", (pos != 0) ? " " : "");
	if(flags & CU_ESP_SERIAL_TRACE_FLAG_RX_DISABLED)
		pos += snprintf(tmp + pos, sizeof(tmp) - (size_t)pos, "%sRX-OFF", (pos != 0) ? " " : "");
	if(flags & CU_ESP_SERIAL_TRACE_FLAG_SCRAMBLE)
		pos += snprintf(tmp + pos, sizeof(tmp) - (size_t)pos, "%sSCRAMBLE", (pos != 0) ? " " : "");
	if(flags & CU_ESP_SERIAL_TRACE_FLAG_OVERRUN)
		pos += snprintf(tmp + pos, sizeof(tmp) - (size_t)pos, "%sOVERRUN", (pos != 0) ? " " : "");
	tmp[sizeof(tmp) - 1u] = '\0';
	snprintf(out, (size_t)out_size, "%s", tmp);
}

static void cu_esp_serial_trace_append_escaped_byte(char *out, auint out_size, uint8 val)
{
	char tmp[8];
	size_t len;

	if(out_size == 0u)
		return;
	len = strlen(out);
	if(len >= (size_t)(out_size - 1u))
		return;
	if(val == '\r')
		snprintf(tmp, sizeof(tmp), "\\r");
	else if(val == '\n')
		snprintf(tmp, sizeof(tmp), "\\n");
	else if(val == '\t')
		snprintf(tmp, sizeof(tmp), "\\t");
	else if(val == '\\')
		snprintf(tmp, sizeof(tmp), "\\\\");
	else if(val == '"')
		snprintf(tmp, sizeof(tmp), "\\\"");
	else if((val >= 32u) && (val < 127u))
		snprintf(tmp, sizeof(tmp), "%c", (int)val);
	else
		snprintf(tmp, sizeof(tmp), "\\x%02X", (unsigned)val);
	strncat(out, tmp, (size_t)(out_size - 1u) - len);
}

boole cu_esp_serial_trace_get_enabled(void)
{
	return cu_esp_serial_trace_enabled;
}

void cu_esp_serial_trace_set_enabled(boole enable)
{
	if(enable){
		cu_esp_serial_trace_evt_count = 0u;
		cu_esp_serial_trace_line_count = 0u;
		cu_esp_serial_trace_drop_count = 0u;
		cu_esp_serial_trace_full = FALSE;
		cu_esp_serial_trace_count_avr_tx = 0u;
		cu_esp_serial_trace_count_avr_rx = 0u;
		cu_esp_serial_trace_count_backend_tx = 0u;
		cu_esp_serial_trace_count_backend_rx = 0u;
		cu_esp_serial_trace_count_cfg = 0u;
		cu_esp_serial_trace_enabled = TRUE;
	}else{
		cu_esp_serial_trace_enabled = FALSE;
	}
	cu_uart_trace_gate_refresh();
}

void cu_esp_serial_trace_clear(void)
{
	cu_esp_serial_trace_evt_count = 0u;
	cu_esp_serial_trace_line_count = 0u;
	cu_esp_serial_trace_drop_count = 0u;
	cu_esp_serial_trace_full = FALSE;
	cu_esp_serial_trace_count_avr_tx = 0u;
	cu_esp_serial_trace_count_avr_rx = 0u;
	cu_esp_serial_trace_count_backend_tx = 0u;
	cu_esp_serial_trace_count_backend_rx = 0u;
	cu_esp_serial_trace_count_cfg = 0u;
}

auint cu_esp_serial_trace_get_event_count(void)
{
	return cu_esp_serial_trace_evt_count;
}

auint cu_esp_serial_trace_get_line_count(void)
{
	return cu_esp_serial_trace_line_count;
}

auint cu_esp_serial_trace_get_capacity(void)
{
	return CU_ESP_SERIAL_TRACE_CAP;
}

auint cu_esp_serial_trace_get_drop_count(void)
{
	return cu_esp_serial_trace_drop_count;
}

boole cu_esp_serial_trace_is_full(void)
{
	return cu_esp_serial_trace_full;
}

void cu_esp_serial_trace_note_backend_tx(uint8 val, auint cycle)
{
	cu_esp_serial_trace_push(CU_ESP_SERIAL_TRACE_EVT_BTX, val, 0u, cycle);
}

void cu_esp_serial_trace_note_backend_rx(uint8 val, auint cycle)
{
	cu_esp_serial_trace_push(CU_ESP_SERIAL_TRACE_EVT_BRX, val, 0u, cycle);
}

void cu_esp_serial_trace_get_counts(auint* avr_tx, auint* avr_rx, auint* backend_tx, auint* backend_rx, auint* cfg)
{
	if(avr_tx != NULL) *avr_tx = cu_esp_serial_trace_count_avr_tx;
	if(avr_rx != NULL) *avr_rx = cu_esp_serial_trace_count_avr_rx;
	if(backend_tx != NULL) *backend_tx = cu_esp_serial_trace_count_backend_tx;
	if(backend_rx != NULL) *backend_rx = cu_esp_serial_trace_count_backend_rx;
	if(cfg != NULL) *cfg = cu_esp_serial_trace_count_cfg;
}

void cu_esp_serial_trace_get_uart_status(char* out, auint out_size)
{
	char flags[96];

	if(out_size == 0u)
		return;
	cu_esp_serial_trace_append_flags(flags, sizeof(flags), cu_esp_serial_trace_health_flags());
	snprintf(out, (size_t)out_size,
		"UART %s  Tx:%u Rx:%u  %u/%u  %u%c%u  %u cyc/bit  %u cyc/frame",
		flags,
		(unsigned)esp_state.uart_tx_enabled,
		(unsigned)esp_state.uart_rx_enabled,
		(unsigned)esp_state.uart_baud_bits,
		(unsigned)esp_state.uart_baud_bits_module,
		(unsigned)esp_state.uart_data_bits,
		(esp_state.uart_parity == 0u) ? 'N' : ((esp_state.uart_parity == 2u) ? 'E' : 'O'),
		(unsigned)esp_state.uart_stop_bits,
		(unsigned)cu_uart_get_bit_cycles(),
		(unsigned)cu_uart_get_frame_cycles());
}

void cu_esp_serial_trace_format_line(auint idx, char* out, auint out_size)
{
	auint evt_idx;
	auint next_idx;
	auint prev_line_cycle = 0u;
	cu_esp_serial_trace_evt_t const *evt;

	if(out_size == 0u)
		return;
	out[0] = '\0';
	if(idx >= cu_esp_serial_trace_line_count)
		return;

	evt_idx = cu_esp_serial_trace_line_starts[idx];
	next_idx = (idx + 1u < cu_esp_serial_trace_line_count) ?
		cu_esp_serial_trace_line_starts[idx + 1u] : cu_esp_serial_trace_evt_count;
	evt = &cu_esp_serial_trace_evts[evt_idx];
	if(idx != 0u)
		prev_line_cycle = cu_esp_serial_trace_evts[cu_esp_serial_trace_line_starts[idx - 1u]].cycle;

	{
		char flags[96];
		double dms = 0.0;
		if(idx != 0u)
			dms = ((double)WRAP32(evt->cycle - prev_line_cycle) * 1000.0) / (double)ESP_UZEBOX_CORE_FREQUENCY;
		cu_esp_serial_trace_append_flags(flags, sizeof(flags), evt->flags);
		if((evt->type == CU_ESP_SERIAL_TRACE_EVT_TX) || (evt->type == CU_ESP_SERIAL_TRACE_EVT_RX) ||
		   (evt->type == CU_ESP_SERIAL_TRACE_EVT_BTX) || (evt->type == CU_ESP_SERIAL_TRACE_EVT_BRX)){
			char payload[160];
			auint i;
			payload[0] = '\0';
			for(i = evt_idx; i < next_idx; i++){
				if(strlen(payload) >= sizeof(payload) - 8u){
					strncat(payload, "...", sizeof(payload) - 1u - strlen(payload));
					break;
				}
				cu_esp_serial_trace_append_escaped_byte(payload, sizeof(payload), cu_esp_serial_trace_evts[i].v0);
			}
			snprintf(out, (size_t)out_size, "+%.3f ms  %s  %ub  \"%s\"  [%s]",
				dms,
				cu_esp_serial_trace_type_name(evt->type),
				(unsigned)(next_idx - evt_idx),
				payload,
				flags);
		}else{
			snprintf(out, (size_t)out_size, "+%.3f ms  CFG  %s=0x%02X  [%s]",
				dms,
				cu_esp_serial_trace_port_name(evt->v0),
				(unsigned)evt->v1,
				flags);
		}
	}
}

boole cu_esp_serial_trace_export(char const* path, boole raw)
{
	FILE *f;

	if(path == NULL || path[0] == '\0')
		return FALSE;

	f = fopen(path, "wb");
	if(f == NULL)
		return FALSE;

	if(raw){
		auint i;
		for(i = 0u; i < cu_esp_serial_trace_evt_count; ++i){
			char flags[96];
			cu_esp_serial_trace_evt_t const *evt = &cu_esp_serial_trace_evts[i];
			cu_esp_serial_trace_append_flags(flags, sizeof(flags), evt->flags);
			if((evt->type == CU_ESP_SERIAL_TRACE_EVT_TX) || (evt->type == CU_ESP_SERIAL_TRACE_EVT_RX) ||
		   (evt->type == CU_ESP_SERIAL_TRACE_EVT_BTX) || (evt->type == CU_ESP_SERIAL_TRACE_EVT_BRX)){
				char payload[32];
				payload[0] = '\0';
				cu_esp_serial_trace_append_escaped_byte(payload, sizeof(payload), evt->v0);
				fprintf(f, "%10u  %-7s  0x%02X  \"%s\"  [%s]\n",
					(unsigned)evt->cycle,
					cu_esp_serial_trace_type_name(evt->type),
					(unsigned)evt->v0,
					payload,
					flags);
			}else{
				fprintf(f, "%10u  CFG  %s=0x%02X  [%s]\n",
					(unsigned)evt->cycle,
					cu_esp_serial_trace_port_name(evt->v0),
					(unsigned)evt->v1,
					flags);
			}
		}
	}else{
		auint i;
		auint line_count = cu_esp_serial_trace_get_line_count();
		char line[256];
		for(i = 0u; i < line_count; ++i){
			cu_esp_serial_trace_format_line(i, line, (auint)sizeof(line));
			fprintf(f, "%s\n", line);
		}
	}

	fclose(f);
	return TRUE;
}

static void cu_uart_debug_prefetch_rx(auint cycle)
{
	auint frame_cycles;

	if(!cu_uart_route_is_passthrough())
		return;
	if(!cu_uart_profile_is_debug())
		return;
	if(!esp_state.uart_rx_enabled)
		return;
	if(esp_state.uart_rx_pending_valid)
		return;
	if(cu_uart_cached_rx_ready(cycle) == 0u)
		return;

	esp_state.uart_rx_pending_byte = cu_uart_backend_read(cycle);
	cu_uart_poll_cache_invalidate();
	esp_state.uart_rx_pending_valid = 1u;
	esp_state.uart_overrun = 0u;
	esp_state.uart_rx_pending_cycle = cycle;
	frame_cycles = cu_uart_get_frame_cycles();
	if(frame_cycles == 0u)
		frame_cycles = 1u;
	esp_state.read_ready_cycle = WRAP32(cycle + frame_cycles);
}

static void cu_uart_debug_refresh_overrun(auint cycle)
{
	auint frame_cycles;

	if(!cu_uart_profile_is_debug())
		return;
	if(!esp_state.uart_rx_pending_valid)
		return;
	if(cu_uart_cached_rx_ready(cycle) == 0u)
		return;

	frame_cycles = cu_uart_get_frame_cycles();
	if(frame_cycles == 0u)
		frame_cycles = 1u;
	if(WRAP32(cycle - esp_state.uart_rx_pending_cycle) >= frame_cycles)
		esp_state.uart_overrun = 1u;
}

static void cu_uart_schedule_tx_ready(auint cycle)
{
	auint ready_cycles = cu_uart_get_tx_ready_cycles();
	auint frame_cycles = cu_uart_get_frame_cycles();

	if(ready_cycles == 0u)
		ready_cycles = 1u;
	if(frame_cycles == 0u)
		frame_cycles = 1u;

	esp_state.write_ready_cycle = WRAP32(cycle + ready_cycles);
	esp_state.uart_txc_pending = 1u;
	esp_state.uart_tx_complete_cycle = WRAP32(cycle + frame_cycles);
}

static void cu_uart_emit_tx_byte(uint8 val, auint cycle)
{
	uint8 send_val = val;
	auint max;
	uint32 send_sock;
	char ch;

	if(esp_state.uart_scramble)
		send_val = cu_uart_scramble_byte(send_val, cycle, 0x11u);

	/* A raw endpoint is the device on the far side of the UART. The ESP module
	 * has its own in-process parser, so avoid running the generic endpoint
	 * dispatch chain for every ESP byte on this hot path. */
	if(cu_uart_route_is_passthrough()){
		cu_uart_backend_write(send_val, cycle);
		return;
	}

	if(!cu_uart_route_uses_esp_rules() || !esp_state.emulation_model)
		return;

	max = (auint)sizeof(esp_state.write_buf);
	if(esp_state.write_buf_pos_in >= (max - 1u)){
		esp_state.write_buf_pos_in = 0UL;
		esp_state.write_buf[0] = '\0';
		if(esp_state.user_input_mode == ESP_USER_MODE_AT)
			cu_esp_txp_error();
		return;
	}
	esp_state.write_buf[esp_state.write_buf_pos_in++] = (sint8)send_val;

	/* ESP-AT raw-input commands (MQTTLONG*, MQTTPUBRAW, SYSFLASH) consume
	 * exact bytes without line parsing or echo. */
	if(esp_state.mqtt_input_kind != CU_ESP_MQTT_INPUT_NONE){
		if(esp_state.write_buf_pos_in >= esp_state.mqtt_input_expected){
			uint8 kind=esp_state.mqtt_input_kind; sint32 ok=0;
			if(kind==CU_ESP_MQTT_INPUT_CLIENTID || kind==CU_ESP_MQTT_INPUT_USERNAME || kind==CU_ESP_MQTT_INPUT_PASSWORD){
				sint8 *dst=(kind==CU_ESP_MQTT_INPUT_CLIENTID)?esp_state.mqtt_client_id:(kind==CU_ESP_MQTT_INPUT_USERNAME)?esp_state.mqtt_username:esp_state.mqtt_password;
				auint cap=(kind==CU_ESP_MQTT_INPUT_CLIENTID)?sizeof(esp_state.mqtt_client_id):(kind==CU_ESP_MQTT_INPUT_USERNAME)?sizeof(esp_state.mqtt_username):sizeof(esp_state.mqtt_password);
				auint n=esp_state.mqtt_input_expected;if(n>=cap)n=cap-1u;memcpy(dst,esp_state.write_buf,n);dst[n]=0;
			}else if(kind==CU_ESP_MQTT_INPUT_PUBRAW){
				ok=cu_esp_mqtt_publish((char*)esp_state.mqtt_raw_topic,(const uint8*)esp_state.write_buf,esp_state.mqtt_input_expected,esp_state.mqtt_raw_qos,esp_state.mqtt_raw_retain);
			}else if(kind==CU_ESP_MQTT_INPUT_SYSFLASH){
				uint32 off=esp_state.sysflash_write_offset,n=esp_state.mqtt_input_expected;if(off+n<=CU_ESP_SYSFLASH_PART_SIZE){memcpy(&esp_state.sysflash_data[off],esp_state.write_buf,n);ok=0;}else ok=-1;esp_state.sysflash_write_active=0u;
			}
			esp_state.mqtt_input_kind=CU_ESP_MQTT_INPUT_NONE;esp_state.mqtt_input_expected=0u;esp_state.rx_await_bytes=0u;esp_state.write_buf_pos_in=0u;esp_state.write_buf[0]=0;
			if(kind==CU_ESP_MQTT_INPUT_PUBRAW){
				cu_esp_txp((ok==0)?"+MQTTPUB:OK\r\n":"+MQTTPUB:FAIL\r\n");
			}else if(ok==0)cu_esp_txp_ok();else cu_esp_txp_error();
		}
		return;
	}

	if((esp_state.state & ESP_ECHO) && esp_state.user_input_mode < ESP_USER_MODE_UNVARNISHED){
		ch = (char)send_val;
		cu_esp_txl(&ch, 1u);
	}

	if(esp_state.uart_logging){
		if(esp_state.uart_logging == 1u){
			fwrite(&cycle, 1u, sizeof(cycle), esp_state.uart_logging_file);
			fputc(1, esp_state.uart_logging_file);
			fputc((val & 0xFFu), esp_state.uart_logging_file);
		}else{
			cu_esp_host_serial_write(val);
		}
	}

	if(esp_state.user_input_mode == ESP_USER_MODE_UNVARNISHED){
		if(send_val == '+'){
			if(esp_state.num_plus == 0u){
				auint dt = WRAP32(cycle - esp_state.last_plus);
				if(esp_state.last_plus != 0u && dt < CU_ESP_ESCAPE_GUARD_CYCLES){
					esp_state.num_plus = 0u;
					if(esp_state.unvarnished_bytes == 0u)
						esp_state.unvarnished_end_cycle = WRAP32(cycle + ESP_UNVARNISHED_DELAY);
				}else{
					esp_state.num_plus = 1u;
					esp_state.unvarnished_end_cycle = WRAP32(cycle + CU_ESP_ESCAPE_GUARD_CYCLES);
				}
			}else if(esp_state.num_plus < 3u){
				esp_state.num_plus++;
			}
		}else{
			esp_state.num_plus = 0u;
		}

		esp_state.last_plus = cycle;
		esp_state.unvarnished_bytes++;
		if(esp_state.unvarnished_bytes == 1u && esp_state.unvarnished_end_cycle == 0u)
			esp_state.unvarnished_end_cycle = WRAP32(cycle + ESP_UNVARNISHED_DELAY);
		return;
	}

	if(esp_state.user_input_mode == ESP_USER_MODE_SEND){
		if(esp_state.send_extended){
			/* The byte was appended above. Re-process it through CIPSENDEX escaping. */
			esp_state.write_buf_pos_in--;
			if(esp_state.send_extended_escape){
				esp_state.send_extended_escape=0u;
				if(send_val=='0'){esp_state.remaining_send_bytes=0u;}
				else esp_state.write_buf[esp_state.write_buf_pos_in++]=(sint8)send_val;
			}else if(send_val=='\\'){esp_state.send_extended_escape=1u;}
			else esp_state.write_buf[esp_state.write_buf_pos_in++]=(sint8)send_val;
		}
		if(esp_state.remaining_send_bytes != 0u) esp_state.remaining_send_bytes--;

		if(esp_state.remaining_send_bytes == 0u){
			send_sock = esp_state.send_to_socket;
			cu_esp_txp("Recv ");
			cu_esp_txi((sint32)esp_state.write_buf_pos_in);
			cu_esp_txp(" bytes\r\n");
			if(cu_esp_net_send(send_sock, esp_state.write_buf, (sint32)esp_state.write_buf_pos_in, 0) == ESP_SOCKET_ERROR)
				cu_esp_txp("SEND FAIL\r\n");
			else
				cu_esp_txp("SEND OK\r\n");
			if(send_sock < ESP_MAX_LINKS)
				esp_state.udp_send_override[send_sock] = 0u;
			esp_state.user_input_mode = ESP_USER_MODE_AT;
			esp_state.rx_await_bytes = 0u;esp_state.send_extended=0u;esp_state.send_extended_escape=0u;
			esp_state.write_buf_pos_in = 0u;
			esp_state.write_buf[0] = '\0';
		}
		return;
	}

	if(esp_state.user_input_mode == ESP_USER_MODE_AT){
		if(send_val == '\n' && esp_state.write_buf_pos_in > 1u &&
		   esp_state.write_buf[esp_state.write_buf_pos_in - 2u] == '\r'){
			esp_state.write_buf[esp_state.write_buf_pos_in] = '\0';
			cu_esp_process_at(esp_state.write_buf);
			esp_state.write_buf_pos_in = 0u;
			esp_state.write_buf[0] = '\0';
		}
	}
}

static void cu_uart_start_tx_shift(uint8 val, auint cycle)
{
	auint frame_cycles = cu_uart_get_frame_cycles();

	if(frame_cycles == 0u)
		frame_cycles = 1u;

	esp_state.uart_tx_shift_valid = 1u;
	esp_state.uart_tx_shift_byte = val;
	esp_state.uart_txc_pending = 1u;
	esp_state.uart_tx_complete_cycle = WRAP32(cycle + frame_cycles);
	if(!cu_uart_tx_emit_at_frame_end())
		cu_uart_emit_tx_byte(val, cycle);
}

static void cu_uart_pump_tx(auint cycle)
{
	if(!cu_uart_profile_has_tx_double_buffer())
		return;

	while(esp_state.uart_tx_shift_valid && (cycle >= esp_state.uart_tx_complete_cycle)){
		auint launch_cycle = esp_state.uart_tx_complete_cycle;
		uint8 completed_byte = esp_state.uart_tx_shift_byte;

		if(cu_uart_tx_emit_at_frame_end())
			cu_uart_emit_tx_byte(completed_byte, launch_cycle);
		esp_state.uart_tx_shift_valid = 0u;
		if(esp_state.uart_tx_udr_valid){
			uint8 next_val = esp_state.uart_tx_udr_byte;
			esp_state.uart_tx_udr_valid = 0u;
			cu_uart_start_tx_shift(next_val, launch_cycle);
		}else{
			esp_state.uart_txc_pending = 0u;
			esp_state.uart_tx_complete_cycle = launch_cycle;
		}
	}
}

static void cu_uart_schedule_rx_ready(auint cycle)
{
	auint ready_cycles = cu_uart_get_rx_ready_cycles();

	if(ready_cycles == 0u)
		ready_cycles = 1u;

	esp_state.read_ready_cycle = WRAP32(cycle + ready_cycles);
}

static auint cu_uart_tx_status(auint cycle)
{
	auint flags = 0u;
	cu_uart_pump_tx(cycle);

	if(!esp_state.uart_tx_enabled)
		return 0u;

	if(cu_uart_profile_has_tx_double_buffer()){
		if(esp_state.uart_tx_udr_valid == 0u)
			flags |= (1u << UDRE0);
		if((esp_state.uart_tx_shift_valid == 0u) && (esp_state.uart_tx_udr_valid == 0u))
			flags |= (1u << TXC0);
	}else{
		if((esp_state.write_ready_cycle == 0UL) || (cycle >= esp_state.write_ready_cycle))
			flags |= (1u << UDRE0);
		if((esp_state.uart_txc_pending == 0u) || (cycle >= esp_state.uart_tx_complete_cycle))
			flags |= (1u << TXC0);
	}
	return flags;
}

static auint cu_uart_rx_status(auint cycle)
{
	auint flags = 0u;

	if(esp_state.read_ready_cycle > cycle && (esp_state.read_ready_cycle - cycle) > 100000UL)
		esp_state.read_ready_cycle = cycle;

	if(cu_uart_route_is_passthrough()){
		if(cu_uart_profile_is_debug()){
			cu_uart_debug_prefetch_rx(cycle);
			cu_uart_debug_refresh_overrun(cycle);
			if(esp_state.uart_rx_pending_valid && (cycle >= esp_state.read_ready_cycle)){
				esp_state.uart_rx_error_flags = cu_uart_current_rx_error_flags();
				flags |= (1u << RXC0);
				flags |= esp_state.uart_rx_error_flags;
			}
		}else if(esp_state.uart_rx_enabled && (cycle >= esp_state.read_ready_cycle) && (cu_uart_cached_rx_ready(cycle) != 0u)){
			esp_state.uart_rx_error_flags = cu_uart_current_rx_error_flags();
			flags |= (1u << RXC0);
			flags |= esp_state.uart_rx_error_flags;
		}
	}else if(!cu_uart_runtime_is_inert()){
		cu_uart_poll_cache_refresh(cycle);
		if((cycle >= esp_state.read_ready_cycle) && (cu_uart_poll_cache.rx_ready != 0u)){
			esp_state.uart_rx_error_flags = cu_uart_current_rx_error_flags();
			flags |= (1u << RXC0);
			flags |= esp_state.uart_rx_error_flags;
		}
	}

	return flags;
}

auint cu_esp_uzebox_status(auint cycle)
{
	return cu_uart_tx_status(cycle) | cu_uart_rx_status(cycle);
}

auint cu_esp_uzebox_read_ready(auint cycle)
{
	/* TX completion can deliver an AVR byte to the emulated ESP and generate
	 * an immediate AT response, so advance the TX shifter before checking RX. */
	cu_uart_pump_tx(cycle);
	return cu_uart_rx_status(cycle) & ((1u << RXC0) | (1u << FE0) | (1u << DOR0) | (1u << UPE0));
}

auint cu_esp_uzebox_write_ready(auint cycle)
{
	/* Tight UDRE/TXC polling is a common transmit hot loop. Do not service RX,
	 * sockets, MQTT, DNS, or the AT receive queue just to answer TX readiness. */
	return cu_uart_tx_status(cycle) & ((1u << UDRE0) | (1u << TXC0));
}

void cu_esp_uzebox_write(uint8 val, auint cycle)
{
	cu_uart_pump_tx(cycle);
	cu_esp_serial_trace_push(CU_ESP_SERIAL_TRACE_EVT_TX, val, 0u, cycle);

	if(!esp_state.uart_tx_enabled)
		return;

	cu_uart_poll_cache_invalidate();

	if(!cu_uart_profile_has_tx_double_buffer()){
		cu_uart_schedule_tx_ready(cycle);
		cu_uart_emit_tx_byte(val, cycle);
		return;
	}

	esp_state.uart_txc_pending = 1u;

	if(esp_state.uart_tx_shift_valid == 0u){
		cu_uart_start_tx_shift(val, cycle);
	}else if(esp_state.uart_tx_udr_valid == 0u){
		esp_state.uart_tx_udr_valid = 1u;
		esp_state.uart_tx_udr_byte = val;
	}else{
		esp_state.uart_tx_udr_byte = val;
	}
}

auint cu_esp_uzebox_read(auint cycle)
{
	auint val;
	uint8 rxv;

	cu_uart_pump_tx(cycle);

	if(cu_uart_route_is_passthrough()){
		if(cu_uart_profile_is_debug()){
			cu_uart_debug_prefetch_rx(cycle);
			cu_uart_debug_refresh_overrun(cycle);
			if(esp_state.uart_rx_pending_valid && (cycle >= esp_state.read_ready_cycle)){
				rxv = esp_state.uart_rx_pending_byte;
				if(esp_state.uart_scramble)
					rxv = cu_uart_scramble_byte(rxv, cycle, 0x29u);
				esp_state.last_read_byte = (sint8)rxv;
				esp_state.uart_rx_pending_valid = 0u;
				esp_state.uart_rx_pending_cycle = cycle;
				cu_esp_serial_trace_push(CU_ESP_SERIAL_TRACE_EVT_RX, (uint8)esp_state.last_read_byte, 0u, cycle);
				esp_state.uart_rx_error_flags = 0u;
				esp_state.uart_overrun = 0u;
				esp_state.read_ready_cycle = 0UL;
				cu_uart_debug_prefetch_rx(cycle);
			}
		}else if((cycle >= esp_state.read_ready_cycle) && (cu_uart_cached_rx_ready(cycle) != 0u)){
			rxv = cu_uart_backend_read(cycle);
			cu_uart_poll_cache_invalidate();
			if(esp_state.uart_scramble)
				rxv = cu_uart_scramble_byte(rxv, cycle, 0x29u);
			esp_state.last_read_byte = (sint8)rxv;
			cu_esp_serial_trace_push(CU_ESP_SERIAL_TRACE_EVT_RX, (uint8)esp_state.last_read_byte, 0u, cycle);
			esp_state.uart_rx_error_flags = 0u;
			esp_state.uart_overrun = 0u;
			cu_uart_schedule_rx_ready(cycle);
		}
		return (auint)esp_state.last_read_byte;
	}

	if(!esp_state.emulation_model)
		return (auint)esp_state.last_read_byte;

	cu_uart_poll_cache_invalidate();

	if(esp_state.read_buf_pos_out == esp_state.read_buf_pos_in){
		print_message("ESP UART: Uzebox read, but no data is buffered\n");
		return (auint)esp_state.last_read_byte;
	}

	val = (auint)(uint8)esp_state.read_buf[esp_state.read_buf_pos_out++];
	if(esp_state.read_buf_pos_out >= sizeof(esp_state.read_buf))
		esp_state.read_buf_pos_out = 0UL;

	if(esp_state.uart_scramble)
		val = (auint)cu_uart_scramble_byte((uint8)val, cycle, 0x29u);

	if(esp_state.uart_logging){
		if(esp_state.uart_logging == 1u){
			fwrite(&cycle, 1u, sizeof(cycle), esp_state.uart_logging_file);
			fputc(0, esp_state.uart_logging_file);
			fputc((val & 0xFFu), esp_state.uart_logging_file);
		}else{
			cu_esp_host_serial_write((uint8)val);
		}
	}

	{
		boole rx_empty = FALSE;
		if(esp_state.read_buf_pos_in == esp_state.read_buf_pos_out){
			esp_state.read_buf_pos_in = 0UL;
			esp_state.read_buf_pos_out = 0UL;
			rx_empty = TRUE;
		}

		esp_state.last_read_byte = (sint8)val;
		cu_esp_serial_trace_push(CU_ESP_SERIAL_TRACE_EVT_RX, (uint8)val, 0u, cycle);
		esp_state.uart_rx_error_flags = 0u;
		esp_state.uart_overrun = 0u;
		if(rx_empty)
			esp_state.read_ready_cycle = 0UL;
		else
			cu_uart_schedule_rx_ready(cycle);
	}

	if(esp_state.uart_rx_enabled)
		return val;
	return 0u;
}


void cu_esp_uzebox_modify(auint port, auint val, auint cycle)
{
	boole esp_rules;

	cu_uart_pump_tx(cycle);
	esp_rules = cu_uart_route_uses_esp_rules();
	if(esp_rules){
		print_message("\tESP uzebox modify UART, cycle: %u, port: %u, val: %u\n", (unsigned)cycle, (unsigned)port, (unsigned)val);
		(void)0;
	}else if(cu_uart_route_is_passthrough()){
		print_message("\tUART cable modify, cycle: %u, port: %u, val: %u\n", (unsigned)cycle, (unsigned)port, (unsigned)val);
		(void)0;
	}
	cu_uart_poll_cache_invalidate();
	if(port == CU_IO_UCSR0A || port == CU_IO_UCSR0C || port == CU_IO_UBRR0L || port == CU_IO_UBRR0H)
		cu_uart_timing_cache_invalidate();

	if(port == CU_IO_UCSR0A){
		esp_state.uart_double_speed = ((val & (1u << U2X0)) >> U2X0);
		if(val & (1u << TXC0))
			esp_state.uart_txc_pending = 0u;
		esp_state.uart_ucsr0a = val;
	}else if(port == CU_IO_UCSR0B){
		uint8 prev_tx_enabled = esp_state.uart_tx_enabled;
		uint8 prev_rx_enabled = esp_state.uart_rx_enabled;
		esp_state.uart_tx_enabled = ((val & (1u << TXEN0)) >> TXEN0);
		esp_state.uart_rx_enabled = ((val & (1u << RXEN0)) >> RXEN0);
		esp_state.uart_ucsr0b = val;

		/*
		 * A lot of Uzebox-side code reinitializes the UART by writing UCSR0B
		 * from 0 to RXEN|TXEN and then immediately polling UDRE/TXC or waiting
		 * for the first receive opportunity. Preserve AVR-like behaviour here by
		 * discarding stale scheduler state when the UART is disabled and by making
		 * the transmitter immediately idle/ready again when it is re-enabled.
		 */
		if(!esp_state.uart_tx_enabled){
			esp_state.uart_tx_udr_valid = 0u;
			esp_state.uart_tx_udr_byte = 0u;
			esp_state.uart_tx_shift_valid = 0u;
			esp_state.uart_tx_shift_byte = 0u;
			esp_state.uart_txc_pending = 0u;
			esp_state.uart_tx_complete_cycle = 0u;
			esp_state.write_ready_cycle = 0u;
		}else if(!prev_tx_enabled){
			esp_state.uart_tx_udr_valid = 0u;
			esp_state.uart_tx_udr_byte = 0u;
			esp_state.uart_tx_shift_valid = 0u;
			esp_state.uart_tx_shift_byte = 0u;
			esp_state.uart_txc_pending = 0u;
			esp_state.uart_tx_complete_cycle = 0u;
			esp_state.write_ready_cycle = 0u;
		}

		if(!esp_state.uart_rx_enabled){
			esp_state.uart_rx_pending_valid = 0u;
			esp_state.uart_rx_pending_cycle = 0u;
			esp_state.uart_overrun = 0u;
			esp_state.uart_rx_error_flags = 0u;
			esp_state.read_ready_cycle = 0u;
		}else if(!prev_rx_enabled){
			esp_state.uart_rx_pending_valid = 0u;
			esp_state.uart_rx_pending_cycle = 0u;
			esp_state.uart_overrun = 0u;
			esp_state.uart_rx_error_flags = 0u;
			esp_state.read_ready_cycle = 0u;
		}
	}else if(port == CU_IO_UCSR0C){
		esp_state.uart_synchronous = ((val & (3u << UMSEL00)) >> UMSEL00);
		esp_state.uart_data_bits = 5u + ((val & (3u << UCSZ00)) >> UCSZ00);
		esp_state.uart_stop_bits = 1u + ((val & (1u << USBS0)) >> USBS0);
		esp_state.uart_parity = ((val & (3u << UPM00)) >> UPM00);
		esp_state.uart_ucsr0c = val;
	}else if(port == CU_IO_UBRR0L){
		esp_state.uart_ubrr0l = (val & 0xFFu);
		esp_state.uart_baud_bits &= ~0xFFu;
		esp_state.uart_baud_bits |= esp_state.uart_ubrr0l;
	}else if(port == CU_IO_UBRR0H){
		esp_state.uart_ubrr0h = (val & 0x0Fu);
		esp_state.uart_baud_bits &= ~(0x0F00u);
		esp_state.uart_baud_bits |= (esp_state.uart_ubrr0h << 8);
	}

	esp_state.uart_scramble = 0u;
	esp_state.uart_overrun = 0u;
	esp_state.uart_rx_error_flags = 0u;
	esp_state.uart_rx_pending_valid = 0u;
	esp_state.uart_rx_pending_cycle = 0u;

	if(esp_rules && esp_state.uart_synchronous){
		print_message("\t\tESP UART broken: synchronous mode\n");
		esp_state.uart_scramble = 1u;
		esp_state.uart_tx_enabled = 0u;
		esp_state.uart_rx_enabled = 0u;
	}

	if(esp_rules && (!esp_state.uart_baud_bits ||
	   (esp_state.uart_baud_bits != esp_state.uart_baud_bits_module) ||
	   (esp_state.uart_data_bits != 8u) ||
	   (esp_state.uart_stop_bits != 1u) ||
	   (esp_state.uart_parity != 0u))){
		print_message("\t\tESP UART broken: faking frame errors\n");
		esp_state.uart_scramble = 1u;
	}

	if(esp_rules){
		if(!esp_state.uart_tx_enabled || !esp_state.uart_rx_enabled){
			print_message("\t\tESP UART broken: TxE:%u RxE:%u\n", (unsigned)esp_state.uart_tx_enabled, (unsigned)esp_state.uart_rx_enabled);
			(void)0;
		}
	}else if(cu_uart_route_is_passthrough()){
		print_message("\t\tRAW UART LINK: TxE:%u RxE:%u\n", (unsigned)esp_state.uart_tx_enabled, (unsigned)esp_state.uart_rx_enabled);
	}

	if(esp_rules || cu_uart_route_is_passthrough()){
		print_message("\t\tUCSR0A=0x%02x UCSR0B=0x%02x UCSR0C=0x%02x\n",
			(unsigned)esp_state.uart_ucsr0a, (unsigned)esp_state.uart_ucsr0b, (unsigned)esp_state.uart_ucsr0c);
		if(esp_rules){
			print_message("\t\tTx:%u Rx:%u Ubaud:%u Ebaud:%u Stop:%u Parity:%u Data:%u Sync:%u BitCycles:%u FrameCycles:%u\n",
				(unsigned)esp_state.uart_tx_enabled, (unsigned)esp_state.uart_rx_enabled,
				(unsigned)esp_state.uart_baud_bits, (unsigned)esp_state.uart_baud_bits_module,
				(unsigned)esp_state.uart_stop_bits, (unsigned)esp_state.uart_parity,
				(unsigned)esp_state.uart_data_bits, (unsigned)esp_state.uart_synchronous,
				(unsigned)cu_uart_get_bit_cycles(), (unsigned)cu_uart_get_frame_cycles());
		}else{
			print_message("\t\tTx:%u Rx:%u Ubaud:%u Stop:%u Parity:%u Data:%u Sync:%u BitCycles:%u FrameCycles:%u\n",
				(unsigned)esp_state.uart_tx_enabled, (unsigned)esp_state.uart_rx_enabled,
				(unsigned)esp_state.uart_baud_bits,
				(unsigned)esp_state.uart_stop_bits, (unsigned)esp_state.uart_parity,
				(unsigned)esp_state.uart_data_bits, (unsigned)esp_state.uart_synchronous,
				(unsigned)cu_uart_get_bit_cycles(), (unsigned)cu_uart_get_frame_cycles());
		}
	}

	cu_esp_serial_trace_push(CU_ESP_SERIAL_TRACE_EVT_CFG, (uint8)port, (uint8)val, cycle);
}

void cu_esp_reset_uart(void)
{
	if(esp_state.uart_baud_bits_module_default == 0u){
		esp_state.uart_baud_bits_module_default = ESP_FACTORY_DEFAULT_BAUD_BITS;
		esp_state.baud_rate = ESP_FACTORY_BAUD_RATE;
	}

	if(esp_state.uart_profile > CU_UART_PROFILE_DEBUG)
		esp_state.uart_profile = CU_UART_PROFILE_FAST;

	cu_uart_poll_cache_invalidate();
	cu_uart_timing_cache_invalidate();
	esp_state.uart_baud_bits_module = esp_state.uart_baud_bits_module_default;
	esp_state.read_buf_pos_in = 0UL;
	esp_state.read_buf_pos_out = 0UL;
	esp_state.read_ready_cycle = 0UL;
	esp_state.write_ready_cycle = 0UL;
	esp_state.uart_rx_pending_valid = 0u;
	esp_state.uart_rx_pending_byte = 0u;
	esp_state.uart_overrun = 0u;
	esp_state.uart_rx_error_flags = 0u;
	esp_state.uart_txc_pending = 0u;
	esp_state.uart_tx_udr_valid = 0u;
	esp_state.uart_tx_udr_byte = 0u;
	esp_state.uart_tx_shift_valid = 0u;
	esp_state.uart_tx_shift_byte = 0u;
	esp_state.uart_rx_pending_cycle = 0UL;
	esp_state.uart_tx_complete_cycle = 0UL;
	esp_state.loopback_head = 0u;
	esp_state.loopback_tail = 0u;
	esp_state.loopback_count = 0u;
	esp_state.loopback_drops = 0u;
	esp_state.state |= ESP_ECHO;
}

#endif
