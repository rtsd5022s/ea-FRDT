#!/usr/bin/env python3
"""ea-FRDT dump file header interpreter"""
import struct, sys

def interpret(path):
    with open(path, "rb") as f:
        h = f.read(256)
    if len(h) < 256:
        print("ERROR: file too small (<256 bytes)")
        return
    magic = h[0:8].rstrip(b"\x00").decode("ascii", errors="replace")
    if magic != "ea-FRDT":
        print(f"WARNING: magic = {repr(magic)} (expected 'ea-FRDT')")
    ver_len = h[0x88]
    version = h[0x08:0x08+ver_len].decode("utf-8", errors="replace")
    ts = struct.unpack_from("<Q", h, 0x90)[0]
    fmt_id = h[0x98]
    endian = "big" if h[0x99] else "little"
    align = h[0x9A]
    total_steps = struct.unpack_from("<Q", h, 0xA0)[0]
    hsize = struct.unpack_from("<Q", h, 0xB0)[0]
    step_interval = h[0xC0:0xD0]  # DPD128 raw bytes
    record_size = struct.unpack_from("<Q", h, 0xD0)[0]
    crc = struct.unpack_from("<I", h, 0xE0)[0]
    flags = struct.unpack_from("<I", h, 0xE4)[0]
    cpu_hash = h[0xE8:0xF8]

    fmt_names = {0:"FP4",1:"FP6",2:"FP8-E4M3",3:"FP8-E5M2",4:"FP16",
                 5:"BF16",6:"TF32",7:"FP32",8:"Dec32-BID",9:"DPD32",
                 10:"FP64",11:"Dec64-BID",12:"DPD64",13:"x87-80bit",
                 14:"FP128",15:"Dec128-BID",16:"DPD128"}

    print(f"=== ea-FRDT Dump File Header ===")
    print(f"  magic         : {magic}")
    print(f"  version       : {version}")
    print(f"  timestamp     : {ts}")
    print(f"  format_id     : {fmt_id} ({fmt_names.get(fmt_id,'?')})")
    print(f"  endianness    : {endian}")
    print(f"  payload_align : {align}")
    print(f"  total_steps   : {total_steps}")
    print(f"  header_size   : {hsize}")
    print(f"  record_size   : {record_size} bytes")
    print(f"  step_interval : {step_interval.hex(' ')}")
    print(f"  crc32         : 0x{crc:08X}")
    print(f"  flags         : 0x{flags:08X}")
    print(f"  cpu_brand_hash: {cpu_hash.hex(' ')}")
    payload = total_steps * record_size
    print(f"  ---")
    print(f"  payload_size  : {payload} bytes (256 + {payload} = {256+payload})")
    print(f"  expected_file : {256+payload} bytes")

if __name__ == "__main__":
    interpret(sys.argv[1] if len(sys.argv) > 1 else "sample.bin")
