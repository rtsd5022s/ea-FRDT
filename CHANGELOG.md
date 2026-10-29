# Changelog

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
