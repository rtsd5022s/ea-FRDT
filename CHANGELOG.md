# Changelog

## Unreleased (main)

### Added
- `--compare <file>`: read-only single-format baseline anchor; locates the last record with `"format":"<token>"`, compares the deterministic steps (decimal string) and exit reason, prints `[compare] ... MISMATCH ...` only on `stderr`, keeps the exit code unchanged, and records a `baseline_compare` object in the single-format JSON.
- W2 opt-in switches, all default-off and none altering the natural-termination step baseline:
  - `--steps <N>`: stop after exactly N steps with reason `fixed` and `params.fixed_steps`; complements the wall-clock `-t` timeout; non-integer or out-of-range values exit 2.
  - `--pin <core>` / `--boost` (single format only): bind the calling thread to one logical core and/or raise thread priority; rejected with exit 2 under `-m all`; a RAII guard restores full affinity and normal priority on every exit path; a capability absent on a platform degrades to a no-op rather than failing.
  - `--dump-events <file>` (single format only): JSONL trace with one `start` record and one `exit` record (reason, steps, escaped value, elapsed_ms); opening the file implies quiet output.
  - `--self-test`: runs pure-logic checks before any stress run — CRC-32/IEEE known vectors, DPD `encode3/decode3` round-trip for 0..999, BID codec packed bit-width and sign/exponent metadata, hex number parsing, and control-character sanitization — then prints `CHECKS=n FAILS=n` and exits.
- W3 opt-in switches, all default-off, none altering the natural-termination step baseline, and never adding a new exit-reason enum:
  - `--max-seconds <s>`: independent wall-clock hard cap; expiry maps to the existing `reason=timeout`; smallest positive cap among `-t` and this one wins; `0` = off.
  - `--preset iron` (alias `--iron`, single format only): enables `--boost` and applies a 60 s default wall cap when `--max-seconds` is not given; it never auto-pins; it reads no temperature sensor and performs no automatic throttling, emitting only a `[iron]` advisory on stderr; rejected under `-m all` (exit 2); JSON `params` records `preset` and `max_seconds`.
  - `--pairwise <A,B>` (alias `--crosscheck`): standalone same-start compare over the minimal whitelist `dec32,dpd32` / `dec64,dpd64` / `f32,tf32`, reporting `MATCH`/`DIFF` on steps and reason; mutually exclusive with the walk/concurrency/dump/scheduling switches (exit 2); `--json` emits `mode=pairwise`, `match`, `engines[]`.
  - `--sample-window <ms>`: quiet/JSON-only per-wall-clock-window steps/s segments summarized as `min`/`avg`/`p95`/`max` in the optional JSON `throughput` object; off by default, in which case the default JSON shape omits the block; no histogram.
  - `--probe boundaries`: standalone fixed-32-step batch table of 8 quick formats × 4 boundary starts (`default`/`min-normal`/`max-finite`/`subnormal`); `--json` emits `mode=probe`, `target=boundaries`, `cells[]` (8×4=32); it never runs the natural-termination walk.
- `tests/verify_cli_contract.ps1`: CLI contract assertion suite (groups A–L, 76 checks) wired into `run_tests.ps1`, covering mode guards, pure-JSON stdout, stderr-only warnings, dump active bits, help drift, and the W2/W3 opt-in behaviors (fixed steps, pin/boost, events, iron/max-seconds, pairwise, sample-window, probe).
- Scheduling resources are reset deterministically between runs: the high-resolution timer uses a ref-counted pair, and thread affinity/priority are restored on exit instead of leaking state.

