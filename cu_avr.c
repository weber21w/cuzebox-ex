/*
 *  AVR microcontroller emulation
 *
 *  Copyright (C) 2016
 *    Sandor Zsuga (Jubatian)
 *  Uzem (the base of CUzeBox) is copyright (C)
 *    David Etherton,
 *    Eric Anderton,
 *    Alec Bourque (Uze),
 *    Filipe Rinaldi,
 *    Sandor Zsuga (Jubatian),
 *    Matt Pandina (Artcfox)
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/



#include "cu_avr.h"
#ifdef ENABLE_DEBUGGER
#include "debug_timing.h"
#endif
#include "cu_avrc.h"
#include "cu_avrfg.h"
#include "cu_ctr.h"
#include "cu_spi.h"

extern uint8 cu_kbd_enabled;
extern auint cu_hap_process(auint prev, auint curr);
extern auint cu_kbd_process(auint prev, auint curr);
extern void cu_esp_uzebox_write(uint8 val, auint cycle);
extern auint cu_esp_uzebox_read(auint cycle);
extern auint cu_esp_uzebox_status(auint cycle);
extern auint cu_esp_uzebox_read_ready(auint cycle);
extern auint cu_esp_uzebox_write_ready(auint cycle);
extern void cu_esp_uzebox_modify(auint port, auint val, auint cycle);
extern void cu_esp_reset_pin(uint8 state, auint cycle);
#if defined(ENABLE_DEBUGGER) && defined(ENABLE_API_SERVER)
extern void cu_uart_debug_logic_capture_set(boole tx_enable, boole rx_enable);
#endif
/* Marker for this compilation unit for sub-includes */
#define CU_AVR_C



/* CPU state */
cu_state_cpu_t  cpu_state;

/* Compiled AVR instructions */
uint32          cpu_code[32768];

/* Access info structure for SRAM */
uint8           access_mem[4096U];

/* Access info structure for I/O */
uint8           access_io[256U];

/* Precalculated flags */
uint8           cpu_pflags[CU_AVRFG_SIZE];

/* Whether the flags were already precalculated */
boole           pflags_done = FALSE;

#ifdef ENABLE_DEBUGGER
/*
** Lightweight execution profiler. Indexed by AVR word address, so it can be
** resolved against normal Uzebox map / symbol files by external tools. The
** profiler is intentionally debugger-gated so normal release builds do not
** pay the storage cost.
*/
static boole    profiler_enabled = FALSE;
static uint32   profiler_hits[32768U];
static uint64_t profiler_cycles[32768U];
#endif

/* Row generation structure */
cu_row_t        video_row;
#ifdef ENABLE_DEBUGGER
uint8           breakpoint_map[32768U / 8U];
typedef struct{
 uint8 enabled;
 uint8 region;
 uint8 flags;
 uint16 start_addr;
 uint16 end_addr;
} cu_avr_watchpoint_t;

typedef struct{
 boole valid;
 uint8 slot;
 uint8 region;
 uint8 flags;
 uint16 addr;
 uint8 value;
 uint16 pc;
} cu_avr_watchhit_t;

cu_avr_watchpoint_t watchpoint_map[CU_AVR_WATCH_SLOTS];
static boole         watchpoints_armed = FALSE;
static boole         cu_debug_mem_active = FALSE;
cu_avr_watchhit_t    watchpoint_hit;
boole                temp_break_active = FALSE;
uint16               temp_break_addr = 0U;
auint                debug_step_remaining = 0U;
static boole         debug_event_break = FALSE;
#ifdef ENABLE_MEMORY_TRACE
static boole                    memory_trace_on = FALSE;
static cu_avr_memtrace_event_t  memory_trace_buf[CU_AVR_MEMTRACE_CAP];
static uint32                   memory_trace_seq = 0U;
static auint                    memory_trace_count = 0U;
#endif
#ifdef ENABLE_AUDIO_TRACE
static boole                    audio_trace_on = FALSE;
static boole                    audio_debug_active = FALSE;
static cu_avr_audio_event_t     audio_trace_buf[CU_AVR_AUDIO_TRACE_CAP];
static uint32                   audio_trace_seq = 0U;
static auint                    audio_trace_count_ = 0U;
static auint                    audio_break_mode_ = CU_AVR_AUDIO_BREAK_OFF;
static uint8                    audio_break_low_ = 0U;
static uint8                    audio_break_high_ = 255U;
static boole                    audio_break_hit_valid = FALSE;
static cu_avr_audio_event_t     audio_break_hit_event;
static uint64_t                 audio_event_total_ = 0ULL;
static uint64_t                 audio_rail_low_total_ = 0ULL;
static uint64_t                 audio_rail_high_total_ = 0ULL;
static auint                    audio_peak_distance_ = 0U;
#endif
#endif

#ifdef ENABLE_DEBUGGER
static void  cu_avr_debug_mem_refresh(void);
static void  cu_avr_watchpoints_refresh(void);
static void  cu_avr_debug_mem_event(auint region, auint flags, auint addr, auint value);
#ifdef ENABLE_AUDIO_TRACE
static void  cu_avr_audio_debug_refresh(void);
static void  cu_avr_audio_debug_event(auint value);
#endif
#define CU_AVR_SRAM_WRITE(addr, value) \
 do{ \
  auint cu_sram_addr_ = ((auint)(addr)) & 0x0FFFU; \
  auint cu_sram_value_ = ((auint)(value)) & 0xFFU; \
  cpu_state.sram[cu_sram_addr_] = (uint8)cu_sram_value_; \
  access_mem[cu_sram_addr_] |= CU_MEM_W; \
  if (cu_debug_mem_active){ cu_avr_debug_mem_event(CU_AVR_WATCH_REGION_SRAM, CU_AVR_WATCH_WRITE, cu_sram_addr_, cu_sram_value_); } \
 }while(0)
static inline auint cu_avr_sram_read(auint addr)
{
 auint cu_sram_addr_ = addr & 0x0FFFU;
 auint cu_sram_value_ = cpu_state.sram[cu_sram_addr_];
 access_mem[cu_sram_addr_] |= CU_MEM_R;
 if (cu_debug_mem_active){
  cu_avr_debug_mem_event(CU_AVR_WATCH_REGION_SRAM, CU_AVR_WATCH_READ,
                         cu_sram_addr_, cu_sram_value_);
 }
 return cu_sram_value_;
}
#define CU_AVR_SRAM_READ(addr) cu_avr_sram_read((auint)(addr))
#else
#define cu_avr_watchpoints_refresh() ((void)0)
#define CU_AVR_SRAM_WRITE(addr, value) \
 do{ \
  auint cu_sram_addr_ = ((auint)(addr)) & 0x0FFFU; \
  cpu_state.sram[cu_sram_addr_] = (uint8)(((auint)(value)) & 0xFFU); \
  access_mem[cu_sram_addr_] |= CU_MEM_W; \
 }while(0)
static inline auint cu_avr_sram_read(auint addr)
{
 auint cu_sram_addr_ = addr & 0x0FFFU;
 access_mem[cu_sram_addr_] |= CU_MEM_R;
 return (auint)cpu_state.sram[cu_sram_addr_];
}
#define CU_AVR_SRAM_READ(addr) cu_avr_sram_read((auint)(addr))
#endif

/* Frame information structure */
cu_frameinfo_t  video_frame;

/* Sync pulse counter (0 - 270) */
auint           video_pulsectr;

/* Cycle of previous edge of sync signal */
auint           video_pedge;

/* Cycle of previous rising edge of sync signal (used to find VSync) */
auint           video_prise;

/* Row generation flag (passes when a row has to be signalled) */
boole           video_rowflag;

/* Cycle counter within row. At most approx. 2010 */
auint           video_cycle;

/* API/debugger beam state. The renderer row buffer deliberately uses a
** VIDEO_CY_MAX rollover sentinel, so it is not a safe live debugger clock at
** a mid-row break. Keep an absolute line-start timestamp (updated only at row
** sync boundaries) and derive the live beam cycle from cpu_state.cycle. The
** separate PORTC buffer is written only while explicitly armed. */
#if defined(ENABLE_DEBUGGER) && defined(ENABLE_API_SERVER)
static auint   video_beam_line_start_cycle;
static auint   video_beam_pulse;
static auint   video_beam_prev_pulse;
static auint   video_beam_prev_line_cycles;
#define VIDEO_BEAM_CYCLE_NOW() WRAP32(cpu_state.cycle - video_beam_line_start_cycle)
#if defined(ENABLE_BEAM_CAPTURE)
static uint8   video_beam_pixels[2032U];
static boole   video_beam_capture_on = FALSE;
#endif
#endif

/* Timing analyzer hooks are deliberately tiny and only called at already
** occurring hardware events.  The expensive event capture is still guarded
** by the analyzer's opt-in enable/mask state. */
static void cu_avr_timing_irq_enter(auint vector)
{
#if defined(ENABLE_DEBUGGER) && defined(ENABLE_API_SERVER)
 if (cu_debug_timing_trace_gate){
  cu_debug_timing_trace_irq_enter(vector, video_beam_pulse, VIDEO_BEAM_CYCLE_NOW(), cpu_state.cycle);
 }
#else
 (void)vector;
#endif
}

static void cu_avr_timing_irq_exit(void)
{
#if defined(ENABLE_DEBUGGER) && defined(ENABLE_API_SERVER)
 if (cu_debug_timing_trace_gate){
  cu_debug_timing_trace_irq_exit(video_beam_pulse, VIDEO_BEAM_CYCLE_NOW(), cpu_state.cycle);
 }
#endif
}

/* A tiny sample FIFO to align sample output with rows cleanly */
auint           audio_samples[4U];
auint           audio_rp;
auint           audio_wp;

/* Next hardware event's cycle. It can be safely set to
** WRAP32(cpu_state.cycle + 1U) to force full processing next time. */
auint           cycle_next_event;

/* Timer1 TCNT1 adjustment value: WRAP32(cpu_state.cycle - timer1_base)
** gives the correct TCNT1 any time. */
auint           timer1_base;

/* Interrupt might be waiting to be serviced flag. This is set nonzero by
** any event which should trigger an IT including setting the I flag in the
** status register. It can be safely set nonzero to ask for a certain IT
** check. */
boole           event_it;

/* Interrupt entry necessary if set. */
boole           event_it_enter;

/* Vector to call when entering interrupt */
auint           event_it_vect;

/* EEPROM change indicator for saves to persistent storage */
boole           eeprom_changed;

/* Code ROM change indicator for saves to persistent storage */
boole           crom_changed;
static boole    flash_activity;

/* Watchdog-based debug counter: Time of last WDR execution */
auint           wd_last;

/* Watchdog-based debug counter: PC of last WDR execution */
auint           wd_last_pc;

/* Watchdog-based debug counter: Smallest interval between two WDR calls in a
** frame. Index 0 is the work value, Index 1 is the latched value. */
auint           wd_interval_min[2];

/* Watchdog-based debug counter: PC of baginning WDR of interval. Index 0 is
** the work value, Index 1 is the latched value. */
auint           wd_interval_beg[2];

/* Watchdog-based debug counter: PC of ending WDR of interval. Index 0 is the
** work value, Index 1 is the latched value. */
auint           wd_interval_end[2];

#ifdef HEADLESS
  SDL_Event qe; /* A quit event for whisper port writes to terminate program(HEADLESS only) */
#endif

/* Watchdog 16 millisecond timer base tick count */
#define WD_16MS_BASE (458176U - 1024U)

/* Watchdog timing seed mask base, used to mask for the 16 ms timer */
#define WD_SEED_MASK 2048U

/* EEPROM programming time base, assume ~1.75ms */
#define EEPROM_EWR_TIM 50000U

/* SPM programming time base, assuming ~4ms */
#define SPM_PROG_TIM 100000U

/* Maximal cycles in a video scanline (above which sync error is returned) */
#define VIDEO_CY_MAX 1920U

