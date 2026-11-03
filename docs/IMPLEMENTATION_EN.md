# ea-FRDT Current Implementation

## Project positioning

ea-FRDT (Extreme Architecture - Floating-point Rigorous Devil Test) is a C++23 command-line floating-point stress tool. It starts from a floating-point value and steps repeatedly (multiply by a factor, multiply by relative ULP, add absolute ULP, or subtract absolute ULP), printing the result at each step (or staying quiet under `-q`) until the format overflows to infinity, underflows to zero, reaches the `2^120` safety counter, times out, or stops with no progress. The tool does not report GFLOPS. It answers one question: **at which step, with which bit pattern, and after how many operations does this format break**.

It supports 17 floating-point formats across five paths: native hardware, hand-rolled bit-level simulation, pure-software binary extended precision, IEEE 754-2008 decimal BID, and IEEE 754-2008 decimal DPD. Concurrent mode (`-m all`) pins one worker per core and runs the format chain independently on each.

## The 17 formats

`-m all` iterates the fixed enum order below; the default chain omits entries 9, 12, 15, 16 (DPD32/64/128 and Dec128), i.e. 13 of 17, addable via `--only`.

| # | Format | Width | Path | Notes |
|---|---|---|---|---|
| 0 | FP4 (E2M1) | 4 | Bit-level | Smallest experimental format |
| 1 | FP6 (E3M2) | 6 | Bit-level | Same family |
| 2 | FP8 E4M3 | 8 | Bit-level | OCP FP8 |
| 3 | FP8 E5M2 | 8 | Bit-level | IEEE 8-bit float |
| 4 | FP16 | 16 | Native | `_Float16` under Clang, `std::float16_t` under GCC |
| 5 | BF16 | 16 | Bit-level | Brain Float, 8-bit exponent |
| 6 | TF32 | 19 | Bit-level | TensorFloat-32, 8 exp + 10 mantissa |
| 7 | FP32 | 32 | Native | float |
| 8 | Dec32[BID] | 32 | BID | IEEE 754 decimal |
| 9 | DPD32 | 32 | DPD | IEEE 754 densely packed decimal |
| 10 | FP64 | 64 | Native | double |
| 11 | Dec64[BID] | 64 | BID | IEEE 754 decimal |
| 12 | DPD64 | 64 | DPD | IEEE 754 densely packed decimal |
| 13 | x87 | 80 | Pure software | 80-bit extended, pure integer simulation |
| 14 | FP128 | 128 | Pure software | binary128, 112-bit mantissa, 224-bit intermediate |
| 15 | Dec128[BID] | 128 | BID | Hand-rolled UInt256 big-integer multiply |
| 16 | DPD128 | 128 | DPD | IEEE 754 densely packed decimal |

## Module architecture

The codebase is modular: 11 top-level headers in `include/`, a `formats/` sub-module (6 shared engines + `formats_all.h` + 17 thin per-format headers), and 5 non-template source files in `src/`. There is no monolithic format header.

```
include/
  base_types.h      Enums (StepMode/DispFmt/StartKind/ShowBits/FmtId), u128 step
                    counter, RunResult, global timeout atomic
  common.h          Standard-library aggregation; NativeF16/F32/F64 aliases
  lang.h            Bilingual tr() table (zh/en)
  color.h           ANSI color constants
  format_util.h    Bit-string to hex/bin, number formatting, parse_number
  drive.h           Stepping driver template, reason/step name tables
  runner.h          ALL mode: worker pool, monitor, summary table, warning confirm
  platform.h        OS abstraction: affinity, high-res timer, core counts
  uint128.h         Derived from UInt256 for 128-bit helpers
  uint256.h         Hand-rolled 256-bit big integer, schoolbook multiply, div10
  dpd128.h          Early minimal DPD encoder header; no longer referenced by the v2 writer (kept on disk). The DPD128 engine lives in formats/engine_dpd.h
  formats/
    formats_all.h
    engine_sim.h    SVal bit-level engine (fp4/fp6/fp8/bf16/tf32)
    engine_native.h Native float16/float/double engine
    engine_bid_codec.h   BID combo5 pack/unpack codec
    engine_decnum.h     BID dec32/64/dec128 coefficient arithmetic
    engine_dpd_codec.h  Single declet (3 digits -> 10 bits) Clarke table
    engine_dpd.h        combo5 + fixed-length symmetric pack/unpack, real
                        pack->unpack self-check, run_dpd for dpd32/64/128
    fp4 fp6 fp8_e4m3 fp8_e5m2 bf16 tf32 fp16 fp32 fp64
    dec32 dec64 dec128 dpd32 dpd64 dpd128 x87 fp128   (17 thin headers)
src/
  float_inf_walk.cpp  main(), argv parsing, single-format dispatch,
                      256-byte schema v2 dump header (header+payload dual CRC-32, explicit length/record geometry/run snapshot)
  drive.cpp           Non-template driver definitions (reason_name, ...)
  lang.cpp format_util.cpp platform.cpp
```

