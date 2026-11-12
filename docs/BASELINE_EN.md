# Baseline Results (reference measurements)

Environment: i7-6700 (4C8T), Windows x86_64, MSYS2 clang64 Clang, release `-Oz -flto=thin -static`.
Command: `float_inf_walk 0 -m <format> -q` (from 1.0 × 1.02 to overflow).

> Steps and times below are **historical reference measurements of that build on that machine**. Steps are mostly determined by bit width and Emax and are fairly build-stable; wall times vary strongly with CPU, optimization level, and compiler and are not promised across machines/versions. When comparing BID vs DPD, focus on whether equal-width step counts agree, not on absolute milliseconds.

## Single-thread step baseline (1.0 × 1.02 → overflow)

| Format | Steps (ref.) | Time (ref.) |
|---|---|---|
| FP4 (E2M1) | 1 (`identity->stop`) | ~0 ms |
| FP6 (E3M2) | 1 (`identity->stop`) | ~0 ms |
| FP8 (E4M3) | 1 (`identity->stop`) | ~0 ms |
| FP8 (E5M2) | 1 (`identity->stop`) | ~0 ms |
| FP16 | 574 | 9 ms |
| BF16 | 3913 | 16 ms |
| TF32 | 4623 | 21 ms |
| FP32 | 4481 | 30 ms |
| Dec32 (BID) | 11279 | 45 ms |
| DPD32 | 11279 | 57 ms |
| FP64 | 35843 | 209 ms |
| Dec64 (BID) | 44767 | 229 ms |
| DPD64 | 44767 | 230 ms |
| x87 (80-bit) | 573521 | 3900 ms |
| FP128 | 573521 | 3911 ms |
| Dec128 (BID) | 714521 | 12244 ms |
| DPD128 | 714521 | 4868 ms |

## Notes

- FP4/FP6/FP8 hit `identity->stop` on the first step from 1.0: 1.0×1.02 rounds back to 1.0 in these tiny formats.
- Dec32/DPD32 match (11279), Dec64/DPD64 match (44767), Dec128/DPD128 match (714521): under the same decimal arithmetic, BID and DPD codecs agree on step counts.
- x87 and FP128 measure close (573521) here; both are pure software at a 15-bit exponent scale, and time differences come from their wide-mantissa multiplies.
- Dec128(BID) carries a 113-bit coefficient through UInt256 big-integer multiply and is the heaviest; DPD128 walks a decimal digit array declet by declet. On narrow widths DPD is slightly slower than BID due to lookup/per-digit overhead; which path wins shifts with coefficient width — trust local measurement.
- `-q` now only suppresses the live lines: the run still ends with one `TOTAL <n> steps · <reason> · wall <t>` footer. It does not affect the readings, because elapsed time comes from `RunResult` and excludes printing.

## Default `-m all` scope

The default chain has 13 formats and excludes:

- DPD32/DPD64/DPD128 (software decimal declets; the confirmation notes they are markedly slower than native paths)
- Dec128(BID) (UInt256 wide-coefficient multiply; the confirmation notes it runs roughly 2–3× slower than DPD128)

Add them with `--only dpd32,dpd64,dpd128,dec128`; this confirmation gate exists only in `-m all`.

## Common parameter examples (see PARAMETERS_EN.md for semantics)

- `-m fp32` single; `-m all` concurrent default 13-chain (DPD×3 and Dec128 excluded)
- `--only fp32,fp64`: subset
- `-g x1.02` (default) / `-g mululp` / `-g addulp` / `-g subulp`
- `-k 1.01` custom factor; `-a max-finite`; `-a min-normal`; `-s` smallest subnormal start
- `-a 0x3f800000`: FP32 1.0 raw-bit start (hex start uses `-a`; `-s` takes no argument)
- `-l 3` three passes; `-t 10` ten-second timeout
- `-j 0,2`: cores, zero-based (first core is 0); `-q` quiet; `--dump f.bin` records under schema v2 (header+payload dual CRC-32 and record geometry checked by `dump_inspect.py`; single-format mode only; `-m all` emits only an empty stub)
- `--lang zh|en`: UI language (default `zh`)
