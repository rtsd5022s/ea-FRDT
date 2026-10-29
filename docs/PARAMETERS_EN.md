# Command-Line Parameter Reference

This document lists every command-line parameter of `ea-FRDT`, its default value, and valid ranges — verbatim from the source.

## Positional

| Argument | Default | Description |
|---|---|---|
| `delay_ms` | none (required, else help is shown) | Per-step delay in milliseconds, supports fractional (e.g. `0.5`, `0.01`). `0` = unlimited speed. In `-m all` defaults to `10` when omitted. Negative values are clamped to `0`. |

## Help

| Flag | Short | Description |
|---|---|---|
| `--help` | `-h` | Show bilingual help and exit. |

## Start and stepping

| Flag | Short | Default | Valid range | Description |
|---|---|---|---|---|
| `--start <x>` | `-a` | `1.0` | finite number, negatives, scientific (`1e-3`), engineering, hex Raw (`0x3F800000`), keywords `min-normal` / `max-finite` | Walk start value. Cannot be NaN/Inf. Mutually exclusive with `-s`. |
| `--subnormal` | `-s` | off | no value | Start from the format's minimum subnormal and walk upward. Mutually exclusive with numeric `-a`. |
| `--factor <r>` | `-k` | `1.02` | positive, non-zero, non-NaN, non-Inf; scientific notation accepted; `<1e-10` triggers warning | Per-step multiplier. `>1` walks to +inf, `0<r<1` walks to 0. |
| `--step <s>` | `-g` | `x1.02` | `x1.02` (alias `x102`) / `mululp` / `addulp` | Step mode. `x1.02` = multiply by factor; `mululp` = multiply by `(1+1ULP)`; `addulp` = add `1ULP`. |

## Floating-point format

| Flag | Short | Default | Description |
|---|---|---|---|
| `--mode <m>` | `-m` | `f32` | Select floating-point format. See full list below. `all` = run all formats concurrently (excludes DPD32/DPD64/DPD128/Dec128[BID] by default; specify via `--only`). |

### Full list of 17 formats

| Mode value | Display name | Category | Width | Notes |
|---|---|---|---|---|
| `f16` / `fp16` | FP16 | Native | 16 | IEEE 754 half precision |
| `f32` / `fp32` | FP32 | Native | 32 | IEEE 754 single (default) |
| `f64` / `fp64` | FP64 | Native | 64 | IEEE 754 double |
| `fp4` | FP4(E2M1) | Hand-rolled bit | 4 | 2 exp bits + 1 mantissa bit |
| `fp6` | FP6(E3M2) | Hand-rolled bit | 6 | 3 exp bits + 2 mantissa bits |
| `fp8` / `fp8-e4m3` | FP8(E4M3) | Hand-rolled bit | 8 | 4 exp bits + 3 mantissa bits |
| `fp8-e5m2` | FP8(E5M2) | Hand-rolled bit | 8 | 5 exp bits + 2 mantissa bits |
| `bf16` | BF16(E8M7) | Hand-rolled bit | 16 | Brain Float, 8 exp + 7 mantissa |
| `tf32` | TF32(E8M10) | Hand-rolled bit | 19 | TensorFloat-32, 8 exp + 10 mantissa |
| `x87` | x87 | Pure software | 80 | x87 extended 80-bit emulation (no `long double`) |
| `fp128` | FP128 | Pure software | 128 | Full 112-bit mantissa emulation (no libquadmath) |
| `dec32` / `decimal32` | Decimal32[BID] | Decimal BID | 32 | 7 significant digits |
| `dec64` / `decimal64` | Decimal64[BID] | Decimal BID | 64 | 16 significant digits |
| `dec128` / `decimal128` | Decimal128[BID] | Decimal BID | 128 | 34 significant digits, hand-rolled UInt256 |
| `dpd32` / `dpd-dec32` | DPD32[DPD] | Decimal DPD | 32 | 7 significant digits, densely packed |
| `dpd64` / `dpd-dec64` | DPD64[DPD] | Decimal DPD | 64 | 16 significant digits |
| `dpd128` / `dpd-dec128` | DPD128[DPD] | Decimal DPD | 128 | 34 significant digits |

## Display and output

