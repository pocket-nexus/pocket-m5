#!/usr/bin/env python3
"""Capture the real Tab5 framebuffer or inject a tap through USB JTAG.

Requires CONFIG_TAB5_UI_VALIDATION=y and the matching firmware ELF. Neither
core is halted. OpenOCD is started on localhost and stopped after each action.
Only Python's standard library is used; output PNGs and logs are run artifacts.
"""
import argparse
import datetime
import glob
import hashlib
import json
import os
from pathlib import Path
import shutil
import socket
import struct
import subprocess
import tempfile
import time
import zlib


def tool(name, pattern):
    found = shutil.which(name)
    if found:
        return Path(found)
    choices = sorted(glob.glob(os.path.expanduser(pattern)))
    if not choices:
        raise RuntimeError(f"{name} is unavailable; activate the ESP-IDF environment")
    return Path(choices[-1])


def png_rgb565(data, width, height, stride):
    rows = bytearray()
    for y in range(height):
        rows.append(0)
        for x in range(width):
            offset = y * stride + 2 * x
            pixel = data[offset] | (data[offset + 1] << 8)
            r, g, b = pixel >> 11, (pixel >> 5) & 63, pixel & 31
            rows.extend(((r << 3) | (r >> 2), (g << 2) | (g >> 4),
                         (b << 3) | (b >> 2)))

    def chunk(kind, payload):
        return (struct.pack(">I", len(payload)) + kind + payload +
                struct.pack(">I", zlib.crc32(kind + payload)))

    return (b"\x89PNG\r\n\x1a\n" +
            chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)) +
            chunk(b"IDAT", zlib.compress(rows)) + chunk(b"IEND", b""))


class OpenOCD:
    def __init__(self, args):
        executable = Path(args.openocd) if args.openocd else tool(
            "openocd", "~/.espressif/tools/openocd-esp32/*/openocd-esp32/bin/openocd")
        scripts = Path(args.scripts) if args.scripts else executable.parent.parent / "share/openocd/scripts"
        self.log = Path(args.log)
        self.log.parent.mkdir(parents=True, exist_ok=True)
        self.output = self.log.open("w")
        # Refuse to connect to somebody else's debugger session.
        probe = socket.socket()
        try:
            probe.bind(("127.0.0.1", args.port))
        finally:
            probe.close()
        self.process = subprocess.Popen([
            str(executable), "-s", str(scripts), "-f", "board/esp32p4-builtin.cfg",
            "-c", f"bindto 127.0.0.1; tcl_port {args.port}; telnet_port disabled; gdb_port disabled",
        ], stdout=self.output, stderr=subprocess.STDOUT)
        self.socket = None
        try:
            deadline = time.monotonic() + 8
            while time.monotonic() < deadline:
                if self.process.poll() is not None:
                    raise RuntimeError(f"OpenOCD exited; see {self.log}")
                try:
                    self.socket = socket.create_connection(("127.0.0.1", args.port), timeout=.2)
                    break
                except OSError:
                    time.sleep(.05)
            if not self.socket:
                raise RuntimeError("OpenOCD did not open its TCL endpoint")
            self.socket.settimeout(30)
            targets = self.command("targets")
            if targets.count("running") != 2:
                raise RuntimeError(f"Both cores must be running; no halt/resume is performed: {targets}")
        except BaseException:
            self.close()
            raise

    def command(self, text):
        self.socket.sendall(text.encode() + b"\x1a")
        result = bytearray()
        while not result.endswith(b"\x1a"):
            part = self.socket.recv(4096)
            if not part:
                raise RuntimeError("OpenOCD disconnected")
            result.extend(part)
        return result[:-1].decode()

    def read(self, address, count=1):
        result = self.command(f"read_memory 0x{address:x} 32 {count}")
        try:
            values = [int(word, 0) for word in result.split()]
        except ValueError as error:
            raise RuntimeError(f"OpenOCD memory read failed: {result}") from error
        if len(values) != count:
            raise RuntimeError(f"OpenOCD returned {len(values)} of {count} words")
        return values

    def write(self, address, values):
        result = self.command(f"write_memory 0x{address:x} 32 {{{' '.join(str(v) for v in values)}}}")
        if result:
            raise RuntimeError(f"OpenOCD memory write failed: {result}")

    def close(self):
        if self.socket:
            self.socket.close()
        if self.process.poll() is None:
            self.process.terminate()
            self.process.wait(timeout=5)
        self.output.close()