## Core data structures

**UInt256**: four `uint64_t` words, little-endian word order. Provides `mul64` (64×64→128, split into 32×32 quarters), `mul128` (128×128→256 via mul64 calls plus carry propagation), `div10` (divide by 10 with remainder, short division), `ge` (greater-or-equal compare), `to_lo` (low 64 bits). No add/subtract/shift; added on demand.

**DecNum** (BID dec32/64): `uint64_t coeff` + `int exp10`. BID layout pack/unpack (combo5 joint encoding) plus a round-trip self-check.

**BigDec** (BID dec128): coefficient is `UInt256`, exponent is `int`. Multiplying by 1.02 = coefficient × 102, exponent − 2, then renormalize: if the coefficient exceeds `10^33 − 1`, divide by 10 and increment exponent until back in range.

**DpdNum** (DPD ×3): coefficient is a `std::vector<int>` of decimal digits; multiplication is per-digit with carry in plain integer arithmetic. Packing encodes each decimal triplet into 10 DPD bits; the coefficient uses a fixed-length symmetric pack/unpack bitstring sub-unit, and startup runs a real pack→unpack round-trip self-check (`[DPD-SELFCHECK] OK`; a `MISMATCH` on stderr means the round-trip failed).

**SVal** (bit-level): parameterized by `E` (exponent bits), `M` (mantissa bits), `bias`; sign/exponent/mantissa get/set plus overflow/underflow/normalize/subnormal detection. FP4/FP6/FP8/BF16/TF32 use it.

**WorkerSlot**: `alignas(64)`, one cache line; worker id, current format, current value string, step count, steps/s, elapsed ms, loop count, done flag. Each worker writes only its own slot; no locks.

## Step modes

| Mode | Flag | Behavior |
|---|---|---|
| x1.02 (default) | `-k 1.02` / `-g x1.02` | Multiply by `factor` each step; `factor<1` walks down; `0`/negative/non-finite rejected |
| Relative ULP | `-g mululp` | Multiply by `1 + 1 ULP` each step |
| Absolute ULP add | `-g addulp` | Add `1 ULP` to the mantissa each step |
| Absolute ULP sub | `-g subulp` | Subtract `1 ULP` from the mantissa each step (descending) |

## Start values

| Start | Flag | Notes |
|---|---|---|
| Default | (none) | Start at `1.0` |
| Numeric / raw | `-a <x>` | Number, scientific/engineering notation, or `0x` raw bit pattern; negatives walk toward `-inf`; NaN/Inf rejected |
| Min normal | `-a min-normal` (`min`) | Smallest **normal** positive number |
| Max finite | `-a max-finite` (`max`) | Largest finite positive number |
| Min subnormal | `-s` / `--subnormal` | Smallest subnormal positive; mutually exclusive with `-a` |

The first positional argument is `delay_ms` (throttle), not the start value.

## Concurrency model

In `-m all`:

1. Parse `--only`; otherwise build the default 13-format chain (DPD32/64/128 and Dec128 omitted).
2. Gather warnings; if the chain includes DPD or Dec128, a level-2 red `[!!!]` confirmation prints and waits up to 10 s for `y`; other keys/timeout return `cancelled`. (This unified confirmation runs in `-m all`.)
3. Print CPU brand (CPUID leaf `0x80000002–4`), logical core count; physical count via `GetLogicalProcessorInformation` on Windows, `0` (skipped) on Linux.
4. Spawn workers per `-j` (`1`, `1,3,5`, `1-4`, `1,3-5`); empty spec = all logical cores; out-of-range ids dropped.
5. Each worker pins to a core (Windows `SetThreadAffinityMask`, Linux `pthread_setaffinity_np`; failure degrades to unpinned) and receives value-copies of chain and parameters.
6. Monitor refreshes every `-r` ms (`1000` default in `-m all`); one line per worker: `[wN] Format: value steps/s elapsed loop=N`.
7. `LIVE:` aggregates current-segment throughput across workers.
8. On completion, print a summary table: per-worker per-format step counts, exit reason, elapsed, total elapsed.
9. Time display is layered Y/M/D/H/M/S/ms; all layers render as duration grows.
10. Loop display: `-l 1` omits `loop=`; `-l N` shows `loop=current/total`; `-l -1` shows `loop=current`.