/* Watchdog-based debug counter: Maximal interval to display */
#define WD_INTERVAL_MAX (VIDEO_CY_MAX * 262U / 2U)


/* Flags in CU_IO_SREG */
#define SREG_I  7U
#define SREG_T  6U
#define SREG_H  5U
#define SREG_S  4U
#define SREG_V  3U
#define SREG_N  2U
#define SREG_Z  1U
#define SREG_C  0U
#define SREG_IM 0x80U
#define SREG_TM 0x40U
#define SREG_HM 0x20U
#define SREG_SM 0x10U
#define SREG_VM 0x08U
#define SREG_NM 0x04U
#define SREG_ZM 0x02U
#define SREG_CM 0x01U


/* Macros for managing the flags */

/* Clear flags by mask */
#define SREG_CLR(fl, xm) (fl &= (auint)(~((auint)(xm))))

/* Set flags by mask */
#define SREG_SET(fl, xm) (fl |= (xm))

/* Set Zero if (at most) 16 bit "val" is zero */
#define SREG_SET_Z(fl, val) (fl |= SREG_ZM & (((auint)(val) - 1U) >> 16))

/* Set Carry by bit 15 (for multiplications) */
#define SREG_SET_C_BIT15(fl, val) (fl |= ((val) >> 15) & 1U)

/* Set Carry by bit 16 (for float multiplications and adiw & sbiw) */
#define SREG_SET_C_BIT16(fl, val) (fl |= ((val) >> 16) & 1U)

/* Combine N and V into S (for all ops updating N or V) */
#define SREG_COM_NV(fl) (fl |= (((fl) << 1) ^ ((fl) << 2)) & 0x10U)

/* Get carry flag (for carry overs) */
#define SREG_GET_C(fl) ((fl) & 1U)


/* Macro for calculating a multiplication's flags */
#define PROCFLAGS_MUL(fl, res) \
 do{ \
  SREG_CLR(fl, SREG_CM | SREG_ZM); \
  SREG_SET_C_BIT15(fl, res); \
  SREG_SET_Z(fl, res & 0xFFFFU); \
 }while(0)

/* Macro for calculating a floating multiplication's flags */
#define PROCFLAGS_FMUL(fl, res) \
 do{ \
  SREG_CLR(fl, SREG_CM | SREG_ZM); \
  SREG_SET_C_BIT16(fl, res); \
  SREG_SET_Z(fl, res & 0xFFFFU); \
 }while(0)


/* Macro for updating hardware from within instructions */
#if defined(ENABLE_DEBUGGER) && defined(ENABLE_API_SERVER) && defined(ENABLE_BEAM_CAPTURE)
#define UPDATE_VIDEO_BEAM \
 do{ \
  if (video_beam_capture_on){ \
   auint cu_beam_cycle_ = VIDEO_BEAM_CYCLE_NOW(); \
   /* UPDATE_HARDWARE samples after advancing the absolute clock. Convert \
   ** the live instruction-boundary position back to the cycle just emitted. */ \
   if (cu_beam_cycle_ != 0U){ cu_beam_cycle_ --; } \
   if (cu_beam_cycle_ < 2032U){ video_beam_pixels[cu_beam_cycle_] = cpu_state.iors[CU_IO_PORTC]; } \
  } \
 }while(0)
#else
#define UPDATE_VIDEO_BEAM do{}while(0)
#endif

#define UPDATE_HARDWARE \
 do{ \
  cpu_state.cycle = WRAP32(cpu_state.cycle + 1U); \
  if (cycle_next_event == cpu_state.cycle){ cu_avr_hwexec(); } \
  video_row.pixels[video_cycle] = cpu_state.iors[CU_IO_PORTC]; \
  video_cycle ++; \
  UPDATE_VIDEO_BEAM; \
 }while(0)

/* Macro for consuming last instruction cycle before which ITs are triggered */
#define UPDATE_HARDWARE_IT \
 do{ \
  if (event_it){ cu_avr_itcheck(); } \
  UPDATE_HARDWARE; \
 }while(0)


/* Vectors base when IVSEL is 1 (MCUCR.1; Boot loader base) */
#define VBASE_BOOT     0x7800U


/* Various vectors */

/* Reset */
#define VECT_RESET     0x0000U
/* Watchdog */
#define VECT_WDT       0x0010U
/* Timer1 Comparator A */
#define VECT_T1COMPA   0x001AU
/* Timer1 Comparator B */
#define VECT_T1COMPB   0x001CU
/* Timer1 Overflow */
#define VECT_T1OVF     0x001EU
/* SPI */
#define VECT_SPISTC    0x0026U
/* USART0_TXC */
#define VECT_USART0_TX 0x002CU



/*
** Get Watchdog timeout ticks (WDP3 ignored, should be zero)
*/
static auint cu_avr_getwdto(void)
{
 auint presc = cpu_state.iors[CU_IO_WDTCSR] & 7U;
 return ( ((auint)(WD_16MS_BASE) << presc) +
          (cpu_state.wd_seed & (((auint)(WD_SEED_MASK) << presc) - 1U)) );
}



/*
** Resets the CPU by watchdog. It initializes CPU state according to CPU reset
** without touching other hardware and timing.
*/
static void  cu_avr_reset_wd(void)
{
 auint i;

 for (i = 0U; i < 256U; i++){ /* Most I/O regs are reset to zero */
  cpu_state.iors[i] = 0U;
 }
 cpu_state.iors[CU_IO_SPL] = 0xFFU;
 cpu_state.iors[CU_IO_SPH] = 0x10U;

 cpu_state.latch = 0U;

 cpu_state.pc = (((cpu_state.fuse[1] & 1U) ^ 1U) * VBASE_BOOT) + VECT_RESET;

 cpu_state.spi_tran = FALSE;
 cpu_state.wd_end   = WRAP32(cu_avr_getwdto() + cpu_state.cycle);
 cpu_state.eep_wrte = FALSE;
 cpu_state.spm_prge = FALSE;

 /* A watchdog reset reinitializes the AVR I/O state. Reset the SPI bus-side
 ** peripheral state as well so a bootloader hand-off can not leave SD or
 ** SPI RAM transactions half-open across the reset. External memory contents
 ** are preserved by the peripheral reset helpers. */
 cu_spi_reset(cpu_state.cycle);

 cu_avr_io_update();
}



/*
** Emulates cycle-precise hardware tasks. This is called through the
** UPDATE_HARDWARE macro if cycle_next_event matches the cycle counter (a new
** HW event is to be processed).
*/
static void cu_avr_hwexec(void)
{
 auint nextev = ~0U;
 auint t0;
 auint t1;
 auint t2;

 /* Timer 1 */

 if ((cpu_state.iors[CU_IO_TCCR1B] & 0x07U) != 0U){ /* Timer 1 started */

  t0 = (cpu_state.cycle - timer1_base) & 0xFFFFU;   /* Current TCNT1 value */
  t1 = ( ( ((auint)(cpu_state.iors[CU_IO_OCR1AL])     ) |
           ((auint)(cpu_state.iors[CU_IO_OCR1AH]) << 8) ) + 1U) & 0xFFFFU;
  t2 = ( ( ((auint)(cpu_state.iors[CU_IO_OCR1BL])     ) |
           ((auint)(cpu_state.iors[CU_IO_OCR1BH]) << 8) ) + 1U) & 0xFFFFU;

  if ((cpu_state.iors[CU_IO_TCCR1B] & 0x08U) != 0U){ /* Timer 1 in CTC mode: Counts to OCR1A, then resets */

   if (t0 == 0x0000U){                    /* Timer overflow (might happen if it starts above Comp. A, or Comp. A is 0) */
#if defined(ENABLE_DEBUGGER) && defined(ENABLE_API_SERVER)
    if (cu_debug_timing_trace_gate) cu_debug_timing_trace_event(CU_TIMING_EVT_T1_OVF, 1U, t0, video_beam_pulse, VIDEO_BEAM_CYCLE_NOW(), cpu_state.cycle);
#endif
    cpu_state.iors[CU_IO_TIFR1] |= 0x01U;
    event_it = TRUE;
   }

   if (t0 == t2){
#if defined(ENABLE_DEBUGGER) && defined(ENABLE_API_SERVER)
    if (cu_debug_timing_trace_gate) cu_debug_timing_trace_event(CU_TIMING_EVT_T1_COMPB, 1U, t0, video_beam_pulse, VIDEO_BEAM_CYCLE_NOW(), cpu_state.cycle);
#endif
    cpu_state.iors[CU_IO_TIFR1] |= 0x04U; /* Comparator B interrupt */
    event_it = TRUE;
   }

   if (t0 == t1){
#if defined(ENABLE_DEBUGGER) && defined(ENABLE_API_SERVER)
    if (cu_debug_timing_trace_gate) cu_debug_timing_trace_event(CU_TIMING_EVT_T1_COMPA, 1U, t0, video_beam_pulse, VIDEO_BEAM_CYCLE_NOW(), cpu_state.cycle);
#endif
    cpu_state.iors[CU_IO_TIFR1] |= 0x02U; /* Comparator A interrupt */
    event_it = TRUE;
    timer1_base = cpu_state.cycle;        /* Reset timer to zero */
    t0 = 0U;                              /* Also reset for event calculation */
   }

   if ( (t0 != t2) &&
        (nextev > (t2 - t0)) ){ nextev = t2 - t0; } /* Next Comp. B match */
   if ( (nextev > (t1 - t0)) ){ nextev = t1 - t0; } /* Next Comp. A match */
   if ( (nextev > (0x10000U - t0)) ){ nextev = 0x10000U - t0; } /* Next Overflow (if timer was set above Comp. A) */

  }else{ /* Timer 1 in normal mode: wrapping 16 bit counter */

   if (t0 == 0x0000U){
#if defined(ENABLE_DEBUGGER) && defined(ENABLE_API_SERVER)
    if (cu_debug_timing_trace_gate) cu_debug_timing_trace_event(CU_TIMING_EVT_T1_OVF, 1U, t0, video_beam_pulse, VIDEO_BEAM_CYCLE_NOW(), cpu_state.cycle);
#endif
    cpu_state.iors[CU_IO_TIFR1] |= 0x01U; /* Overflow interrupt */
    event_it = TRUE;
    timer1_base = cpu_state.cycle;        /* Reset timer to zero */
   }

   if (nextev > (0x10000U - t0)){ nextev = 0x10000U - t0; }

  }

 }

 /* Watchdog (in Uzebox used to seed random number generators and soft reset) */

 if ((cpu_state.iors[CU_IO_WDTCSR] & 0x48U) != 0U){ /* Watchdog is operational */

  if (cpu_state.cycle == cpu_state.wd_end){ /* Watchdog timed out */
   cpu_state.iors[CU_IO_WDTCSR] |= 0x80U;   /* Watchdog interrupt flag */
   event_it = TRUE;
   cpu_state.wd_end = WRAP32(cu_avr_getwdto() + cpu_state.cycle);
   if ((cpu_state.iors[CU_IO_WDTCSR] & 0x48U) == 0x08U){ /* WDE set & WDIE clear: System reset! */
    cu_avr_reset_wd();
   }
  }

  t0 = WRAP32(cpu_state.wd_end - cpu_state.cycle);
  if (nextev > t0){ nextev = t0; }

 }

 /* USART0 peripherals (RLE mode end scanline) */

 if (cpu_state.usr0_tran){

  if (cpu_state.cycle == cpu_state.usr0_end){

   cpu_state.usr0_tran = FALSE;
   cpu_state.iors[CU_IO_UCSR0A] |= 0x40U; /* USART0 TX complete flag */
   event_it = TRUE;

  }else{

   t0 = WRAP32(cpu_state.usr0_end - cpu_state.cycle);
   if (nextev > t0){ nextev = t0; }

  }

 }

 /* SPI peripherals (SD card, SPI RAM) */

 if (cpu_state.spi_tran){

  if (cpu_state.cycle == cpu_state.spi_end){

   cpu_state.spi_tran = FALSE;
   cpu_state.iors[CU_IO_SPSR] |= 0x80U; /* SPI interrupt */
   event_it = TRUE;
   cpu_state.iors[CU_IO_SPDR] = cpu_state.spi_rx;
   cu_spi_send(cpu_state.spi_tx, cpu_state.cycle);

  }else{

   t0 = WRAP32(cpu_state.spi_end - cpu_state.cycle);
   if (nextev > t0){ nextev = t0; }

  }

 }

 /* EEPROM */

 if (cpu_state.eep_wrte){

  if (cpu_state.cycle == cpu_state.eep_end){

   cpu_state.eep_wrte = FALSE;
   t0 = cpu_state.iors[CU_IO_EECR] & 0x30U; /* EEPROM write mode */
   t1 = ( ((auint)(cpu_state.iors[CU_IO_EEARH]) << 8) |
          ((auint)(cpu_state.iors[CU_IO_EEARL])     ) ) & 0x7FFU;
   t2 = cpu_state.eepr[t1];
   if       (t0 == 0x00U){ /* Erase and Write */
    cpu_state.eepr[t1]  = cpu_state.iors[CU_IO_EEDR];
   }else if (t0 == 0x10U){ /* Erase only */
    cpu_state.eepr[t1]  = 0xFFU;
   }else if (t0 == 0x20U){ /* Write only */
    cpu_state.eepr[t1] |= cpu_state.iors[CU_IO_EEDR];
   }else{                  /* Reserved: Do nothing */
   }
   if (t2 != t1){ eeprom_changed = TRUE; }
   cpu_state.iors[CU_IO_EECR] &= ~0x02U; /* Clear EEPE (programming completed) */

  }else{

   t0 = WRAP32(cpu_state.eep_end - cpu_state.cycle);
   if (nextev > t0){ nextev = t0; }

  }

 }else if ((cpu_state.iors[CU_IO_EECR] & 0x04U) != 0U){

  if (cpu_state.cycle == cpu_state.eep_end){
   cpu_state.iors[CU_IO_EECR] &= ~0x04U; /* Clear EEMPE (disable programming) */
  }else{
   t0 = WRAP32(cpu_state.eep_end - cpu_state.cycle);
   if (nextev > t0){ nextev = t0; }
  }

 }else{}

 /* SPM */

 if (cpu_state.spm_prge){

  if (cpu_state.cycle == cpu_state.spm_end){

   cpu_state.spm_prge = FALSE; /* Completed erasing / programming */
   cpu_state.iors[CU_IO_SPMCSR] &= ~0x1FU;

  }else{

   t0 = WRAP32(cpu_state.spm_end - cpu_state.cycle);
   if (nextev > t0){ nextev = t0; }

  }

 }else if ((cpu_state.iors[CU_IO_SPMCSR] & 0x01U) != 0U){

  if (cpu_state.cycle == cpu_state.spm_end){
   cpu_state.iors[CU_IO_SPMCSR] &= ~0x01U; /* Cancel SPM instruction */
  }else{
   t0 = WRAP32(cpu_state.spm_end - cpu_state.cycle);
   if (nextev > t0){ nextev = t0; }
  }

 }else{}

 /* Calculate next event's cycle */

 cycle_next_event = WRAP32(cpu_state.cycle + nextev);
}