### Changed
- Single-format and `-m all` JSON expose an expanded `system` block: `hardware_threads`, `physical_threads`, `hires_timer`, `affinity`, `compiler` (selected from `__clang__`/`__GNUC__`), and `cpp_std`.
- x87 dump record uses a fixed 16-byte slot (10 valid bytes + 6 zero padding); the earlier v1.0.6 note about a 10-byte record described the pre-widening behavior and is retained as history.
- Diagnostic warnings (slow-engine level, critical warnings) are emitted on `stderr`; the interactive confirmation prompt remains on `stdout`, so `--json` stdout stays a single pure JSON object with no ANSI warning prefix.
- `--dump-packed`: the v2 header active bit at `0x0B` is set only when a record actually shrinks; native whole-width formats keep it 0 while tight formats such as tf32 (3B < 4B) set it 1.

### Fixed
- Single-format mode rejects `-l/--loop`, `--only`, and `-j/--jobs` with exit code 2 and a bilingual error instead of silently accepting them; outside `-m all` the JSON `loop` field always reports the honest value 1.
- Native `-a min-normal` starts from the IEEE minimum normalized positive value (binary16 `2^-14`; f32/f64 `std::numeric_limits<T>::min()`) rather than `denorm_min()`, removing the immediate identity/short stop; the start tag is now `min-normal`.
- New `-y/--yes` skips the interactive critical-warning confirmation for non-interactive/CI use; a cancelled `-m all` run now exits 1 (was 0) so a cancellation is never read as success in automation.
- Engineering (`eng`) display falls back to scientific notation when the normalized mantissa underflows to infinity, preventing a broken `infe-324` string; quiet mode no longer calls `slow_update()` on exit, preserving the true terminal value such as the underflow result `"0"`.
- `tests/run_tests.ps1` resolves a real `python`/`python3` executable by scanning `PATH` directly and ignoring same-name aliases/functions whose `Source` is empty, so the runner reaches both strict verifiers in MSYS2 Clang environments.

## v1.0.6 (2026-11-03)

