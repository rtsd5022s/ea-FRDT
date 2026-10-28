# Single-thread mode

## What is single-thread mode

Single-thread mode runs one float format on one thread. No worker pool, no monitor thread, no thread scheduling overhead. The main loop runs on the main thread, step by step, printing each step until overflow or underflow.

Without `-m all` on the command line, you're in single-thread mode:

```
float_inf_walk 0 -m fp32
float_inf_walk 0 -m dec64 -k 1.5
float_inf_walk 0 -m x87 -t 10
```

Single-thread mode is simple and direct. No thread scheduling overhead, no cache-line-aligned WorkerSlot, no monitor thread. Just a for loop: multiply, check overflow, print, repeat.

## Startup sequence

What main does on entry:

1. On Windows: call `timeBeginPeriod(1)` to bump system timer to 1ms, set console to UTF-8, enable ANSI escape processing
2. Register SIGINT handler (Ctrl+C)
3. Parse command line arguments
4. Select format based on `-m`
5. Call the corresponding drive function to start walking

Argument parsing is in main, split into positional and optional. First positional arg is delay_ms (per-step throttle), rest are `-x value` optional args.

## Argument parsing

Recognized arguments in single-thread mode:

**`-m <format>`**: Select format. Default f32. See format list for options.

**`-a <start>`**: Start point. Default 1.0. Can be:
- Plain number: `-a 1.5`
- Scientific: `-a 1.5e-3`
- Hex raw: `-a 0x3f800000`
- Keywords: `min-normal` (smallest normalized), `max-finite` (largest finite)
- Negative: `-a -1`

**`-k <factor>`**: Per-step factor. Default 1.02. >1 walks up to overflow, <1 walks down to underflow. Scientific notation supported.

**`-g <stepmode>`**: Step mode. Default multiplicative (×1.02). Options:
- `mululp`: each step ×(1+1ULP)
- `addulp`: each step +1ULP

When mululp or addulp is selected, the x1.02 factor display is hidden.

**`-f <fmt>`**: Display format. Default engineering. Options:
- `eng`: engineering notation, exponent multiples of 3
- `sci`: scientific notation
- `full`: full display, all significant digits

**`-d <n>`**: Decimal places. Default 6. Range 0 to 100. `-d -1` means full display.

**`--bits <mode>`**: Bit display in parentheses. Default off. Options:
- `hex`: hex bit pattern
- `bin`: binary bit pattern, segmented
- `off`: no display

**`-t <sec>`**: Timeout seconds. Default 0 (no limit, walk until overflow/underflow).

**`-l <n>`**: Loop count. Default 1. `-l -1` infinite loop.

**`-s`**: Start from smallest denormal.

**`-q`**: Quiet mode, no live printing, print result only after done.

**`--lang <zh|en>`**: UI language.

## Main loop

The main loop is in the drive function. drive is a template function dispatched by format type.

Pseudocode:

```
current = start
steps = 0
t0 = now
while (not exited) {
    if (timeout) break
    old = current
    current = current * factor    // or addulp/mululp
    steps++
    if (overflow) {
        print overflow->inf
        break
    }
    if (underflow) {
        print underflow->0
        break
    }
    if (per-step refresh) {
        print current
    }
    if (delay_ms > 0) {
        sleep(delay_ms)
    }
}
print final result
```

## Overflow detection

Each format detects overflow differently:

**Native formats (FP16/FP32/FP64)**: After multiply, check `std::isinf(x)`. If inf, overflow.

**Hand-rolled bit formats (FP4/FP6/FP8/BF16/TF32)**: Check if exponent field is all 1s. If exponent becomes all 1s after multiply, overflow. Or check if raw exceeds max finite raw.

**x87/FP128**: Pure software, check if exponent exceeds Emax. If exponent > Emax, mark overflow.

**BID decimal**: Check `exp10 + digits - 1 > Emax`. Not simple exp10 compare, because coefficient can carry after multiply, need to add significant digits. Emax is 96 for Dec32, 384 for Dec64, 6144 for Dec128.

**DPD decimal**: Same overflow check as BID, but encoding goes through DPD route.

## Underflow detection

Underflow means value is very small, approaching zero.

**Native formats**: After multiply, check `x == 0` or smallest denormal. If value < smallest normalized, enter denormal range. Keep multiplying until zero.

**Hand-rolled bit formats**: Check if exponent field is all 0s. If exponent all 0s and mantissa all 0s, it's zero.

**Decimal formats**: Check if exp10 < emin. emin is -95 for Dec32, -383 for Dec64, -6143 for Dec128.

## Print format

Each step prints one line:

```
FP32: 1.234567e+10   4481 steps
```

Or with bit display:

```
FP32: 1.234567e+10   [3f800000]
```

Engineering notation mantissa has fixed 6 decimal places. Scientific doesn't force exponent to multiples of 3. Full display doesn't truncate.

Quiet mode `-q` doesn't print intermediate steps, only prints a summary line at the end.

## Timeout control

`-t 10` means stop after 10 seconds. Timeout is checked at each loop start:

```
if (g_timeout_sec > 0) {
    elapsed = now - t0
    if (elapsed >= g_timeout_sec) break
}
```

After timeout, print `timeout` exit reason, then print result.

## Loops

`-l 3` means run 3 passes. After each pass (overflow or underflow), reset current to start, start over. After 3 passes, print summary.

`-l -1` infinite loop, must pair with `-t`.

## Ctrl+C

On Ctrl+C, signal handler sets `g_sigint = true`. Main loop sees the flag at next step, prints "Program exited early (Ctrl+C received)", prints result, exits.

## Single-thread format chain

Single-thread mode runs one format at a time. To see results for multiple formats, run the command multiple times. This is the biggest difference from `-m all` concurrent mode.

## Performance

Single-thread mode has no thread overhead, no locks, no cache-line alignment. Pure computation. The bottleneck is the float arithmetic itself.

x87 and FP128 are pure software integer math, slower than native FP64. DPD formats use lookup tables and per-digit decimal arithmetic, with no hardware FPU acceleration.

## Exit reasons

On exit, prints an exit reason line:

- `overflow->inf`: overflowed to infinity
- `underflow->0`: underflowed to zero
- `timeout`: timed out
- `cancelled`: DPD warning cancelled
- `sigint`: Ctrl+C received

## Notes

1. Single-thread mode prints every step by default. Formats with many steps (FP128 hundreds of thousands) will flood the screen. Add `-q` for quiet.
2. `-k 1.02` from 1.0: FP32 takes 4481 steps to overflow. FP128 takes 570k steps.
3. `-k 0.5` decreasing: from 1.0 underflows to 0 quickly.
4. `-a max-finite` with `-k 0.5` walks down from max, measures steps from max to zero.
5. `-s` starts from smallest denormal, walks through denormal range before normalized range.
6. DPD formats show a warning on first run, press y to confirm.
7. `-d -1` full display, no decimal truncation, good for exact values.
8. `--bits bin` segmented binary display, sign/exponent/mantissa space-separated.
9. Negative start with `-k 1.02` walks to -inf first.
10. Single-thread mode doesn't pin cores, OS schedules automatically, may drift between cores.