/*
** Enters requested interrupt (event_it_enter must be true)
*/
static void cu_avr_interrupt(void)
{
 auint tmp;

 event_it_enter = FALSE; /* Requested IT entry performed */

 SREG_CLR(cpu_state.iors[CU_IO_SREG], SREG_IM);

 tmp   = ((auint)(cpu_state.iors[CU_IO_SPL])     ) +
         ((auint)(cpu_state.iors[CU_IO_SPH]) << 8);
 CU_AVR_SRAM_WRITE(tmp, (cpu_state.pc     ) & 0xFFU);
 tmp --;
 CU_AVR_SRAM_WRITE(tmp, (cpu_state.pc >> 8) & 0xFFU);
 tmp --;
 cpu_state.iors[CU_IO_SPL] = (tmp     ) & 0xFFU;
 cpu_state.iors[CU_IO_SPH] = (tmp >> 8) & 0xFFU;

 cu_avr_timing_irq_enter(event_it_vect);
 cpu_state.pc = event_it_vect;

 UPDATE_HARDWARE;
 UPDATE_HARDWARE;
 UPDATE_HARDWARE;
}



/*
** Checks for interrupts and triggers if any is pending. Clears event_it when
** there are no more interrupts waiting for servicing.
*/
static void cu_avr_itcheck(void)
{
 auint vbase;

 /* Global interrupt enable? */

 if ((cpu_state.iors[CU_IO_SREG] & SREG_IM) == 0U){
  event_it = FALSE;
  return;
 }

 /* Interrupts are enabled, so check them */

 vbase = ((cpu_state.iors[CU_IO_MCUCR] >> 1) & 1U) * VBASE_BOOT;

 if       ( (cpu_state.iors[CU_IO_SPCR] &
             cpu_state.iors[CU_IO_SPSR] & 0x80U)   !=    0U ){ /* SPI */

  cpu_state.iors[CU_IO_SPSR] ^= 0x80U;
  event_it_enter = TRUE;
  event_it_vect  = vbase + VECT_SPISTC;

 }else if ( (cpu_state.iors[CU_IO_WDTCSR] & 0xC0U) == 0xC0U ){ /* Watchdog */

  cpu_state.iors[CU_IO_WDTCSR] &= 0x7FU;             /* Always clear WDIF */
  if ((cpu_state.iors[CU_IO_WDTCSR] & 0x08U) != 0U){ /* WDE */
   cpu_state.iors[CU_IO_WDTCSR] &= 0xBFU;            /* Clear WDIE too (next WD timeout is a sys reset) */
  }
  event_it_enter = TRUE;
  event_it_vect  = vbase + VECT_WDT;

 }else if ( (cpu_state.iors[CU_IO_TIFR1] &
             cpu_state.iors[CU_IO_TIMSK1] & 0x02U) !=    0U ){ /* Timer 1 Comparator A */

  cpu_state.iors[CU_IO_TIFR1] ^= 0x02U;
  event_it_enter = TRUE;
  event_it_vect  = vbase + VECT_T1COMPA;

 }else if ( (cpu_state.iors[CU_IO_TIFR1] &
             cpu_state.iors[CU_IO_TIMSK1] & 0x04U) !=    0U ){ /* Timer 1 Comparator B */

  cpu_state.iors[CU_IO_TIFR1] ^= 0x04U;
  event_it_enter = TRUE;
  event_it_vect  = vbase + VECT_T1COMPB;

 }else if ( (cpu_state.iors[CU_IO_TIFR1] &
             cpu_state.iors[CU_IO_TIMSK1] & 0x01U) !=    0U ){ /* Timer 1 Overflow */

  cpu_state.iors[CU_IO_TIFR1] ^= 0x01U;
  event_it_enter = TRUE;
  event_it_vect  = vbase + VECT_T1OVF;

 }else if ( ((cpu_state.iors[CU_IO_UCSR0A] & (1U << TXC0)) != 0U) &&
             ((cpu_state.iors[CU_IO_UCSR0B] & (1U << TXCIE0)) != 0U) ){ /* USART0 TX complete */

  /* TXC only requests an interrupt when TXCIE is enabled. Polling-only UART
  ** software (including Catacombs of the Damned) leaves TXCIE clear. The old
  ** unconditional check repeatedly entered the USART TX vector merely because
  ** an idle transmitter reports TXC, corrupting video timing and game state. */
  cpu_state.iors[CU_IO_UCSR0A] &= (uint8)~(1U << TXC0);
  event_it_enter = TRUE;
  event_it_vect  = vbase + VECT_USART0_TX;

 }else{ /* No interrupts are pending */

  event_it = FALSE;

 }
}



