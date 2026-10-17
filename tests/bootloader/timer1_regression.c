#define SDL_MAIN_HANDLED
#undef main
#include "../../cu_vfat.h"
#define main timer1_emulator_main
#include "../../main.c"
#undef main
#include "../../cu_avr.c"
#include <time.h>

#define CHECK(c, msg) do { if (!(c)) { fprintf(stderr, "FAIL: %s (line %d)\n", msg, __LINE__); return 1; } } while (0)

static void setup(auint cycle, auint count, auint a, auint b, boole ctc)
{
 memset(&cpu_state, 0, sizeof(cpu_state));
 cpu_state.cycle = cycle;
 timer1_base = WRAP32(cycle - count);
 cpu_state.iors[CU_IO_TCCR1B] = ctc ? 9U : 1U;
 cpu_state.iors[CU_IO_OCR1AL] = a & 255U;
 cpu_state.iors[CU_IO_OCR1AH] = a >> 8;
 cpu_state.iors[CU_IO_OCR1BL] = b & 255U;
 cpu_state.iors[CU_IO_OCR1BH] = b >> 8;
timer1_compare_blocked = FALSE;
 event_it_enter = FALSE;
 cu_avr_hwexec();
}
static void next(void){ cpu_state.cycle = cycle_next_event; cu_avr_hwexec(); }
static void write_counter(auint count)
{
 cu_avr_write_io(CU_IO_TCNT1H, count >> 8);
 cu_avr_write_io(CU_IO_TCNT1L, count & 255U);
}

/* Identical event-scheduler work in both binaries; no sleeps, audio or GUI. */
static void benchmark(boole ctc)
{
 unsigned long n;
 clock_t begin;
 uint32 checksum = 0U;
 setup(1U, 1U, 1819U, 909U, ctc);
 begin = clock();
 for (n = 0; n < 4000000UL; ++n){
  next();
  checksum += cpu_state.iors[CU_IO_TIFR1];
  cpu_state.iors[CU_IO_TIFR1] = 0U;
 }
 printf("%s scheduler: %.6f s, checksum %u\n", ctc ? "CTC" : "normal", (double)(clock() - begin) / CLOCKS_PER_SEC, (unsigned)checksum);
}

