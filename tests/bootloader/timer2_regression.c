#define SDL_MAIN_HANDLED
#undef main
#include "../../cu_vfat.h"
#define main timer2_emulator_main
#include "../../main.c"
#undef main
#include "../../cu_avr.c"

#define CHECK(c, msg) do { if (!(c)) { fprintf(stderr, "FAIL: %s (line %d)\n", msg, __LINE__); return 1; } } while (0)

/* Timer2 in normal mode at clock select cs, TCNT2 set to count, at cycle. */
static void start(auint cycle, auint cs, auint count)
{
 memset(&cpu_state, 0, sizeof(cpu_state));
 cpu_state.cycle = cycle;
 cu_avr_io_update();
 cu_avr_write_io(CU_IO_TCCR2B, cs);
 cu_avr_write_io(CU_IO_TCNT2, count);
}
static auint tcnt2_after(auint cycles)
{
 cpu_state.cycle = WRAP32(cpu_state.cycle + cycles);
 return cu_avr_read_io(CU_IO_TCNT2);
}
static auint tov2(void){ return cu_avr_read_io(CU_IO_TIFR2) & 1U; }

/* TCCR2B = 1; TCNT2 = 0xFE; 10 NOPs; [0x100] = TCNT2 (LDS); [0x101] = TIFR2 (IN); RJMP . */
static const uint8 prog[] = {
 0x01U, 0xE0U, 0x00U, 0x93U, 0xB1U, 0x00U, 0x0EU, 0xEFU, 0x00U, 0x93U, 0xB2U, 0x00U,
 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
 0x10U, 0x91U, 0xB2U, 0x00U, 0x10U, 0x93U, 0x00U, 0x01U, 0x27U, 0xB3U,
 0x20U, 0x93U, 0x01U, 0x01U, 0xFFU, 0xCFU
};

#ifndef TIMER2_TEST_ENTRY
#define TIMER2_TEST_ENTRY main
#endif
int TIMER2_TEST_ENTRY(void)
{
 static const auint ps[8] = {0U, 1U, 8U, 32U, 64U, 128U, 256U, 1024U};
 savestate_blob_t state = {0};
 auint cs;

 for (cs = 1U; cs < 8U; cs++){
  start(1000U, cs, 0U);
  CHECK(tcnt2_after(ps[cs] * 200U - 1U) == 199U, "one cycle short of 200 ticks, at each prescaler");
  CHECK(tcnt2_after(1U) == 200U, "200 ticks at each prescaler");
 }

 start(1000U, 1U, 0xF0U);
 CHECK(!tov2(), "no TOV2 before the wrap");
 CHECK(tcnt2_after(0x20U) == 0x10U && tov2(), "the wrap sets TOV2");
 cu_avr_write_io(CU_IO_TIFR2, 0U);
 CHECK(tov2(), "writing a zero leaves TOV2");
 cu_avr_write_io(CU_IO_TIFR2, 1U);
 CHECK(!tov2(), "writing a one clears TOV2");
 CHECK(tcnt2_after(0x100U) == 0x10U && tov2(), "the next wrap sets it again");

 start(1000U, 1U, 0U);
 (void)tcnt2_after(100U);
 cu_avr_write_io(CU_IO_TCCR2B, 0U);
 CHECK(tcnt2_after(5000U) == 100U, "stopped, the count is held");
 cu_avr_write_io(CU_IO_TCCR2B, 2U);
 CHECK(tcnt2_after(80U) == 110U, "started again at /8 from the held count");

 start(1000U, 1U, 0x40U);
 cu_avr_write_io(CU_IO_TCCR2A, 3U); /* fast PWM, the audio output: not counted, as before */
 CHECK(tcnt2_after(5000U) == 0x40U, "PWM mode leaves TCNT2 as it was");

 start(0xFFFFFF00U, 1U, 0U);
 CHECK(tcnt2_after(0x180U) == 0x80U && tov2(), "counts across the CPU cycle rollover");

 start(1000U, 7U, 0U);
 cpu_state.cycle += 1024U * 33U;
 CHECK(cu_avr_get_state()->iors[CU_IO_TCNT2] == 33U, "cu_avr_get_state() brings TCNT2 up to date");

 start(1000U, 1U, 0x40U);
 (void)tcnt2_after(50U);
 cu_avr_reset_wd();
 CHECK(cpu_state.iors[CU_IO_TCNT2] == 0U, "a watchdog reset clears TCNT2");
 cu_avr_write_io(CU_IO_TCCR2B, 1U);
 CHECK(tcnt2_after(100U) == 100U, "after a reset the count starts from 0, not the old count");

 /* Real AVR code: LDS of TCNT2 and IN of TIFR2 through the decoder, across a wrap.
 ** 10 NOPs between the write and the read, give or take where in the two-cycle
 ** STS and LDS the access falls. */
 memset(&cpu_state, 0, sizeof(cpu_state));
 cpu_state.fuse[1] = 0xD3U;
 memcpy(&cpu_state.crom[0], prog, sizeof(prog));
 cu_avr_reset();
 (void)cu_avr_run();
 CHECK(cpu_state.sram[0x100U] >= 0x08U && cpu_state.sram[0x100U] <= 0x0AU, "AVR code: TCNT2 0xFE plus 10 cycles");
 CHECK((cpu_state.sram[0x101U] & 1U) == 1U, "AVR code: TOV2 set by the wrap");

 /* A state saved and restored: TCNT2 is in the CPU blob, its layout unchanged */
 main_headless = TRUE;
 cu_spir_set_size(1U);
 cu_vfat_reset();
 start(1000U, 1U, 0U);
 (void)tcnt2_after(77U);
 CHECK(savestate_capture_to_mem(&state), "capture");
 CHECK(tcnt2_after(500U) == (77U + 500U) % 256U, "runs on after the capture");
 CHECK(savestate_restore_from_mem(&state), "restore");
 CHECK(cu_avr_read_io(CU_IO_TCNT2) == 77U, "restored count");
 CHECK(tcnt2_after(10U) == 87U, "counts on from the restored count");
 savestate_free_blob(&state);

 puts("Timer2 regression: PASS (normal mode at each prescaler, TOV2 and its clearing, stop/start, PWM untouched, rollover, AVR code, watchdog reset, save/restore)");
 return 0;
}
