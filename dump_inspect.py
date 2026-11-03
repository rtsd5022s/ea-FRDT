#!/usr/bin/env python3
"""ea-FRDT v2 dump file header interpreter + integrity verifier.

Schema v2 fixed 256-byte header, little-endian integers:
  0x00 char[8]  magic "EAFRDT2\0"
  0x08 u8        schema (=2); 0x09 reserved
  0x0A u8        payload endian (0=little, 1=big); 0x0B u8 packed (0=natural,1=tight)
  0x0C u32       flags (bit0 reserved, bit1 truncated, bit2 empty)
  0x10 u32       header CRC-32/IEEE (this 4-byte slot counted as zero)
  0x14 u32       payload CRC-32/IEEE over bytes [256, 256+payload_bytes)
  0x18 u64       payload_bytes; 0x20 u64 record_count; 0x28 u32 record_size
  0x2C u8 format_id; 0x2D u8 step_mode; 0x2E u8 start_kind; 0x2F u8 reason (0xFF=unknown)
  0x30 f64 start_value; 0x38 f64 factor
  0x40 i64 timestamp_unix_s; 0x48 u64 elapsed_us
  0x50 u32 timeout_s; 0x54 i32 loop_count; 0x58 f64 delay_ms
  0x60 char[32] format_name; 0x80 char[32] tool_version; 0xA0..0xFF reserved zero

Exit codes: 0 all checks PASS; 1 readable but checksum/length mismatch;
            2 unsupported/legacy/truncated header; 3 open/parse error.
"""
import sys
import os
import struct
import binascii
import datetime

MAGIC = b"EAFRDT2"
HEADER_SIZE = 256

FMT_NAMES = {
    0: "FP4(E2M1)", 1: "FP6(E3M2)", 2: "FP8(E4M3)", 3: "FP8(E5M2)",
    4: "FP16", 5: "BF16", 6: "TF32", 7: "FP32", 8: "Dec32(BID)", 9: "DPD32",
    10: "FP64", 11: "Dec64(BID)", 12: "DPD64", 13: "x87(80bit)", 14: "FP128",
    15: "Dec128(BID)", 16: "DPD128",
}
STEP_NAMES = {0: "x1.02", 1: "mululp", 2: "addulp", 3: "subulp"}
KIND_NAMES = {0: "numeric/default", 1: "subnormal", 2: "min-normal", 3: "max-finite"}
REASON_NAMES = {0: "overflow", 1: "underflow", 2: "identity", 3: "safety",
                4: "timeout", 5: "nan", 0xFF: "unknown/not-run"}


def hexdump(data, base=0):
    lines = []
    for i in range(0, len(data), 16):
        chunk = data[i:i + 16]
        hexpart = " ".join(f"{b:02X}" for b in chunk)
        asc = "".join(chr(b) if 32 <= b < 127 else "." for b in chunk)
        lines.append(f"  {base + i:04X}  {hexpart:<48s}  {asc}")
    return "\n".join(lines)


def stamp(ts):
    try:
        return datetime.datetime.fromtimestamp(ts, datetime.timezone.utc).strftime(
            "%Y-%m-%d %H:%M:%S UTC")
    except (OverflowError, OSError, ValueError):
        return "<invalid epoch>"


