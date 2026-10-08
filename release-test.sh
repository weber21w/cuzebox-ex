#!/usr/bin/env bash
set -euo pipefail

EMU="${1:-./cuzebox}"
ROM="${2:-}"
PORT="${CZ_API_PORT:-24680}"
HOST="${CZ_API_HOST:-127.0.0.1}"
LOG="${CZ_API_LOG:-/tmp/cuzebox-api-test.log}"

if [[ -z "$ROM" ]]; then
	echo "usage: $0 <emulator-binary> <rom-file>" >&2
	exit 2
fi

"$EMU" "$ROM" >"$LOG" 2>&1 &
PID=$!
cleanup() {
	set +e
	python3 tools/api_test_runner.py --host "$HOST" --port "$PORT" --timeout 1 quit >/dev/null 2>&1 || true
	wait "$PID" >/dev/null 2>&1 || true
}
trap cleanup EXIT

python3 tools/api_test_runner.py --host "$HOST" --port "$PORT" wait-ready --ready-timeout 15 >/dev/null
python3 tools/api_test_runner.py --host "$HOST" --port "$PORT" get-rom-info
python3 tools/api_test_runner.py --host "$HOST" --port "$PORT" run-frames 10 --wait --run-timeout 10
python3 tools/api_test_runner.py --host "$HOST" --port "$PORT" get-state

cat <<'MSG'
Smoke test passed. Extend this by adding per-ROM assertions, for example:
  python3 tools/api_test_runner.py assert-mem SRAM 0x0152 0x20
  python3 tools/api_test_runner.py set-input 0 0x0080 2
  python3 tools/api_test_runner.py run-frames 60 --wait
MSG
