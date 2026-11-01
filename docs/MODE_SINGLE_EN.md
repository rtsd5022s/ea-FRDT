# Single-thread mode

## What single-thread mode is

Without `-m all`, one format runs on the main thread: no worker pool, no monitor thread, no scheduling overhead. The loop walks step by step and prints each step until an exit reason fires.

```
float_inf_walk 0 -m fp32
float_inf_walk 0 -m dec64 -k 1.5
float_inf_walk 0 -m x87 -t 10
```

The first positional argument is `delay_ms` (per-step throttle, `0` = no throttle); the start value is always `-a`, never positional. A missing positional in single format prints help and exits `2`.

## Startup sequence

1. Windows: `timeBeginPeriod(1)`, UTF-8 console, ANSI virtual-terminal processing under `#ifdef _WIN32`; register the SIGINT (Ctrl+C) handler.
2. Parse argv: flags plus the optional positional `delay_ms`.
3. Select the format via `-m`, validate parameters, then call the format's drive function.

## Arguments

**`-m <format>`**: select format; default `f32`. See the 17-format list in PARAMETERS.

**`-a <start>`**: start value; default `1.0`.
- Plain number `-a 1.5`; scientific `-a 1.5e-3`; engineering; raw bits `-a 0x3F800000`.
- Keywords: `min-normal`/`min` (smallest normal positive), `max-finite`/`max` (largest finite).
- Negative values walk toward `-inf`; NaN/Inf rejected.

**`-s` / `--subnormal`**: start at the smallest subnormal positive; mutually exclusive with `-a`.

**`-k <factor>`**: per-step multiplier, default `1.02`; `>1` upward to overflow, `0<r<1` downward to zero; `0`/negative/NaN/Inf rejected; scientific notation accepted.

**`-g <mode>`**: step mode, default `x1.02` (alias `x102`).
- `mululp`: multiply by `1 + 1 ULP`
- `addulp`: add `1 ULP` to the mantissa
- `subulp`: subtract `1 ULP` from the mantissa (descending)

**`-f <fmt>`**: `eng` (default, exponent multiple of 3) / `sci` / `full`.

**`-d <n>` / `--digits <n>`**: sci/eng decimals; default `6`, `-1` = full, valid `-1` or `0..100`.

**`--bits hex|bin|off`** (`--showbits`): parenthesized bit pattern; `bin` groups sign/exponent/coefficient; default `off`.

**`-t <sec>`**: timeout seconds; default `0` (run until a termination reason); negative clamps to `0`.

**`-l <n>`**: loop count; default `1`; `0` treated as `1`; `-1` infinite; `>1` repeats (cap 1,000,000); values `< -1` rejected.

**`-q` / `--quiet`**: suppress the live per-step and finish lines; the result is still captured in `RunResult`. `--json`/`--dump` imply quiet.

**`--json`**: one machine-readable object (format/steps/elapsed/steps_per_sec/reason/final_value/params/system/timestamp).

**`--dump <file>`**: 256-byte header + records; payload CRC-32 at `0xE0`; see `dump_inspect.py`.

**`--lang zh|en`**: UI language; default `zh`.

## Main loop

The drive template loops: check timeout → compute next value → increment step → classify overflow/underflow/identity/NaN → render (unless quiet) → sleep `delay_ms`. Pseudocode:

```
current = start; steps = 0; t0 = now
while (!exited) {
    if (timeout_fired) { reason = timeout; break }
    current = step(current)          // factor / mululp / addulp / subulp
    steps++
    classify(current)                 // overflow / underflow / identity / nan
    if (!quiet) print(current)
    if (delay_ms > 0) sleep(delay_ms)
}
store result in RunResult
```

## Exit detection by path

- **Native (FP16/FP32/FP64)**: `std::isinf` after multiply; `x == 0` / subnormal floor for underflow; NaN reachable here only.
- **Bit-level (FP4/FP6/FP8/BF16/TF32)**: exponent field all-ones = overflow; exponent all-zero with zero mantissa = zero; subnormal handled explicitly.
- **x87 / FP128**: pure integer arithmetic; exponent compared against Emax/Emin; intermediate products use `__int128` helpers.
- **BID dec32/64/128**: overflow uses top-digit exponent `exp10 + digits - 1 > Emax` (Emax 96 / 384 / 6144); underflow compares against Emin (−95 / −383 / −6143).
- **DPD ×3**: same decimal boundary predicates, packed through the DPD codec; startup runs a pack→unpack self-check.

## Six enumerated exit reasons

- `overflow->inf`, `underflow->0`, `identity->stop` (no progress), `safety->limit` (`2^120` counter), `timeout`, `nan->nan` (native only).
- Non-enum outer messages: `cancelled` (confirmation declined/timed out, mainly `-m all`), `sigint` (Ctrl+C).

## Timeout and loops

`-t 10` stops after 10 s; the atomic timeout flag is checked each iteration; on stop the reason is `timeout`. `-l 3` repeats the walk three times (reset to start after each pass); `-l -1` repeats indefinitely. Combined with `-t 0`, `-l -1` raises a level-1 confirmation warning in `-m all` (a warning, not a hard rejection); pairing a timeout is strongly recommended, especially over SSH.

## Ctrl+C

The handler sets a sigint flag; the loop notices it on the next iteration, prints the bilingual early-exit message (`sigint`), stores the result, and exits.

## Reference baselines (×1.02 from 1.0, overflow steps)

Approximate anchors, machine-dependent; see `BASELINE_EN.md` for the maintained table:

| Format | Order of magnitude |
|---|---|
| FP4/FP6/FP8 | a handful to a few hundred steps |
| FP16 | ~600 |
| BF16/TF32/FP32 | a few thousand |
| Dec32 / DPD32 | ~11,279 |
| FP64 | tens of thousands |
| Dec64 / DPD64 | ~44,767 |
| x87 / FP128 | hundreds of thousands |
| Dec128 / DPD128 | seven figures |

## Input validation (hard exits, code `2`, stderr)

Unknown format/flag; unparseable start/factor; factor 0/negative/non-finite; `--loop < -1` or `> 1,000,000`; conflicting `-a`/`-s`; malformed `0x` pattern. Negative timeout clamps to `0` rather than rejecting; `--loop 0` is treated as `1`. Error strings pass through `sanitize()`.

## Exit codes

- `0`: normal end (any driver reason), `sigint`, or `cancelled`.
- `2`: argument/validation error.

## Choosing a format

- Quick smoke: FP32/FP64; low precision: FP4/FP6/FP8; high precision: FP128/Dec128; decimal behavior: dec32/64/128 or DPD×3; 80-bit extended: x87.
- `-q` suppresses flooding on very long formats; `--bits bin` inspects field transitions; `--dump` records every raw record for offline replay.

## Notes

1. Without `-q`, long walks (FP128/Dec128) flood the screen; use `-q` or `-t`.
2. `-g addulp` takes the most steps (one ULP per step); `-g mululp` grows step size with magnitude.
3. `-a max-finite -k 0.5` measures descent from the largest finite value toward zero.
4. Single mode does not pin cores; the OS may migrate the thread.