/*
** Writes an I/O port
*/
static void  cu_avr_write_io(auint port, auint val)
{
 auint pval = cpu_state.iors[port]; /* Previous value */
 auint cval = val & 0xFFU;          /* Current (requested) value */
 auint pio;
 auint cio;
 auint t0;
 auint t1;
 auint t2;
#if defined(ENABLE_DEBUGGER) && defined(ENABLE_API_SERVER)
 auint trace_old_pina = 0U;
 boole trace_pina_valid = FALSE;
#endif

 access_io[port] |= CU_MEM_W;
#ifdef ENABLE_DEBUGGER
 if (cu_debug_mem_active){ cu_avr_debug_mem_event(CU_AVR_WATCH_REGION_IO, CU_AVR_WATCH_WRITE, port & 0x00FFU, cval); }
#endif

 switch (port){

  case CU_IO_PORTA:   /* Controller inputs, SPI RAM Chip Select */
  case CU_IO_DDRA:

   if (port == CU_IO_PORTA){
    pio = pval & cpu_state.iors[CU_IO_DDRA];
    cio = cval & cpu_state.iors[CU_IO_DDRA];
   }else{
    pio = pval & cpu_state.iors[CU_IO_PORTA];
    cio = cval & cpu_state.iors[CU_IO_PORTA];
   }
#if defined(ENABLE_DEBUGGER) && defined(ENABLE_API_SERVER)
   trace_old_pina = cpu_state.iors[CU_IO_PINA];
   trace_pina_valid = TRUE;
#endif
   t2 = cu_hap_process(pio, cio);
   t0 = cu_ctr_process(pio, cio);
   t1 = cu_kbd_process(pio, cio);
   cpu_state.iors[CU_IO_PINA] = t2|t1|t0;

   cu_spi_cs_set(CU_SPI_CS_RAM, (cio & 0x10U) == 0U, cpu_state.cycle);
   break;

  case CU_IO_PORTB:   /* Sync output */
  case CU_IO_DDRB:

   if (port == CU_IO_PORTB){
    pio = pval & cpu_state.iors[CU_IO_DDRB];
    cio = cval & cpu_state.iors[CU_IO_DDRB];
   }else{
    pio = pval & cpu_state.iors[CU_IO_PORTB];
    cio = cval & cpu_state.iors[CU_IO_PORTB];
   }

   if (((pio ^ cio) & 1U) != 0U){   /* Sync edge */

    t0 = WRAP32(cpu_state.cycle - video_pedge); /* Cycles elapsed since previous edge */
    video_pedge = cpu_state.cycle;

    if ((cio & 1U) == 1U){      /* Rising edge */

     if ( (video_pulsectr <= 268U) &&
          (video_pulsectr != 251U) ){
      video_pulsectr ++;
     }
     if (video_pulsectr <  252U){
      video_frame.rowcdif ++;
     }
     if (video_pulsectr == 270U){
      video_pulsectr      = 0U;
      video_frame.rowcdif = 0U - 252U;
     }
     t1 = WRAP32(cpu_state.cycle - video_prise);
     video_prise = cpu_state.cycle;
     if       ( (t1 >=  944U) &&
                (t1 <= 1012U) && /* Sync to first normal pulse (978 cycles apart from last rise) */
                (video_pulsectr >= 252U) ){
      video_pulsectr = 270U;
     }else if ( (t1 >= 1718U) &&
                (t1 <= 1786U) ){ /* Sync to first VSync pulse (1752 cycles apart from last rise) */
      video_pulsectr = 252U;
     }else{}

     if ( (video_pulsectr < 252U) ||
          (video_pulsectr == 270U) ){ /* 0 - 251 & 270 are normal rises 136 cycles after the fall */
      video_frame.pulse[video_pulsectr].rise = t0 - 136U;
     }else{
      switch (video_pulsectr){
       case 252U:
       case 253U:
       case 254U:
       case 255U:
       case 256U:
       case 257U:                /* 251 - 257 come 68 cycles after the fall */
        video_frame.pulse[video_pulsectr].rise = t0 - 68U;
        break;
       case 258U:
       case 259U:
       case 260U:
       case 261U:
       case 262U:
       case 263U:                /* 258 - 263 come 774 cycles after the fall */
        video_frame.pulse[video_pulsectr].rise = t0 - 774U;
        break;
       case 264U:
       case 265U:
       case 266U:
       case 267U:
       case 268U:
       case 269U:                /* 264 - 269 come 68 cycles after the fall */
        video_frame.pulse[video_pulsectr].rise = t0 - 68U;
        break;
       default:                  /* Out of sync */
        break;
      }
     }

    }else{                       /* Falling edge */

     if ( (video_pulsectr < 252U) ||
          (video_pulsectr == 270U) ){ /* 0 - 251 & 270 are normal falls 1684 cycles after the rise */
      video_frame.pulse[video_pulsectr].fall = t0 - 1684U;
      video_rowflag = TRUE;      /* Trigger new row */
      video_cycle   = VIDEO_CY_MAX; /* Also flags new row */
#if defined(ENABLE_DEBUGGER) && defined(ENABLE_API_SERVER)
      t2 = VIDEO_BEAM_CYCLE_NOW();
      if (cu_debug_timing_scan_gate) cu_debug_timing_row_boundary(video_beam_pulse, t2, video_pulsectr);
      video_beam_prev_pulse = video_beam_pulse;
      video_beam_prev_line_cycles = t2;
      video_beam_line_start_cycle = cpu_state.cycle;
      video_beam_pulse = video_pulsectr;
#endif
     }else{
      switch (video_pulsectr){
       case 252U:
       case 253U:
       case 254U:
       case 255U:
       case 256U:
       case 257U:                /* 252 - 257 come 842 cycles after the rise */
        video_frame.pulse[video_pulsectr].fall = t0 - 842U;
        break;
       case 258U:
       case 259U:
       case 260U:
       case 261U:
       case 262U:
       case 263U:                /* 258 - 263 come 136 cycles after the rise */
        video_frame.pulse[video_pulsectr].fall = t0 - 136U;
        break;
       case 264U:
       case 265U:
       case 266U:
       case 267U:
       case 268U:
       case 269U:                /* 264 - 269 come 842 cycles after the rise */
        video_frame.pulse[video_pulsectr].fall = t0 - 842U;
        break;
       default:                  /* Out of sync */
        break;
      }
      if ((video_pulsectr & 1U) != 0U){
       video_rowflag = TRUE;    /* Odd pulses trigger new row */
       video_cycle   = VIDEO_CY_MAX; /* Also flags new row */
#if defined(ENABLE_DEBUGGER) && defined(ENABLE_API_SERVER)
       t2 = VIDEO_BEAM_CYCLE_NOW();
       if (cu_debug_timing_scan_gate) cu_debug_timing_row_boundary(video_beam_pulse, t2, video_pulsectr);
       video_beam_prev_pulse = video_beam_pulse;
       video_beam_prev_line_cycles = t2;
       video_beam_line_start_cycle = cpu_state.cycle;
       video_beam_pulse = video_pulsectr;
#endif
      }
     }

    }

#if defined(ENABLE_DEBUGGER) && defined(ENABLE_API_SERVER)
    if (cu_debug_timing_sync_gate){
     cu_debug_timing_sync_edge(video_beam_pulse, VIDEO_BEAM_CYCLE_NOW(), ((cio & 1U) != 0U) ? TRUE : FALSE);
    }
#endif
   }

   break;

  case CU_IO_PORTC:   /* Pixel output */

   /* Special shortcut masking with the DDR register. This port tolerates a
   ** bit of inaccuracy since it is only graphics and is most frequently
   ** written "normally" (the DDR is used for fade effects on it) */
   cval &= cpu_state.iors[CU_IO_DDRC];
   break;

  case CU_IO_PORTD:   /* SD card Chip Select */
  case CU_IO_DDRD:
   if((pval & (1<<3)) != (cval & (1<<3))) /* ESP8266 reset pin changed */
     cu_esp_reset_pin(((cval & (1<<3)) ? 1:0), cpu_state.cycle);

   if (port == CU_IO_PORTD){
    cio = cval & cpu_state.iors[CU_IO_DDRD];
   }else{
    cio = cval & cpu_state.iors[CU_IO_PORTD];
   }
   cu_spi_cs_set(CU_SPI_CS_SD, (cio & 0x40U) == 0U, cpu_state.cycle);
   break;

  case CU_IO_OCR2A:   /* PWM audio output */

   if (cpu_state.iors[CU_IO_TCCR2B] != 0U){
    audio_samples[audio_wp] = cval;
#if defined(ENABLE_DEBUGGER) && defined(ENABLE_AUDIO_TRACE)
    if (audio_debug_active){ cu_avr_audio_debug_event(cval); }
#endif
    if (audio_rp == audio_wp){ audio_rp = (audio_rp + 1U) & 0x3U; }
    audio_wp = (audio_wp + 1U) & 0x3U;
   }
   break;

  case CU_IO_TCNT1H:  /* Timer1 counter, high */

   cpu_state.latch = cval; /* Write into latch (value written to the port itself is ignored) */
   break;

  case CU_IO_TCNT1L:  /* Timer1 counter, low */

   t0    = (cpu_state.latch << 8) | cval;
   timer1_base = WRAP32(cpu_state.cycle - t0);
   cycle_next_event = WRAP32(cpu_state.cycle + 1U); /* Request HW processing */
   break;

  case CU_IO_TIFR1:   /* Timer1 interrupt flags */

   cval  = pval & (~cval);
   break;

  case CU_IO_TCCR1B:  /* Timer1 control */
  case CU_IO_OCR1AH:  /* Timer1 comparator A, high */
  case CU_IO_OCR1AL:  /* Timer1 comparator A, low */
  case CU_IO_OCR1BH:  /* Timer1 comparator B, high */
  case CU_IO_OCR1BL:  /* Timer1 comparator B, low */

   cycle_next_event = WRAP32(cpu_state.cycle + 1U); /* Request HW processing */
   break;


  case CU_IO_UDR0:    /* USART0 data */
   cu_esp_uzebox_write(cval, cpu_state.cycle); /* module will handle dropping based off UART settings and state... */
//   cpu_state.usr0_tran = TRUE;
//   cpu_state.usr0_end  = WRAP32( cpu_state.cycle + 1304 );
//   if ( (WRAP32(cycle_next_event  - cpu_state.cycle)) >
//        (WRAP32(cpu_state.usr0_end - cpu_state.cycle)) ){
//    cycle_next_event = cpu_state.usr0_end; /* Set USR HW processing target */
//   }
   break;

  case CU_IO_UCSR0B: /* Rx/Tx on/off */
  case CU_IO_UCSR0A: /* UART double speed mode */
  case CU_IO_UCSR0C: /* UART frame settings(ie 8N1) */
  case CU_IO_UBRR0L: /* Baud bits 0-7 */
  case CU_IO_UBRR0H: /* Baud bits 8-11 */
   cu_esp_uzebox_modify(port, cval, cpu_state.cycle);
   break;

  case CU_IO_SPDR:    /* SPI data */

   cpu_state.iors[CU_IO_SPSR] &= ~0x80U;
   /* Note: By the doc first a read would be necessary for clearing SPIF here,
   ** but that's a very unusual use case, so ignored */
   if ((cpu_state.iors[CU_IO_SPCR] & 0x40U) != 0U){ /* SPI enabled */
    if (cpu_state.spi_tran){                /* Already sending */
     cpu_state.iors[CU_IO_SPSR] |= 0x40U;   /* Signal write collision */
    }else{
     cpu_state.spi_tran = TRUE;
     cpu_state.spi_end  = WRAP32( cpu_state.cycle +
          (16U << ( ((cpu_state.iors[CU_IO_SPCR] & 0x3U) << 1) |
                    ((cpu_state.iors[CU_IO_SPSR] & 0x1U) ^ 1U) )) );
     if ( (WRAP32(cycle_next_event  - cpu_state.cycle)) >
          (WRAP32(cpu_state.spi_end - cpu_state.cycle)) ){
      cycle_next_event = cpu_state.spi_end; /* Set SPI HW processing target */
     }
     cpu_state.spi_rx = cu_spi_recv(cpu_state.cycle);
     cpu_state.spi_tx = cval;
#if defined(ENABLE_DEBUGGER) && defined(ENABLE_API_SERVER)
     if (cu_debug_timing_trace_gate && cu_debug_timing_trace_event_enabled(CU_TIMING_EVT_SPI)){
      auint spi_flags = 0U;
      if ((cpu_state.iors[CU_IO_SPCR] & 0x08U) != 0U) spi_flags |= CU_TIMING_SPI_CPOL;
      if ((cpu_state.iors[CU_IO_SPCR] & 0x04U) != 0U) spi_flags |= CU_TIMING_SPI_CPHA;
      if ((cpu_state.iors[CU_IO_SPCR] & 0x20U) != 0U) spi_flags |= CU_TIMING_SPI_DORD;
      cu_debug_timing_trace_spi(cval, cpu_state.spi_rx, WRAP32(cpu_state.spi_end - cpu_state.cycle), spi_flags, video_beam_pulse, VIDEO_BEAM_CYCLE_NOW(), cpu_state.cycle);
     }
#endif
    }
   }
   break;

  case CU_IO_SPCR:    /* SPI control */

   break;

  case CU_IO_SPSR:    /* SPI status */

   break;

  case CU_IO_EECR:    /* EEPROM control */

   if ((pval & 0x04U) == 0U){
    cval &= ~0x02U;   /* Without EEMPE, programming (EEPE) can not start */
   }else{
    cpu_state.eep_end  = WRAP32(cpu_state.cycle + 4U); /* Open EEPE window (4 cycles) */
    if ( (WRAP32(cycle_next_event  - cpu_state.cycle)) >
         (WRAP32(cpu_state.eep_end - cpu_state.cycle)) ){
     cycle_next_event = cpu_state.eep_end; /* Set EEPROM HW processing target */
    }
   }

   if ( ((pval & 0x02U) == 0U) &&
        ((cval & 0x02U) != 0U) ){ /* Programming started */
    cpu_state.eep_wrte = TRUE;
    t0 = cpu_state.iors[CU_IO_EECR] & 0x30U;  /* EEPROM write mode */
    cpu_state.eep_end  = EEPROM_EWR_TIM;
    if (t0 == 0U){ cpu_state.eep_end *= 2U; } /* Erase + Write */
    cpu_state.eep_end  = WRAP32(cpu_state.eep_end + cpu_state.cycle);
    if ( (WRAP32(cycle_next_event  - cpu_state.cycle)) >
         (WRAP32(cpu_state.eep_end - cpu_state.cycle)) ){
     cycle_next_event = cpu_state.eep_end; /* Set EEPROM HW processing target */
    }
    UPDATE_HARDWARE;  /* 2 cycles write stall. */
    UPDATE_HARDWARE;  /* Note: IT checks are slightly off due to this, but this inaccuracy is tolerable. */
    cval &= ~0x04U;   /* Turn off EEMPE (succesfully entered programming) */
   }

   if (cval & 0x01U){ /* EEPROM read (EERE) strobe */
    if (!cpu_state.eep_wrte){ /* During writing it can't be done */
     t0 = ( ((auint)(cpu_state.iors[CU_IO_EEARH]) << 8) |
            ((auint)(cpu_state.iors[CU_IO_EEARL])     ) ) & 0x7FFU;
     cpu_state.iors[CU_IO_EEDR] = cpu_state.eepr[t0];
     UPDATE_HARDWARE;
     UPDATE_HARDWARE;
     UPDATE_HARDWARE; /* 4 cycles read stall. */
     UPDATE_HARDWARE; /* Note: IT checks are slightly off due to this, but this inaccuracy is tolerable. */
    }
    cval &= ~0x01U;
    /* Note: The EERE bit is a little hazy, it is not described whether it is
    ** cleared after write or not, the SBI / CBI instructions might work
    ** differently on this port than read + mask + write. Clearing it however
    ** works for the documented usage (the bit is never read anyway). */
   }
   break;

  case CU_IO_EEARH:   /* EEPROM address & data registers */
  case CU_IO_EEARL:
  case CU_IO_EEDR:

   if (cpu_state.eep_wrte){
    cval = pval;      /* During EEPROM programming, these can't be modified */
   }
   break;

  case CU_IO_WDTCSR:  /* Watchdog timer control */

   if ( ((pval & 0x48U) == 0U) &&
        ((cval & 0x48U) != 0U) ){ /* Watchdog becomes enabled, so start it */
    cpu_state.wd_end = WRAP32(cu_avr_getwdto() + cpu_state.cycle);
    if ( (WRAP32(cycle_next_event - cpu_state.cycle)) >
         (WRAP32(cpu_state.wd_end - cpu_state.cycle)) ){
     cycle_next_event = cpu_state.wd_end; /* Set Watchdog timeout HW processing target */
    }
   }
   break;

  case CU_IO_SPMCSR:  /* Store program memory control & status */

   cval = (cval & (~0x40U)) | (pval & 0x40U); /* RRWSB bit can not be written */
   if ( ((pval & 0x01U) == 0U) &&
        ((cval & 0x01U) != 0U) && /* SPM enable just turned on */
        (!cpu_state.spm_prge) ){
    t0 = cval & 0x1EU;    /* SPM mode select bits */
    if ( (t0 == 0x00U) || /* Page load (filling temp buffer) */
         (t0 == 0x02U) || /* Page erase */
         (t0 == 0x04U) || /* Page write */
         (t0 == 0x08U) || /* Boot lock bit set */
         (t0 == 0x10U) ){ /* RWW section read enable */
     cpu_state.spm_mode = t0;
     cpu_state.spm_end  = WRAP32(cpu_state.cycle + 4U);
     if ( (WRAP32(cycle_next_event  - cpu_state.cycle)) >
          (WRAP32(cpu_state.spm_end - cpu_state.cycle)) ){
      cycle_next_event = cpu_state.spm_end; /* Set SPM HW processing target */
     }
    }
   }
   break;

  case CU_IO_SREG:    /* Status register */

   if ((((~pval) & cval) & SREG_IM) != 0U){
    event_it = TRUE;  /* Interrupts become enabled, so check them */
   }
   break;

 case 0x39: /* Emulator-only, whisper logging */
   printf("%02x", cval);
 case 0x3A: /* Purposely printf, so it works with -DHEADLESS=1 */
   printf("%c", cval);

  default:

   break;

 }

#if defined(ENABLE_DEBUGGER) && defined(ENABLE_API_SERVER)
 if (cu_debug_timing_trace_gate){
  if (port == CU_IO_PORTC && cval != pval){
   cu_debug_timing_trace_event(CU_TIMING_EVT_PORTC, cval, 0U, video_beam_pulse, VIDEO_BEAM_CYCLE_NOW(), cpu_state.cycle);
  }else if (port == CU_IO_PORTB || port == CU_IO_DDRB){
   auint oldio = (port == CU_IO_PORTB) ? (pval & cpu_state.iors[CU_IO_DDRB]) : (pval & cpu_state.iors[CU_IO_PORTB]);
   auint newio = (port == CU_IO_PORTB) ? (cval & cpu_state.iors[CU_IO_DDRB]) : (cval & cpu_state.iors[CU_IO_PORTB]);
   if (((oldio ^ newio) & 1U) != 0U) cu_debug_timing_trace_event(CU_TIMING_EVT_SYNC, newio & 1U, 0U, video_beam_pulse, VIDEO_BEAM_CYCLE_NOW(), cpu_state.cycle);
  }else if (port == CU_IO_PORTA || port == CU_IO_DDRA){
   auint oldio = (port == CU_IO_PORTA) ? (pval & cpu_state.iors[CU_IO_DDRA]) : (pval & cpu_state.iors[CU_IO_PORTA]);
   auint newio = (port == CU_IO_PORTA) ? (cval & cpu_state.iors[CU_IO_DDRA]) : (cval & cpu_state.iors[CU_IO_PORTA]);
   if (((oldio ^ newio) & 0x10U) != 0U) cu_debug_timing_trace_event(CU_TIMING_EVT_RAM_CS, (newio & 0x10U)?1U:0U, 0U, video_beam_pulse, VIDEO_BEAM_CYCLE_NOW(), cpu_state.cycle);
   if (((oldio ^ newio) & 0x04U) != 0U) cu_debug_timing_trace_event(CU_TIMING_EVT_CTR_LATCH, (newio & 0x04U)?1U:0U, 0U, video_beam_pulse, VIDEO_BEAM_CYCLE_NOW(), cpu_state.cycle);
   if (((oldio ^ newio) & 0x08U) != 0U) cu_debug_timing_trace_event(CU_TIMING_EVT_CTR_CLOCK, (newio & 0x08U)?1U:0U, 0U, video_beam_pulse, VIDEO_BEAM_CYCLE_NOW(), cpu_state.cycle);
   if (trace_pina_valid){
    auint new_pina = cpu_state.iors[CU_IO_PINA];
    if (((trace_old_pina ^ new_pina) & 0x01U) != 0U) cu_debug_timing_trace_event(CU_TIMING_EVT_CTR_DATA0, new_pina & 1U, 0U, video_beam_pulse, VIDEO_BEAM_CYCLE_NOW(), cpu_state.cycle);
    if (((trace_old_pina ^ new_pina) & 0x02U) != 0U) cu_debug_timing_trace_event(CU_TIMING_EVT_CTR_DATA1, (new_pina >> 1) & 1U, 0U, video_beam_pulse, VIDEO_BEAM_CYCLE_NOW(), cpu_state.cycle);
   }
  }else if (port == CU_IO_PORTD || port == CU_IO_DDRD){
   auint oldio = (port == CU_IO_PORTD) ? (pval & cpu_state.iors[CU_IO_DDRD]) : (pval & cpu_state.iors[CU_IO_PORTD]);
   auint newio = (port == CU_IO_PORTD) ? (cval & cpu_state.iors[CU_IO_DDRD]) : (cval & cpu_state.iors[CU_IO_PORTD]);
   if (((oldio ^ newio) & 0x40U) != 0U) cu_debug_timing_trace_event(CU_TIMING_EVT_SD_CS, (newio & 0x40U)?1U:0U, 0U, video_beam_pulse, VIDEO_BEAM_CYCLE_NOW(), cpu_state.cycle);
  }
 }
#endif
 cpu_state.iors[port] = cval;
}



