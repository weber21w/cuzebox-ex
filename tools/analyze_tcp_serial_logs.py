#!/usr/bin/env python3
"""Summarize one or two CUzeBox TCP Serial diagnostic CSV logs."""

from __future__ import annotations

import argparse
import csv
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable


NUMERIC_FIELDS = {
    "ms",
    "last_error",
    "last_send_error",
    "last_recv_error",
    "state",
    "socket",
    "listener",
    "txq",
    "rxq",
    "txq_hi",
    "rxq_hi",
    "socket_rx_pending",
    "avr_tx",
    "avr_rx",
    "socket_tx",
    "socket_rx",
    "send_calls",
    "recv_calls",
    "send_would_block",
    "recv_would_block",
    "send_errors",
    "recv_errors",
    "send_zero",
    "recv_zero",
    "connects",
    "disconnects",
    "state_changes",
    "service_calls",
    "gap_gt_50",
    "gap_gt_250",
    "max_gap_ms",
    "connected_since_ms",
    "last_state_change_ms",
    "last_avr_tx_ms",
    "last_avr_rx_ms",
    "last_socket_tx_ms",
    "last_socket_rx_ms",
    "detail",
}

STATE_NAMES = {
    0: "DISCONNECTED",
    1: "CONNECTING",
    2: "CONNECTED",
    3: "RETRY_WAIT",
    4: "LISTENING",
}


@dataclass
class LogData:
    path: Path
    rows: list[dict[str, object]]

    @property
    def first(self) -> dict[str, object]:
        return self.rows[0]

    @property
    def last(self) -> dict[str, object]:
        return self.rows[-1]


def load_log(path: Path) -> LogData:
    rows: list[dict[str, object]] = []
    with path.open(newline="", encoding="utf-8") as f:
        reader = csv.DictReader(f)
        missing = {"ms", "event", "state", "avr_tx", "avr_rx", "socket_tx", "socket_rx"} - set(reader.fieldnames or ())
        if missing:
            raise ValueError(f"{path}: missing columns: {', '.join(sorted(missing))}")
        for raw in reader:
            row: dict[str, object] = dict(raw)
            for field in NUMERIC_FIELDS:
                value = raw.get(field, "")
                try:
                    row[field] = int(value or 0)
                except ValueError:
                    row[field] = 0
            rows.append(row)
    if not rows:
        raise ValueError(f"{path}: no diagnostic rows")
    return LogData(path=path, rows=rows)


def delta(first: dict[str, object], last: dict[str, object], field: str) -> int:
    return int(last[field]) - int(first[field])


def find_plateaus(rows: list[dict[str, object]], source: str, sink: str, minimum_ms: int = 250) -> list[tuple[int, int, int]]:
    """Find periods where source advanced but sink did not."""
    result: list[tuple[int, int, int]] = []
    start = 0
    for i in range(1, len(rows)):
        source_growth = int(rows[i][source]) - int(rows[start][source])
        sink_growth = int(rows[i][sink]) - int(rows[start][sink])
        elapsed = int(rows[i]["ms"]) - int(rows[start]["ms"])
        if sink_growth != 0:
            if elapsed >= minimum_ms and source_growth > 0:
                result.append((int(rows[start]["ms"]), elapsed, source_growth))
            start = i
    if len(rows) > 1:
        elapsed = int(rows[-1]["ms"]) - int(rows[start]["ms"])
        source_growth = int(rows[-1][source]) - int(rows[start][source])
        sink_growth = int(rows[-1][sink]) - int(rows[start][sink])
        if elapsed >= minimum_ms and source_growth > 0 and sink_growth == 0:
            result.append((int(rows[start]["ms"]), elapsed, source_growth))
    return result


def summarize(log: LogData) -> str:
    first, last = log.first, log.last
    duration = int(last["ms"]) - int(first["ms"])
    events: dict[str, int] = {}
    for row in log.rows:
        event = str(row.get("event", ""))
        events[event] = events.get(event, 0) + 1

    avr_to_socket_stalls = find_plateaus(log.rows, "avr_tx", "socket_tx")
    socket_to_avr_stalls = find_plateaus(log.rows, "socket_rx", "avr_rx")

    lines = [
        f"{log.path.name}",
        f"  role: {last.get('mode', '')}",
        f"  duration: {duration} ms, rows: {len(log.rows)}",
        f"  final state: {STATE_NAMES.get(int(last['state']), str(last['state']))}, last error: {last['last_error']}",
        f"  bytes AVR->backend/socket: {delta(first, last, 'avr_tx')} / {delta(first, last, 'socket_tx')}",
        f"  bytes socket/backend->AVR: {delta(first, last, 'socket_rx')} / {delta(first, last, 'avr_rx')}",
        f"  queue high-water TX/RX: {last['txq_hi']} / {last['rxq_hi']}",
        f"  would-block send/recv: {last['send_would_block']} / {last['recv_would_block']}",
        f"  errors send/recv: {last['send_errors']} / {last['recv_errors']}",
        f"  last OS send/recv error: {last.get('last_send_error', 0)} / {last.get('last_recv_error', 0)}",
        f"  zero returns send/recv: {last['send_zero']} / {last['recv_zero']}",
        f"  connects/disconnects/state changes: {last['connects']} / {last['disconnects']} / {last['state_changes']}",
        f"  service max gap: {last['max_gap_ms']} ms; >50 ms: {last['gap_gt_50']}; >250 ms: {last['gap_gt_250']}",
        "  event counts: " + ", ".join(f"{k}={v}" for k, v in sorted(events.items())),
    ]

    if avr_to_socket_stalls:
        worst = max(avr_to_socket_stalls, key=lambda x: x[1])
        lines.append(
            f"  AVR->socket plateau: {len(avr_to_socket_stalls)} interval(s), worst {worst[1]} ms while {worst[2]} AVR byte(s) accumulated"
        )
    else:
        lines.append("  AVR->socket plateau: none >=250 ms")

    if socket_to_avr_stalls:
        worst = max(socket_to_avr_stalls, key=lambda x: x[1])
        lines.append(
            f"  socket->AVR plateau: {len(socket_to_avr_stalls)} interval(s), worst {worst[1]} ms while {worst[2]} socket byte(s) accumulated"
        )
    else:
        lines.append("  socket->AVR plateau: none >=250 ms")

    lines.append("  last events:")
    for row in log.rows[-12:]:
        lines.append(
            "    "
            f"{row['ms']} {row.get('event', ''):<10} state={STATE_NAMES.get(int(row['state']), row['state'])} "
            f"txq/rxq={row['txq']}/{row['rxq']} avr={row['avr_tx']}/{row['avr_rx']} "
            f"sock={row['socket_tx']}/{row['socket_rx']} err={row['last_error']} "
            f"reason={row.get('reason', '')} detail={row.get('detail', 0)}"
        )
    return "\n".join(lines)


