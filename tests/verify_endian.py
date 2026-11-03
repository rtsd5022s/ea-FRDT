#!/usr/bin/env python3
"""Deterministic endian / width geometry verifier for ea-FRDT raw dumps.

Usage: python tests/verify_endian.py <sample-dir>

Invariant checks (no dependence on absolute record count; record count varies
with the -t timeout and host speed, so only geometry, byte-order relation and
head raw bits are asserted):
  * every sample exists and is non-empty
  * size is an exact multiple of the record width
  * raw streams carry NO v2 magic (EAFRDT2)
  * rev pair: head BE == reverse(head LE); common-prefix records are byte-reversed
  * same pair (1-byte wide): BE == LE byte-for-byte (endian has no effect)
  * native f32/f64 head raw bits are equal under opposite decode; f32 value equal

Exit code: 0 = TOTAL_FAILS=0; 1 = at least one FAIL.
"""
import os
import sys
import struct
import math

MAGIC = b"EAFRDT2"

# (le_file, be_file, width_bytes, description, direction: rev | same)
PAIRS = [
    ("f32_le.bin",      "f32_be.bin",      4, "native drive.h sizeof(float)=4",          "rev"),
    ("f64_le.bin",      "f64_be.bin",      8, "native drive.h sizeof(double)=8",         "rev"),
    ("tf32_nat_le.bin", "tf32_nat_be.bin", 4, "engine_sim natural aligned 4B",            "rev"),
    ("bf16_nat_le.bin", "bf16_nat_be.bin", 2, "engine_sim natural aligned 2B",            "rev"),
    ("t32pk_le.bin",    "t32pk_be.bin",    3, "engine_sim packed tight 3B odd width",    "rev"),
    ("fp8_le.bin",      "fp8_be.bin",      1, "engine_sim natural 1B (no endian effect)", "same"),
    ("x87_le.bin",      "x87_be.bin",     16, "independent x87 fixed 16B slot (10B valid + 6B zero)", "rev"),
    ("fp128_le.bin",    "fp128_be.bin",   16, "independent fp128 fixed 16B dump_cb",      "rev"),
    ("dec32_le.bin",    "dec32_be.bin",    4, "decnum BID 32-bit 4B dump_cb",             "rev"),
    ("dpd32_le.bin",    "dpd32_be.bin",    4, "dpd32 tight 32-bit 4B dump_cb",            "rev"),
    ("fp16_le.bin",     "fp16_be.bin",     2, "native drive.h sizeof(_Float16)=2",        "rev"),
    ("dec64_le.bin",    "dec64_be.bin",    8, "decnum BID Decimal64 fixed 8B dump_cb",    "rev"),
    ("dec128_le.bin",   "dec128_be.bin",  16, "BigDec BID128 fixed 16B dump_cb",          "rev"),
    ("dpd64_le.bin",    "dpd64_be.bin",    8, "engine_dpd DPD64 fixed 8B dump_cb",        "rev"),
    ("dpd128_le.bin",   "dpd128_be.bin",  16, "engine_dpd DPD128 fixed 16B dump_cb",      "rev"),
]


def main():
    sample_dir = sys.argv[1]
    fails = []

    def rd(name):
        path = os.path.join(sample_dir, name)
        return open(path, "rb").read() if os.path.exists(path) else None

    def check(cond, msg):
        print(("PASS " if cond else "FAIL ") + msg)
        if not cond:
            fails.append(msg)

    for le, be, width, desc, mode in PAIRS:
        a, b = rd(le), rd(be)
        print("==== " + desc + f"  [dir={mode}, w={width}]")
        check(a is not None and len(a) > 0, f"{le} present and non-empty")
        check(b is not None and len(b) > 0, f"{be} present and non-empty")
        if not (a and b):
            continue
        check(len(a) % width == 0, f"{le} size%{width}==0 (size={len(a)})")
        check(len(b) % width == 0, f"{be} size%{width}==0 (size={len(b)})")
        check(a[:7] != MAGIC, f"{le} has no v2 magic")
        check(b[:7] != MAGIC, f"{be} has no v2 magic")
        if mode == "rev":
            check(b[:width] == a[:width][::-1],
                  f"head BE==reverse(LE): LE={a[:width].hex()} BE={b[:width].hex()}")
            k = min(len(a) // width, len(b) // width)
            bad = 0
            for i in range(k):
                ra = a[i * width:(i + 1) * width]
                rb = b[i * width:(i + 1) * width]
                if rb != ra[::-1]:
                    bad += 1
            check(bad == 0, f"common prefix {k} records byte-reversed; mismatches={bad}")
        else:
            check(b[:width] == a[:width],
                  f"head BE==LE (1B unchanged): LE={a[:width].hex()} BE={b[:width].hex()}")
            k = min(len(a) // width, len(b) // width)
            bad = 0
            for i in range(k):
                if b[i * width:(i + 1) * width] != a[i * width:(i + 1) * width]:
                    bad += 1
            check(bad == 0, f"common prefix {k} records identical; mismatches={bad}")

    # native integer-bit equality under opposite decode confirms value preserved,
    # bytes merely reordered (not reinterpreted into a different number).
    for name, width in (("f32", 4), ("f64", 8)):
        a, b = rd(f"{name}_le.bin"), rd(f"{name}_be.bin")
        if a and b and len(a) >= width and len(b) >= width:
            ia = int.from_bytes(a[:width], "little")
            ib = int.from_bytes(b[:width], "big")
            check(ia == ib, f"native {name} head raw bits equal: 0x{ia:0{width * 2}X} == 0x{ib:0{width * 2}X}")
            if name == "f32":
                fa = struct.unpack("<f", a[:4])[0]
                fb = struct.unpack(">f", b[:4])[0]
                if math.isfinite(fa) and math.isfinite(fb):
                    check(fa == fb, f"native f32 head value equal LE={fa!r} BE={fb!r}")

    print("TOTAL_FAILS=" + str(len(fails)))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
