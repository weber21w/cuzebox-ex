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



#ifndef CU_AVR_H
#define CU_AVR_H



#include "cu_types.h"


/*
** Auto-fuses the CPU based on ROM contents and boot priority. The bootpri
** flag requests prioritizing the bootloader when set TRUE, otherwise game is
** prioritized (used when both a game and a bootloader appears to be present
** in the ROM). This should be called before reset unless the CPU state is
** already loaded (which includes fuse bits).
*/
void  cu_avr_autofuse(boole bootpri);


/*
** Resets the CPU as if it was power-cycled. It properly initializes
** everything from the state as if cu_avr_crom_update() and cu_avr_io_update()
** was called.
*/
void  cu_avr_reset(void);


/*
** Run emulation. Returns according to the return values defined in cu_types
** (emulating up to about 2050 cycles).
*/
auint cu_avr_run(void);


/*
** Returns emulator's cycle counter. It may be used to time emulation when it
** doesn't generate proper video signal. This is the cycle member of the CPU
** state (32 bits wrapping).
*/
auint cu_avr_getcycle(void);

/* Current cycle offset within the scanline capture buffer. On a correctly
** timed Uzebox line this advances from 0 through 1819. Values beyond 1819
** indicate that the generated line is late; the emulator guard remains 1920. */
auint cu_avr_get_video_cycle(void);

/* Current composite-sync pulse counter used by the video timing core. */
auint cu_avr_get_video_pulse(void);

/* Debug/API beam state. Live row/cycle position remains available without
** PORTC capture. When capture is armed, samples are indexed from an absolute
** line-start timestamp updated at row-producing sync falling edges. */
auint        cu_avr_get_video_beam_cycle(void);
auint        cu_avr_get_video_beam_pulse(void);
uint8 const* cu_avr_get_video_beam_pixels(void);
void         cu_avr_video_beam_capture_enable(boole enable);
boole        cu_avr_video_beam_capture_built(void);
boole        cu_avr_video_beam_capture_enabled(void);

#ifdef ENABLE_DEBUGGER
/*
** Lightweight per-PC execution profiler. Counts instruction executions and
** AVR cycles consumed at each word address. This is meant for developer tools
** and the automation API, not normal-speed release builds.
*/
void  cu_avr_profiler_enable(boole enable);
boole cu_avr_profiler_enabled(void);
void  cu_avr_profiler_reset(void);
boole cu_avr_profiler_get(auint word_addr, uint32* out_hits, uint64_t* out_cycles);
#endif



/*
** Clears all debugger breakpoints.
*/
void  cu_avr_breakpoints_clear(void);


/*
** Sets or clears a debugger breakpoint at the specified program word address.
*/
void  cu_avr_breakpoint_set(auint word_addr, boole enable);


/*
** Returns whether a debugger breakpoint is set at the specified program word
** address.
*/
boole cu_avr_breakpoint_get(auint word_addr);


/*
** Finds the next debugger breakpoint at or after the specified program word
** address. Returns TRUE if found and stores the address into out_word_addr.
*/
boole cu_avr_breakpoint_next(auint start_word_addr, auint* out_word_addr);

/*
** Sets or clears a temporary execute breakpoint used by debugger navigation
** helpers such as run-to-cursor / step-over / step-out. This breakpoint is
** not part of the normal user breakpoint map and is cleared automatically
** when it fires.
*/
void  cu_avr_temp_break_set(auint word_addr, boole enable);
void  cu_avr_temp_break_clear(void);
boole cu_avr_temp_break_get(auint* out_word_addr);

/*
** Arms a debugger single-instruction step countdown. When nonzero, the AVR
** core stops with CU_BREAK after the requested number of instructions have
** executed. This is separate from frame stepping.
*/
void  cu_avr_debug_step_instructions(auint count);
void  cu_avr_debug_step_clear(void);
boole cu_avr_debug_step_active(void);

/* Peripheral/debugger helpers can request a one-shot stop at the end of the
** current AVR instruction. The request shares the existing post-instruction
** debugger stop gate rather than adding another per-instruction branch. */
#ifdef ENABLE_DEBUGGER
void  cu_avr_debug_request_break(void);
#endif

#define CU_AVR_WATCH_REGION_SRAM  0U
#define CU_AVR_WATCH_REGION_IO    1U
#define CU_AVR_WATCH_READ         0x01U
#define CU_AVR_WATCH_WRITE        0x02U
#define CU_AVR_WATCH_SLOTS        8U

/* Historical SRAM/I/O access trace. The storage and hot-path trace writer are
** compiled only when ENABLE_MEMORY_TRACE is enabled. Public functions remain
** available as no-op/status stubs so API code can report the feature cleanly. */
#define CU_AVR_MEMTRACE_CAP       4096U
typedef struct{
 uint32 seq;
 uint32 abs_cycle;
 uint16 pc;
 uint16 addr;
 uint16 row;
 uint16 beam_cycle;
 uint8 region;
 uint8 flags;
 uint8 value;
 uint8 reserved;
} cu_avr_memtrace_event_t;

void  cu_avr_memory_trace_enable(boole enable);
boole cu_avr_memory_trace_built(void);
boole cu_avr_memory_trace_enabled(void);
void  cu_avr_memory_trace_clear(void);
auint cu_avr_memory_trace_count(void);
uint32 cu_avr_memory_trace_first_seq(void);
boole cu_avr_memory_trace_get(uint32 seq, cu_avr_memtrace_event_t* out);

