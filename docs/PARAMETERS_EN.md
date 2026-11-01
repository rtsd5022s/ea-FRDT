# Command-Line Parameter Reference

This document lists every command-line parameter of `ea-FRDT`, its default value, and valid ranges — verbatim from the source.

## Positional

| Argument | Default | Description |
|---|---|---|
| `delay_ms` | single format: omitted → help (exit `2`); `-m all`: `10` | Per-step delay in milliseconds, parsed as a floating value (e.g. `0`, `0.5`, `0.01`). `0` = unlimited speed. Negative values are clamped to `0`. **This is the throttle, not the start value; the start value uses `-a`.** |

## Help

| Flag | Short | Description |
|---|---|---|
| `--help` | `-h` | Show bilingual help and exit. |

## Start and stepping

| Flag | Short | Default | Valid range | Description |
|---|---|---|---|---|
| `--start <x>` | `-a` | `1.0` | finite number, negatives, scientific (`1e-3`), engineering, hex Raw (`0x3F800000`), keywords `min-normal`/`min`, `max-finite`/`max` | Walk start value. Cannot be NaN/Inf. Mutually exclusive with `-s` and with another numeric `-a`. |
| `--subnormal` | `-s` | off | no value | Start from the format's minimum subnormal and walk upward. Mutually exclusive with `-a`. |
| `--factor <r>` | `-k` | `1.02` | positive, non-zero, non-NaN, non-Inf; scientific notation accepted; `r < 1e-10` raises a level-1 warning | Per-step multiplier. `>1` walks to +inf, `0<r<1` walks down to 0. |
| `--step <s>` | `-g` | `x1.02` | `x1.02` (alias `x102`) / `mululp` / `addulp` / `subulp` | Step mode. `x1.02` = multiply by factor; `mululp` = multiply by `(1+1ULP)`; `addulp` = add `1ULP`; `subulp` = subtract `1ULP`. (`mululp-down` was removed.) |

## Floating-point format

| Flag | Short | Default | Description |
|---|---|---|---|
| `--mode <m>` | `-m` | `f32` | Select floating-point format. See the list below. `all` = run the default 13-format chain concurrently (DPD32/DPD64/DPD128 and Dec128[BID] omitted; add via `--only`). |

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
| `--bits <m>` | `--showbits` | `off` | `hex` / `bin` / `off` (or `none`) | Append raw bit pattern in parentheses after the value. `hex` = hexadecimal; `bin` = binary grouped into sign/exponent/coefficient fields. |
| `--quiet` | `-q` | off | no value | Suppress live per-step and finish lines for the single-format engine; the result is still captured into `RunResult` for `--json`/summary. `--json` and `--dump` imply quiet. |
| `--json` | none | off | no value | Machine-readable JSON. Auto-enables quiet. One object per run (format/steps/elapsed/steps_per_sec/reason/final_value/params/system/timestamp). |
| `--dump <file>` | none | off | file path | `wb+` binary log: 256-byte header followed by per-step records; payload bytes from offset `256` to EOF get an IEEE 802.3 CRC-32, little-endian, stored at `0xE0`; verify independently with `python dump_inspect.py <file>`. Auto-enables quiet. |
| `--dump-packed` | none | off | no value | Compact record width (e.g. tf32 = 3 B instead of 4 B). |
| `--dump-endian <e>` | none | `little` | `little` (aliases `le`/`cpu`) / `big` (alias `be`) | Record byte order for `--dump`. Default little-endian; `big` flips bytes before writing. |
| `--lang <zh|en>` | `-lang` / `--language` | `zh` | `zh` / `en` | UI language. Help, errors, warnings, and summary follow. |

## Concurrency (-m all)

| Flag | Short | Default | Description |
|---|---|---|---|
| `--jobs <spec>` | `-j` | all logical cores | Worker/core spec, `0`-based ids. Single (`-j 4`), comma list (`-j 0,2,5`), range (`-j 0-3`), mixed (`-j 0,2-5`); ids outside `0..hw-1` are dropped, duplicates removed. |
| `--only <list>` | `--modes` | default chain = 13 of 17 (DPD32/64/128 and Dec128 omitted) | Comma-separated format subset, e.g. `--only fp32,fp64,dec64,dec128,dpd64`; unknown names exit `2`. |
| `--refresh <ms>` | `-r` | single mode `0` (every step); `-m all` `1000` when unspecified | Monitor refresh interval in ms. `0` = refresh every step. Negative clamped to `0`. |

## Control

| Flag | Short | Default | Values | Description |
|---|---|---|---|---|
| `--timeout <s>` | `-t` | `0` | non-negative integer seconds | Timeout stop. `0` = run until overflow/underflow/other reason. Negative clamped to `0`. |
| `--loop <n>` | `-l` | `1` | `-1` = infinite; `1` = single pass (default); `>1` = repeat N times; max `1000000`; `0` treated as `1`; values `< -1` rejected | Loop count. Combined with `-t 0`, `-l -1` raises a level-1 confirmation warning in `-m all` (not a hard rejection). |

## Exit codes

| Code | Meaning |
|---|---|
| `0` | Normal end, or cancelled by confirmation (`cancelled`), or SIGINT (`sigint`) |
| `2` | Argument error (parse failure, mutual-exclusion conflict, out-of-range, unknown format/flag) |

## Safety guards and warnings

Hard exits (`2`) on stderr for invalid input only:

- Unknown format/flag, unparseable start or factor, `0`/negative/non-finite factor, special starts conflicting (`-a` vs `-s`, two named/numeric starts), out-of-range `--digits`, `--loop` `< -1` or `> 1000000`.
- Malformed `0x` raw pattern; hex accepts at most 32 characters (covers 128-bit floats).

Non-fatal warnings gathered and confirmed in `-m all` (level-1 yellow `[!]`, level-2 red `[!!!]`; press `y` within 10 s or auto-cancel as `cancelled`):

- `-l -1` together with `-t 0`: level-1 yellow warning (may peg CPU at 100%).
- `-k < 1e-10`: level-1 yellow warning (walk may need very many steps).
- DPD32/DPD64/DPD128 included: level-2 red warning (pure software decimal-array arithmetic, roughly 20–30× slower, weak CPUs stay loaded longer).
- Dec128[BID] included: level-2 red warning (UInt256 schoolbook coefficient multiply every step, slower still).

Error messages pass through `sanitize()`, stripping ANSI escapes and C0 control characters to prevent terminal injection.

## Quick examples

```
float_inf_walk 0                                # f32 from 1.0 x1.02
float_inf_walk 0 -a 100 -k 1.1                  # start at 100, x1.1
float_inf_walk 0 -a 0x3F800000 -m f32          # raw bit start
float_inf_walk 0 -a max-finite -k 0.5           # max value descending to 0
float_inf_walk 0 -m fp32 -s                    # minimum subnormal start
float_inf_walk 0 -m fp4 -g addulp              # enumerate upward
float_inf_walk 0 -m all --only fp32,dec128 -j 0,2 -q
float_inf_walk 0 -m all -l 3 -t 10              # loop 3x, max 10s each
float_inf_walk 0 -m f32 --bits hex             # hex bits in parentheses
float_inf_walk 0 -m f32 --json                  # JSON for scripts
float_inf_walk 0 -m f64 --dump walk.bin         # dump with payload CRC-32
```
