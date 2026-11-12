# Frequently Asked Questions (QA)

This file consolidates verified behavior. For CLI semantics see `PARAMETERS_EN.md`, architecture `IMPLEMENTATION_EN.md`, measured steps `BASELINE_EN.md`; on disagreement the source and those docs prevail.

## 1. Dependencies and build

### Q: Does Dec128 depend on `__int128`?
**A: No.** The Dec128(BID) path uses a hand-rolled `UInt256` schoolbook multiply over 64-bit words, no `__int128`. x87/FP128 use `__int128` only for 64×64 helper intermediates; that is a separate path.

### Q: Do x87 and FP128 depend on libquadmath?
**A: No.** Pure software integer simulation; no libquadmath, no `long double`, no `__float128`.

### Q: Is MSVC supported?
**A: No.** The toolchain must be GNU/Clang (MSYS2 clang64 / GCC). Every header starts with `#if defined(_WIN32) && !defined(__MINGW32__) #error ...`, which rejects both MSVC cl.exe **and** LLVM's official prebuilt clang targeting `x86_64-pc-windows-msvc` (that clang defines `_MSC_VER` but not `__MINGW32__`). On Windows you must use MSYS2 clang64 (target `x86_64-w64-mingw32`).

### Q: Third-party libraries?
**A: None.** No Boost/Eigen/libquadmath; implementation lives in `include/` and `src/`; the C ABI static library lives in `libeafrdt/`.

### Q: Does the Windows static exe need a runtime?
**A: With `-static` it runs on Windows x86_64 without an extra runtime install; binary size varies with optimization, see the actual build.**

### Q: Linux / macOS?
**A: Buildable from source.** Linux uses GCC/Clang and `pthread`; `physical_cores()` returns `0` on Linux (banner hides physical core count). Windows artifacts are the primary release; other platforms build via CMake.

## 2. Formats and step counts

### Q: Which formats?
**A: 17 total; the default `-m all` chain has 13:**
FP4 → FP6 → FP8E4M3 → FP8E5M2 → FP16 → BF16 → TF32 → FP32 → Dec32(BID) → FP64 → Dec64(BID) → x87 → FP128.

**Excluded by default (add with `--only`):** DPD32, DPD64, DPD128, Dec128(BID).

### Q: Reference step counts (1.0 × 1.02 → overflow)?
**A: Full table in `BASELINE_EN.md`.** Order of magnitude: FP32 ≈ 4481, Dec32/DPD32 ≈ 11279, FP64 ≈ 35843, Dec64/DPD64 ≈ 44767, x87/FP128 hundreds of thousands, Dec128/DPD128 seven figures. Steps are mostly determined by width and Emax and fairly build-stable; times vary with machine and optimization.

### Q: Are DPD and Dec128 in the default chain?
**A: No.** They are slow software paths; add them with `--only dpd32,dpd64,dpd128,dec128`, or run single-format `-m dpd64` (no confirmation gate in single mode).

### Q: BID vs DPD?
**A: BID keeps a binary-integer coefficient and adjusts by 10^n; DPD keeps a decimal digit array and multiplies declet-wise with carry. Different encoding and arithmetic, both within IEEE 754-2008 decimal Emax/Emin.**

## 3. Stepping modes

### Q: Default step?
**A: Multiply `1.02` (`-k 1.02` / `-g x1.02`).**

### Q: `-k 1`?
**A: No progress; exits `identity->stop`.**

### Q: `-k 0`, negative, NaN/Inf factor?
**A: Hard exit code `2` with a stderr error; these are invalid input, not runtime outcomes.**

### Q: factor < 1?
**A: Descending walk to `underflow->0`.**

### Q: `-g mululp` / `addulp` / `subulp`?
**A: `mululp` multiplies by `1 + 1 ULP`; `addulp` adds one ULP; `subulp` subtracts one ULP (descending).**

## 4. Start values

### Q: Default start?
**A: 1.0. The first positional argument is `delay_ms` (throttle), not the start value.**

### Q: Negative start?
**A: Use `-a -1` (walks toward `-inf`); a negative value is never a positional argument.**

