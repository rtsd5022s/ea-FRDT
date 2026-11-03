#!/usr/bin/env python3
"""Strict verifier for raw/no-header dump modes and -m all behaviour.

Usage: python tests/verify_raw.py <sample-dir> <dump_inspect.py path>

Checked invariants:
  * v2.bin  : normal header, magic present, payload present, passes dump_inspect.py
  * raw_le.bin / raw_nh.bin : --dump-raw and --dump-no-header are the SAME bytes
                              (aliases identical), no magic, f32 %4 geometry
  * raw_tf32pack.bin : packed odd 3B width, no magic, size%3==0
  * allraw.bin : raw mode with -m all removes the empty output file (must be absent)
  * all_v2.bin : normal (non-raw) -m all keeps exactly a 256-byte empty v2 shell,
                 magic present and passes dump_inspect.py

Raw streams intentionally have no magic, so dump_inspect.py (v2-only) must NOT be
run against them; v2/all_v2 files are delegated to the authoritative inspector.

Exit code: 0 = TOTAL_FAILS=0; 1 = at least one FAIL.
"""
import os
import sys
import subprocess

MAGIC = b"EAFRDT2"
HEADER_SIZE = 256


def main():
    sample_dir = sys.argv[1]
    inspect_py = sys.argv[2]
    fails = []

    def check(cond, msg):
        print(("PASS " if cond else "FAIL ") + msg)
        if not cond:
            fails.append(msg)

    def rd(name):
        path = os.path.join(sample_dir, name)
        return open(path, "rb").read() if os.path.exists(path) else None

    def absent(name):
        return not os.path.exists(os.path.join(sample_dir, name))

    def inspect_exit(name):
        proc = subprocess.run(
            [sys.executable, inspect_py, os.path.join(sample_dir, name)],
            capture_output=True,
        )
        return proc.returncode

    # normal v2 header with payload
    v2 = rd("v2.bin")
    check(v2 is not None and len(v2) > HEADER_SIZE,
          f"v2.bin exists and exceeds 256B header (size={0 if v2 is None else len(v2)})")
    check(v2 is not None and v2[:7] == MAGIC, "v2.bin has v2 magic")
    check(v2 is None or len(v2) % 4 == 0, "v2.bin f32 geometry size%4==0")
    if v2 is not None:
        check(inspect_exit("v2.bin") == 0, "dump_inspect.py accepts v2.bin (exit 0)")

    # raw aliases must produce byte-identical output
    le = rd("raw_le.bin")
    nh = rd("raw_nh.bin")
    check(le is not None and len(le) > 0, "raw_le.bin non-empty")
    check(nh is not None and len(nh) > 0, "raw_nh.bin non-empty")
    check(le is None or le[:7] != MAGIC, "raw_le.bin has no v2 magic")
    check(nh is None or nh[:7] != MAGIC, "raw_nh.bin has no v2 magic")
    check(le == nh, "--dump-raw bytes == --dump-no-header bytes (aliases identical)")
    check(le is None or len(le) % 4 == 0, "raw_le f32 natural width size%4==0")

    # packed odd width
    pk = rd("raw_tf32pack.bin")
    check(pk is not None and len(pk) > 0, "raw_tf32pack.bin non-empty")
    check(pk is None or pk[:7] != MAGIC, "raw_tf32pack.bin has no v2 magic")
    check(pk is None or len(pk) % 3 == 0, "packed tf32 size%3==0 (tight 3B odd width)")

    # raw + -m all suppresses records and removes the empty file
    check(absent("allraw.bin"), "raw mode with -m all removes empty allraw.bin")

    # normal (non-raw) -m all keeps exactly the 256B empty shell with magic
    av = rd("all_v2.bin")
    check(av is not None and len(av) == HEADER_SIZE,
          f"all_v2.bin is exactly 256B empty shell (size={0 if av is None else len(av)})")
    check(av is None or av[:7] == MAGIC, "all_v2.bin keeps v2 magic")
    if av is not None:
        check(inspect_exit("all_v2.bin") == 0, "dump_inspect.py accepts all_v2.bin (exit 0)")

    print("TOTAL_FAILS=" + str(len(fails)))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