## BID path

BID (Binary Integer Decimal) stores the decimal coefficient as a binary integer: `uint64_t` for dec32/64, `UInt256` for dec128. Layout follows IEEE 754-2008: 1 sign bit, combination field, exponent continuation plus coefficient; the top combination bits encode NaN/infinity/finite. Multiplying by 1.02 = coefficient × 102, exponent − 2; normalization divides by 10 and increments exponent while the coefficient exceeds range; rounding (ties-to-even) is decided by the `div10` remainder.

## DPD path

DPD (Densely Packed Decimal) encodes 3 decimal digits into 10 binary bits. The path is independent of BID: coefficient = `std::vector<int>` digits, per-digit multiplication. DPD32/64/128 are absent from the default `-m all` chain (pure software, no hardware decimal FPU; roughly 20–30× slower). Inclusion raises a level-2 red confirmation. A single-declet Clarke table lives in `engine_dpd_codec.h`; multi-declet packing and the round-trip self-check live in `engine_dpd.h`.

## Pure-software binary formats

**x87** (80-bit): 1 sign, 15 exponent, 64 mantissa bits (including the explicit leading bit); pure integer simulation, no `long double`.

**FP128** (128-bit): 1 sign, 15 exponent, 112 mantissa bits; pure integer simulation with a 224-bit intermediate product; no `__float128` or libquadmath.

These exist to remove compiler-extension dependence: GCC `long double` is 80-bit on x86-64 but 128-bit on ARM; libquadmath is GCC-proprietary. Pure software keeps behavior consistent across GNU targets.

## Safety guards

Hard exits (code `2`, stderr) only on invalid input: zero/negative/non-finite factor, unparseable start, unknown format/flag, `--loop` outside `-1..1000000`, malformed `0x` pattern, conflicting special starts.

Non-fatal warnings gathered and confirmed in `-m all` (level-1 yellow `[!]`, level-2 red `[!!!]`, 10 s `y` gate):

- `-l -1` with `-t 0`: level-1 warning (may peg CPU); a warning, not a rejection.
- `-k < 1e-10`: level-1 warning (very many steps likely).
- Included DPD×3 / Dec128: level-2 red confirmation.

The step counter is `unsigned __int128` (`u128`); the driver safety limit is `2^120`, unreachable in ordinary runs. Ctrl+C surfaces the bilingual `sigint` message.

## Output formats

- Engineering notation (default), 6 mantissa digits; `-f sci`, `-f full`; `-d`/`--digits` controls decimals (`-1` = full, range `0..100`).
- `--bits hex|bin|off` (`--showbits`) appends the bit pattern in parentheses; `bin` groups sign/exponent/coefficient.
- `--json` emits one machine-readable object (implies quiet); `--dump <file>` applies to single-format mode only: it writes a 256-byte schema v2 fixed header plus per-step records, with a header CRC-32, a payload CRC-32, and explicit payload length/record geometry; `python dump_inspect.py <file>` verifies both CRCs and length. `-m all` runs concurrent mixed-width workers and does not serialize records; it emits only an empty header stub with flags bit2=empty.
- Bilingual UI via `--lang zh|en` (default `zh`).

## Dump file format (schema v2, fixed 256 bytes; multi-byte integers little-endian)

Records follow the header at offset `256 .. EOF`. The layout below is what `dump_inspect.py` uses to recompute header CRC, payload CRC, and record geometry.

