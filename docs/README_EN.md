# ea-FRDT

Floating point range deep tester for 17 formats. C++23, multithreaded.

## Build

GCC 13+ or Clang 16+, C++23 required.

```bash
g++ -std=c++23 -O2 -march=native \
    -Iinclude src/float_inf_walk.cpp src/lang.cpp src/format_util.cpp src/drive.cpp \
    -o float_inf_walk
```

Add `-lwinmm` on Windows, `-static -static-libgcc -static-libstdc++` for static link.

## Run

```bash
# Default: FP32 from 1.0, x1.02 each step until overflow
./float_inf_walk 0

# FP16 from max-finite, x0.5 until underflow
./float_inf_walk 0 -m fp16 -a max-finite -k 0.5

# All formats concurrently, bind cores 1-4
./float_inf_walk 0 -m all -j 1-4

# 10 second timeout
./float_inf_walk 0 -m all -t 10
```

## Formats

Native: f16 f32 f64

Software bit-level: fp4 fp6 fp8-e4m3 fp8-e5m2 bf16 tf32

Pure software: x87(80-bit) fp128(112-bit)

Decimal: dec32/64/128(BID) dpd32/64/128(DPD)

## Flags

| Flag | Description |
|---|---|
| `-m` | Format, see above |
| `-a` | Start value, supports negative, 0x raw, min-normal, max-finite |
| `-k` | Factor per step, default 1.02, >1 overflow <1 underflow |
| `-g` | mululp / addulp step mode |
| `-j` | Core IDs, single/comma/range |
| `-t` | Timeout seconds, 0=run to end |
| `-l` | Loop count, -1=infinite |
| `-q` | Quiet, summary table only |

## Layout

```
include/   headers
src/       sources
docs/      this file
```

## License

MIT
