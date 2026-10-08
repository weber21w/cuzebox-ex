#!/usr/bin/env python3
"""Regression for opt-in UART waveform capture and browser reconstruction."""
from __future__ import annotations
import pathlib, re, shutil, subprocess, tempfile
ROOT=pathlib.Path(__file__).resolve().parent.parent

def main():
    uart=(ROOT/'cu_uart.c').read_text()
    avr=(ROOT/'cu_avr.c').read_text()
    html=(ROOT/'tools/web_debugger/debugger.html').read_text()
    # Normal UART already called cu_esp_serial_trace_push(). The waveform feature
    # must share its one disabled early gate and do all format work after it.
    m=re.search(r'static void cu_esp_serial_trace_push\(.*?\n\}',uart,re.S)
    if not m: raise SystemExit('UART trace push function not found')
    body=m.group(0)
    gate=body.find('if(!cu_uart_any_trace_capture)')
    timing=body.find('cu_uart_get_frame_cycles()')
    emit=body.find('cu_debug_timing_trace_uart(')
    if not (0 <= gate < timing < emit): raise SystemExit('UART disabled fast gate is not ahead of waveform work')
    if 'CU_TIMING_EVT_UART_TX, cval' in avr: raise SystemExit('legacy duplicate UDR UART trace hook still present')
    for token in ('UART TXD','UART RXD','wire_start_cycle','uart_txd','uart_rxd'):
        if token not in html: raise SystemExit(f'missing web UART waveform token: {token}')
    node=shutil.which('node')
    if node:
        funcs=[]
        for name in ('uartParityBit','expandUart'):
            mm=re.search(rf'function {name}\([^\n]+',html)
            if not mm: raise SystemExit(f'{name} missing')
            funcs.append(mm.group(0))
        js='\n'.join(funcs)+r'''
const e={type:'UART_TX',value:0x55,wire_start_cycle:100,bit_cycles:10,data_bits:8,stop_bits:1,parity:0,synchronous:0};
const a=expandUart(e);
if(a[0].abs!==100||a[0].value!==0)throw Error('start bit');
if(a[1].abs!==110||a[1].value!==1)throw Error('data0');
if(a[2].abs!==120||a[2].value!==0)throw Error('data1');
if(a[a.length-2].abs!==190||a[a.length-2].value!==1)throw Error('stop start');
if(a[a.length-1].abs!==200||a[a.length-1].value!==1)throw Error('frame end');
const pe={type:'UART_RX',value:1,wire_start_cycle:1000,bit_cycles:8,data_bits:8,stop_bits:1,parity:2,synchronous:0};
const p=expandUart(pe);
const parity=p.find(x=>x.abs===1072);
if(!parity||parity.value!==1)throw Error('even parity');
if(expandUart({...e,synchronous:1}).length!==0)throw Error('sync UART must not be reconstructed as async');
console.log('UART waveform reconstruction: PASS');
'''
        with tempfile.TemporaryDirectory(prefix='cuzebox-uart-js-') as td:
            f=pathlib.Path(td)/'t.js';f.write_text(js)
            subprocess.run([node,str(f)],check=True)
    print('UART logic capture opt-in regression: PASS')
    return 0
if __name__=='__main__': raise SystemExit(main())
