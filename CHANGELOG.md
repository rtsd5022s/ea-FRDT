# Changelog

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