| Flag | Short | Default | Values | Description |
|---|---|---|---|---|
| `--fmt <f>` | `-f` | `eng` | `eng` / `sci` / `full` | Number display format. `eng` = engineering; `sci` = scientific; `full` = full decimal expansion. |
| `--digits <n>` | `-d` | `6` | `-1` or `0~100` | Mantissa decimals in sci/eng. `-1` = full precision. |
| `--bits <m>` | `--showbits` | `off` | `hex` / `bin` / `off` (or `none`) | Append raw bit pattern in parentheses after the value. `hex` = hexadecimal; `bin` = binary grouped by fields. |
| `--quiet` | `-q` | off | no value | Quiet: skip per-step output, print only the summary table. |
| `--json` | none | off | no value | Machine-readable JSON output. Auto-enables quiet. One JSON line per run (format/steps/elapsed_ms/steps_per_sec/reason/final_value/params/system/timestamp). In `-m all` emits a `workers` array. |
| `--dump <file>` | none | off | file path | Binary-serialize every step's raw bytes to file. Auto-enables quiet. |
| `--dump-align <n>` | none | `8` | `1` / `2` / `4` / `8` / `16` | Record alignment for `--dump`. Each step writes sizeof(S) bytes then zero-pads to align. |
| `--dump-endian <e>` | none | `little` | `little` (aliases `le`/`cpu`) / `big` (alias `be`) | Byte order for `--dump`. Default follows x86 CPU native little-endian; `big` flips byte order before writing. |
| `--lang <zh|en>` | `-lang` / `--language` | `zh` | `zh` / `en` | UI language. Errors, warnings, summary table all follow. |

## Concurrency (-m all)

| Flag | Short | Default | Description |
|---|---|---|---|
| `--jobs <spec>` | `-j` | `all` (all logical cores) | Worker/core spec. Single (`-j 4`), comma list (`-j 1,3,5`), range (`-j 1-4`), mixed (`-j 1,3-5`). Hard cap = `std::thread::hardware_concurrency()`. |
| `--only <list>` | `--modes` | all 17 (DPD/Dec128 excluded by default) | Comma-separated format subset, e.g. `--only fp32,fp64,dec64`. |
| `--refresh <ms>` | `-r` | single mode `0` (every step); `-m all` defaults `1000` | Monitor refresh interval in ms. `0` = refresh every step. Negative clamped to `0`. |

## Control

| Flag | Short | Default | Values | Description |
|---|---|---|---|---|
| `--timeout <s>` | `-t` | `0` | non-negative integer seconds | Timeout stop. `0` = run until overflow/underflow. Negative clamped to `0`. |
| `--loop <n>` | `-l` | `1` | `-1` = infinite; `1` = single pass (default); `>1` = repeat N times; max `1000000`; `0` treated as `1` | Loop count. `-l -1` with `-t 0` triggers a DoS warning. |

## Exit codes

| Code | Meaning |
|---|---|
| `0` | Normal end (overflow / underflow / timeout / loop complete) |
| `2` | Argument error (parse failure, mutual-exclusion conflict, out-of-range, unknown format) |

## Safety guards

- `-l -1` with `-t 0`: yellow warning, possible 100% CPU.
- `-k < 1e-10`: yellow warning, step count may explode.
- DPD32/DPD64/DPD128 in `-m all`: yellow warning, requires `y` confirmation, auto-cancel after 10 s.
- Dec128[BID] in concurrent mode: yellow warning, requires `y` confirmation.
- Error messages pass through `sanitize()` which strips ANSI escapes and C0 control chars to prevent terminal injection.
- Hex input accepts at most 32 characters (covers 128-bit floats).
- Start cannot be NaN/Inf; factor cannot be 0 / negative / NaN / Inf.

## Quick examples

```
ea-FRDT.exe 0                                # f32 from 1.0 x1.02
ea-FRDT.exe 0 -a 100 -k 1.1                  # start at 100, x1.1
ea-FRDT.exe 0 -a 0x3F800000 -m f32          # raw bit start
ea-FRDT.exe 0 -a max-finite -k 0.5           # max value descending to 0
ea-FRDT.exe 0 -m fp4 -g addulp               # enumerate to inf
ea-FRDT.exe 0 -m all --only fp32,dec128 -j 1,3 -q
ea-FRDT.exe 0 -m all -l 3 -t 10              # loop 3x, max 10s each
ea-FRDT.exe 0 -m f32 --json                  # JSON for scripts
ea-FRDT.exe 0 -m f64 --dump walk.bin --dump-align 8
```

---

*The algorithm may go off the rails.*
