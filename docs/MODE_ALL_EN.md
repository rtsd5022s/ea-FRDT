# Concurrent mode (-m all multithreaded)

## What is concurrent mode

Concurrent mode runs multiple threads simultaneously, each thread independently running the full 17-format chain. Threads don't share state; each has its own current value, step count, and result list.

Add `-m all` to the command line:

```
float_inf_walk 0 -m all
float_inf_walk 0 -m all -j 1-4
float_inf_walk 0 -m all -t 10 -q
```

The biggest difference from single-thread mode: not running one format at a time, but each thread running the full set of 17 formats. Thread count is controlled by `-j`, each thread pinned to a specified core.

## Startup sequence

`-m all` enters the `run_all` function:

1. Parse core list (`-j` arg), determine thread count and core pinning
2. Print banner: CPU model, logical cores, physical cores, factor, throttle, thread count, format count
3. Create WorkerSlot for each worker, `alignas(64)` cache-line aligned
4. Start worker thread pool, each thread value-copies parameters, pins core, runs format chain
5. Start monitor thread, periodically refreshes screen showing each worker status
6. Wait for all workers to finish
7. Print summary table

## Core ID parsing

`-j` supports four syntaxes:

- **Single**: `-j 4` uses only core 4
- **Comma**: `-j 1,3` uses cores 1 and 3
- **Range**: `-j 1-4` uses cores 1 through 4
- **Mixed**: `-j 1,3-5` uses cores 1, 3, 4, 5

IDs are 1-based, not 0-based. `-j 1` is the first logical core.

Parsing is in `parse_core_list`: split by comma, each segment with `-` expands as a range. Result is a core ID array.

Hard cap is `std::thread::hardware_concurrency()`, excess gets truncated. On an 8-core CPU, `-j 1-16` truncates to 1-8.

## WorkerSlot structure

One WorkerSlot per worker:

```cpp
struct alignas(64) WorkerSlot {
    RunResult cur;
    std::vector<RunResult> done;
    int loop_done = 0;
    u128 total_steps = 0;
    std::vector<u128> sum_steps;
};
```

`alignas(64)` is key: align the whole struct to 64 bytes (CPU cache line size). Each worker's data occupies its own cache line, so when multiple cores read/write simultaneously they don't step on each other's cache line, avoiding false sharing.

Fields:
- `cur`: result for the currently running format
- `done`: result list for all formats in this round
- `loop_done`: current loop count
- `total_steps`: cumulative total steps (u128 to prevent overflow)
- `sum_steps`: cumulative steps per format

## Worker thread

What each worker thread does:

```
pin to specified core
reset loop counter
while (not done) {
    loop counter++
    if (timeout) break
    clear done list
    for (each format in chain) {
        cur = fresh empty result
        run this format
        append cur to done
        total_steps += cur.steps
        sum_steps[format] += cur.steps
    }
}
```

Parameters are value-copied, each worker has its own copy, no sharing. Format chain is also a value-copied vector.

## Core pinning

Each worker thread pins to a specified core:

- **Windows**: `SetThreadAffinityMask(GetCurrentThread(), 1 << (core_id - 1))`, also set `THREAD_PRIORITY_TIME_CRITICAL`
- **Linux**: `pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset)`

Cross-platform logic is isolated in `platform.h`, worker code just calls `fw::plat::bind_thread_to_core(core_id)`.

Pinning benefits: threads don't drift between cores, always run on the same core, cache hit rate high. Without pinning, OS scheduler may move threads around, cache goes cold.

## Monitor thread

The monitor thread is a separate thread responsible for screen refresh:

```
while (workers not done) {
    sleep(refresh_ms)
    clear screen
    for (each worker) {
        print [wN] format current speed time loop
    }
}
```

Refresh interval `-r`:
- `-m all` defaults to 1000ms (1 second)
- Single-thread mode defaults to 0 (every step)

Monitor line format:

```
[w1] FP32: 1.234567e+10   4500000 steps/s   1.234s   loop=1/3
```

Time display is layered: under 1 minute shows seconds only, over 1 minute shows min:sec, over 1 hour shows hr:min:sec, over 1 day shows day:hr:min:sec.

## Timeout

Timeout is global, all workers share `g_timeout_sec` and `t0`. Each worker checks at each loop start:

```
elapsed = now - t0
if (elapsed >= g_timeout_sec) break
```

After timeout, workers exit, wait for other workers to exit, then print summary.

## Loops

`-l 3` runs 3 passes through the format chain. After each pass (all formats overflow/underflow), clear done list, reset cur, start next pass.

`-l -1` infinite loop, pair with `-t`.

In loop mode the monitor shows `loop=1/3`, `loop=2/3`, `loop=3/3`. Infinite loop shows `loop=1/-1`.

## Summary table

After all workers finish, each worker prints a section:

```
--- worker 1 ---
format      single steps  total steps  exit            time ms  total time
FP32        4481          13443        overflow->inf   0.5      1.5
FP64        35843         107529       overflow->inf   4.2      5.7
...
Total: 120972 steps, 5.7s = 21223 steps/s
```

Columns:
- Format name
- Single-pass steps: steps for this format this round
- Cumulative steps: all loops combined
- Exit reason: overflow->inf / underflow->0 / timeout / cancelled
- Single-pass time: how long this format took this round
- Total time: wall clock from start

Last row is total: total steps, total wall clock, average speed.

## DPD warning

If the format chain includes DPD32/DPD64/DPD128, a yellow warning shows before start:

```
[!] DPD algorithm is computationally heavy, performance may drop 20-30x,
higher precision even slower. Press y to continue, auto-cancel in 10s...
```

Press y to continue, auto-cancels if no key within 10 seconds. Windows uses `_kbhit`/`_getch` non-blocking keyboard check, Linux uses `std::cin.read` blocking read.

`-m all` doesn't include DPD formats by default; specify manually with `--only dpd32,dpd64`.

## Format chain order

17 formats run in this order:

```
FP4 → FP6 → FP8E4M3 → FP8E5M2 → FP16 → BF16 → TF32 → FP32
→ Dec32 → DPD32 → FP64 → Dec64 → DPD64
→ x87 → FP128 → Dec128 → DPD128
```

Small to large, small formats first. Each worker runs this full chain.

`--only` specifies a format subset: `--only fp32,fp64` runs only FP32 and FP64.

## Performance considerations

Performance bottlenecks in concurrent mode:

- **Core pinning**: eliminates core migration, high cache hit rate
- **alignas(64)**: eliminates false sharing, multi-cores don't step on each other's cache lines
- **Value copy**: workers don't share data, no locks needed
- **Independent monitor**: monitor doesn't block workers, only reads WorkerSlot periodically

x87 and FP128 are pure software integer math, slower than native formats. DPD has lookup table overhead. These are determined by computation volume, not by the concurrency model.

## Notes

1. `-m all`: every worker runs the full 17 formats, not assigned different formats per worker.
2. `-j` IDs are 1-based, hard cap is logical core count.
3. DPD formats are not in the default `-m all` chain, add manually.
4. `alignas(64)` eliminates false sharing, removing it causes cache ping-pong slowdown.
5. `-r` refresh interval defaults to 1 second, too frequent slows workers.
6. Monitor time display is layered, doesn't always show seconds.
7. Summary table's "single-pass steps" is the last pass, not average.
8. Infinite loop `-l -1` must pair with `-t`, otherwise runs forever.
9. Core pinning may fail in containers/VMs without permission, workers run unpinned.
10. Quiet mode `-q` skips the monitor, prints summary directly after done.
