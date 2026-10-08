#!/usr/bin/env python3
"""Regression for pay-for-play AVR memory diagnostics."""
from __future__ import annotations
import os
import pathlib
import shutil
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).resolve().parent.parent
SDL = r'''#ifndef SDL_H
#define SDL_H
#include <stdint.h>
typedef uint8_t Uint8; typedef uint16_t Uint16; typedef uint32_t Uint32;
typedef int8_t Sint8; typedef int16_t Sint16; typedef int32_t Sint32;
#endif
'''


def preprocess_file(cc: str, inc: pathlib.Path, filename: str, *defs: str) -> str:
    cmd = [cc, '-E', '-P', '-std=gnu17', '-I', str(inc), '-I', str(ROOT), *defs, str(ROOT / filename)]
    return subprocess.run(cmd, check=True, text=True, stdout=subprocess.PIPE).stdout


def preprocess(cc: str, inc: pathlib.Path, *defs: str) -> str:
    return preprocess_file(cc, inc, 'cu_avr.c', *defs)


def make_cflags(*assignments: str) -> str:
    make = shutil.which('make')
    if not make:
        raise SystemExit('No make executable')
    cmd = [make, '--no-print-directory', "--eval=print-debug-flags:;@echo $(CFLAGS)", 'print-debug-flags', *assignments]
    return subprocess.run(cmd, cwd=ROOT, check=True, text=True, stdout=subprocess.PIPE).stdout.strip()


