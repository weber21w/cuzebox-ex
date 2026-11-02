# Current handoff navigation

Written November 1, 2026. The older file referenced by the parent `AGENTS.md`
was missing; this note supplies current navigation, not a reconstruction of it.

Read [DEVELOPMENT_HANDOFF.txt](DEVELOPMENT_HANDOFF.txt) before continuing the
UART work, Linux migration, performance comparisons, or preparing publication.
The UART fix is local and has not been pushed by this conversation.
The subsequent [PNG/cycle screenshot change](documentation/screenshots.md) is
also local. Its frame-cache overhead is not covered by the UART-only benchmark.

Then consult the relevant maintained documentation:

- [USART timing and interrupt behavior](documentation/serial.md)
- [Timer1 and GPIO build options](documentation/avr-hardware-options.md)
- [Timer1 compare behavior](documentation/timer1.md)
- [Interrupt regression coverage](tests/preferences/README.md)
- [Compact / Full Debug captures](documentation/input-capture-modes.md)
- [Native builds and feature flags](readme.md)
- [Native download packaging and writable data](documentation/releases.md)
- [Latest GitHub comparison and per-file review](documentation/prepush-review-2026-11-01.md)

The parent instruction's description of SEI/RETI delays as unfixed is stale
relative to current source/tests. The current README also describes the capture
modes as implemented. Verify changes against the current tree and tests.
The October 15 GitHub comparison document is historical, not current proof of
publication; confirm the actual Git root, remote and diff before any push.
