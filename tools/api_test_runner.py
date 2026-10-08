#!/usr/bin/env python3
import argparse
import json
import sys
from typing import Any, Dict


from cuzebox_api import CUzeBoxApi


def print_json(obj: Dict[str, Any]) -> None:
	print(json.dumps(obj, indent=2, sort_keys=True))


def main() -> int:
	parser = argparse.ArgumentParser(description="CUzeBox API test runner")
	parser.add_argument("--host", default="127.0.0.1")
	parser.add_argument("--port", type=int, default=24680)
	parser.add_argument("--timeout", type=float, default=5.0)
	sub = parser.add_subparsers(dest="cmd", required=True)

	p = sub.add_parser("wait-ready")
	p.add_argument("--ready-timeout", type=float, default=10.0)

	sub.add_parser("ping")
	sub.add_parser("get-rom-info")
	sub.add_parser("get-state")
	sub.add_parser("get-frame")
	p = sub.add_parser("wait-frame")
	p.add_argument("target")
	p.add_argument("timeout_frames", nargs="?", default=None)
	sub.add_parser("read-regs")

	p = sub.add_parser("read-mem")
	p.add_argument("region")
	p.add_argument("addr")
	p.add_argument("len", nargs="?", default="1")

	p = sub.add_parser("assert-mem")
	p.add_argument("region")
	p.add_argument("addr")
	p.add_argument("expected")

	p = sub.add_parser("wait-mem")
	p.add_argument("region")
	p.add_argument("addr")
	p.add_argument("op")
	p.add_argument("value")
	p.add_argument("timeout_frames", nargs="?", default=None)

	p = sub.add_parser("read-symbol")
	p.add_argument("name")

	p = sub.add_parser("write-symbol")
	p.add_argument("name")
	p.add_argument("value")

	p = sub.add_parser("set-input")
	p.add_argument("player")
	p.add_argument("mask")
	p.add_argument("frames")

	p = sub.add_parser("queue-input")
	p.add_argument("player")
	p.add_argument("pairs", nargs="+")

	p = sub.add_parser("clear-input")
	p.add_argument("player")

	p = sub.add_parser("run-frames")
	p.add_argument("count")
	p.add_argument("--wait", action="store_true")
	p.add_argument("--run-timeout", type=float, default=10.0)

	p = sub.add_parser("load-rom")
	p.add_argument("mode", choices=["WAIT", "RUN", "wait", "run"])
	p.add_argument("path")

	p = sub.add_parser("screenshot")
	p.add_argument("path", nargs="?", default=None)

	sub.add_parser("pause")
	sub.add_parser("resume")
	sub.add_parser("step-frame")
	sub.add_parser("reset")
	sub.add_parser("quit")
	sub.add_parser("help")

	args = parser.parse_args()
	api = CUzeBoxApi(args.host, args.port, args.timeout)

	try:
		if args.cmd == "wait-ready":
			print_json(api.wait_ready(args.ready_timeout))
			return 0

		api.connect()
		if args.cmd == "ping":
			resp = api.command("PING")
		elif args.cmd == "get-rom-info":
			resp = api.command("GET_ROM_INFO")
		elif args.cmd == "get-state":
			resp = api.command("GET_STATE")
		elif args.cmd == "get-frame":
			resp = api.command("GET_FRAME")
		elif args.cmd == "wait-frame":
			cmd = f"WAIT_FRAME {args.target}"
			if args.timeout_frames is not None:
				cmd += f" {args.timeout_frames}"
			resp = api.command(cmd)
		elif args.cmd == "read-regs":
			resp = api.command("READ_REGS")
		elif args.cmd == "read-mem":
			resp = api.command(f"READ_MEM {args.region} {args.addr} {args.len}")
		elif args.cmd == "assert-mem":
			resp = api.command(f"READ_MEM {args.region} {args.addr} 1")
			values = resp.get("values", [])
			expected = int(args.expected, 0)
			actual = int(values[0]) if values else None
			print_json(resp)
			if actual != expected:
				print(f"assert-mem failed: expected {expected:#x}, got {actual!r}", file=sys.stderr)
				return 2
			return 0
		elif args.cmd == "wait-mem":
			cmd = f"WAIT_MEM {args.region} {args.addr} {args.op} {args.value}"
			if args.timeout_frames is not None:
				cmd += f" {args.timeout_frames}"
			resp = api.command(cmd)
		elif args.cmd == "read-symbol":
			resp = api.command(f"READ_SYMBOL {args.name}")
		elif args.cmd == "write-symbol":
			resp = api.command(f"WRITE_SYMBOL {args.name} {args.value}")
		elif args.cmd == "set-input":
			resp = api.command(f"SET_INPUT {args.player} {args.mask} {args.frames}")
		elif args.cmd == "queue-input":
			resp = api.command("QUEUE_INPUT " + args.player + " " + " ".join(args.pairs))
		elif args.cmd == "clear-input":
			resp = api.command(f"CLEAR_INPUT {args.player}")
		elif args.cmd == "run-frames":
			resp = api.command(f"RUN_FRAMES {args.count}")
			if args.wait:
				resp = api.wait_run_complete(args.run_timeout)
		elif args.cmd == "load-rom":
			resp = api.command(f"LOAD_ROM {args.mode} {args.path}")
		elif args.cmd == "screenshot":
			resp = api.command("SCREENSHOT" if args.path is None else f"SCREENSHOT {args.path}")
		elif args.cmd == "pause":
			resp = api.command("PAUSE")
		elif args.cmd == "resume":
			resp = api.command("RESUME")
		elif args.cmd == "step-frame":
			resp = api.command("STEP_FRAME")
		elif args.cmd == "reset":
			resp = api.command("RESET")
		elif args.cmd == "quit":
			resp = api.command("QUIT")
		elif args.cmd == "help":
			resp = api.command("HELP")
		else:
			raise RuntimeError(f"Unhandled command {args.cmd}")
		print_json(resp)
		return 0
	except Exception as exc:  # noqa: BLE001
		print(f"error: {exc}", file=sys.stderr)
		return 1
	finally:
		api.close()


if __name__ == "__main__":
	raise SystemExit(main())
