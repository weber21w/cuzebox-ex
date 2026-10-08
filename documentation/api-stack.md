# Automation API stack inspection

These commands expose the current AVR stack without adding instrumentation to
the emulator core.

```text
STACK [COUNT]
CALLSTACK [COUNT]
```

`STACK` returns bytes beginning at `SP + 1`, which is the first occupied stack
location on the emulated ATmega644. The default is 32 bytes and the request is
clamped to 128 bytes and the top of SRAM.

`CALLSTACK` scans pairs of bytes above the current stack pointer as AVR return
addresses. It only reports candidates whose preceding program word matches
`RCALL` or `ICALL`, or whose preceding two-word instruction matches `CALL`.
The default is 16 frames and the request is clamped to 32.

Because AVR code can store arbitrary data between return addresses, and because
interrupt return addresses are not preceded by a call instruction, `CALLSTACK`
is deliberately marked as heuristic. It is useful for normal C call chains but
is not a complete unwinder. `STACK` remains the authoritative raw view.

Each call-stack entry includes the stack address, return address, call-site
address, call type, and disassembly text for the call site.

## Example

```text
PAUSE
READ_REGS
STACK 48
CALLSTACK 12
DISASM PC 8
```
