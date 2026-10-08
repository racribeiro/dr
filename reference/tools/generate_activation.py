#!/usr/bin/env python3
"""Validate the supplied DUML fixture and emit the SDK's passive bootstrap table.

Prints to stdout; the runtime never reads this file or needs Python.
"""

import argparse
from pathlib import Path
import re
import sys


def crc(data, polynomial, initial):
    result = initial
    for byte in data:
        result ^= byte
        for _ in range(8):
            result = (result >> 1) ^ (polynomial if result & 1 else 0)
    return result


def load_profile(path):
    frames = []
    kinds = set()
    for line_number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        if not line.strip() or line.startswith("#"):
            continue
        match = re.fullmatch(r"(\d{2}) ([0-9a-f]{2})/([0-9a-f]{2}) ([0-9a-f]+)", line)
        if not match:
            raise ValueError(f"line {line_number}: malformed profile entry")
        index = int(match[1])
        kind = (int(match[2], 16), int(match[3], 16))
        frame = bytes.fromhex(match[4])
        if index != len(frames) or kind in kinds:
            raise ValueError(f"line {line_number}: duplicate/out-of-order entry")
        if not 13 <= len(frame) <= 1023 or frame[0] != 0x55 or frame[2] >> 2 != 1:
            raise ValueError(f"line {line_number}: invalid DUML header")
        if frame[1] | ((frame[2] & 3) << 8) != len(frame):
            raise ValueError(f"line {line_number}: invalid DUML length")
        if (frame[9], frame[10]) != kind:
            raise ValueError(f"line {line_number}: command label mismatch")
        if crc(frame[:3], 0x8c, 0x0a) ^ 0xff != frame[3]:
            raise ValueError(f"line {line_number}: invalid CRC-8")
        if crc(frame[:-2], 0x8408, 0x3692) != int.from_bytes(frame[-2:], "little"):
            raise ValueError(f"line {line_number}: invalid CRC-16")
        if kind[0] in (1, 4):
            raise ValueError(f"line {line_number}: movement/control set in passive profile")
        kinds.add(kind)
        frames.append(frame)
    if len(frames) != 55 or (frames[0][9], frames[0][10]) != (0, 1):
        raise ValueError("expected 55 frames with 00/01 heartbeat first")
    return frames


def generate(frames):
    retained = [f for f in frames if (f[9], f[10]) not in
                ((0x18, 0x47), (7, 7), (7, 0x0c), (7, 0x0e))]
    if len(retained) != 51:
        raise ValueError("expected 51 frames after excluding liveview/credential queries")
    lines = [
        "/* GENERATED from reference/activation/app-profile-55.txt.",
        " * See reference/tools/generate_activation.py and docs/INTEGRATION.md.",
        " * 51 of 55 supplied kinds: no sets 01/04, credential GETs or liveview.",
        " * Outer DUML sequence/CRCs are regenerated; nested values stay opaque. */",
        "static const activation_template_t activation_templates[] = {",
    ]
    for frame in retained:
        payload = frame[11:-2]
        if len(payload) > 132:
            raise ValueError("payload exceeds activation_template_t capacity")
        fields = ", ".join(f"0x{frame[i]:02x}" for i in (4, 5, 8, 9, 10))
        data = ",".join(f"0x{byte:02x}" for byte in payload) or "0"
        lines.append(f"    {{{fields}, {len(payload)}, {{{data}}}}},")
    lines.append("};")
    return "\n".join(lines) + "\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("profile", nargs="?", type=Path,
                        default=Path(__file__).resolve().parents[1] / "activation/app-profile-55.txt")
    parser.add_argument("--check", type=Path, help="verify a checked-in generated table without changing it")
    args = parser.parse_args()
    try:
        output = generate(load_profile(args.profile))
        if args.check:
            if args.check.read_text(encoding="utf-8") != output:
                raise ValueError(f"generated table differs from {args.check}")
            print("activation table matches validated source profile (51 retained frames)")
        else:
            sys.stdout.write(output)
    except (OSError, ValueError) as error:
        parser.exit(1, f"activation profile: {error}\n")


if __name__ == "__main__":
    main()