void  cu_avr_watchpoints_clear(void);
void  cu_avr_watchpoint_set(auint slot, boole enable, auint region, auint flags, auint start_addr, auint end_addr);
boole cu_avr_watchpoint_get(auint slot, boole* out_enable, auint* out_region, auint* out_flags, auint* out_start_addr, auint* out_end_addr);
boole cu_avr_watchpoint_next(auint start_slot, auint* out_slot);
boole cu_avr_watchpoint_get_last(boole clear, auint* out_slot, auint* out_region, auint* out_flags, auint* out_addr, auint* out_value, auint* out_pc);

/* Cycle-correlated native Uzebox DAC (OCR2A) tracing. This observes the final
** 8-bit sample written by the emulated kernel mixer; it does not pretend to
** expose individual logical mixer channels. Storage and the OCR2A hot-path
** gate are compiled only with ENABLE_AUDIO_TRACE. */
#define CU_AVR_AUDIO_TRACE_CAP       4096U
#define CU_AVR_AUDIO_BREAK_OFF       0U
#define CU_AVR_AUDIO_BREAK_RAIL      1U
#define CU_AVR_AUDIO_BREAK_OUTSIDE   2U
#define CU_AVR_AUDIO_FLAG_RAIL_LOW   0x01U
#define CU_AVR_AUDIO_FLAG_RAIL_HIGH  0x02U
#define CU_AVR_AUDIO_FLAG_BREAK      0x04U

typedef struct{
 uint32 seq;
 uint32 abs_cycle;
 uint16 pc;
 uint16 row;
 uint16 beam_cycle;
 uint8 value;
 uint8 flags;
 uint16 reserved;
} cu_avr_audio_event_t;

void   cu_avr_audio_trace_enable(boole enable);
boole  cu_avr_audio_trace_built(void);
boole  cu_avr_audio_trace_enabled(void);
void   cu_avr_audio_trace_clear(void);
auint  cu_avr_audio_trace_count(void);
uint32 cu_avr_audio_trace_first_seq(void);
boole  cu_avr_audio_trace_get(uint32 seq, cu_avr_audio_event_t* out);
void   cu_avr_audio_break_set(auint mode, auint low, auint high);
auint  cu_avr_audio_break_mode(void);
auint  cu_avr_audio_break_low(void);
auint  cu_avr_audio_break_high(void);
boole  cu_avr_audio_break_hit(boole clear, cu_avr_audio_event_t* out);
uint64_t cu_avr_audio_event_total(void);
uint64_t cu_avr_audio_rail_low_total(void);
uint64_t cu_avr_audio_rail_high_total(void);
auint  cu_avr_audio_peak_distance(void);

/*
** Return current row. Note that continuing emulation will modify the returned
** structure's contents.
*/
cu_row_t const* cu_avr_get_row(void);


/*
** Return frame info. Note that continuing emulation will modify the returned
** structure's contents.
*/
cu_frameinfo_t const* cu_avr_get_frameinfo(void);


/*
** Returns memory access info block. It can be written (with zeros) to clear
** flags which are only set by the emulator. Note that the highest 256 bytes
** of the RAM come first here! (so address 0x0100 corresponds to AVR address
** 0x0100)
*/
uint8* cu_avr_get_meminfo(void);


/*
** Returns I/O register access info block. It can be written (with zeros) to
** clear flags which are only set by the emulator. It doesn't reflect implicit
** accesses, only those explicitly performed by read or write operations.
*/
uint8* cu_avr_get_ioinfo(void);


/*
** Returns whether the EEPROM changed since the last clear of this indicator.
** Calling cu_avr_io_update() clears this indicator (as well as resetting by
** cu_avr_reset()). Passing TRUE also clears it. This can be used to save
** EEPROM state to persistent storage when it changes.
*/
boole cu_avr_eeprom_ischanged(boole clear);


/*
** Returns whether the Code ROM changed since the last clear of this
** indicator. Calling cu_avr_io_update() clears this indicator (as well as
** resetting by cu_avr_reset()). Passing TRUE also clears it. This can be used
** to save Code ROM state to persistent storage when it changes.
*/
boole cu_avr_crom_ischanged(boole clear);

/* Actual SPM page erase/write activity since the last clear, or an ongoing
** programming operation. ROM loading and debugger patches do not set this.
** Reset and cu_avr_io_update() clear the history. Not part of save states. */
boole cu_avr_flash_active(boole clear);


/*
** Returns whether the Code ROM was modified since reset. This can be used to
** determine if it is necessary to include the Code ROM in a save state.
** Internal Code ROM writes and the cu_avr_crom_update() function can set it.
*/
boole cu_avr_crom_ismod(void);


/*
** Returns AVR CPU state structure. It may be written, the Code ROM must be
** recompiled (by cu_avr_crom_update()) if anything in that area was updated
** or freshly written, and the IO space needs to be updated (by
** cu_avr_io_update()) if anything in that area was modified.
*/
cu_state_cpu_t* cu_avr_get_state(void);


/*
** Updates a section of the Code ROM. This must be called after writing into
** the Code ROM so the emulator recompiles the affected instructions. The
** "base" and "len" parameters specify the range to update in bytes.
*/
void  cu_avr_crom_update(auint base, auint len);


/*
** Updates the I/O area. If any change is performed in the I/O register
** contents (iors, 0x20 - 0xFF), this have to be called to update internal
** emulator state over it. It also updates state related to additional
** variables in the structure (such as the watchdog timer).
*/
void  cu_avr_io_update(void);


/*
** Returns last measured interval between WDR calls. Returns begin and end
** (word) addresses of WDR instructions into beg and end.
*/
auint cu_avr_get_lastwdrinterval(auint* beg, auint* end);


#endif
