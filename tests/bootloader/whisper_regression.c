#define SDL_MAIN_HANDLED
#include <stdarg.h>
#include "../../cu_avr.h"

static char output[64];
static unsigned output_len;
/* This core-only fixture supplies the diagnostic clock normally owned by main. */
uint32 mainui_get_frame_counter(void){return 0U;}
static int whisper_printf(char const* fmt, ...)
{
 va_list args;
 int len;
 va_start(args, fmt);
 len = vsnprintf(output + output_len, sizeof(output) - output_len, fmt, args);
 va_end(args);
 if (len > 0){ output_len += (unsigned)len; }
 return len;
}

/* Intercept only stdout output, while exercising the production I/O handler. */
#define printf whisper_printf
#include "../../cu_avr.c"
#undef printf

int main(void)
{
 memset(&cpu_state, 0, sizeof(cpu_state));
 cu_avr_write_io(0x39U, 0x41U);
 cu_avr_write_io(0x3AU, 0x42U);
#if FLAG_PRINTF_WHISPER || HEADLESS
 if (strcmp(output, "41B")){
  fprintf(stderr, "FAIL: whisper output '%s', expected '41B'\n", output); return 1;
 }
#else
 if (output_len){ fprintf(stderr, "FAIL: disabled whisper emitted output\n"); return 1; }
#endif
 if (cpu_state.iors[0x39U] != 0x41U || cpu_state.iors[0x3AU] != 0x42U){
  fprintf(stderr, "FAIL: whisper register values\n"); return 1;
 }
 puts("Whisper regression: PASS");
 return 0;
}