/*
** Reads from an I/O port
*/
static auint cu_avr_read_io(auint port)
{
 auint t0;
 auint ret = 0U;

 access_io[port] |= CU_MEM_R;

 switch (port){

  case CU_IO_TCNT1L:
   t0  = WRAP32(cpu_state.cycle - timer1_base); /* Current TCNT1 value */
   cpu_state.latch = (t0 >> 8) & 0xFFU;
   ret = t0 & 0xFFU;
   break;

  case CU_IO_TCNT1H:
   ret = cpu_state.latch;
   break;

  case CU_IO_UDR0: /* read a UART byte */
   ret = cu_esp_uzebox_read(cpu_state.cycle); /* module will do the appropriate thing based on settings and state... */
   break;

  case CU_IO_UCSR0A: /* UART Rx/Tx ready status */
   cpu_state.iors[CU_IO_UCSR0A] &= ~((1<<UDRE0) | (1<<RXC0) | (1<<TXC0) | (1<<FE0) | (1<<DOR0) | (1<<UPE0));
   cpu_state.iors[CU_IO_UCSR0A] |= cu_esp_uzebox_status(cpu_state.cycle);

   ret = cpu_state.iors[CU_IO_UCSR0A];
   break;
#ifdef HEADLESS
  case 0x3A: /* reading from first whisper port, allows program to detect if running headless */
  qe.type = SDL_QUIT;
  SDL_PushEvent(&qe);
#endif
  break;
  default:
   ret = cpu_state.iors[port];
   break;
 }

#ifdef ENABLE_DEBUGGER
 if (cu_debug_mem_active){ cu_avr_debug_mem_event(CU_AVR_WATCH_REGION_IO, CU_AVR_WATCH_READ, port & 0x00FFU, ret); }
#endif

 return ret;
}

#ifdef ENABLE_DEBUGGER
static void cu_avr_debug_mem_refresh(void)
{
 cu_debug_mem_active = watchpoints_armed;
#ifdef ENABLE_MEMORY_TRACE
 if (memory_trace_on){ cu_debug_mem_active = TRUE; }
#endif
}

static void cu_avr_watchpoints_refresh(void)
{
 auint i;
 watchpoints_armed = FALSE;
 for (i = 0U; i < CU_AVR_WATCH_SLOTS; i++){
  if ((watchpoint_map[i].enabled != 0U) && ((watchpoint_map[i].flags & (CU_AVR_WATCH_READ | CU_AVR_WATCH_WRITE)) != 0U)){
   watchpoints_armed = TRUE;
   break;
  }
 }
 cu_avr_debug_mem_refresh();
}

static void cu_avr_debug_mem_event(auint region, auint flags, auint addr, auint value)
{
 auint i;
#ifdef ENABLE_MEMORY_TRACE
 if (memory_trace_on){
  uint32 seq = memory_trace_seq++;
  cu_avr_memtrace_event_t* e = &memory_trace_buf[seq % CU_AVR_MEMTRACE_CAP];
  e->seq = seq;
  e->abs_cycle = cpu_state.cycle;
  e->pc = (uint16)(cpu_state.pc & 0x7FFFU);
  e->addr = (uint16)(addr & 0xFFFFU);
#if defined(ENABLE_API_SERVER)
  e->row = (uint16)(video_beam_pulse & 0xFFFFU);
  e->beam_cycle = (uint16)((VIDEO_BEAM_CYCLE_NOW() > 0xFFFFU) ? 0xFFFFU : VIDEO_BEAM_CYCLE_NOW());
#else
  e->row = (uint16)(video_pulsectr & 0xFFFFU);
  e->beam_cycle = (uint16)((video_cycle > 0xFFFFU) ? 0xFFFFU : video_cycle);
#endif
  e->region = (uint8)region;
  e->flags = (uint8)flags;
  e->value = (uint8)(value & 0xFFU);
  e->reserved = 0U;
  if (memory_trace_count < CU_AVR_MEMTRACE_CAP){ memory_trace_count++; }
 }
#endif
 if ((!watchpoints_armed) || watchpoint_hit.valid){ return; }
 for (i = 0U; i < CU_AVR_WATCH_SLOTS; i++){
  cu_avr_watchpoint_t const* wp = &watchpoint_map[i];
  if (wp->enabled == 0U){ continue; }
  if (wp->region != (uint8)region){ continue; }
  if ((wp->flags & flags) == 0U){ continue; }
  if ((addr < wp->start_addr) || (addr > wp->end_addr)){ continue; }
  watchpoint_hit.valid = TRUE;
  watchpoint_hit.slot = (uint8)i;
  watchpoint_hit.region = (uint8)region;
  watchpoint_hit.flags = (uint8)flags;
  watchpoint_hit.addr = (uint16)(addr & 0xFFFFU);
  watchpoint_hit.value = (uint8)(value & 0xFFU);
  watchpoint_hit.pc = (uint16)(cpu_state.pc & 0x7FFFU);
  debug_event_break = TRUE;
  return;
 }
}
#ifdef ENABLE_AUDIO_TRACE
static void cu_avr_audio_debug_refresh(void)
{
 audio_debug_active = (audio_trace_on || (audio_break_mode_ != CU_AVR_AUDIO_BREAK_OFF)) ? TRUE : FALSE;
}

