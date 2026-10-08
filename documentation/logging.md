# Messages and debugging output

In **CONFIG > DISPLAY**, **ON-SCREEN MESSAGES** controls the transient messages
at the bottom left. Turning it off immediately clears them. Console diagnostics
can still be enabled independently with **LOG VERBOSITY**. Both settings are
saved in `config.cfg` and restored on startup or Reload.

| Level | Output |
| --- | --- |
| Off (0) | No application log messages or transient UI notices. |
| Errors (1) | Failures, including file write/close errors. |
| Info (2, default) | Errors, file-operation summaries and ordinary UI notices. |
| Debug (3) | Info plus routine emulator, network and device diagnostics. |
| Trace (4) | All available diagnostics, including compiled-in UART/AT traces. |

Info hides routine diagnostic chatter during gameplay. Successful file writes
are reported once when the file channel closes/flushes, with the accumulated
byte count. A sector write does not produce its own message. SD creation,
resize, rename, delete and directory operations are reported when they change
the host filesystem. A file held open by the guest may not be reported until
it is closed, another file is opened on that channel, or the emulator exits.
UI actions such as saving a screenshot/config/profile keep their existing
completion notices. File channels report host errors without changing the
guest's write permissions or the `SdAllowNewFiles` policy.

At Info and Debug, identical diagnostics are suppressed for one wall-clock
second. Routine output is limited to four messages per second at Info and
sixteen at Debug. Distinct errors bypass that burst limit. The next diagnostic
after a window expires prints a console summary of suppressed messages. The
overlay also coalesces identical lines and has a bounded message queue.
Trace bypasses diagnostic rate limits for active troubleshooting and can
produce substantial output. Logs omitted by build flags remain unavailable.

The verbosity filter covers the application's logging API and transient UI
notices. Periodic console performance status requires Debug; window captions
remain enabled. Third-party library output and bytes deliberately emitted by
emulated software to its console port are separate outputs.
Explicitly armed debugger traces retain their own
capture controls and buffers.

For developers, `print_error` reports a failure, `print_info` reports a useful
operation summary, `print_message` is routine Debug output, and `print_trace`
is detailed Trace output. The compile-time console/headless flags can still
remove these calls. Do not log individual sectors/bytes at Info or log secrets.
