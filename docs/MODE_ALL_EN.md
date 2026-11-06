# Concurrent mode (`-m all`, multithreaded)

## What concurrent mode is

`-m all` runs one worker per selected logical core; every worker independently walks the default format chain. Workers share no mutable state; each holds its own current value, step count, and result list.

The default chain has **13 formats**: DPD32/DPD64/DPD128 and Dec128(BID) are excluded by default and added only via `--only`.

```
float_inf_walk 0 -m all
float_inf_walk 0 -m all -j 0-3
float_inf_walk 0 -m all -t 10 -q
```

Difference from single mode: not one format per invocation, but each worker repeatedly walks the chain. Worker count and core set are controlled by `-j`; each worker binds to its assigned core.

## Startup sequence

`-m all` enters `run_all`:

1. Parse the `-j` core spec, decide worker count and affinity.
2. Print banner: CPU brand, logical cores, physical cores (Linux reports `0` and hides the field), factor, throttle, worker count, chain length.
3. Create one `alignas(64)` `WorkerSlot` per worker.
4. Spawn worker threads: value-copy parameters, bind the core, walk the chain.
5. Spawn the monitor, refreshing per `-r`.
6. Join all workers.
7. Print the summary table; `-q` suppresses the live monitor and keeps the table.

## Core ID parsing (`-j` / `--jobs`)

**Zero-based**: IDs index logical processors directly; the first core is `0`. Empty spec (default) selects every logical core `0..hw-1`.

- Single: `-j 3` = core `3`
- Comma: `-j 0,2` = cores `0, 2`
- Range: `-j 0-3` = cores `0,1,2,3`
- Mixed: `-j 0,2-4` = `0,2,3,4`

`parse_jobs` splits on commas, expands `-` ranges, then drops `v < 0 || v >= hw`, sorts, and deduplicates. A spec resolving to empty exits `2` with `expected 0~(hw-1)`.

## WorkerSlot

```cpp
struct alignas(64) WorkerSlot {
    RunResult cur;
    std::vector<RunResult> done;
    int loop_done = 0;
    u128 total_steps = 0;
    std::vector<u128> sum_steps;
};
```

`alignas(64)` pins the struct to a CPU cache line, avoiding false sharing across cores. Fields: current-format result, per-pass result list, loop counter, cumulative `u128` step total, per-format step accumulator.

## Worker thread

```
bind core
loop counter = 0
while (!done) {
    loop counter++
    if (timeout) break
    clear done
    for (fmt : chain) {
        cur = fresh RunResult
        run fmt
        done.push_back(cur)
        total_steps += cur.steps
        sum_steps[fmt] += cur.steps
    }
}
```

Parameters and the chain vector are value-copied; workers never read another's stack.

## Core binding

- **Windows**: `SetThreadAffinityMask(GetCurrentThread(), 1ULL << core_id)` (no offset), and `SetThreadPriority(..., THREAD_PRIORITY_TIME_CRITICAL)`.
- **Linux**: `pthread_setaffinity_np` with `CPU_SET(core_id, ...)`; priority raise is best-effort: try `SCHED_BATCH` (no root), fall back to `nice(-5)`; silently skipped when CAP_SYS_NICE is missing in containers/VMs.

Cross-platform calls go through `fw::plat::bind_thread_to_core` in `platform.cpp`. Binding reduces migration and keeps caches warm; if the container/VM denies permission, binding fails and the worker runs unpinned.

## Confirmation warnings (this gate exists only in `-m all`)

`confirm_warnings` aggregates non-fatal warnings before workers start; within 10 s press `y` to continue, otherwise the run is `cancelled` and exits `0`:

- Level-1 yellow `[!]`: `-l -1` together with `-t 0`; factor `< 1e-10`.
- Level-2 red `[!!!]`: chain includes DPD32/DPD64/DPD128 (slow software declets); chain includes Dec128 (slow UInt256 arithmetic). These warn rather than reject.

Single-format invocations do not go through this gate.

## Monitor

A dedicated thread sleeps `-r` (default `1000` ms in `-m all`; single mode refreshes per step) and prints one line per worker:

```
[w1] FP32: 1.234567e+10   4.50e6 steps/s   1.234s   loop=1/3
```

Elapsed time renders layered (s → m:s → h:m:s → d:h:m:s), refreshed in place with `\r`.

## Timeout and loops

The global `g_timeout_sec` and shared start time gate every worker; on expiry the reason is `timeout`. `-l 3` repeats the chain three passes; the monitor shows `loop=1/3` …; `-l -1` repeats indefinitely. `-l -1` with `-t 0` only raises the level-1 warning above; pairing a timeout remains strongly recommended, especially over SSH.

## Summary table

After join, per-worker sections list each format's single-pass steps, cumulative steps, exit reason, per-pass and cumulative time; the last row totals steps, wall clock, and aggregate throughput. Exit reasons drawn from the seven enums (`overflow->inf`, `underflow->0`, `identity->stop`, `safety->limit`, `timeout`, `nan->nan`, `fixed`; `fixed` only appears with the single-format opt-in `--steps <n>`), plus outer `cancelled` / `sigint`.

## Default chain order (13)

```
FP4 → FP6 → FP8E4M3 → FP8E5M2 → FP16 → BF16 → TF32 → FP32
→ Dec32 → FP64 → Dec64 → X87 → FP128
```

`--only` selects a subset or adds slow formats, e.g. `--only dpd32,dpd64,dpd128,dec128`; `--only fp32,fp64` runs just those two. Small-to-large order means small formats finish first, but a worker waits for its whole chain.

## Performance notes

Binding removes migration; `alignas(64)` removes false sharing; value copies remove locking; the monitor only reads slots. x87/FP128 are pure software and DPD uses per-digit declet arithmetic, so chain throughput is dominated by those formats. Windows uses `THREAD_PRIORITY_TIME_CRITICAL`; Linux uses best-effort `SCHED_BATCH`/`nice(-5)`. Measured throughput fluctuates under background load and thermal throttling.

## Risks (stress-test expectations)

- **System responsiveness**: pinned, high-priority (Windows) workers leave other processes fewer cores; leave cores free (`-j 0-1` on a 4-core box) for responsiveness.
- **Thermals / power**: sustained all-core load heats up and may downclock; laptops and weak-cooling machines throttle within minutes; power draw rises well above idle.
- **Long-running slow formats**: DPD×3 and Dec128 keep a worker loaded longer; they are excluded from the default chain for this reason.
- **Infinite loop**: `-l -1` without a timeout relies on Ctrl+C; a dropped SSH session may orphan the process. Use `-t`.
- **Resource footprint**: slots are kilobytes; memory is not the bottleneck, CPU is. Some platforms have hardware watchdogs reacting to prolonged pegging.
- **Do not run in production**: this is a stress tool, not a production workload.
- **Accuracy**: pure-software integer formats reproduce the same step counts as single-core runs; divergence suggests binding failure or external interference.
- **`--dump` in all mode**: concurrent workers use mixed, interleaved record widths, so no records are serialized in all mode; even with `--dump`, only a 256-byte flags bit2=empty stub is written (`payload_bytes=0`, `record_count=0`, `record_size=0`, `reason=0xFF`). Use single-format `-m <fmt> --dump` for per-step binary records.