static void cu_avr_audio_debug_event(auint value)
{
 cu_avr_audio_event_t ev;
 auint dist;
 boole hit = FALSE;
 value &= 0xFFU;
 memset(&ev, 0, sizeof(ev));
 ev.seq = audio_trace_seq;
 ev.abs_cycle = cpu_state.cycle;
 ev.pc = (uint16)(cpu_state.pc & 0x7FFFU);
#if defined(ENABLE_API_SERVER)
 ev.row = (uint16)(video_beam_pulse & 0xFFFFU);
 ev.beam_cycle = (uint16)((VIDEO_BEAM_CYCLE_NOW() > 0xFFFFU) ? 0xFFFFU : VIDEO_BEAM_CYCLE_NOW());
#else
 ev.row = (uint16)(video_pulsectr & 0xFFFFU);
 ev.beam_cycle = (uint16)((video_cycle > 0xFFFFU) ? 0xFFFFU : video_cycle);
#endif
 ev.value = (uint8)value;
 if (value == 0U){ ev.flags |= CU_AVR_AUDIO_FLAG_RAIL_LOW; audio_rail_low_total_++; }
 if (value == 255U){ ev.flags |= CU_AVR_AUDIO_FLAG_RAIL_HIGH; audio_rail_high_total_++; }
 dist = (value >= 128U) ? (value - 128U) : (128U - value);
 if (dist > audio_peak_distance_){ audio_peak_distance_ = dist; }
 audio_event_total_++;
 if (audio_break_mode_ == CU_AVR_AUDIO_BREAK_RAIL){
  hit = ((value == 0U) || (value == 255U)) ? TRUE : FALSE;
 }else if (audio_break_mode_ == CU_AVR_AUDIO_BREAK_OUTSIDE){
  hit = ((value <= (auint)audio_break_low_) || (value >= (auint)audio_break_high_)) ? TRUE : FALSE;
 }
 if (hit){ ev.flags |= CU_AVR_AUDIO_FLAG_BREAK; }
 if (audio_trace_on){
  cu_avr_audio_event_t* dst = &audio_trace_buf[audio_trace_seq % CU_AVR_AUDIO_TRACE_CAP];
  ev.seq = audio_trace_seq;
  *dst = ev;
  audio_trace_seq++;
  if (audio_trace_count_ < CU_AVR_AUDIO_TRACE_CAP){ audio_trace_count_++; }
 }
 if (hit && !audio_break_hit_valid){
  audio_break_hit_event = ev;
  audio_break_hit_valid = TRUE;
  debug_event_break = TRUE;
 }
}
#endif
#endif


/*
** Emulates a single (compiled) AVR instruction and any associated hardware
** tasks.
*/
#ifdef __EMSCRIPTEN__
#ifndef FLAG_NATIVE
#include "cu_avr_e.h"
#else
#include "cu_avr_n.h"
#endif
#else
#include "cu_avr_n.h"
#endif



/*
** Auto-fuses the CPU based on ROM contents and boot priority. The bootpri
** flag requests prioritizing the bootloader when set TRUE, otherwise game is
** prioritized (used when both a game and a bootloader appears to be present
** in the ROM). This should be called before reset unless the CPU state is
** already loaded (which includes fuse bits).
*/
void  cu_avr_autofuse(boole bootpri)
{
 boole hasgame;
 boole hasboot;

 /* Default fuse settings are according to Uzebox */
 /* (For now, only the BOOTRST fuse is actually used) */

 cpu_state.fuse[0] = 0xD7U; /* CLKSEL = 0111; SUT = 01; CKOUT = 1; CKDIV = 1 */
 cpu_state.fuse[1] = 0xD2U; /* BOOTRST = 0; BOOTSZ = 01; EESAVE = 0; WDTON = 1; SPIEN = 0; JTAGEN = 1; OCDEN = 1 */
 cpu_state.fuse[2] = 0xFFU; /* BODLEVEL = 111 */

 /* Determine boot fuses according to the contents of the ROM and the boot
 ** prioritization parameter. The boot loader is used when it exists and it
 ** is prioritized or when it looks like there is no game in the Code ROM. */

 /* Both all-zero and erased (0xFFFF) reset words are treated as blank.
 ** CUzeBox historically used zero-filled ROM staging, while actual AVR flash
 ** is erased to 0xFF. Supporting both avoids falsely selecting an empty boot
 ** section when a loader path starts from erased flash. */
 hasgame = !(((cpu_state.crom[                    0U] == 0x00U) &&
              (cpu_state.crom[                    1U] == 0x00U)) ||
             ((cpu_state.crom[                    0U] == 0xFFU) &&
              (cpu_state.crom[                    1U] == 0xFFU)));
 hasboot = !(((cpu_state.crom[(VBASE_BOOT * 2U) + 0U] == 0x00U) &&
              (cpu_state.crom[(VBASE_BOOT * 2U) + 1U] == 0x00U)) ||
             ((cpu_state.crom[(VBASE_BOOT * 2U) + 0U] == 0xFFU) &&
              (cpu_state.crom[(VBASE_BOOT * 2U) + 1U] == 0xFFU)));
 if ( (!(hasboot && bootpri)) &&
      (hasgame) ){ cpu_state.fuse[1] |= 0x01U; } /* Turn off bootloader */
}



/*
** Resets the CPU as if it was power-cycled. It properly initializes
** everything from the state as if cu_avr_crom_update() and cu_avr_io_update()
** was called.
*/
void  cu_avr_reset(void)
{
 auint i;

 for (i = 0U; i < 4096U; i++){
  access_mem[i] = 0U;
 }

 for (i = 0U; i < 256U; i++){
  access_io[i] = 0U;
 }

 for (i = 0U; i < 271U; i++){
  video_frame.pulse[i].rise = CU_NOSYNC;
  video_frame.pulse[i].fall = CU_NOSYNC;
 }

 for (i = 0U; i < 4U; i++){
  audio_samples[i] = 0x80U;
 }

 cpu_state.cycle    = 0U;
 video_pulsectr     = 0U;
 video_pedge        = cpu_state.cycle;
 video_prise        = cpu_state.cycle;
 video_rowflag      = FALSE;
 video_cycle        = 0U;
#if defined(ENABLE_DEBUGGER) && defined(ENABLE_API_SERVER)
 video_beam_line_start_cycle = cpu_state.cycle;
 video_beam_pulse            = 0U;
 video_beam_prev_pulse       = 0U;
 video_beam_prev_line_cycles = 0U;
#if defined(ENABLE_BEAM_CAPTURE)
 video_beam_capture_on       = FALSE;
#endif
#endif
 audio_rp           = 0U;
 audio_wp           = 0U;
 cycle_next_event   = WRAP32(cpu_state.cycle + 1U);
 timer1_base        = cpu_state.cycle;
 event_it           = TRUE;
 event_it_enter     = FALSE;
 wd_last            = cpu_state.cycle;
 wd_last_pc         = 0U;

 wd_interval_min[0] = WD_INTERVAL_MAX;
 wd_interval_min[1] = 0U;
 wd_interval_beg[0] = 0U;
 wd_interval_beg[1] = 0U;
 wd_interval_end[0] = 0U;
 wd_interval_end[1] = 0U;

 if (!pflags_done){
  cu_avrfg_fill(&cpu_pflags[0]);
  pflags_done = TRUE;
 }

 cu_avr_crom_update(0U, 65536U);

#ifdef ENABLE_DEBUGGER
 memset(&watchpoint_hit, 0, sizeof(watchpoint_hit));
 temp_break_active = FALSE;
 temp_break_addr = 0U;
 debug_step_remaining = 0U;
 debug_event_break = FALSE;
#ifdef ENABLE_MEMORY_TRACE
 memory_trace_on = FALSE;
 memory_trace_seq = 0U;
 memory_trace_count = 0U;
#endif
#ifdef ENABLE_AUDIO_TRACE
 audio_trace_on = FALSE;
 audio_trace_seq = 0U;
 audio_trace_count_ = 0U;
 audio_break_mode_ = CU_AVR_AUDIO_BREAK_OFF;
 audio_break_low_ = 0U;
 audio_break_high_ = 255U;
 audio_break_hit_valid = FALSE;
 audio_event_total_ = 0ULL;
 audio_rail_low_total_ = 0ULL;
 audio_rail_high_total_ = 0ULL;
 audio_peak_distance_ = 0U;
 cu_avr_audio_debug_refresh();
#endif
 cu_avr_debug_mem_refresh();
 cu_debug_timing_reset();
#if defined(ENABLE_API_SERVER)
 cu_uart_debug_logic_capture_set(FALSE,FALSE);
#endif
#endif

 cu_avr_reset_wd();
 cu_ctr_reset();
 cu_spi_reset(cpu_state.cycle);
 cu_esp_reset_pin(0U, 0U);
 cpu_state.crom_mod = FALSE; /* Initial code ROM state: not modified. */
}



/*
** Run emulation. Returns according to the return values defined in cu_types
** (emulating up to about 2050 cycles).
*/
auint cu_avr_run(void)
{
 auint ret = 0U;
 auint i;

 video_rowflag = FALSE;

 while (video_cycle < VIDEO_CY_MAX){ /* Also signals proper row end */
#ifdef ENABLE_DEBUGGER
  auint curpc = cpu_state.pc & 0x7FFFU;
  if (temp_break_active && (curpc == temp_break_addr)){
   /*
   ** Temporary execute breakpoints are debugger navigation helpers only.
   ** They should stop exactly once and must not pollute the user's normal
   ** persistent breakpoint list, so they are auto-cleared on hit.
   */
   temp_break_active = FALSE;
   temp_break_addr = 0U;
   ret |= CU_BREAK;
   break;
  }
  if (cu_avr_breakpoint_get(curpc)){
   ret |= CU_BREAK;
   break;
  }
#endif
#ifdef ENABLE_DEBUGGER
  {
   auint profile_pc = cpu_state.pc & 0x7FFFU;
   auint profile_cycle = cpu_state.cycle;
   cu_avr_exec();       /* Note: This inlines as only this single call exists */
   if (profiler_enabled){
    profiler_hits[profile_pc]++;
    profiler_cycles[profile_pc] += (uint32)(WRAP32(cpu_state.cycle - profile_cycle));
   }
#ifdef ENABLE_API_SERVER
   if (cu_debug_timing_instruction_gate){
    auint timing_cycles = WRAP32(cpu_state.cycle - profile_cycle);
    auint timing_end_cycle = VIDEO_BEAM_CYCLE_NOW();
    auint timing_start_row = video_beam_pulse;
    auint timing_start_cycle;
    if (timing_cycles > timing_end_cycle){
     auint crossed = timing_cycles - timing_end_cycle;
     timing_start_row = video_beam_prev_pulse;
     timing_start_cycle = (video_beam_prev_line_cycles > crossed) ? (video_beam_prev_line_cycles - crossed) : 0U;
    }else{
     timing_start_cycle = timing_end_cycle - timing_cycles;
    }
    if (cu_debug_timing_after_instruction(timing_start_row, timing_start_cycle, video_beam_pulse, timing_end_cycle, profile_pc, timing_cycles, cpu_state.cycle)){
     ret |= CU_BREAK;
     break;
    }
   }
#endif
  }
#else
  cu_avr_exec();       /* Note: This inlines as only this single call exists */
#endif
#ifdef ENABLE_DEBUGGER
  if (debug_event_break){
   debug_event_break = FALSE;
   ret |= CU_BREAK;
   break;
  }
  if (debug_step_remaining != 0U){
   debug_step_remaining --;
   if (debug_step_remaining == 0U){
    ret |= CU_BREAK;
    break;
   }
  }
#endif
 }
#ifdef ENABLE_DEBUGGER
 /* Normal emulation only returns here after the row rollover sentinel has
 ** been reached. Debugger breaks may intentionally stop mid-scanline; in
 ** that case preserving video_cycle lets resume/step continue from the exact
 ** beam position instead of unsigned-underflowing the renderer counter. */
 if (video_cycle >= VIDEO_CY_MAX){ video_cycle -= VIDEO_CY_MAX; }
#else
 video_cycle -= VIDEO_CY_MAX; /* Next line pixels */
#endif

 if (video_rowflag){

  ret |= CU_GET_ROW;

  audio_rp = (audio_rp + 1U) & 0x3U;
  if (audio_rp == audio_wp){ audio_rp = (audio_rp - 1U) & 0x3U; }
  video_row.sample = audio_samples[audio_rp];

  video_row.pno = video_pulsectr;

  if (video_pulsectr == 270U){ /* Frame completed, can return it */

   ret |= CU_GET_FRAME;

   if (wd_interval_min[0] < WD_INTERVAL_MAX){ /* Latch WDR interval debug counter */
    wd_interval_min[1] = wd_interval_min[0];
    wd_interval_end[1] = wd_interval_end[0];
    wd_interval_beg[1] = wd_interval_beg[0];
   }
   wd_interval_min[0] = WD_INTERVAL_MAX; /* Clear for next frame */
   wd_interval_beg[0] = 0U;
   wd_interval_end[0] = 0U;

  }else if (video_pulsectr == 0U){

   for (i = 1U; i < 271U; i++){ /* Clear sync info for next frame (pulse 0 is already produced here) */
    video_frame.pulse[i].rise = CU_NOSYNC;
    video_frame.pulse[i].fall = CU_NOSYNC;
   }

  }

 }else if ((ret & CU_BREAK) == 0U){

  ret |= CU_GET_ROW;
  ret |= CU_SYNCERR;

 }

 return ret;
}