def main():
    started_at = datetime.datetime.now(datetime.timezone.utc).isoformat()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--elf", required=True, type=Path)
    parser.add_argument("--openocd")
    parser.add_argument("--scripts")
    parser.add_argument("--nm")
    parser.add_argument("--port", type=int, default=16666)
    parser.add_argument("--timeout", type=float, default=5)
    parser.add_argument("--log", default=".pocket-build/validation/worker/ui/openocd.log")
    actions = parser.add_subparsers(dest="action", required=True)
    actions.add_parser("status")
    capture = actions.add_parser("capture")
    capture.add_argument("output", type=Path)
    tap = actions.add_parser("tap")
    tap.add_argument("x", type=int)
    tap.add_argument("y", type=int)
    tap.add_argument("--frames", type=int, default=3)
    args = parser.parse_args()
    nm = Path(args.nm) if args.nm else tool(
        "riscv32-esp-elf-nm", "~/.espressif/tools/riscv32-esp-elf/*/riscv32-esp-elf/bin/riscv32-esp-elf-nm")
    symbols = subprocess.check_output([str(nm), str(args.elf)], text=True)
    matches = [line.split()[0] for line in symbols.splitlines()
               if line.split()[-1:] == ["tab5_validation_mailbox"]]
    if len(matches) != 1:
        raise RuntimeError("ELF has no unique validation mailbox; build with CONFIG_TAB5_UI_VALIDATION=y")
    address = int(matches[0], 16)
    ocd = OpenOCD(args)
    try:
        words = ocd.read(address, 20)
        if words[:3] != [0x54414235, 1, 20]:
            raise RuntimeError("Mailbox identity mismatch; verify the ELF matches installed firmware")
        result = {"action": args.action, "mailbox": hex(address),
                  "started_at_utc": started_at,
                  "elf_sha256": hashlib.sha256(args.elf.read_bytes()).hexdigest(),
                  "logical_size": words[16:18], "polls": words[18], "flushes": words[19]}
        if args.action != "status":
            req, done, status = (3, 4, 5) if args.action == "capture" else (10, 11, 12)
            if words[req] != words[done]:
                raise RuntimeError("A previous debugger request is still pending")
            sequence = (words[req] + 1) & 0xffffffff or 1
            if args.action == "tap":
                if not (0 <= args.x < words[16] and 0 <= args.y < words[17] and 1 <= args.frames <= 30):
                    raise RuntimeError("Tap must fit the logical viewport and hold for 1..30 frames")
                ocd.write(address + 13 * 4, [args.x, args.y, args.frames])
            ocd.write(address + req * 4, [sequence])
            deadline = time.monotonic() + args.timeout
            while ocd.read(address + done * 4)[0] != sequence:
                if time.monotonic() >= deadline:
                    raise RuntimeError("Request timed out; capture requires a subsequent rendered frame")
                time.sleep(.02)
            outcome = ocd.read(address + status * 4)[0]
            if outcome:
                raise RuntimeError(f"Firmware rejected request with status {outcome:#x}")
            result["sequence"] = sequence
            if args.action == "tap":
                result.update(x=args.x, y=args.y, frames=args.frames)
            else:
                pixels, width, height, stride = words[6:10]
                if (width, height, stride) != (720, 1280, 1440):
                    raise RuntimeError("Capture geometry differs from the supported Tab5 RGB565 layout")
                with tempfile.TemporaryDirectory(prefix="tab5-capture-") as directory:
                    raw = Path(directory) / "frame.rgb565"
                    response = ocd.command(f"dump_image {{{raw}}} 0x{pixels:x} {height * stride}")
                    if not raw.exists() or raw.stat().st_size != height * stride:
                        raise RuntimeError(f"Framebuffer dump failed: {response}")
                    data = raw.read_bytes()
                args.output.parent.mkdir(parents=True, exist_ok=True)
                args.output.write_bytes(png_rgb565(data, width, height, stride))
                result.update(output=str(args.output.resolve()), width=width, height=height,
                              rgb565_sha256=hashlib.sha256(data).hexdigest())
        result["targets"] = ocd.command("targets")
        result["finished_at_utc"] = datetime.datetime.now(datetime.timezone.utc).isoformat()
        print(json.dumps(result, indent=2))
    finally:
        ocd.close()


if __name__ == "__main__":
    main()
