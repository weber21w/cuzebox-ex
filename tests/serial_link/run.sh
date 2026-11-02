#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
TMP="${TMPDIR:-/tmp}/cuzebox-serial-link-test-$$"
CC_BIN="${CC:-gcc}"

cleanup() {
    rm -rf "$TMP"
}
trap cleanup EXIT

mkdir -p "$TMP/include/SDL2"
: > "$TMP/include/SDL2/SDL.h"

COMMON=(
    -std=gnu17 -O1 -ffunction-sections -fdata-sections
    -DTARGET_LINUX -DENABLE_ESP=1 -DFLAG_NOCONSOLE=1 -DHEADLESS=1
    -I"$TMP/include" -I"$ROOT"
)

"$CC_BIN" "${COMMON[@]}" -c "$ROOT/cu_esp_net.c" -o "$TMP/cu_esp_net.o"
"$CC_BIN" "${COMMON[@]}" -c "$ROOT/cu_uart.c" -o "$TMP/cu_uart.o"
"$CC_BIN" "${COMMON[@]}" \
    "$ROOT/tests/serial_link/tcp_serial_async_test.c" "$TMP/cu_esp_net.o" "$TMP/cu_uart.o" \
    -Wl,--gc-sections -pthread -o "$TMP/tcp_serial_async_test"
"$TMP/tcp_serial_async_test"

"$CC_BIN" "${COMMON[@]}" \
    "$ROOT/tests/serial_link/uart_timing_test.c" "$TMP/cu_uart.o" \
    -Wl,--gc-sections -o "$TMP/uart_timing_test"
"$TMP/uart_timing_test"

"$CC_BIN" "${COMMON[@]}" \
    "$ROOT/tests/serial_link/uart_passthrough_poll_test.c" "$TMP/cu_uart.o" \
    -Wl,--gc-sections -o "$TMP/uart_passthrough_poll_test"
"$TMP/uart_passthrough_poll_test"

"$CC_BIN" "${COMMON[@]}" \
    "$ROOT/tests/serial_link/esp_uart_hotpath_test.c" "$TMP/cu_uart.o" \
    -Wl,--gc-sections -o "$TMP/esp_uart_hotpath_test"
"$TMP/esp_uart_hotpath_test"

# The capture-profile trial also uses the measured ESP transmitter clock.
"$CC_BIN" "${COMMON[@]}" -DCU_ESP_AT_CAPTURE_PROFILE=1 -c "$ROOT/cu_uart.c" -o "$TMP/cu_uart_capture.o"
"$CC_BIN" "${COMMON[@]}" -DCU_ESP_AT_CAPTURE_PROFILE=1 \
    "$ROOT/tests/serial_link/esp_uart_hotpath_test.c" "$TMP/cu_uart_capture.o" \
    -Wl,--gc-sections -o "$TMP/esp_uart_capture_test"
"$TMP/esp_uart_capture_test"

python3 "$ROOT/tests/serial_link/uart_irq_mask_test.py"
python3 "$ROOT/tests/serial_link/connect_failure_test.py"
python3 "$ROOT/tests/serial_link/tcp_serial_source_audit.py"

"$CC_BIN" "${COMMON[@]}" -DENABLE_ESP_SOFTAP=1 \
    "$ROOT/tests/serial_link/esp_lan_policy_test.c" "$TMP/cu_esp_net.o" \
    "$ROOT/cu_esp_ap.c" "$ROOT/cu_esp_data.c" \
    -Wl,--gc-sections,--wrap=socket,--wrap=bind,--wrap=setsockopt,--wrap=getsockname,--wrap=fcntl,--wrap=close \
    -pthread -o "$TMP/esp_lan_policy_test"
"$TMP/esp_lan_policy_test"

printf '%s\n' 'serial link regression tests OK'