def paired_summary(logs: Iterable[LogData]) -> str:
    data = list(logs)
    if len(data) != 2:
        return ""
    a, b = data
    start = max(int(a.first["ms"]), int(b.first["ms"]))
    end = min(int(a.last["ms"]), int(b.last["ms"]))
    a_tx = delta(a.first, a.last, "socket_tx")
    a_rx = delta(a.first, a.last, "socket_rx")
    b_tx = delta(b.first, b.last, "socket_tx")
    b_rx = delta(b.first, b.last, "socket_rx")
    a_avr_tx = delta(a.first, a.last, "avr_tx")
    a_avr_rx = delta(a.first, a.last, "avr_rx")
    b_avr_tx = delta(b.first, b.last, "avr_tx")
    b_avr_rx = delta(b.first, b.last, "avr_rx")
    lines = [
        "Paired overlap",
        f"  common wall-clock interval: {max(0, end - start)} ms",
        f"  socket bytes A->B sent/received: {a_tx} / {b_rx} (difference {a_tx - b_rx:+d})",
        f"  socket bytes B->A sent/received: {b_tx} / {a_rx} (difference {b_tx - a_rx:+d})",
        f"  AVR/backend A TX/RX: {a_avr_tx} / {a_avr_rx}",
        f"  AVR/backend B TX/RX: {b_avr_tx} / {b_avr_rx}",
        f"  maximum service gap A/B: {a.last['max_gap_ms']} / {b.last['max_gap_ms']} ms",
        f"  final state A/B: {STATE_NAMES.get(int(a.last['state']), a.last['state'])} / {STATE_NAMES.get(int(b.last['state']), b.last['state'])}",
        f"  last socket errors A send/recv: {a.last.get('last_send_error', 0)} / {a.last.get('last_recv_error', 0)}",
        f"  last socket errors B send/recv: {b.last.get('last_send_error', 0)} / {b.last.get('last_recv_error', 0)}",
    ]

    conclusions: list[str] = []
    if a_avr_tx > a_tx:
        conclusions.append("A accepted more AVR bytes than it handed to the socket; inspect A TXQ/would-block plateaus.")
    if b_avr_tx > b_tx:
        conclusions.append("B accepted more AVR bytes than it handed to the socket; inspect B TXQ/would-block plateaus.")
    if a_rx > a_avr_rx:
        conclusions.append("A received socket bytes that were not consumed by the emulated AVR UART.")
    if b_rx > b_avr_rx:
        conclusions.append("B received socket bytes that were not consumed by the emulated AVR UART.")
    if abs(a_tx - b_rx) > 16 or abs(b_tx - a_rx) > 16:
        conclusions.append("Socket send/receive totals differ materially; the logs may end at different times or bytes were still queued in the OS TCP stack.")
    if int(a.last["max_gap_ms"]) > 50 or int(b.last["max_gap_ms"]) > 50:
        conclusions.append("At least one CUzeBox instance left the TCP Serial route unserviced for over 50 ms; this can produce visible lockstep waves.")
    if int(a.last["state"]) != 2 or int(b.last["state"]) != 2:
        conclusions.append("At least one socket left CONNECTED; inspect the final STATE/RECV_ZERO/SEND_ERROR/RECV_ERROR rows.")
    if not conclusions:
        conclusions.append("No obvious transport-stage mismatch is visible in the final counters; inspect the timestamped plateau and final-event sections.")

    lines.append("  Automated interpretation:")
    lines.extend(f"    - {item}" for item in conclusions)
    lines.extend(
        [
            "  Interpretation guide:",
            "    AVR TX grows but socket TX stops: CUzeBox backend send path stalled.",
            "    Socket RX grows but AVR RX stops: emulated UART delivery stalled.",
            "    Both socket TX and RX stop with a STATE/RECV_ZERO/error event: TCP connection closed.",
            "    Both socket TX and RX stop without a state/error event while SERVICE_GAP rows appear: emulator scheduling/service stall.",
        ]
    )
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("logs", nargs="+", type=Path, help="one or two tcp-serial-*.csv files")
    parser.add_argument("-o", "--output", type=Path)
    args = parser.parse_args()

    logs = [load_log(path) for path in args.logs]
    report = "\n\n".join(summarize(log) for log in logs)
    pair = paired_summary(logs)
    if pair:
        report += "\n\n" + pair
    report += "\n"

    if args.output:
        args.output.write_text(report, encoding="utf-8")
    print(report, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