### Added
- Dump schema v2: fixed 256-byte header (magic `EAFRDT2`, version, format id, endian, total steps, record size, flags, CPU brand hash, DPD128 step interval); header CRC-32 at `0x10` and payload CRC-32/IEEE at `0xE0`; explicit payload bytes / record count / record size; format, step mode, start kind/value, exit reason, and a run snapshot (timestamp, elapsed, timeout, loops, delay). `--dump` applies to single-format mode only; `-m all` writes only a flags bit2=empty stub with no records.
- `--dump-raw` / `--dump-no-header`: emit the continuous raw bit stream from offset 0 without the 256-byte header (single format only).
- `dump_inspect.py`: header interpreter, full 256-byte hex dump with every field decoded, exit codes 0/1/2/3, recomputation of both CRCs and record geometry, tolerant of legacy or non-dump files.
- `UInt128` derived from `UInt256` (`w[2]`, `mul64`, `div10`).
- `-q` now suppresses live/finish output uniformly for every single-format engine; `--json` and `--dump` imply quiet as well.
- DPD coefficient path: fixed-length symmetric pack/unpack declet sub-unit; startup runs a real pack→unpack round-trip self-check, replacing the former hardcoded placeholder `OK` (long-standing debt #28 closed).
- Release build regression verified under `-Oz -flto=thin -ffunction-sections -fdata-sections -Wl,--gc-sections -static` (≈0.6 MB stripped static Windows binary).

### Changed
- Modular refactor: the 17 formats moved to dedicated headers under `include/formats/`, with shared logic extracted into `engine_*.h` modules (`engine_sim`, `engine_native`, `engine_bid_codec`, `engine_decnum`, `engine_dpd_codec`, `engine_dpd`).
- `--dump-endian` unified across all formats; self-written paths no longer reverse endian logic.
- Repository hygiene: stray runtime log removed, orphan experiment header deleted, deterministic byte-order/raw round-trip regressions pinned under `tests/` with a unified coverage matrix; Markdown docs corrected to current source facts and paired CN/EN.
- libeafrdt documentation records that native f32/f64 use hardware `float`/`double` and match the CLI X102 walk exactly (f32=4481, f64=35843, both overflow); module declaration surface (`eafrdt.cppm`) synced with `eafrdt.h`.

### Fixed
- `step_subulp` subnormal branch: no longer wraps the mantissa back to all-ones when already at emin.
- x87 dump record width is 10 bytes (not 16); JSON subulp label and `step_name` SUB_ULP case corrected.
- DPD step title wording unified and timeout exit reason corrected.
- CI `__cpuid` cross-platformized so Linux g++/clang++ build succeeds.
- Benign compiler warnings silenced under `-Wall -Wextra` without any algorithm, count, or ABI change; CI wording corrected to reflect that the workflow sets no `-Werror` (warnings do not fail the job).
- libeafrdt: unused static `dpd_encode3` codec marked `[[maybe_unused]]`; archive rebuilds warning-free with unchanged smoke counts.

### Removed
- `-g mululp-down` was removed. Valid step modes are `x1.02` (default), `mululp`, `addulp`, `subulp`.

## v1.0.3 (2026-10-29)

### Added
- `-g subulp`: subtract 1 ULP per step.
- `-g mululp-down`: multiply by `(1 - 1ULP)` per step.
- `-a min` now starts from smallest subnormal (not min-normal).

### Fixed
- Self-written formats min-normal: exp was hardcoded `1`, now `emin(f)`.
- Self-written formats max-finite: man was `(1<<M)-1`, now `(1<<(M+1))-1` (Q1.f implicit leading bit).
- FP128 max-finite: lo was `0`, now `~0ULL` (112-bit mantissa all ones).
- Subnormal biased exponent displays as all-zero bits in hex/bin/dump.
- Native f16/f32 min: `numeric_limits::denorm_min()` returns 0 on this clang, hardcoded correct values.
- DPD min: exp10 was `-bias` (below emin), now `emin = 1 - emax`; underflow check uses `top_exp` not raw `exp10`.

### Removed
- `--dump-align` parameter: each format decides its own record width (fp4/fp6/fp8=1B, x87/fp128=16B, etc.).

## v1.0.1 (2026-10-29)

### Added
- `--json` output mode: machine-readable JSON for benchmark/eval media. One JSON object with format, steps, elapsed_ms, steps_per_sec, reason, final_value, params, system info, timestamp.
- `--dump <file>`: binary-serialize every step's raw bytes to a file.
- `--dump-align <1|2|4|8|16>`: record alignment for binary dump.
- libeafrdt: standalone static library with C ABI (extern "C"), UInt256-only, no `__int128` in x87/q128 paths.
- C++23 module interface (`eafrdt.cppm`) for the static library.
- Symbols stripped from release binary to reduce AV false positives (~615KB).

### Fixed
- `bid128_from_double` / `bid128_mul`: correctly parse 34-digit decimal coefficient, normalize to 10^34 boundary (dec128 step count now 715048, within 0.07% of reference).
- Branch protection on `main`: CI must pass, 1 approving review required.

## 1.0-deta1 (2026-10-28)

First public beta release.

### Added
- 17 floating-point formats: FP4, FP6, FP8 E4M3, FP8 E5M2, FP16, BF16, TF32, FP32, Dec32[BID], DPD32, FP64, Dec64[BID], DPD64, x87, FP128, Dec128[BID], DPD128.
- 5 stepping modes: multiply by factor, multiply by 1+1 ULP, add 1 ULP, descend from max-finite, ascend from min-normal.
- Concurrent mode (`-m all`): per-core workers, cache-line isolated, Windows affinity pinning.
- Hand-rolled UInt256 big-integer arithmetic for Dec128 (no `__int128` dependency).
- Pure software x87 (80-bit) and FP128 (112-bit) simulation (no libquadmath, no `long double`).
- DPD (densely packed decimal) independent header with lookup-table codec.
- Bilingual UI (zh/en).
- Safety guards: DoS protection for infinite loops, factor validation, DPD/dec128 confirmation prompts.
- Static Windows x86_64 release binary.
- CI: GCC and Clang build on Ubuntu with smoke tests.
