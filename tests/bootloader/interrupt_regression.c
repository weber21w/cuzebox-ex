#define SDL_MAIN_HANDLED
#undef main
#include "../../cu_vfat.h"
#define main interrupt_emulator_main
#include "../../main.c"
#undef main
#include "../../cu_avr.c"

/* Exercise both production decoders against the same core and expectations. */
#define cu_avr_exec cu_avr_exec_table
#include "../../cu_avr_e.h"
#undef cu_avr_exec

#define CHECK(c, message) do { \
 if (!(c)){ fprintf(stderr, "FAIL: %s (line %d)\n", message, __LINE__); return 1; } \
} while (0)

static void setup(void)
{
 memset(&cpu_state, 0, sizeof(cpu_state));
 cpu_state.fuse[1] = 0xD3U;
 cu_avr_reset();
 cpu_state.pc = 0U;
 cpu_state.cycle = 0U;
 cpu_state.iors[CU_IO_SPL] = 0xE0U;
 cpu_state.iors[CU_IO_SPH] = 0x10U;
 cpu_state.iors[CU_IO_SREG] = 0U;
 cpu_state.iors[CU_IO_TIFR1] = 2U;
 cpu_state.iors[CU_IO_TIMSK1] = 2U;
 event_it = TRUE;
 event_it_enter = FALSE;
 cycle_next_event = 10000U;
 video_cycle = 0U;
}

static void instruction(auint pc, auint word)
{
 cpu_state.crom[pc * 2U] = word & 255U;
 cpu_state.crom[pc * 2U + 1U] = word >> 8;
 cu_avr_crom_update(pc * 2U, 2U);
}

static auint stack_pointer(void)
{
 return cpu_state.iors[CU_IO_SPL] | (cpu_state.iors[CU_IO_SPH] << 8);
}

static auint stacked_pc(void)
{
 auint sp = stack_pointer();
 return (cpu_state.sram[(sp + 1U) & 0xFFFU] << 8) |
         cpu_state.sram[(sp + 2U) & 0xFFFU];
}

static void return_to(auint pc)
{
 auint sp = stack_pointer();
 cpu_state.sram[(sp + 1U) & 0xFFFU] = pc >> 8;
 cpu_state.sram[(sp + 2U) & 0xFFFU] = pc & 255U;
}

static void compare_after(auint clocks)
{
 /* The Timer1 comparator latches OCR1A at the following timer clock. */
 cpu_state.iors[CU_IO_TIFR1] = 0U;
 cpu_state.iors[CU_IO_TCCR1B] = 1U;
 cpu_state.iors[CU_IO_OCR1AL] = clocks - 1U;
 cpu_state.iors[CU_IO_OCR1AH] = 0U;
 cpu_state.iors[CU_IO_OCR1BL] = 0xFFU;
 cpu_state.iors[CU_IO_OCR1BH] = 0xFFU;
 timer1_base = cpu_state.cycle;
 timer1_compare_blocked = FALSE;
 cu_avr_hwexec();
}