/*
** Clears all debugger breakpoints.
*/
void cu_avr_breakpoints_clear(void)
{
#ifdef ENABLE_DEBUGGER
 memset(&breakpoint_map[0], 0, sizeof(breakpoint_map));
#endif
}


/*
** Sets or clears a debugger breakpoint at the specified program word address.
*/
void cu_avr_breakpoint_set(auint word_addr, boole enable)
{
#ifdef ENABLE_DEBUGGER
 auint idx = word_addr & 0x7FFFU;
 auint bit = 1U << (idx & 0x7U);
 if (enable){
  breakpoint_map[idx >> 3] |= bit;
 }else{
  breakpoint_map[idx >> 3] &= (auint)(~bit) & 0xFFU;
 }
#else
 (void)word_addr;
 (void)enable;
#endif
}


/*
** Returns whether a debugger breakpoint is set at the specified program word
** address.
*/
boole cu_avr_breakpoint_get(auint word_addr)
{
#ifdef ENABLE_DEBUGGER
 auint idx = word_addr & 0x7FFFU;
 return ((breakpoint_map[idx >> 3] & (1U << (idx & 0x7U))) != 0U);
#else
 (void)word_addr;
 return FALSE;
#endif
}


/*
** Finds the next debugger breakpoint at or after the specified program word
** address.
*/
boole cu_avr_breakpoint_next(auint start_word_addr, auint* out_word_addr)
{
#ifdef ENABLE_DEBUGGER
 auint idx;
 for (idx = (start_word_addr & 0x7FFFU); idx < 32768U; idx++){
  if (cu_avr_breakpoint_get(idx)){
   if (out_word_addr != NULL){ *out_word_addr = idx; }
   return TRUE;
  }
 }
#else
 (void)start_word_addr;
 (void)out_word_addr;
#endif
 return FALSE;
}


void cu_avr_temp_break_set(auint word_addr, boole enable)
{
#ifdef ENABLE_DEBUGGER
 temp_break_active = enable;
 temp_break_addr = (uint16)(word_addr & 0x7FFFU);
#else
 (void)word_addr;
 (void)enable;
#endif
}

void cu_avr_temp_break_clear(void)
{
#ifdef ENABLE_DEBUGGER
 temp_break_active = FALSE;
 temp_break_addr = 0U;
#endif
}

boole cu_avr_temp_break_get(auint* out_word_addr)
{
#ifdef ENABLE_DEBUGGER
 if (out_word_addr != NULL){ *out_word_addr = temp_break_addr; }
 return temp_break_active;
#else
 if (out_word_addr != NULL){ *out_word_addr = 0U; }
 return FALSE;
#endif
}

void cu_avr_debug_step_instructions(auint count)
{
#ifdef ENABLE_DEBUGGER
 debug_step_remaining = count;
#else
 (void)count;
#endif
}

void cu_avr_debug_step_clear(void)
{
#ifdef ENABLE_DEBUGGER
 debug_step_remaining = 0U;
#endif
}

boole cu_avr_debug_step_active(void)
{
#ifdef ENABLE_DEBUGGER
 return (debug_step_remaining != 0U);
#else
 return FALSE;
#endif
}

#ifdef ENABLE_DEBUGGER
void cu_avr_debug_request_break(void)
{
 debug_event_break = TRUE;
}
#endif

void cu_avr_audio_trace_enable(boole enable)
{
#if defined(ENABLE_DEBUGGER) && defined(ENABLE_AUDIO_TRACE)
 audio_trace_on = enable ? TRUE : FALSE;
 cu_avr_audio_debug_refresh();
#else
 (void)enable;
#endif
}

boole cu_avr_audio_trace_built(void)
{
#if defined(ENABLE_DEBUGGER) && defined(ENABLE_AUDIO_TRACE)
 return TRUE;
#else
 return FALSE;
#endif
}

boole cu_avr_audio_trace_enabled(void)
{
#if defined(ENABLE_DEBUGGER) && defined(ENABLE_AUDIO_TRACE)
 return audio_trace_on;
#else
 return FALSE;
#endif
}

void cu_avr_audio_trace_clear(void)
{
#if defined(ENABLE_DEBUGGER) && defined(ENABLE_AUDIO_TRACE)
 audio_trace_seq = 0U;
 audio_trace_count_ = 0U;
 audio_event_total_ = 0ULL;
 audio_rail_low_total_ = 0ULL;
 audio_rail_high_total_ = 0ULL;
 audio_peak_distance_ = 0U;
 audio_break_hit_valid = FALSE;
#endif
}

auint cu_avr_audio_trace_count(void)
{
#if defined(ENABLE_DEBUGGER) && defined(ENABLE_AUDIO_TRACE)
 return audio_trace_count_;
#else
 return 0U;
#endif
}

uint32 cu_avr_audio_trace_first_seq(void)
{
#if defined(ENABLE_DEBUGGER) && defined(ENABLE_AUDIO_TRACE)
 return audio_trace_seq - (uint32)audio_trace_count_;
#else
 return 0U;
#endif
}

boole cu_avr_audio_trace_get(uint32 seq, cu_avr_audio_event_t* out)
{
#if defined(ENABLE_DEBUGGER) && defined(ENABLE_AUDIO_TRACE)
 uint32 first = cu_avr_audio_trace_first_seq();
 if ((seq < first) || (seq >= audio_trace_seq)){ return FALSE; }
 if (out != NULL){ *out = audio_trace_buf[seq % CU_AVR_AUDIO_TRACE_CAP]; }
 return TRUE;
#else
 (void)seq; (void)out; return FALSE;
#endif
}

void cu_avr_audio_break_set(auint mode, auint low, auint high)
{
#if defined(ENABLE_DEBUGGER) && defined(ENABLE_AUDIO_TRACE)
 if (mode > CU_AVR_AUDIO_BREAK_OUTSIDE){ mode = CU_AVR_AUDIO_BREAK_OFF; }
 if (low > 255U){ low = 255U; }
 if (high > 255U){ high = 255U; }
 if (low > high){ auint t = low; low = high; high = t; }
 audio_break_mode_ = mode;
 audio_break_low_ = (uint8)low;
 audio_break_high_ = (uint8)high;
 audio_break_hit_valid = FALSE;
 cu_avr_audio_debug_refresh();
#else
 (void)mode; (void)low; (void)high;
#endif
}

auint cu_avr_audio_break_mode(void)
{
#if defined(ENABLE_DEBUGGER) && defined(ENABLE_AUDIO_TRACE)
 return audio_break_mode_;
#else
 return CU_AVR_AUDIO_BREAK_OFF;
#endif
}

auint cu_avr_audio_break_low(void)
{
#if defined(ENABLE_DEBUGGER) && defined(ENABLE_AUDIO_TRACE)
 return audio_break_low_;
#else
 return 0U;
#endif
}

auint cu_avr_audio_break_high(void)
{
#if defined(ENABLE_DEBUGGER) && defined(ENABLE_AUDIO_TRACE)
 return audio_break_high_;
#else
 return 255U;
#endif
}

boole cu_avr_audio_break_hit(boole clear, cu_avr_audio_event_t* out)
{
#if defined(ENABLE_DEBUGGER) && defined(ENABLE_AUDIO_TRACE)
 boole ret = audio_break_hit_valid;
 if (ret && out != NULL){ *out = audio_break_hit_event; }
 if (clear){ audio_break_hit_valid = FALSE; }
 return ret;
#else
 (void)clear; (void)out; return FALSE;
#endif
}

uint64_t cu_avr_audio_event_total(void)
{
#if defined(ENABLE_DEBUGGER) && defined(ENABLE_AUDIO_TRACE)
 return audio_event_total_;
#else
 return 0ULL;
#endif
}
uint64_t cu_avr_audio_rail_low_total(void)
{
#if defined(ENABLE_DEBUGGER) && defined(ENABLE_AUDIO_TRACE)
 return audio_rail_low_total_;
#else
 return 0ULL;
#endif
}
uint64_t cu_avr_audio_rail_high_total(void)
{
#if defined(ENABLE_DEBUGGER) && defined(ENABLE_AUDIO_TRACE)
 return audio_rail_high_total_;
#else
 return 0ULL;
#endif
}
auint cu_avr_audio_peak_distance(void)
{
#if defined(ENABLE_DEBUGGER) && defined(ENABLE_AUDIO_TRACE)
 return audio_peak_distance_;
#else
 return 0U;
#endif
}

void cu_avr_memory_trace_enable(boole enable)
{
#if defined(ENABLE_DEBUGGER) && defined(ENABLE_MEMORY_TRACE)
 memory_trace_on = enable ? TRUE : FALSE;
 cu_avr_debug_mem_refresh();
#else
 (void)enable;
#endif
}

boole cu_avr_memory_trace_built(void)
{
#if defined(ENABLE_DEBUGGER) && defined(ENABLE_MEMORY_TRACE)
 return TRUE;
#else
 return FALSE;
#endif
}

boole cu_avr_memory_trace_enabled(void)
{
#if defined(ENABLE_DEBUGGER) && defined(ENABLE_MEMORY_TRACE)
 return memory_trace_on;
#else
 return FALSE;
#endif
}

void cu_avr_memory_trace_clear(void)
{
#if defined(ENABLE_DEBUGGER) && defined(ENABLE_MEMORY_TRACE)
 memory_trace_seq = 0U;
 memory_trace_count = 0U;
#endif
}

auint cu_avr_memory_trace_count(void)
{
#if defined(ENABLE_DEBUGGER) && defined(ENABLE_MEMORY_TRACE)
 return memory_trace_count;
#else
 return 0U;
#endif
}

uint32 cu_avr_memory_trace_first_seq(void)
{
#if defined(ENABLE_DEBUGGER) && defined(ENABLE_MEMORY_TRACE)
 return memory_trace_seq - (uint32)memory_trace_count;
#else
 return 0U;
#endif
}

boole cu_avr_memory_trace_get(uint32 seq, cu_avr_memtrace_event_t* out)
{
#if defined(ENABLE_DEBUGGER) && defined(ENABLE_MEMORY_TRACE)
 uint32 first = cu_avr_memory_trace_first_seq();
 if ((seq < first) || (seq >= memory_trace_seq)){ return FALSE; }
 if (out != NULL){ *out = memory_trace_buf[seq % CU_AVR_MEMTRACE_CAP]; }
 return TRUE;
#else
 (void)seq; (void)out;
 return FALSE;
#endif
}

void cu_avr_watchpoints_clear(void)
{
#ifdef ENABLE_DEBUGGER
 memset(&watchpoint_map[0], 0, sizeof(watchpoint_map));
 cu_avr_watchpoints_refresh();
 memset(&watchpoint_hit, 0, sizeof(watchpoint_hit));
#endif
}

