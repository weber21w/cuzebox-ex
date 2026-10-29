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
#if FLAG_AVR_TIMER1_CLOCK
 timer1_clock=(cu_timer1_clock_t){(uint32_t)cpu_state.cycle,(uint32_t)cpu_state.cycle,0U,1U,0U};
#endif
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
 CHECK(video_row.pixels[0] == 0x0FU, "OUT PINC reaches the picture on its own cycle, as OUT PORTC does");
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

static void hex_marker(auint command, auint value)
{
 instruction(0,0xC004U); instruction(1,0x4355U); instruction(2,0x4848U);
 instruction(3,command | (value << 8)); instruction(4,(command | (value << 8)) ^ 0xFFFFU);
 cpu_state.pc=0U;
}
static int hex_hints_test(void (*step)(void))
{
 auint preset;
 appcfg_defaults(&main_cfg); main_rom_is_uze=FALSE;
 cu_avr_set_hex_hint_hook(main_hex_hint); setup();
 cpu_state.iors[CU_IO_TIMSK1]=0U;
 hex_marker(4U,4U); step();
 CHECK(cpu_state.pc==5U && cpu_state.cycle==2U,"HEX marker retains hardware jump timing");
 CHECK(mainui_get_area_weighted(),"HEX sampling hint");
 hex_marker(3U,3U); step();
 CHECK(mainui_get_area_weighted() && cu_avr_interlace_enabled(),"masked hints preserve other flags");
 hex_marker(4U,0U); step(); CHECK(!mainui_get_area_weighted(),"clear masked sampling hint");
 hex_marker(0x80U,4U); step(); CHECK(cu_spir_get_state()->size==262144U,"SPI size changes synchronously");
 hex_marker(0x80U,1U); step(); CHECK(cu_spir_get_state()->size==65536U,"SPI shrink does not restore allocation size");
 hex_marker(0x80U,3U); step(); CHECK(cu_spir_get_state()->size==65536U,"invalid SPI size ignored");
 hex_marker(0x80U,0U); step(); CHECK(cu_spir_get_state()->size==0U,"SPI disable");
 main_cfg.spiram_pages_mode=3U; main_apply_spiram_policy(FALSE,0U);
 hex_marker(0x80U,8U); step(); CHECK(cu_spir_get_state()->size==131072U,"user SPI override wins");
 main_cfg.area_weighted_mode=1U; hex_marker(4U,4U); step();
 CHECK(!mainui_get_area_weighted(),"user display override wins");
 main_cfg.spiram_pages_mode=0U; main_cfg.area_weighted_mode=0U;
 hex_marker(4U,0U); instruction(4,0U); step(); CHECK(mainui_get_area_weighted(),"bad checksum ignored");
 for (preset=0U; preset<=3U; ++preset){
  hex_marker(0x83U,preset); step();
  CHECK(cu_spisd_model_get()->preset==(preset ? preset : CU_SPISD_PRESET_NORMAL),"HEX SD timing presets");
 }
 CHECK(main_cfg.sd_timing_preset==CU_SPISD_PRESET_UZE_HINT,"SD hint does not change saved selection");
 main_sd_apply_policy();
 CHECK(cu_spisd_model_get()->preset==CU_SPISD_PRESET_FAST,"SD hint survives policy refresh");
 hex_marker(0x83U,4U); step();
 CHECK(cu_spisd_model_get()->preset==CU_SPISD_PRESET_FAST,"invalid SD hint ignored");
 hex_marker(0x83U,0U); step();
 CHECK(cu_spisd_model_get()->preset==CU_SPISD_PRESET_NORMAL,"clearing SD hint restores normal fallback");
 main_cfg.sd_timing_preset=CU_SPISD_PRESET_SLOW;
 hex_marker(0x83U,3U); step();
 CHECK(cu_spisd_model_get()->preset==CU_SPISD_PRESET_SLOW,"explicit SD selection wins");
 main_cfg.sd_timing_preset=CU_SPISD_PRESET_CUSTOM; main_cfg.sd_read_wait_bytes=57U;
 hex_marker(0x83U,1U); step();
 CHECK(cu_spisd_model_get()->preset==CU_SPISD_PRESET_CUSTOM && cu_spisd_model_get()->read_wait_bytes==57U,"custom SD timing wins");
 main_cfg.sd_timing_preset=CU_SPISD_PRESET_UZE_HINT; main_sd_apply_policy();
 CHECK(cu_spisd_model_get()->preset==CU_SPISD_PRESET_SLOW,"returning to hint applies latest request");
#if defined(ENABLE_ESP)
 cu_esp_set_serial_override(CU_ESP_SERIAL_INHERIT);
 cu_esp_set_serial_route(CU_ESP_SERIAL_UZE_HINT);
 hex_marker(0x81U,1U); step();
 CHECK(cu_esp_get_serial_route()==CU_ESP_SERIAL_ESP_MODULE,"HEX enables ESP UART");
 main_apply_rom_peripheral_policy(FALSE,NULL);
 CHECK(cu_esp_get_serial_route()==CU_ESP_SERIAL_ESP_MODULE,"ESP hint survives policy refresh");
 hex_marker(0x81U,2U); step();
 CHECK(cu_esp_get_serial_route()==CU_ESP_SERIAL_ESP_MODULE,"invalid ESP value ignored");
 hex_marker(0x81U,0U); step();
 CHECK(cu_esp_get_serial_route()==CU_ESP_SERIAL_DISCONNECTED,"HEX disables ESP UART");
 cu_esp_set_serial_route(CU_ESP_SERIAL_LOOPBACK);
 hex_marker(0x81U,1U); step();
 CHECK(cu_esp_get_serial_route()==CU_ESP_SERIAL_LOOPBACK,"explicit UART selection wins");
 cu_esp_set_serial_route(CU_ESP_SERIAL_UZE_HINT);
 cu_esp_set_serial_override(CU_ESP_SERIAL_DISCONNECTED);
 hex_marker(0x81U,1U); step();
 CHECK(cu_esp_get_serial_route()==CU_ESP_SERIAL_DISCONNECTED,"per-ROM UART override wins");
 cu_esp_set_serial_override(CU_ESP_SERIAL_INHERIT);
 cu_avr_reset();
 CHECK(cu_esp_get_serial_route()==CU_ESP_SERIAL_DISCONNECTED,"reset clears ESP hint");
 cpu_state.iors[CU_IO_TIMSK1]=0U;
 hex_marker(0x84U,1U); step();
 CHECK(cu_esp_get_serial_route()==CU_ESP_SERIAL_TCP_SERIAL,"HEX enables UzeLink UART");
 hex_marker(0x84U,2U); step();
 CHECK(cu_esp_get_serial_route()==CU_ESP_SERIAL_TCP_SERIAL,"invalid UzeLink value ignored");
 main_apply_rom_peripheral_policy(FALSE,NULL);
 CHECK(cu_esp_get_serial_route()==CU_ESP_SERIAL_TCP_SERIAL,"UzeLink hint survives policy refresh");
 hex_marker(0x81U,1U); step();
 CHECK(cu_esp_get_serial_route()==CU_ESP_SERIAL_DISCONNECTED,"ESP and UzeLink conflict");
 hex_marker(0x84U,0U); step();
 CHECK(cu_esp_get_serial_route()==CU_ESP_SERIAL_ESP_MODULE,"clearing UzeLink retains ESP request");
 hex_marker(0x84U,1U); step();
 CHECK(cu_esp_get_serial_route()==CU_ESP_SERIAL_DISCONNECTED,"UART conflict is independent of hint order");
 hex_marker(0x81U,0U); step();
 CHECK(cu_esp_get_serial_route()==CU_ESP_SERIAL_TCP_SERIAL,"clearing ESP retains UzeLink request");
 cu_esp_set_serial_route(CU_ESP_SERIAL_LOOPBACK);
 hex_marker(0x84U,1U); step();
 CHECK(cu_esp_get_serial_route()==CU_ESP_SERIAL_LOOPBACK,"explicit UART selection overrides UzeLink");
 cu_esp_set_serial_route(CU_ESP_SERIAL_UZE_HINT);
 cu_esp_set_serial_override(CU_ESP_SERIAL_DISCONNECTED);
 hex_marker(0x84U,1U); step();
 CHECK(cu_esp_get_serial_route()==CU_ESP_SERIAL_DISCONNECTED,"per-ROM UART selection overrides UzeLink");
 cu_esp_set_serial_override(CU_ESP_SERIAL_INHERIT);
 hex_marker(0x84U,0U); step();
 CHECK(cu_esp_get_serial_route()==CU_ESP_SERIAL_DISCONNECTED,"HEX disables UzeLink UART");
 hex_marker(0x85U,1U); step();
 CHECK(cu_esp_get_softap_enabled() && cu_esp_get_serial_route()==CU_ESP_SERIAL_ESP_MODULE,"AP hint enables SoftAP and ESP route");
 hex_marker(0x85U,2U); step();
 CHECK(cu_esp_get_softap_enabled(),"invalid AP value ignored");
 hex_marker(0x81U,1U); step(); hex_marker(0x85U,0U); step();
 CHECK(!cu_esp_get_softap_enabled() && cu_esp_get_serial_route()==CU_ESP_SERIAL_ESP_MODULE,"AP clear retains separate ESP request");
 hex_marker(0x81U,0U); step(); hex_marker(0x85U,1U); step();
 main_cfg.esp_softap_mode=MAINUI_ESP_SOFTAP_MODE_DISABLE;
 main_apply_rom_peripheral_policy(FALSE,NULL);
 CHECK(!cu_esp_get_softap_enabled(),"explicit SoftAP disable wins");
 main_cfg.esp_softap_mode=MAINUI_ESP_SOFTAP_MODE_ENABLE;
 hex_marker(0x85U,0U); step();
 CHECK(cu_esp_get_softap_enabled(),"explicit SoftAP enable wins");
 main_cfg.esp_softap_mode=MAINUI_ESP_SOFTAP_MODE_HINT;
 hex_marker(0x85U,1U); step(); hex_marker(0x84U,1U); step();
 CHECK(cu_esp_get_serial_route()==CU_ESP_SERIAL_DISCONNECTED,"SoftAP and UzeLink conflict");
 cu_esp_set_serial_route(CU_ESP_SERIAL_LOOPBACK);
 CHECK(cu_esp_get_serial_route()==CU_ESP_SERIAL_LOOPBACK,"explicit UART selection overrides conflicting hints");
 cu_esp_set_serial_route(CU_ESP_SERIAL_UZE_HINT);
 cu_avr_reset(); cpu_state.iors[CU_IO_TIMSK1]=0U;
 CHECK(!main_hex_uzelink && !main_hex_esp8266_ap && !cu_esp_get_softap_enabled() && cu_esp_get_serial_route()==CU_ESP_SERIAL_DISCONNECTED,"reset clears AP and UzeLink requests");
#endif
 hex_marker(0x83U,3U); step(); cu_avr_reset(); cpu_state.iors[CU_IO_TIMSK1]=0U;
 CHECK(main_hex_sd_timing==0U && cu_spisd_model_get()->preset==CU_SPISD_PRESET_NORMAL,"reset clears SD hint");
 main_rom_is_uze=TRUE; main_rom_head.video=0U;
 hex_marker(4U,4U); step(); CHECK(!mainui_get_area_weighted(),"UZE ignores HEX hints");
 main_rom_head.sd_timing=CU_SPISD_PRESET_SLOW; main_sd_apply_policy();
 hex_marker(0x83U,3U); step();
 CHECK(cu_spisd_model_get()->preset==CU_SPISD_PRESET_SLOW,"UZE SD hint is not overridden by HEX marker");
#if defined(ENABLE_ESP)
 main_rom_head.pdefault=PERIPHERAL_ESP8266_AP; main_rom_head.uart=0U;
 main_apply_rom_peripheral_policy(TRUE,&main_rom_head);
 hex_marker(0x81U,0U); step();
 CHECK(cu_esp_get_serial_route()==CU_ESP_SERIAL_ESP_MODULE,"UZE ESP hint is not overridden by HEX marker");
 hex_marker(0x84U,1U); step(); hex_marker(0x85U,0U); step();
 CHECK(cu_esp_get_softap_enabled() && cu_esp_get_serial_route()==CU_ESP_SERIAL_ESP_MODULE,"UZE AP default ignores HEX UART and AP markers");
 main_rom_head.uart=UART_UZELINK_DEFAULT; main_apply_rom_peripheral_policy(TRUE,&main_rom_head);
 CHECK(cu_esp_get_serial_route()==CU_ESP_SERIAL_DISCONNECTED,"UZE conflicting UART defaults still disconnect");
 main_rom_head.pdefault=0U; main_apply_rom_peripheral_policy(TRUE,&main_rom_head);
 CHECK(!cu_esp_get_softap_enabled() && cu_esp_get_serial_route()==CU_ESP_SERIAL_TCP_SERIAL,"UZE UzeLink default remains supported");
#endif
 main_rom_is_uze=FALSE; cu_avr_reset();
 CHECK(!mainui_get_area_weighted() && !main_hex_spiram_set && cu_spir_get_state()->size==131072U,"reset restores HEX defaults");
 cu_avr_set_hex_hint_hook(NULL);
 return 0;
}

int main(void)
{
 if (run_decoder(cu_avr_exec) || run_decoder(cu_avr_exec_table) || peripherals_test()) return 1;
 if (pin_toggle_test(cu_avr_exec) || pin_toggle_test(cu_avr_exec_table)) return 1;
 if (hex_hints_test(cu_avr_exec) || hex_hints_test(cu_avr_exec_table)) return 1;
 puts("HEX hints passed for both decoders.");
 puts("Interrupt timing regression passed for switch and function-table decoders.");
 puts("PINx toggle regression passed for switch and function-table decoders.");
 return 0;
}