static int run_decoder(void (*step)(void))
{
 unsigned clocks;
 setup();
 cpu_state.iors[CU_IO_TIMSK1] = 0U;
 instruction(0, 0x940CU); instruction(1, 8U); /* JMP with operand patched last */
 step();
 CHECK(cpu_state.pc == 8U, "operand write invalidates preceding decoded instruction");
 instruction(0x7FFFU, 0x940CU); instruction(0, 9U);
 cpu_state.pc = 0x7FFFU;
 step();
 CHECK(cpu_state.pc == 9U, "operand invalidation handles flash wrap");

 setup();
 instruction(0, 0x9478U); /* SEI */
 instruction(1, 0xEA05U); /* LDI r16, 0xA5 */
 step();
 CHECK(cpu_state.pc == 1U && cpu_state.cycle == 1U, "SEI does not enter pending IRQ");
 CHECK(cpu_state.iors[CU_IO_SREG] & SREG_IM, "SEI makes I visible immediately");
 CHECK(cpu_state.iors[CU_IO_TIFR1] & 2U, "SEI does not acknowledge pending flag");
 step();
 CHECK(cpu_state.iors[16] == 0xA5U && cpu_state.pc == VECT_T1COMPA, "following instruction completes before IRQ");
 CHECK(stacked_pc() == 2U && stack_pointer() == 0x10DEU, "IRQ stacks following return address");

 setup();
 instruction(0, 0x9478U); /* SEI */
 instruction(1, 0x940EU); /* CALL 8: four cycles, two words */
 instruction(2, 8U);
 step(); step();
 CHECK(cpu_state.pc == VECT_T1COMPA && cpu_state.cycle == 8U, "four-cycle CALL finishes before IRQ entry");
 CHECK(stacked_pc() == 8U && stack_pointer() == 0x10DCU, "CALL and IRQ both push their own return addresses");
 CHECK(cpu_state.sram[0x0DFU] == 0U && cpu_state.sram[0x0E0U] == 3U, "CALL return address remains intact");

 setup();
 instruction(0, 0x9478U); instruction(1, 0x94F8U); /* SEI; CLI */
 instruction(2, 0x0000U);
 step(); step(); step();
 CHECK(cpu_state.pc == 3U && !(cpu_state.iors[CU_IO_SREG] & SREG_IM), "CLI immediately disables pending IRQ");
 CHECK(cpu_state.iors[CU_IO_TIFR1] & 2U, "CLI preserves unserviced interrupt flag");

 setup();
 instruction(0, 0x9478U); instruction(1, 0x9478U); instruction(2, 0xEA05U);
 step(); step();
 CHECK(cpu_state.pc == 2U, "repeated SEI also protects its following instruction");
 step();
 CHECK(cpu_state.pc == VECT_T1COMPA && stacked_pc() == 3U, "IRQ resumes after repeated SEI sequence");

 setup();
 cpu_state.iors[CU_IO_SREG] = SREG_IM;
 instruction(0, 0x9408U); /* SEC: BSET must only defer for I */
 step();
 CHECK(cpu_state.pc == VECT_T1COMPA && (cpu_state.iors[CU_IO_SREG] & SREG_CM), "other BSET bits do not defer IRQ");

 for (clocks = 1U; clocks <= 3U; ++clocks){
  setup();
  compare_after(clocks);
  instruction(0, 0x9478U); instruction(1, 0x940EU); instruction(2, 8U);
  step();
  CHECK(cpu_state.pc == 1U, "new IRQ during SEI is deferred");
  step();
  CHECK(cpu_state.pc == VECT_T1COMPA && stacked_pc() == 8U, "new IRQ during following CALL waits for completion");
 }

 setup();
 instruction(0, 0x9518U); /* RETI */
 instruction(8, 0xEA05U);
 return_to(8U);
 step();
 CHECK(cpu_state.pc == 8U && cpu_state.cycle == 4U, "RETI returns before pending IRQ");
 CHECK(stack_pointer() == 0x10E2U && (cpu_state.iors[CU_IO_SREG] & SREG_IM), "RETI pops stack and enables I");
 step();
 CHECK(cpu_state.pc == VECT_T1COMPA && stacked_pc() == 9U && cpu_state.iors[16] == 0xA5U, "returned instruction executes before IRQ reentry");

 for (clocks = 1U; clocks <= 4U; ++clocks){
  setup(); compare_after(clocks);
  instruction(0, 0x9518U); instruction(8, 0x0000U); return_to(8U);
  step();
  CHECK(cpu_state.pc == 8U, "new IRQ at any RETI cycle waits");
  step();
  CHECK(cpu_state.pc == VECT_T1COMPA && stacked_pc() == 9U, "RETI retains newly arriving IRQ");
 }

 setup();
 instruction(0, 0x9518U); instruction(8, 0x9518U); instruction(9, 0x0000U);
 return_to(8U);
 cpu_state.sram[0x0E3U] = 0U; cpu_state.sram[0x0E4U] = 9U;
 step(); step();
 CHECK(cpu_state.pc == 9U && stack_pointer() == 0x10E4U, "chained RETI completes both returns");
 step();
 CHECK(cpu_state.pc == VECT_T1COMPA && stacked_pc() == 10U, "chained RETI protects final returned instruction");

 setup();
 cpu_state.iors[16] = SREG_IM; cpu_state.iors[17] = 0xA0U; cpu_state.iors[18] = 0x0FU;
 instruction(0, 0xBF2EU); /* OUT SPH, r18 */
 instruction(1, 0xBF0FU); /* OUT SREG, r16 */
 instruction(2, 0xBF1DU); /* OUT SPL, r17 */
 step(); step();
 CHECK(cpu_state.pc == 2U && stack_pointer() == 0x0FE0U, "OUT SREG retains existing interrupt sampling order");
 step();
 CHECK(cpu_state.pc == VECT_T1COMPA && stack_pointer() == 0x0F9EU && stacked_pc() == 3U, "OUT SPL commits before IRQ stacks PC");
 return 0;
}