void cu_avr_watchpoint_set(auint slot, boole enable, auint region, auint flags, auint start_addr, auint end_addr)
{
#ifdef ENABLE_DEBUGGER
 cu_avr_watchpoint_t* wp;
 if (slot >= CU_AVR_WATCH_SLOTS){ return; }
 wp = &watchpoint_map[slot];
 wp->enabled = enable ? 1U : 0U;
 wp->region = (uint8)(region & 0xFFU);
 wp->flags = (uint8)(flags & (CU_AVR_WATCH_READ | CU_AVR_WATCH_WRITE));
 wp->start_addr = (uint16)(start_addr & 0xFFFFU);
 wp->end_addr = (uint16)(end_addr & 0xFFFFU);
 if (wp->end_addr < wp->start_addr){
  uint16 tmp = wp->start_addr;
  wp->start_addr = wp->end_addr;
  wp->end_addr = tmp;
 }
 cu_avr_watchpoints_refresh();
#else
 (void)slot; (void)enable; (void)region; (void)flags; (void)start_addr; (void)end_addr;
#endif
}

boole cu_avr_watchpoint_get(auint slot, boole* out_enable, auint* out_region, auint* out_flags, auint* out_start_addr, auint* out_end_addr)
{
#ifdef ENABLE_DEBUGGER
 cu_avr_watchpoint_t const* wp;
 if (slot >= CU_AVR_WATCH_SLOTS){ return FALSE; }
 wp = &watchpoint_map[slot];
 if (out_enable != NULL){ *out_enable = (wp->enabled != 0U); }
 if (out_region != NULL){ *out_region = wp->region; }
 if (out_flags != NULL){ *out_flags = wp->flags; }
 if (out_start_addr != NULL){ *out_start_addr = wp->start_addr; }
 if (out_end_addr != NULL){ *out_end_addr = wp->end_addr; }
 return (wp->enabled != 0U);
#else
 (void)slot;
 if (out_enable != NULL){ *out_enable = FALSE; }
 if (out_region != NULL){ *out_region = 0U; }
 if (out_flags != NULL){ *out_flags = 0U; }
 if (out_start_addr != NULL){ *out_start_addr = 0U; }
 if (out_end_addr != NULL){ *out_end_addr = 0U; }
 return FALSE;
#endif
}

boole cu_avr_watchpoint_next(auint start_slot, auint* out_slot)
{
#ifdef ENABLE_DEBUGGER
 auint idx;
 for (idx = start_slot; idx < CU_AVR_WATCH_SLOTS; idx++){
  if (watchpoint_map[idx].enabled != 0U){
   if (out_slot != NULL){ *out_slot = idx; }
   return TRUE;
  }
 }
#else
 (void)start_slot;
#endif
 if (out_slot != NULL){ *out_slot = 0U; }
 return FALSE;
}

boole cu_avr_watchpoint_get_last(boole clear, auint* out_slot, auint* out_region, auint* out_flags, auint* out_addr, auint* out_value, auint* out_pc)
{
#ifdef ENABLE_DEBUGGER
 boole ret = watchpoint_hit.valid;
 if (out_slot != NULL){ *out_slot = watchpoint_hit.slot; }
 if (out_region != NULL){ *out_region = watchpoint_hit.region; }
 if (out_flags != NULL){ *out_flags = watchpoint_hit.flags; }
 if (out_addr != NULL){ *out_addr = watchpoint_hit.addr; }
 if (out_value != NULL){ *out_value = watchpoint_hit.value; }
 if (out_pc != NULL){ *out_pc = watchpoint_hit.pc; }
 if (clear){ memset(&watchpoint_hit, 0, sizeof(watchpoint_hit)); }
 return ret;
#else
 (void)clear;
 if (out_slot != NULL){ *out_slot = 0U; }
 if (out_region != NULL){ *out_region = 0U; }
 if (out_flags != NULL){ *out_flags = 0U; }
 if (out_addr != NULL){ *out_addr = 0U; }
 if (out_value != NULL){ *out_value = 0U; }
 if (out_pc != NULL){ *out_pc = 0U; }
 return FALSE;
#endif
}


/*
** Returns emulator's cycle counter. It may be used to time emulation when it
** doesn't generate proper video signal. This is the cycle member of the CPU
** state (32 bits wrapping).
*/
auint cu_avr_getcycle(void)
{
 return cpu_state.cycle;
}

auint cu_avr_get_video_cycle(void)
{
 return video_cycle;
}

auint cu_avr_get_video_pulse(void)
{
 return video_pulsectr;
}

auint cu_avr_get_video_beam_cycle(void)
{
#if defined(ENABLE_DEBUGGER) && defined(ENABLE_API_SERVER)
 return VIDEO_BEAM_CYCLE_NOW();
#else
 return video_cycle;
#endif
}

auint cu_avr_get_video_beam_pulse(void)
{
#if defined(ENABLE_DEBUGGER) && defined(ENABLE_API_SERVER)
 return video_beam_pulse;
#else
 return video_pulsectr;
#endif
}

uint8 const* cu_avr_get_video_beam_pixels(void)
{
#if defined(ENABLE_DEBUGGER) && defined(ENABLE_API_SERVER) && defined(ENABLE_BEAM_CAPTURE)
 return &video_beam_pixels[0];
#else
 return &(video_row.pixels[0]);
#endif
}

void cu_avr_video_beam_capture_enable(boole enable)
{
#if defined(ENABLE_DEBUGGER) && defined(ENABLE_API_SERVER) && defined(ENABLE_BEAM_CAPTURE)
 video_beam_capture_on = enable ? TRUE : FALSE;
 if (video_beam_capture_on){
  memset(video_beam_pixels, 0, sizeof(video_beam_pixels));
  video_beam_pulse = video_pulsectr;
 }
#else
 (void)enable;
#endif
}

boole cu_avr_video_beam_capture_built(void)
{
#if defined(ENABLE_DEBUGGER) && defined(ENABLE_API_SERVER) && defined(ENABLE_BEAM_CAPTURE)
 return TRUE;
#else
 return FALSE;
#endif
}

boole cu_avr_video_beam_capture_enabled(void)
{
#if defined(ENABLE_DEBUGGER) && defined(ENABLE_API_SERVER) && defined(ENABLE_BEAM_CAPTURE)
 return video_beam_capture_on;
#else
 return FALSE;
#endif
}

#ifdef ENABLE_DEBUGGER
void cu_avr_profiler_enable(boole enable)
{
 profiler_enabled = enable;
}

boole cu_avr_profiler_enabled(void)
{
 return profiler_enabled;
}

void cu_avr_profiler_reset(void)
{
 memset(profiler_hits, 0, sizeof(profiler_hits));
 memset(profiler_cycles, 0, sizeof(profiler_cycles));
}

boole cu_avr_profiler_get(auint word_addr, uint32* out_hits, uint64_t* out_cycles)
{
 if (word_addr >= 32768U){ return FALSE; }
 if (out_hits != NULL){ *out_hits = profiler_hits[word_addr]; }
 if (out_cycles != NULL){ *out_cycles = profiler_cycles[word_addr]; }
 return TRUE;
}
#endif



/*
** Return current row. Note that continuing emulation will modify the returned
** structure's contents.
*/
cu_row_t const* cu_avr_get_row(void)
{
 return &video_row;
}



/*
** Return frame info. Note that continuing emulation will modify the returned
** structure's contents.
*/
cu_frameinfo_t const* cu_avr_get_frameinfo(void)
{
 return &video_frame;
}


/*
** Returns memory access info block. It can be written (with zeros) to clear
** flags which are only set by the emulator. Note that the highest 256 bytes
** of the RAM come first here! (so address 0x0100 corresponds to AVR address
** 0x0100)
*/
uint8* cu_avr_get_meminfo(void)
{
 return &access_mem[0];
}


/*
** Returns I/O register access info block. It can be written (with zeros) to
** clear flags which are only set by the emulator.
*/
uint8* cu_avr_get_ioinfo(void)
{
 return &access_io[0];
}


/*
** Returns whether the EEPROM changed since the last clear of this indicator.
** Calling cu_avr_io_update() clears this indicator (as well as resetting by
** cu_avr_reset()). Passing TRUE also clears it. This can be used to save
** EEPROM state to persistent storage when it changes.
*/
boole cu_avr_eeprom_ischanged(boole clear)
{
 boole ret = eeprom_changed;
 if (clear){ eeprom_changed = FALSE; }
 return ret;
}


/*
** Returns whether the Code ROM changed since the last clear of this
** indicator. Calling cu_avr_io_update() clears this indicator (as well as
** resetting by cu_avr_reset()). Passing TRUE also clears it. This can be used
** to save Code ROM state to persistent storage when it changes.
*/
boole cu_avr_flash_active(boole clear)
{
 boole active = flash_activity || cpu_state.spm_prge;
 if (clear){ flash_activity = FALSE; }
 return active;
}

boole cu_avr_crom_ischanged(boole clear)
{
 boole ret = crom_changed;
 if (clear){ crom_changed = FALSE; }
 return ret;
}



/*
** Returns whether the Code ROM was modified since reset. This can be used to
** determine if it is necessary to include the Code ROM in a save state.
** Internal Code ROM writes and the cu_avr_crom_update() function can set it.
*/
boole cu_avr_crom_ismod(void)
{
 return cpu_state.crom_mod;
}



/*
** Returns AVR CPU state structure. It may be written, the Code ROM must be
** recompiled (by cu_avr_crom_update()) if anything in that area was updated
** or freshly written.
*/
cu_state_cpu_t* cu_avr_get_state(void)
{
 auint t0 = WRAP32(cpu_state.cycle - timer1_base); /* Current TCNT1 value */

 cpu_state.iors[CU_IO_TCNT1H] = (t0 >> 8) & 0xFFU;
 cpu_state.iors[CU_IO_TCNT1L] = (t0     ) & 0xFFU;

 return &cpu_state;
}



/*
** Updates a section of the Code ROM. This must be called after writing into
** the Code ROM so the emulator recompiles the affected instructions. The
** "base" and "len" parameters specify the range to update in bytes.
*/
void  cu_avr_crom_update(auint base, auint len)
{
 auint wbase = base >> 1;
 auint wlen  = (len + (base & 1U) + 1U) >> 1;
 auint i;

 if (wbase > 0x7FFFU){ wbase = 0x7FFFU; }
 if ((wbase + wlen) > 0x8000U){ wlen = 0x8000U - wbase; }

 for (i = wbase; i < (wbase + wlen); i++){
  cpu_code[i] = cu_avrc_compile(
      ((auint)(cpu_state.crom[((i << 1) + 0U) & 0xFFFFU])     ) |
      ((auint)(cpu_state.crom[((i << 1) + 1U) & 0xFFFFU]) << 8),
      ((auint)(cpu_state.crom[((i << 1) + 2U) & 0xFFFFU])     ) |
      ((auint)(cpu_state.crom[((i << 1) + 3U) & 0xFFFFU]) << 8) );
 }

 cpu_state.crom_mod = TRUE;
 crom_changed = TRUE;
}



/*
** Updates the I/O area. If any change is performed in the I/O register
** contents (iors, 0x20 - 0xFF), this have to be called to update internal
** emulator state over it. It also updates state related to additional
** variables in the structure (such as the watchdog timer).
*/
void  cu_avr_io_update(void)
{
 auint t0;

 eeprom_changed = FALSE;
 crom_changed   = FALSE;
 flash_activity = FALSE;

 t0    = (cpu_state.iors[CU_IO_TCNT1H] << 8) |
         (cpu_state.iors[CU_IO_TCNT1L]     );
 timer1_base = WRAP32(cpu_state.cycle - t0);

 cycle_next_event = WRAP32(cpu_state.cycle + 1U); /* Request HW processing */
 event_it         = TRUE; /* Request interrupt processing */
}



/*
** Returns last measured interval between WDR calls. Returns begin and end
** (word) addresses of WDR instructions into beg and end.
*/
auint cu_avr_get_lastwdrinterval(auint* beg, auint* end)
{
 *beg = wd_interval_beg[1];
 *end = wd_interval_end[1];
 return wd_interval_min[1];
}
