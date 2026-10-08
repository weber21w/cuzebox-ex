#ifndef DEBUG_TIMING_H
#define DEBUG_TIMING_H
#include "cu_types.h"

#define CU_TIMING_TRACE_CAP 8192U
#define CU_TIMING_BEAM_HISTORY_CAP 4096U
#define CU_TIMING_LINES 271U
#define CU_TIMING_TOP_PC 8U
#define CU_TIMING_AVR_HZ 28636360U

/* Event identifiers deliberately stay <= 31 so they can also be used as
** bits in the capture mask.  PORTC is high-volume and is disabled in the
** default logic-analyzer mask because the beam debugger can capture the
** complete PORTC value once per AVR cycle when explicitly armed. */
typedef enum {
 CU_TIMING_EVT_PORTC=1,
 CU_TIMING_EVT_SYNC=2,
 CU_TIMING_EVT_SPI=3,
 CU_TIMING_EVT_UART_TX=4,
 CU_TIMING_EVT_RAM_CS=5,
 CU_TIMING_EVT_SD_CS=6,
 CU_TIMING_EVT_IRQ_ENTER=7,
 CU_TIMING_EVT_IRQ_EXIT=8,
 CU_TIMING_EVT_T1_COMPA=9,
 CU_TIMING_EVT_T1_COMPB=10,
 CU_TIMING_EVT_T1_OVF=11,
 CU_TIMING_EVT_CTR_LATCH=12,
 CU_TIMING_EVT_CTR_CLOCK=13,
 CU_TIMING_EVT_CTR_DATA0=14,
 CU_TIMING_EVT_CTR_DATA1=15,
 CU_TIMING_EVT_UART_RX=16
} cu_timing_event_type_t;

#define CU_TIMING_MASK_EVENT(t) ((uint32)1U << (t))
#define CU_TIMING_TRACE_MASK_DEFAULT ( \
 CU_TIMING_MASK_EVENT(CU_TIMING_EVT_SYNC) | \
 CU_TIMING_MASK_EVENT(CU_TIMING_EVT_SPI) | \
 CU_TIMING_MASK_EVENT(CU_TIMING_EVT_UART_TX) | \
 CU_TIMING_MASK_EVENT(CU_TIMING_EVT_RAM_CS) | \
 CU_TIMING_MASK_EVENT(CU_TIMING_EVT_SD_CS) | \
 CU_TIMING_MASK_EVENT(CU_TIMING_EVT_IRQ_ENTER) | \
 CU_TIMING_MASK_EVENT(CU_TIMING_EVT_IRQ_EXIT) | \
 CU_TIMING_MASK_EVENT(CU_TIMING_EVT_T1_COMPA) | \
 CU_TIMING_MASK_EVENT(CU_TIMING_EVT_T1_COMPB) | \
 CU_TIMING_MASK_EVENT(CU_TIMING_EVT_T1_OVF) | \
 CU_TIMING_MASK_EVENT(CU_TIMING_EVT_CTR_LATCH) | \
 CU_TIMING_MASK_EVENT(CU_TIMING_EVT_CTR_CLOCK) | \
 CU_TIMING_MASK_EVENT(CU_TIMING_EVT_CTR_DATA0) | \
 CU_TIMING_MASK_EVENT(CU_TIMING_EVT_CTR_DATA1) | \
 CU_TIMING_MASK_EVENT(CU_TIMING_EVT_UART_RX) )
#define CU_TIMING_TRACE_MASK_ALL 0x0001FFFEUL

/* UART event flags. Data bits are stored as (bits - 5) in bits 0..1.
** Parity keeps the AVR UPM value (0=none, 2=even, 3=odd) in bits 3..4. */
#define CU_TIMING_UART_DATA_MASK    0x03U
#define CU_TIMING_UART_STOP2        0x04U
#define CU_TIMING_UART_PARITY_MASK  0x18U
#define CU_TIMING_UART_PARITY_SHIFT 3U
#define CU_TIMING_UART_SYNC         0x20U
#define CU_TIMING_UART_U2X          0x40U

/* SPI event flags. */
#define CU_TIMING_SPI_CPOL 0x01U
#define CU_TIMING_SPI_CPHA 0x02U
#define CU_TIMING_SPI_DORD 0x04U

#define CU_TIMING_RASTER_CYCLE     0U
#define CU_TIMING_RASTER_SYNC_RISE 1U
#define CU_TIMING_RASTER_SYNC_FALL 2U