int main(int argc, char** argv)
{
 (void)argv;
 if (argc > 1){ benchmark(TRUE); benchmark(FALSE); return 0; }
 savestate_blob_t state = {0};
 auint cycle;
 setup(1U, 1U, 9U, 19U, FALSE);
 CHECK(cycle_next_event == 10U && !(cpu_state.iors[CU_IO_TIFR1] & 7U), "A scheduled at compare+1 clock");
 next();
 CHECK(cpu_state.cycle == 10U && (cpu_state.iors[CU_IO_TIFR1] & 7U) == 2U, "normal A flag with interrupts masked");
 CHECK(timer1_base == 0U && cycle_next_event == 20U, "A does not reset normal counter");
 cu_avr_write_io(CU_IO_TIFR1, 2U);
 CHECK(!(cpu_state.iors[CU_IO_TIFR1] & 2U), "write-one-to-clear flag");
 next();
 CHECK((cpu_state.iors[CU_IO_TIFR1] & 7U) == 4U && cycle_next_event == 65536U, "normal B then overflow");
 next();
 CHECK((cpu_state.iors[CU_IO_TIFR1] & 7U) == 5U && cycle_next_event == 65546U, "overflow preserves B and schedules next lap A");

 setup(65000U, 65000U, 99U, 199U, FALSE);
 CHECK(cycle_next_event == 65536U, "compare behind count waits for wrap");
 next(); CHECK(cycle_next_event == 65636U, "wrapped compare scheduled");
 next(); CHECK((cpu_state.iors[CU_IO_TIFR1] & 3U) == 3U, "wrapped A fires without clearing overflow");
 setup(1U, 1U, 65535U, 65535U, FALSE); next();
 CHECK((cpu_state.iors[CU_IO_TIFR1] & 7U) == 7U && cycle_next_event == 131072U, "A B and overflow together, no zero-length event");
 cpu_state.iors[CU_IO_SREG] = SREG_IM;
 cu_avr_write_io(CU_IO_TIMSK1, 7U); cu_avr_itcheck();
 CHECK(event_it_enter && event_it_vect == VECT_T1COMPA && (cpu_state.iors[CU_IO_TIFR1] & 7U) == 5U, "unmask latched A and honor interrupt priority");
 event_it_enter = FALSE; cu_avr_itcheck();
 CHECK(event_it_vect == VECT_T1COMPB && (cpu_state.iors[CU_IO_TIFR1] & 7U) == 1U, "B interrupt follows A");
 event_it_enter = FALSE; cu_avr_itcheck();
 CHECK(event_it_vect == VECT_T1OVF, "overflow interrupt follows compares");
 setup(1U, 1U, 9U, 9U, FALSE); next();
 CHECK((cpu_state.iors[CU_IO_TIFR1] & 7U) == 6U && timer1_base == 0U, "simultaneous normal compares do not clear counter");

 setup(0xFFFFFFFCU, 1U, 5U, 8U, FALSE);
 CHECK(cycle_next_event == 1U, "CPU cycle rollover scheduled"); next();
 CHECK(cpu_state.iors[CU_IO_TIFR1] & 2U, "compare survives CPU cycle rollover");
 setup(100U, 100U, 999U, 1999U, FALSE);
 cu_avr_write_io(CU_IO_OCR1AH, 0U);
 CHECK(cpu_state.iors[CU_IO_OCR1AH] == 3U, "OCR high write stays in TEMP until low-byte commit");
 cu_avr_write_io(CU_IO_OCR1AL, 104U);
 next(); CHECK(cycle_next_event == 105U, "OCR write reschedules earlier match"); next();
 CHECK(cpu_state.iors[CU_IO_TIFR1] & 2U, "reprogrammed compare fires");
 setup(100U, 100U, 999U, 1999U, FALSE);
 cu_avr_write_io(CU_IO_OCR1AH, 0U); cu_avr_write_io(CU_IO_OCR1AL, 49U);
 next(); CHECK(cycle_next_event == 2000U, "compare reprogrammed behind counter does not fire early");
 next(); next(); CHECK(cycle_next_event == 65586U, "behind-counter target scheduled after overflow");

 setup(1U, 1U, 9U, 19U, FALSE);
 write_counter(9U); next();
 CHECK(!(cpu_state.iors[CU_IO_TIFR1] & 2U), "TCNT write blocks A on following clock");
 next(); CHECK(cycle_next_event == 12U, "blocked clock restores B scheduling"); next();
 CHECK(cpu_state.iors[CU_IO_TIFR1] & 4U, "B still works after blocked A");
 setup(1U, 1U, 65535U, 65535U, FALSE);
 write_counter(65535U); next();
 CHECK((cpu_state.iors[CU_IO_TIFR1] & 7U) == 1U, "counter-write block suppresses both compares but not overflow");
 setup(1U, 1U, 9U, 19U, TRUE);
 write_counter(9U); next();
 CHECK(!(cpu_state.iors[CU_IO_TIFR1] & 2U) && timer1_base != cpu_state.cycle, "blocked CTC A does not clear counter");
 next(); CHECK(cycle_next_event == 12U, "CTC restores B target after blocking");
 setup(1U, 1U, 9U, 9U, TRUE); next();
 CHECK((cpu_state.iors[CU_IO_TIFR1] & 7U) == 6U && timer1_base == 10U && cycle_next_event == 20U, "existing CTC simultaneous compares/reset retained");

 setup(1U, 1U, 9U, 19U, FALSE);
 cpu_state.iors[CU_IO_TCCR1B] = 0U;
 write_counter(9U); next();
 CHECK(timer1_compare_blocked && !(cpu_state.iors[CU_IO_TIFR1] & 6U), "stopped timer retains compare inhibition");
 cu_avr_write_io(CU_IO_TCCR1B, 1U); next();
 CHECK(!timer1_compare_blocked, "first running timer clock consumes inhibition");
 setup(1U, 1U, 9U, 19U, FALSE);
 cpu_state.iors[CU_IO_TCCR1A] = 1U;
 cu_avr_hwexec(); CHECK(cycle_next_event == 65536U, "normal-mode implementation does not enable unsupported PWM compares");
 cu_avr_write_io(CU_IO_TCCR1A, 0U); next();
 CHECK(cycle_next_event == 10U, "mode change reschedules normal compare");

 /* Run real AVR code through interrupt entry and RETI in normal mode. */
 memset(&cpu_state, 0, sizeof(cpu_state));
 cpu_state.fuse[1] = 0xD3U;
 cpu_state.crom[0] = 0xFFU; cpu_state.crom[1] = 0xCFU; /* RJMP . */
 cpu_state.crom[VECT_T1COMPA * 2U + 0U] = 0x05U;
 cpu_state.crom[VECT_T1COMPA * 2U + 1U] = 0xEAU; /* LDI r16, 0xA5 */
 cpu_state.crom[VECT_T1COMPA * 2U + 2U] = 0x00U;
 cpu_state.crom[VECT_T1COMPA * 2U + 3U] = 0x93U; /* STS 0x0100, r16 */
 cpu_state.crom[VECT_T1COMPA * 2U + 4U] = 0x00U;
 cpu_state.crom[VECT_T1COMPA * 2U + 5U] = 0x01U;
 cpu_state.crom[VECT_T1COMPA * 2U + 6U] = 0x18U;
 cpu_state.crom[VECT_T1COMPA * 2U + 7U] = 0x95U; /* RETI */
 cu_avr_reset();
 cpu_state.iors[CU_IO_TCCR1B] = 1U;
 cpu_state.iors[CU_IO_OCR1AL] = 9U;
 cpu_state.iors[CU_IO_OCR1BH] = 255U; cpu_state.iors[CU_IO_OCR1BL] = 255U;
 cpu_state.iors[CU_IO_TIMSK1] = 2U;
 cpu_state.iors[CU_IO_SREG] = SREG_IM;
 (void)cu_avr_run();
 CHECK(cpu_state.sram[0x100U] == 0xA5U && (cpu_state.pc & 0x7FFFU) == 0U, "normal compare invokes real AVR ISR and returns");
 CHECK(timer1_base == 0U && !(cpu_state.iors[CU_IO_TIFR1] & 2U), "IRQ clears compare flag without resetting normal timer");

 /* Snapshot in the one-clock write window; old CPU blob layout is unchanged. */
 main_headless = TRUE;
 cu_spir_set_size(1U);
 cu_vfat_reset();
 setup(1U, 1U, 9U, 19U, FALSE);
 write_counter(9U);
 CHECK(savestate_capture_to_mem(&state), "capture pending inhibition");
 cycle = cpu_state.cycle;
 next(); CHECK(!timer1_compare_blocked, "inhibition consumed before restore");
 CHECK(savestate_restore_from_mem(&state), "restore new snapshot");
 CHECK(timer1_compare_blocked && cpu_state.cycle == cycle, "rewind preserves pending inhibition");
 next(); CHECK(!(cpu_state.iors[CU_IO_TIFR1] & 2U), "restored inhibition prevents spurious A");
 state.size -= 12U; /* The same version-1 state without the optional timer trailer. */
 CHECK(savestate_restore_from_mem(&state) && !timer1_compare_blocked, "legacy state without trailer remains loadable");
 savestate_free_blob(&state);
 puts("Timer1 regression: PASS (normal/CTC flags, scheduling, rollover, register writes, masking, TCNT blocking, save/rewind compatibility)");
 return 0;
}