def main() -> int:
    cc = os.environ.get('CC') or shutil.which('cc') or shutil.which('gcc')
    if not cc:
        raise SystemExit('No host C compiler')

    # Both interpreter back ends must use the common SRAM access macros so a
    # future optimized opcode implementation cannot silently bypass watchpoints
    # or history while debugger builds are active.
    for name in ('cu_avr_n.h', 'cu_avr_e.h'):
        text = (ROOT / name).read_text()
        if 'cpu_state.sram[' in text:
            raise SystemExit(f'{name}: direct SRAM access bypasses CU_AVR_SRAM_* gate')

    with tempfile.TemporaryDirectory(prefix='cuzebox-debug-mem-') as td:
        inc = pathlib.Path(td)
        (inc / 'SDL2').mkdir()
        (inc / 'SDL2' / 'SDL.h').write_text(SDL)

        release = preprocess(cc, inc)
        debug = preprocess(cc, inc, '-DENABLE_DEBUGGER=1')
        history = preprocess(cc, inc, '-DENABLE_DEBUGGER=1', '-DENABLE_MEMORY_TRACE=1')
        debug_api = preprocess(cc, inc, '-DENABLE_DEBUGGER=1', '-DENABLE_API_SERVER=1', '-DENABLE_MEMORY_TRACE=1', '-DENABLE_BEAM_CAPTURE=1')
        debug_api_no_capture = preprocess(cc, inc, '-DENABLE_DEBUGGER=1', '-DENABLE_API_SERVER=1', '-DENABLE_MEMORY_TRACE=1')

        for token in ('cu_debug_mem_active', 'cu_avr_debug_mem_event', 'memory_trace_buf'):
            if token in release:
                raise SystemExit(f'release preprocessing unexpectedly contains {token}')
        if 'cu_debug_mem_active' not in debug or 'cu_avr_debug_mem_event' not in debug:
            raise SystemExit('debugger preprocessing lost the shared memory gate')
        if 'memory_trace_buf' in debug:
            raise SystemExit('ENABLE_MEMORY_TRACE=0 preprocessing still contains history storage')
        if 'memory_trace_buf' not in history:
            raise SystemExit('ENABLE_MEMORY_TRACE=1 preprocessing lost history storage')
        if 'video_beam_line_start_cycle' in release or 'video_beam_capture_on' in release:
            raise SystemExit('release preprocessing unexpectedly contains beam-debug capture state')
        if 'static uint8 video_beam_pixels[' not in debug_api or 'video_beam_line_start_cycle' not in debug_api:
            raise SystemExit('debugger+API+beam-capture preprocessing lost capture/live-position state')
        if 'video_beam_line_start_cycle' not in debug_api_no_capture:
            raise SystemExit('debugger+API preprocessing lost zero-history live beam position state')
        if 'static uint8 video_beam_pixels[' in debug_api_no_capture or 'video_beam_capture_on' in debug_api_no_capture:
            raise SystemExit('ENABLE_BEAM_CAPTURE=0 preprocessing still contains PORTC capture storage/state')
        source = (ROOT / 'cu_avr.c').read_text()
        if 'video_beam_cycle ++' in source or 'video_beam_cycle++' in source:
            raise SystemExit('beam debugger regained an unconditional per-cycle counter increment')
        beam_macro = source[source.index('#define UPDATE_VIDEO_BEAM'):source.index('#else', source.index('#define UPDATE_VIDEO_BEAM'))]
        if 'if (video_beam_capture_on)' not in beam_macro:
            raise SystemExit('beam per-cycle capture is not guarded by its runtime arm flag')
        if 'cu_beam_cycle_ --' not in beam_macro:
            raise SystemExit('beam capture lost boundary-to-sample cycle indexing adjustment')
        if 'video_beam_capture_on' in debug_api_no_capture:
            raise SystemExit('beam per-cycle capture branch survives ENABLE_BEAM_CAPTURE=0')

        vfat_release = preprocess_file(cc, inc, 'cu_vfat.c')
        vfat_debug = preprocess_file(cc, inc, 'cu_vfat.c', '-DENABLE_DEBUGGER=1')
        if 'cu_vfat_debug_sector_snapshot' in vfat_release or 'CU_VFAT_DEBUG_ROLE_BOOT' in vfat_release:
            raise SystemExit('release preprocessing unexpectedly contains VFAT filesystem inspector code')
        if 'cu_vfat_debug_sector_snapshot' not in vfat_debug:
            raise SystemExit('debugger preprocessing lost VFAT filesystem inspector')

        sd_release = preprocess_file(cc, inc, 'cu_spisd.c')
        sd_trace = preprocess_file(cc, inc, 'cu_spisd.c', '-DENABLE_DEBUGGER=1', '-DENABLE_SD_TRACE=1')
        sd_fault = preprocess_file(cc, inc, 'cu_spisd.c', '-DENABLE_DEBUGGER=1', '-DENABLE_SD_FAULT=1')
        sd_replay = preprocess_file(cc, inc, 'cu_spisd.c', '-DENABLE_DEBUGGER=1', '-DENABLE_SD_REPLAY=1')
        if 'sd_debug_active' in sd_release or 'cu_spisd_debug_after_send' in sd_release:
            raise SystemExit('ENABLE_SD_TRACE=0 preprocessing still contains SD trace gate/history path')
        if 'sd_debug_active' not in sd_trace or 'sd_trace_buf' not in sd_trace or 'cu_spisd_debug_after_send' not in sd_trace:
            raise SystemExit('ENABLE_SD_TRACE=1 preprocessing lost SD trace gate/history path')
        if 'sd_fault_any' in sd_release or 'sd_fault_rules' in sd_release:
            raise SystemExit('ENABLE_SD_FAULT=0 preprocessing still contains SD fault injection state')
        if 'sd_fault_any' not in sd_fault or 'sd_fault_rules' not in sd_fault:
            raise SystemExit('ENABLE_SD_FAULT=1 preprocessing lost SD fault injection gate/rules')
        if 'sd_replay_events' in sd_release or 'cu_spisd_replay_recv_byte' in sd_release:
            raise SystemExit('ENABLE_SD_REPLAY=0 preprocessing still contains SD replay storage/hot-path hooks')
        if 'sd_replay_events' not in sd_replay or 'cu_spisd_replay_recv_byte' not in sd_replay:
            raise SystemExit('ENABLE_SD_REPLAY=1 preprocessing lost SD deterministic replay storage/hooks')

        timing_source = (ROOT / 'debug_timing.c').read_text()
        if '#if defined(ENABLE_BEAM_HISTORY)' not in timing_source:
            raise SystemExit('beam instruction history storage is not compile-time gated')
        timing_no_history = preprocess_file(cc, inc, 'debug_timing.c')
        timing_history = preprocess_file(cc, inc, 'debug_timing.c', '-DENABLE_BEAM_HISTORY=1')
        if 'static cu_timing_beam_instruction_t beam_history[' in timing_no_history:
            raise SystemExit('ENABLE_BEAM_HISTORY=0 preprocessing still contains instruction history storage')
        if 'static cu_timing_beam_instruction_t beam_history[' not in timing_history:
            raise SystemExit('ENABLE_BEAM_HISTORY=1 preprocessing lost instruction history storage')

    release_flags = make_cflags('FLAG_DEBUGGER=0', 'FLAG_API_SERVER=0')
    forced_release_replay_flags = make_cflags('FLAG_DEBUGGER=0', 'FLAG_API_SERVER=0', 'FLAG_SD_REPLAY=1')
    if '-DENABLE_SD_REPLAY=1' in forced_release_replay_flags:
        raise SystemExit('FLAG_SD_REPLAY must not survive when FLAG_DEBUGGER=0')
    for define in ('-DENABLE_DEBUGGER=1', '-DENABLE_MEMORY_TRACE=1', '-DENABLE_BEAM_CAPTURE=1', '-DENABLE_BEAM_HISTORY=1', '-DENABLE_SD_TRACE=1', '-DENABLE_SD_FAULT=1', '-DENABLE_SD_REPLAY=1'):
        if define in release_flags:
            raise SystemExit(f'release Makefile CFLAGS unexpectedly contain {define}')
    lean_debug_flags = make_cflags('FLAG_DEBUGGER=1', 'FLAG_API_SERVER=1', 'FLAG_MEMORY_TRACE=0', 'FLAG_BEAM_CAPTURE=0', 'FLAG_BEAM_HISTORY=0', 'FLAG_SD_TRACE=0', 'FLAG_SD_FAULT=0', 'FLAG_SD_REPLAY=0')
    if '-DENABLE_DEBUGGER=1' not in lean_debug_flags or '-DENABLE_API_SERVER=1' not in lean_debug_flags:
        raise SystemExit('lean debugger Makefile CFLAGS lost debugger/API defines')
    for define in ('-DENABLE_MEMORY_TRACE=1', '-DENABLE_BEAM_CAPTURE=1', '-DENABLE_BEAM_HISTORY=1', '-DENABLE_SD_TRACE=1', '-DENABLE_SD_FAULT=1', '-DENABLE_SD_REPLAY=1'):
        if define in lean_debug_flags:
            raise SystemExit(f'lean debugger Makefile CFLAGS unexpectedly contain {define}')
    full_debug_flags = make_cflags('FLAG_DEBUGGER=1', 'FLAG_API_SERVER=1')
    for define in ('-DENABLE_DEBUGGER=1', '-DENABLE_MEMORY_TRACE=1', '-DENABLE_BEAM_CAPTURE=1', '-DENABLE_BEAM_HISTORY=1', '-DENABLE_SD_TRACE=1', '-DENABLE_SD_FAULT=1', '-DENABLE_SD_REPLAY=1', '-DENABLE_API_SERVER=1'):
        if define not in full_debug_flags:
            raise SystemExit(f'default debugger Makefile CFLAGS lost {define}')

    print('debug memory pay-for-play regression: PASS')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