typedef struct {
 uint32 seq;
 auint abs_cycle;
 uint16 row;
 uint16 beam_cycle;
 uint8 type;
 uint8 value;      /* Generic value; SPI TX byte. */
 uint8 value2;     /* SPI RX byte; otherwise zero. */
 uint8 flags;      /* SPI CPOL/CPHA/DORD; otherwise zero. */
 uint16 aux;       /* Generic auxiliary value; SPI duration / IRQ vector. */
 uint32 detail;    /* Extended metadata; UART wire-frame start cycle. */
} cu_timing_event_t;

typedef struct {
 uint32 seq;
 auint abs_start;
 auint abs_end;
 uint16 start_row;
 uint16 start_cycle;
 uint16 end_row;
 uint16 end_cycle;
 uint16 pc;
 uint16 cycles;
} cu_timing_beam_instruction_t;

typedef struct {
 uint32 instructions;
 uint32 instruction_cycles;
 uint16 line_cycles;
 sint16 slack_cycles;
 uint8 valid;
 uint8 overrun;
 uint16 top_pc[CU_TIMING_TOP_PC];
 uint32 top_cycles[CU_TIMING_TOP_PC];
} cu_timing_line_stat_t;

typedef struct {
 boole enabled;
 boole any_row;
 auint row;
 auint cycle;
 auint mode;
 boole hit;
 auint hit_row;
 auint hit_cycle;
 auint hit_pc;
} cu_timing_raster_t;

/* Hot-path gates are public deliberately: callers can test one byte-sized
** flag inline and avoid an out-of-line diagnostic function call while the
** corresponding tool is disarmed. The owning module is the only writer. */
extern boole cu_debug_timing_trace_gate;
extern boole cu_debug_timing_instruction_gate;
extern boole cu_debug_timing_scan_gate;
extern boole cu_debug_timing_sync_gate;

void cu_debug_timing_reset(void);
void cu_debug_timing_row_boundary(auint old_row, auint old_cycles, auint new_row);
boole cu_debug_timing_after_instruction(auint start_row, auint start_cycle, auint end_row, auint end_cycle, auint pc, auint cycles, auint abs_end);
void cu_debug_timing_sync_edge(auint row, auint beam_cycle, boole rising);

void cu_debug_timing_raster_set(boole enabled, boole any_row, auint row, auint cycle);
void cu_debug_timing_raster_set_event(boole enabled, boole any_row, auint row, auint mode);
void cu_debug_timing_raster_get(cu_timing_raster_t* out, boole clear_hit);

boole cu_debug_timing_beam_history_built(void);
void cu_debug_timing_beam_history_enable(boole enabled);
boole cu_debug_timing_beam_history_enabled(void);
void cu_debug_timing_beam_history_clear(void);
auint cu_debug_timing_beam_history_count(void);
uint32 cu_debug_timing_beam_history_first_seq(void);
boole cu_debug_timing_beam_history_get(uint32 seq, cu_timing_beam_instruction_t* out);

void cu_debug_timing_trace_enable(boole enabled);
boole cu_debug_timing_trace_enabled(void);
void cu_debug_timing_trace_clear(void);
void cu_debug_timing_trace_set_mask(uint32 mask);
uint32 cu_debug_timing_trace_get_mask(void);
boole cu_debug_timing_trace_event_enabled(auint type);
void cu_debug_timing_trace_event(auint type, auint value, auint aux, auint row, auint beam_cycle, auint abs_cycle);
void cu_debug_timing_trace_spi(auint tx, auint rx, auint duration, auint flags, auint row, auint beam_cycle, auint abs_cycle);
void cu_debug_timing_trace_uart(auint type, auint byte, auint error_flags, auint bit_cycles, auint data_bits, auint stop_bits, auint parity, auint synchronous, auint double_speed, auint wire_start_cycle, auint observed_cycle);
void cu_debug_timing_trace_irq_enter(auint vector, auint row, auint beam_cycle, auint abs_cycle);
void cu_debug_timing_trace_irq_exit(auint row, auint beam_cycle, auint abs_cycle);
auint cu_debug_timing_trace_count(void);
uint32 cu_debug_timing_trace_first_seq(void);
boole cu_debug_timing_trace_get(uint32 seq, cu_timing_event_t* out);

void cu_debug_timing_scan_enable(boole enabled);
boole cu_debug_timing_scan_enabled(void);
void cu_debug_timing_scan_clear(void);
uint32 cu_debug_timing_scan_frame(void);
boole cu_debug_timing_scan_get(auint row, cu_timing_line_stat_t* out);

#endif