/* A one written to a PINx bit toggles PORTx (ATmega644 "Toggling the Pin"),
** through OUT, STS, SBI (its bit only) and CBI (nothing); PINx is unchanged. */
static int pin_toggle_test(void (*step)(void))
{
 setup();
 cpu_state.iors[CU_IO_DDRC] = 0xFFU;
 cpu_state.iors[CU_IO_PORTC] = 0x00U;
 cpu_state.iors[CU_IO_PINC] = 0x55U;
 cpu_state.iors[CU_IO_DDRB] = 0x02U;
 cpu_state.iors[CU_IO_PORTB] = 0x00U;
 cpu_state.iors[16] = 0x0FU;
 instruction(0, 0xB906U); /* OUT PINC, r16 */
 instruction(1, 0x9A37U); /* SBI PINC, 7 */
 instruction(2, 0x9830U); /* CBI PINC, 0 */
 instruction(3, 0xEF0FU); /* LDI r16, 0xFF */
 instruction(4, 0x9300U); instruction(5, 0x0026U); /* STS PINC, r16 */
 instruction(6, 0x9A19U); /* SBI PINB, 1 */
 instruction(7, 0x9A19U); /* SBI PINB, 1 */
 step();
 CHECK(cpu_state.iors[CU_IO_PORTC] == 0x0FU, "OUT to PINC toggles PORTC");
 CHECK(cpu_state.iors[CU_IO_PINC] == 0x55U, "a PINC write leaves PINC");
 step();
 CHECK(cpu_state.iors[CU_IO_PORTC] == 0x8FU, "SBI on PINC toggles only its bit");
 step();
 CHECK(cpu_state.iors[CU_IO_PORTC] == 0x8FU, "CBI on PINC toggles nothing");
 step(); step();
 CHECK(cpu_state.iors[CU_IO_PORTC] == 0x70U, "STS to PINC toggles PORTC");
 step();
 CHECK(cpu_state.iors[CU_IO_PORTB] == 0x02U, "SBI on PINB toggles PORTB");
 step();
 CHECK(cpu_state.iors[CU_IO_PORTB] == 0x00U, "a second SBI toggles it back");
 return 0;
}

static int peripherals_test(void)
{
 static auint const vectors[] = { VECT_WDT, VECT_T1COMPA, VECT_T1COMPB,
                                  VECT_T1OVF, VECT_SPISTC, VECT_USART0_TX };
 unsigned i;
 setup();
 cpu_state.iors[CU_IO_SREG] = SREG_IM;
 cpu_state.iors[CU_IO_WDTCSR] = 0xC0U;
 cpu_state.iors[CU_IO_TIFR1] = cpu_state.iors[CU_IO_TIMSK1] = 7U;
 cpu_state.iors[CU_IO_SPCR] = cpu_state.iors[CU_IO_SPSR] = 0x80U;
 cpu_state.iors[CU_IO_UCSR0A] = 1U << TXC0;
 cpu_state.iors[CU_IO_UCSR0B] = 1U << TXCIE0;
 for (i = 0; i < sizeof(vectors) / sizeof(vectors[0]); ++i){
  event_it_enter = FALSE;
  cu_avr_itcheck();
  CHECK(event_it_enter && event_it_vect == vectors[i], "simultaneous IRQs follow vector priority");
 }

 for (i = 0; i < 4; ++i){
  setup();
  cpu_state.iors[CU_IO_EEARL] = 0x20U;
  cpu_state.iors[CU_IO_EEARH] = 0U;
  cpu_state.eepr[0x20U] = i == 0 ? 0x20U : 0xF0U;
  cpu_state.iors[CU_IO_EEDR] = 0x3FU;
  cpu_state.iors[CU_IO_EECR] = (i << 4) | 2U;
  cpu_state.eep_wrte = TRUE;
  cpu_state.eep_end = cpu_state.cycle;
  eeprom_changed = FALSE;
  cu_avr_hwexec();
  CHECK(cpu_state.eepr[0x20U] == (i == 0 ? 0x3FU : i == 1 ? 0xFFU : i == 2 ? 0x30U : 0xF0U), "EEPROM erase/program modes");
  CHECK(eeprom_changed == (i != 3), "EEPROM dirty flag compares data, not address");
  CHECK(!cpu_state.eep_wrte && !(cpu_state.iors[CU_IO_EECR] & 2U), "EEPROM completion clears busy flags");
 }
 setup();
 cu_avr_write_io(CU_IO_EECR, 4U);
 CHECK(cpu_state.eep_end == 4U && cycle_next_event == 4U, "EEMPE opens a four-clock programming window");
 cpu_state.cycle = 4U; cu_avr_hwexec();
 CHECK(!(cpu_state.iors[CU_IO_EECR] & 4U), "EEMPE expires at four clocks");
 cu_avr_write_io(CU_IO_EECR, 2U);
 CHECK(!cpu_state.eep_wrte, "expired EEMPE cannot start programming");
 cu_avr_write_io(CU_IO_EECR, 4U);
 cu_avr_write_io(CU_IO_EECR, 0x22U);
 CHECK(cpu_state.eep_wrte && cpu_state.eep_end == 4U + EEPROM_EWR_TIM, "new write mode controls operation duration");
 cu_avr_write_io(CU_IO_EECR, 0U);
 CHECK((cpu_state.iors[CU_IO_EECR] & 0x32U) == 0x22U, "busy program retains EEPE and mode");
 return 0;
}

int main(void)
{
 if (run_decoder(cu_avr_exec) || run_decoder(cu_avr_exec_table) || peripherals_test()) return 1;
 if (pin_toggle_test(cu_avr_exec) || pin_toggle_test(cu_avr_exec_table)) return 1;
 puts("Interrupt timing regression passed for switch and function-table decoders.");
 puts("PINx toggle regression passed for switch and function-table decoders.");
 return 0;
}
