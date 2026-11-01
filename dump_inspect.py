#!/usr/bin/env python3
"""ea-FRDT dump file header interpreter — full 256-byte dump"""
import struct, sys, datetime

def hexdump(data, base=0):
    lines = []
    for i in range(0, len(data), 16):
        chunk = data[i:i+16]
        hexpart = " ".join(f"{b:02X}" for b in chunk)
        asc = "".join(chr(b) if 32 <= b < 127 else "." for b in chunk)
        lines.append(f"  {base+i:04X}  {hexpart:<48s}  {asc}")
    return "\n".join(lines)

def interpret(path):
    with open(path, "rb") as f:
        h = f.read(256)
    if len(h) < 256:
        print("ERROR: file too small (<256 bytes)")
        return
    magic = h[0:8].rstrip(b"\x00").decode("ascii", errors="replace")
    ver_len = h[0x88]
    version = h[0x08:0x08+ver_len].decode("utf-8", errors="replace")
    ts = struct.unpack_from("<Q", h, 0x90)[0]
    ts_str = datetime.datetime.utcfromtimestamp(ts).strftime("%Y-%m-%d %H:%M:%S UTC")
    fmt_id = h[0x98]
    endian = "big-endian" if h[0x99] else "little-endian"
    align = h[0x9A]
    total_steps = struct.unpack_from("<Q", h, 0xA0)[0]
    hsize = struct.unpack_from("<Q", h, 0xB0)[0]
    step_interval = h[0xC0:0xD0]
    record_size = struct.unpack_from("<Q", h, 0xD0)[0]
    crc = struct.unpack_from("<I", h, 0xE0)[0]
    flags = struct.unpack_from("<I", h, 0xE4)[0]
    cpu_hash = h[0xE8:0xF8]

    fmt_names = {0:"FP4(E2M1)",1:"FP6(E3M2)",2:"FP8(E4M3)",3:"FP8(E5M2)",
                 4:"FP16",5:"BF16",6:"TF32",7:"FP32",8:"Dec32(BID)",
                 9:"DPD32",10:"FP64",11:"Dec64(BID)",12:"DPD64",
                 13:"x87(80bit)",14:"FP128",15:"Dec128(BID)",16:"DPD128"}

    print(f"╔══════════════════════════════════════════════════════════╗")
    print(f"║       ea-FRDT Dump File Header — Full Interpreter       ║")
    print(f"╚══════════════════════════════════════════════════════════╝")
    print(f"")
    print(f"  0x00  magic           : {magic!r}")
    print(f"  0x08  version_utf8    : {version}  (len={ver_len})")
    print(f"  0x88  version_len     : {ver_len} bytes")
    print(f"  0x89  reserved1      : {h[0x89:0x90].hex(' ')}")
    print(f"  0x90  timestamp       : {ts}  ({ts_str})")
    print(f"  0x98  format_id       : {fmt_id} = {fmt_names.get(fmt_id,'?')}")
    print(f"  0x99  endianness      : {endian}")
    print(f"  0x9A  payload_align  : {align} bytes")
    print(f"  0x9B  reserved2       : {h[0x9B:0xA0].hex(' ')}")
    print(f"  0xA0  total_steps     : {total_steps}  (UInt128 low64=0x{total_steps:X})")
    print(f"  0xA8  total_steps_hi : {struct.unpack_from('<Q', h, 0xA8)[0]}")
    print(f"  0xB0  header_size     : {hsize} bytes")
    print(f"  0xB8  header_size_hi : {struct.unpack_from('<Q', h, 0xB8)[0]}")
    print(f"  0xC0  step_interval   : DPD128 = {step_interval.hex(' ')}")
    print(f"  0xD0  record_size     : {record_size} bytes")
    print(f"  0xD8  record_size_hi : {struct.unpack_from('<Q', h, 0xD8)[0]}")
    print(f"  0xE0  crc32_checksum : 0x{crc:08X}")
    print(f"  0xE4  flags           : 0x{flags:08X}  (bit0=compress, bit1=symtab)")
    print(f"  0xE8  cpu_brand_hash : {cpu_hash.hex(' ')}")
    print(f"  0xF8  reserved3      : {h[0xF8:0x100].hex(' ')}")
    print(f"")
    payload = total_steps * record_size
    import os, binascii
    actual = os.path.getsize(path)
    with open(path, "rb") as pf:
        pf.seek(256)
        payload_data = pf.read()
    calc_crc = binascii.crc32(payload_data) & 0xFFFFFFFF
    print(f"  --- payload math ---")
    print(f"  total_steps × record_size = {total_steps} × {record_size} = {payload}")
    print(f"  header (256) + payload = {256+payload}")
    print(f"  actual file size      = {actual}")
    print(f"  match                 : {'YES' if actual == 256+payload else 'MISMATCH'}")
    print(f"")
    print(f"  --- crc32 payload integrity (IEEE 802.3 over offset 256..EOF) ---")
    print(f"  header crc32 (0xE0)   : 0x{crc:08X}")
    print(f"  recomputed payload    : 0x{calc_crc:08X}")
    print(f"  crc match             : {'YES' if calc_crc == crc else 'MISMATCH'}")
    print(f"")
    print(f"  --- full 256-byte hex dump ---")
    print(hexdump(h))

if __name__ == "__main__":
    interpret(sys.argv[1] if len(sys.argv) > 1 else "sample.bin")