| Offset | Type | Field | Meaning |
|---|---|---|---|
| 0x00 | char[8] | magic | `"EAFRDT2\0"` |
| 0x08 | u8 | schema_version | fixed 2; 0x09 reserved zero |
| 0x0A | u8 | payload_endian | 0=little (x86 native), 1=big |
| 0x0B | u8 | packed | 0=natural width, 1=tight (e.g. tf32=3B instead of 4B) |
| 0x0C | u32 | flags | bit2 (0x4)=empty (no records, e.g. `-m all` or an aborted run) |
| 0x10 | u32 | header_crc32 | IEEE 802.3 reflected polynomial; this 4-byte slot counts as zero while computing |
| 0x14 | u32 | payload_crc32 | over `[256, 256+payload_bytes)`; compatible with Python `binascii.crc32` |
| 0x18 | u64 | payload_bytes | authoritative payload length in bytes |
| 0x20 | u64 | record_count | payload_bytes/record_size when record_size > 0 |
| 0x28 | u32 | record_size | authoritative bytes per record; tight width under packed |
| 0x2C | u8 | format_id | 0..16 per the format table |
| 0x2D | u8 | step_mode | 0=x1.02, 1=mululp, 2=addulp, 3=subulp |
| 0x2E | u8 | start_kind | 0=numeric default, 1=subnormal, 2=min-normal, 3=max-finite |
| 0x2F | u8 | reason | 0 overflow, 1 underflow, 2 identity, 3 safety, 4 timeout, 5 nan, 0xFF=unknown/not run |
| 0x30 | f64 | start_value | IEEE binary64 start value |
| 0x38 | f64 | factor | IEEE binary64 step factor |
| 0x40 | i64 | timestamp_unix_s | Unix seconds (UTC) |
| 0x48 | u64 | elapsed_us | wall-clock microseconds of the run |
| 0x50 | u32 | timeout_sec | timeout limit in seconds (0=unlimited) |
| 0x54 | i32 | loop_count | repetitions (-1=infinite) |
| 0x58 | f64 | delay_ms | per-step throttle milliseconds |
| 0x60 | char[32] | format_name | NUL-padded, max 31 chars |
| 0x80 | char[32] | tool_version | e.g. `v1.0.6` |
| 0xA0..0xFF | bytes | reserved | reserved zero padding |

`dump_inspect.py` exit codes: `0`=header/payload CRC, file size, and record geometry all consistent; `1`=readable but a CRC mismatches, or file size / `record_count×record_size` disagrees with payload_bytes; `2`=not v2 (too short, bad magic, legacy v1 layout); `3`=cannot open/parse. The legacy v1 layout (`0xC0` DPD factor bits, `0xE0` payload CRC, `0xE8` ad-hoc CPU hash) is no longer parsed; old samples are reported as unsupported instead of crashing.

**Raw-stream mode `--dump-raw` / `--dump-no-header`** (a modifier for `--dump`; two equivalent aliases): the 256-byte header above is omitted — no header reserve/write, no CRC, no length alignment; the file receives a continuous raw bit stream appended from offset 0. No u128 step prefix is stored (the step count still feeds the terminal display and `--json`). Record width still follows `--dump-packed` and byte order still follows `--dump-endian`. Single format only; `-m all` (mixed interleaved widths) is unsupported and closes/deletes the output file. The stream has no magic and no length, so `dump_inspect.py` does not recognize it (exit code 2, treated as non-v2); parse externally by fixed record width or hex dump.

## Build

```bash
# CMake (Release default, -Wall -Wextra, output float_inf_walk; CMake 3.20+)
cmake -B build
cmake --build build -j

# Linux direct
g++ -std=c++23 -O2 -Wall -Wextra -pthread -Iinclude src/*.cpp -o float_inf_walk

# Windows MSYS2 clang64 release (recommended; thin LTO, section GC, static)
C:\msys64\clang64\bin\clang++.exe -std=c++23 -Oz -flto=thin \
    -ffunction-sections -fdata-sections -Wl,--gc-sections -static \
    -Iinclude src/*.cpp -lwinmm -o float_inf_walk
```

Requires C++23 (GCC 13+ or Clang 16+); MSVC unsupported. No third-party dependencies (no libquadmath/Boost/Eigen). CI (GitHub Actions) builds on Ubuntu with GCC and Clang and runs short fp32/dec smoke tests, without `-Werror` (warnings do not fail the job). The CMake and direct-compile commands above build only the CLI `float_inf_walk`; `libeafrdt/` sits outside this build and CI and is built manually per `libeafrdt/README.md`.

## Known limitations

- UInt256 has no add/subtract/shift; dec128 needs only multiply and divide-by-10.
- DPD declet chaining uses a fixed-length symmetric pack/unpack sub-unit with a real pack→unpack self-check (debt #28 closed); the former "runs but not compact" gap is closed.
- The `u128` counter and the 64×64 mantissa intermediates of both x87 and fp128 use compiler `unsigned __int128`; dec128(BID) coefficients use hand-rolled UInt256 (pure 64-bit-word schoolbook, no `__int128`); x87 stores one 64-bit mantissa word plus a small integer exponent, no `long double`.
- No MSVC, no GPU offload, no WebAssembly target; g++ 16.2.0 has a local ICE (use Clang).
- Physical core count on Linux returns `0` (display skipped); Windows uses `GetLogicalProcessorInformation`.
