#!/usr/bin/env python3
"""Stream decoded webOS TV mixer PCM through official Developer Mode tooling.

Prerequisites:
- webOS CLI installed (ares-novacom)
- TV registered as a Developer Mode device
- arecord available on the TV

No pairing key, IP address, or Developer Mode passphrase is stored here.
"""

from __future__ import annotations

import argparse
import base64
import re
import subprocess
import sys
from pathlib import Path

SAFE_TOKEN = re.compile(r"^[A-Za-z0-9_,:.-]+$")
BASE64_LINE = re.compile(r"^[A-Za-z0-9+/]+={0,2}$")


def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser()
    p.add_argument("--device", default="tv", help="ares device name (default: tv)")
    p.add_argument("--pcm-device", default="hw:1,2", help="remote ALSA PCM device")
    p.add_argument("--rate", type=int, default=48000)
    p.add_argument("--channels", type=int, default=2)
    p.add_argument("--duration", type=int, default=0, help="seconds; 0 = continuous")
    p.add_argument("--output", type=Path, help="raw S32_LE output file; default = stdout")
    p.add_argument("--ares-novacom", default="ares-novacom")
    return p.parse_args()


def main() -> int:
    args = parse_args()
    if not SAFE_TOKEN.fullmatch(args.pcm_device):
        raise SystemExit("unsafe --pcm-device")
    if args.rate <= 0 or args.channels <= 0 or args.duration < 0:
        raise SystemExit("invalid numeric argument")

    duration = f" -d {args.duration}" if args.duration else ""
    remote_cmd = (
        f"arecord -q -D {args.pcm_device} -f S32_LE "
        f"-c {args.channels} -r {args.rate}{duration} -t raw | base64"
    )

    proc = subprocess.Popen(
        [args.ares_novacom, "-d", args.device, "-r", remote_cmd],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        bufsize=1,
    )

    out = args.output.open("wb") if args.output else sys.stdout.buffer
    written = 0
    try:
        assert proc.stdout is not None
        for line in proc.stdout:
            s = line.strip()
            if not s or s.startswith("[Info]"):
                continue
            if not BASE64_LINE.fullmatch(s):
                print(f"ignoring non-base64 output: {s}", file=sys.stderr)
                continue
            chunk = base64.b64decode(s, validate=True)
            out.write(chunk)
            out.flush()
            written += len(chunk)
    except KeyboardInterrupt:
        proc.terminate()
    finally:
        if args.output:
            out.close()

    rc = proc.wait()
    if proc.stderr:
        err = proc.stderr.read().strip()
        if err:
            print(err, file=sys.stderr)

    print(f"captured {written} bytes of S32_LE PCM", file=sys.stderr)
    return rc


if __name__ == "__main__":
    raise SystemExit(main())