def interpret(path):
    try:
        with open(path, "rb") as f:
            raw = f.read()
    except OSError as e:
        print(f"ERROR: cannot open {path}: {e}")
        return 3

    print("=" * 60)
    print("ea-FRDT Dump Header Inspector (schema v2)")
    print("=" * 60)

    if len(raw) < HEADER_SIZE:
        print(f"RESULT: UNSUPPORTED")
        print(f"  file too small: {len(raw)} bytes < 256 header")
        return 2

    h = raw[:HEADER_SIZE]
    if h[0:7] != MAGIC:
        if h[0:7] == b"ea-FRDT":
            print("RESULT: UNSUPPORTED")
            print("  legacy v1 header detected (magic 'ea-FRDT'); v2 tool does not parse it.")
        else:
            print("RESULT: UNSUPPORTED")
            print(f"  bad magic {h[0:8]!r}; expected 'EAFRDT2'")
        return 2

    schema = h[0x08]
    if schema != 2:
        print("RESULT: UNSUPPORTED")
        print(f"  unsupported schema_version={schema}; this inspector reads schema 2 only")
        return 2

    endian = h[0x0A]
    packed = h[0x0B]
    flags = struct.unpack_from("<I", h, 0x0C)[0]
    hdr_crc = struct.unpack_from("<I", h, 0x10)[0]
    pay_crc = struct.unpack_from("<I", h, 0x14)[0]
    payload_bytes = struct.unpack_from("<Q", h, 0x18)[0]
    record_count = struct.unpack_from("<Q", h, 0x20)[0]
    record_size = struct.unpack_from("<I", h, 0x28)[0]
    fmt_id, step, kind, reason = h[0x2C], h[0x2D], h[0x2E], h[0x2F]
    start = struct.unpack_from("<d", h, 0x30)[0]
    factor = struct.unpack_from("<d", h, 0x38)[0]
    ts = struct.unpack_from("<q", h, 0x40)[0]
    elapsed_us = struct.unpack_from("<Q", h, 0x48)[0]
    timeout_s = struct.unpack_from("<I", h, 0x50)[0]
    loop = struct.unpack_from("<i", h, 0x54)[0]
    delay_ms = struct.unpack_from("<d", h, 0x58)[0]
    fmt_name = h[0x60:0x80].split(b"\0", 1)[0].decode("utf-8", "replace")
    tool_ver = h[0x80:0xA0].split(b"\0", 1)[0].decode("utf-8", "replace")

    # ---- integrity checks ----
    failures = []

    h2 = bytearray(h)
    h2[0x10:0x14] = b"\x00\x00\x00\x00"
    calc_hdr = binascii.crc32(bytes(h2)) & 0xFFFFFFFF
    if calc_hdr != hdr_crc:
        failures.append(f"header CRC32 mismatch: stored 0x{hdr_crc:08X}, recomputed 0x{calc_hdr:08X}")

    actual = len(raw)
    expected = HEADER_SIZE + payload_bytes
    if actual != expected:
        failures.append(f"size mismatch: file={actual}, expected=256+payload={expected}")

    payload = raw[HEADER_SIZE:HEADER_SIZE + payload_bytes]
    calc_pay = binascii.crc32(payload) & 0xFFFFFFFF
    if calc_pay != pay_crc:
        failures.append(f"payload CRC32 mismatch: stored 0x{pay_crc:08X}, recomputed 0x{calc_pay:08X}")

    if record_size > 0 and record_count * record_size != payload_bytes:
        failures.append(
            f"record geometry mismatch: {record_count} x {record_size} = "
            f"{record_count * record_size} bytes != payload {payload_bytes}")

    empty = bool(flags & 0x4)

    # ---- human-readable field dump ----
    print(f"  0x00  magic          : {bytes(h[0:8])!r}")
    print(f"  0x08  schema         : {schema} (fixed 256-byte header)")
    print(f"  0x0A  payload endian : {'big-endian' if endian else 'little-endian'}")
    print(f"  0x0B  packed records : {'tight' if packed else 'natural width'}")
    print(f"  0x0C  flags          : 0x{flags:08X} "
          f"[{'empty' if empty else 'normal'}"
          f"{', truncated' if flags & 0x2 else ''}]")
    print(f"  0x10  header CRC32   : 0x{hdr_crc:08X}  recomputed 0x{calc_hdr:08X}")
    print(f"  0x14  payload CRC32  : 0x{pay_crc:08X}  recomputed 0x{calc_pay:08X}")
    print(f"  0x18  payload_bytes  : {payload_bytes}")
    print(f"  0x20  record_count   : {record_count}")
    print(f"  0x28  record_size    : {record_size} bytes/record")
    print(f"  0x2C  format         : id={fmt_id} = {FMT_NAMES.get(fmt_id, '?')}")
    print(f"  0x60  format_name    : {fmt_name!r}")
    print(f"  0x2D  step_mode      : {STEP_NAMES.get(step, '?')} ({step})")
    print(f"  0x2E  start_kind    : {KIND_NAMES.get(kind, '?')} ({kind})")
    print(f"  0x2F  exit reason    : {REASON_NAMES.get(reason, '?')} (0x{reason:02X})")
    print(f"  0x30  start_value    : {start:.17g}")
    print(f"  0x38  factor         : {factor:.17g}")
    print(f"  0x40  timestamp      : {ts} ({stamp(ts)})")
    print(f"  0x48  elapsed        : {elapsed_us} us ({elapsed_us / 1000.0:.3f} ms)")
    print(f"  0x50  timeout_s     : {timeout_s}")
    print(f"  0x54  loop_count     : {loop}")
    print(f"  0x58  delay_ms       : {delay_ms}")
    print(f"  0x80  tool_version   : {tool_ver!r}")

    print("")
    print("  --- geometry & integrity ---")
    print(f"  256 header + {payload_bytes} payload = {expected}; actual file = {actual}")
    if record_size > 0:
        print(f"  records x record_size = {record_count} x {record_size} = "
              f"{record_count * record_size} bytes")
    if payload_bytes == 0:
        print("  payload is empty (no records; typical of -m all / an aborted run).")

    if record_size > 0 and payload_bytes > 0:
        head = payload[:4 * record_size]
        tail = payload[-4 * record_size:] if payload_bytes > 8 * record_size else b""
        print("")
        print("  --- first records (hex) ---")
        print(hexdump(head, HEADER_SIZE))
        if tail:
            print("  --- last records (hex) ---")
            print(hexdump(tail, HEADER_SIZE + payload_bytes - len(tail)))

    print("")
    if failures:
        print("RESULT: FAIL")
        for msg in failures:
            print(f"  - {msg}")
        return 1
    print("RESULT: PASS (header CRC, payload CRC, size and record geometry all consistent)")
    return 0


if __name__ == "__main__":
    target = sys.argv[1] if len(sys.argv) > 1 else "sample.bin"
    sys.exit(interpret(target))