### Q: `-a max-finite` / `-a min-normal` / `-s`?
**A: `max-finite` = largest finite positive; `min-normal` (alias `min`) = smallest **normal** positive; `-s` (`--subnormal`) = smallest **subnormal** positive; `-a` and `-s` are mutually exclusive.**

### Q: Raw hex start?
**A: `-a 0x3f800000`, interpreted as bits for the target format; a malformed `0x` token exits `2`.**

## 5. Safety and guards

### Q: Can `-l -1` run without a timeout?
**A: It starts, but in `-m all` a level-1 yellow `[!]` confirmation warns that a timeout is recommended; press `y` within 10 s to continue, otherwise `cancelled`. It is not a hard rejection; single mode skips the gate. Always pair a timeout over SSH.**

### Q: Factor close to 1?
**A: There is no “reject if close to 1”. `-k 1` yields `identity->stop`; a factor `< 1e-10` raises only a level-1 warning in `-m all` (many steps ahead); the real backstop is `safety->limit` (`2^120`).**

### Q: What happens before DPD / Dec128?
**A: Only in `-m all`: including DPD×3 or Dec128 raises a level-2 red `[!!!]` confirmation; press `y` within 10 s, otherwise `cancelled`. Single mode runs directly.**

### Q: Invalid input exit code?
**A: Code `2` on stderr; normal end, `cancelled`, and `sigint` are `0`. There is no code `1` convention.**

### Q: Malicious hex / ANSI escapes?
**A: Malformed `0x` is rejected; echoed user input passes through `sanitize()` (ESC/C0/DEL stripped) against terminal injection.**

## 6. Concurrent mode

### Q: What does `-m all` do?
**A: One worker per selected core independently walks the default 13-chain (or an `--only` subset); value-copied, no shared mutable state, no locks.**

### Q: `-j` syntax and indexing?
**A: Zero-based: `-j 0` is the first core; range `-j 0-3`; list `-j 0,2`; mixed `-j 0,2-4`. After parsing, `v<0 || v>=hw` entries are dropped, the rest sorted/deduped; empty (default) selects all logical cores; an empty resolved list exits `2`.**

### Q: Upper bound?
**A: Valid ids are `0..hw-1`; out-of-range ids are dropped rather than erroring individually.**

### Q: Cache-line isolation?
**A: Yes; `alignas(64)` gives each `WorkerSlot` its own cache line, avoiding false sharing.**

### Q: Ctrl+C?
**A: Sets a flag; the current iteration ends, then `sigint` with a bilingual message and a clean exit.**

## 7. Display

### Q: Bit-pattern switch?
**A: `--bits hex|bin|off` (`--showbits`), default `off`; `bin` groups sign/exponent/coefficient. There is no `-x` flag.**

### Q: `-q`?
**A: It suppresses the live per-step and finish lines, leaving only the one-line `TOTAL <n> steps · <reason> · wall <t>` footer printed on exit; the result still lands in `RunResult`. `--json` owns stdout and never prints that footer; `--dump`/`--dump-events` imply quiet and keep the same footer.**

### Q: Monitor elapsed format?
**A: Layered by magnitude (s → m:s → h:m:s → d:h:m:s …), refreshed in place with `\r`.**

## 8. Exit reasons

Driver reasons (seven enums):

- `overflow->inf`
- `underflow->0`
- `identity->stop` (no progress)
- `safety->limit` (`2^120` counter)
- `timeout`
- `nan->nan` (native float/double paths only; integer formats never yield NaN)
- `fixed` (only opt-in `--steps <n>`; exact N-step cap; JSON token `fixed`, terminal text `fixed-steps`)

Outer control messages (not enums): `cancelled` (confirmation declined/timed out, mainly `-m all`), `sigint` (Ctrl+C).

## 9. Language

### Q: Default language?
**A: Default `zh`; `--lang en` switches to English. All UI strings (warnings, errors, Ctrl+C) go through `tr(zh,en)`.**

## 10. Version and maturity

### Q: Where is the version?
**A: Tracked in `CHANGELOG.md` and git tags; this file does not pin a number.**

### Q: Is it stable?
**A: Under active iteration; trust measured behavior and the `BASELINE_EN.md` reference table; numeric results can differ across builds.**
