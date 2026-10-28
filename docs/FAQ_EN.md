# Frequently Asked Questions (QA)

This document lists every behavior exactly to prevent contradictions between docs. If any other document conflicts with this file, this file is authoritative.

## 1. Dependencies and build

### Q: Does Dec128 depend on `__int128`?
**A: No.** Dec128's BID path uses a hand-rolled `UInt256` schoolbook big-integer multiply, pure 64-bit word operations, no `__int128`. The x87 and FP128 pure-software paths still use `__int128` for 128-bit auxiliary arithmetic, but that is not a Dec128 dependency.

### Q: Do x87 and FP128 depend on libquadmath?
**A: No.** Both are pure software integer simulations. No libquadmath, no `long double`, no `__float128`.

### Q: Is MSVC supported?
**A: No.** The project uses C++23 features and GNU/Clang extensions. MSVC is explicitly excluded. Requires GCC 13+ or Clang 16+.

### Q: Do I need third-party libraries?
**A: No.** Zero third-party dependencies. No Boost, Eigen, or libquadmath. All code is in `include/` and `src/`.

### Q: Does the Windows static exe need the VC++ runtime?
**A: No.** The Release `ea-FRDT.exe` is built with UCRT64 static linking (`-static -static-libgcc -static-libstdc++`), 3.3MB, and runs on any Windows x86_64 machine without installation.

### Q: Can I build on Linux or macOS?
**A: Yes, from source.** CI builds on Ubuntu with GCC and Clang. Release currently ships only the Windows exe. Linux/macOS: `cmake -B build && cmake --build build`.

## 2. Formats and step counts

### Q: Which formats are supported?
**A: 17, in the `-m all` chain order:**
FP4 → FP6 → FP8E4M3 → FP8E5M2 → FP16 → BF16 → TF32 → FP32 → Dec32(BID) → DPD32 → FP64 → Dec64(BID) → DPD64 → x87 → FP128 → Dec128(BID) → DPD128.

### Q: What are the reference step counts (from 1.0 times 1.02 to overflow)?
**A: On i7-6700:**
- FP32: 4481 steps
- FP64: 8275 steps
- Dec64: 11958 steps
- x87: 573521 steps
- FP128: 573521 steps
- Dec128: 714521 steps
These are format fingerprints. Any correct implementation should match.

### Q: Are DPD formats included in the default `-m all` chain?
**A: No, by default.** The three DPD formats (dpd32/dpd64/dpd128) are excluded from the default chain because they are pure software decimal-array arithmetic with no hardware acceleration. Add them explicitly with `--only dpd32,dpd64,dpd128`, or run them single-mode with `-m dpd64`.

### Q: What is the difference between BID and DPD?
**A: BID stores the coefficient as a binary integer, using binary multiplication plus division by 10^n. DPD stores the coefficient as a decimal digit string, using per-digit decimal multiplication plus carry propagation. They encode differently and follow different arithmetic paths, but both follow IEEE 754-2008.**

## 3. Stepping modes

### Q: What is the default step?
**A: Multiply by 1.02 (`-k 1.02`).**

### Q: What happens with `-k 1`?
**A: 1.0 × 1 = 1.0, no progress. The tool exits immediately with `identity(stopped)`.**

### Q: What happens with `-k 0`?
**A: Rejected with the error "factor cannot be 0".**

### Q: What happens with factor < 1?
**A: Walks downward from the start value until underflow to zero.**

### Q: What is the difference between `-g mululp` and `-g addulp`?
**A: mululp = multiply by `1 + 1 ULP` (relative step); addulp = add `1 ULP` (absolute step). They are mutually exclusive.**

### Q: Does it still show factor= when using `-g mululp`/`addulp`?
**A: No.** Only the default multiply mode shows `x1.02`. mululp/addulp shows `step=mululp` or `step=addulp`, no factor.

## 4. Start values

### Q: What is the default start?
**A: 1.0.**

### Q: How do negative starts work?
**A: Pass a negative first positional argument (e.g. `-1`). Walks from -1.0 toward -inf.**

### Q: What is `-a max-finite`?
**A: Start at the largest finite positive number for the format. Pair with `-k 0.5` to halve to zero.**

### Q: What is `-a min-normal`?
**A: Start at the smallest normal positive number for the format, walking upward.**

### Q: What does `0x3f800000` as the first argument do?
**A: Raw hex bit pattern, interpreted as the start value according to the format.**

## 5. Safety and guards

### Q: Can I use `-l -1` (infinite loop) alone?
**A: No.** Must also pass `-t <seconds>` timeout. Otherwise it refuses to start with a DoS warning.

### Q: What if the factor is too close to 1?
**A: Rejected. A step too small would walk millions of steps and hit the 2^120 safety limit.**

### Q: What happens before DPD formats start?
**A: In concurrent mode, prints a yellow warning about high algorithm complexity and reduced performance, waits 10 seconds for y to continue; timeout or any other key cancels. Single mode does not show the warning.**

### Q: What happens before dec128 starts?
**A: In concurrent mode, prints a yellow warning and waits 10 seconds for y. Single mode does not show it.**

### Q: What happens on invalid input?
**A: Hard exit with an error message on stderr and a non-zero exit code. No crash.**

## 6. Concurrent mode

### Q: What does `-m all` do?
**A: Each worker pins to a core and independently walks the full format chain.**

### Q: What is the difference between `-j 1-4` and `-j 1,3`?
**A: `-` is a range (1 through 4, four workers); `,` is a list (only cores 1 and 3, two workers). They can be mixed: `-j 1,3-5`.**

### Q: Is there a hard cap on `-j`?
**A: Yes.** The hard cap is the logical core count (`std::thread::hardware_concurrency()`). Values above are rejected or clamped.

### Q: Is there cache-line isolation on worker state?
**A: Yes.** `alignas(64)` pads each `WorkerSlot` to 64 bytes, eliminating false sharing.**

### Q: What happens on Ctrl+C in concurrent mode?
**A: Caught and prints "Program interrupted (received Ctrl+C)" bilingually, exits gracefully.**

## 7. Display

### Q: What does `-x` do?
**A: Appends the raw bit pattern in parentheses after the value. `-x hex` explicit hex, `-x bin` explicit binary (sign/exponent/mantissa grouped by spaces).**

### Q: What does `-q` do?
**A: Quiet mode. No per-step output, only the final summary table.**

### Q: What are the loop= display rules?
**A: loop=1 (default single pass) does not show loop=. loop > 1 shows `loop=current/total`. loop=-1 infinite shows `loop=current` (no /-1 suffix).**

### Q: What are the time display rules?
**A: All layers shown fully: Y/M/D/H/M/S/ms, not just the top two layers.**

### Q: Are `step=` and `factor=` shown together?
**A: No.** Default multiply mode shows `step=multiplied x1.02`. mululp/addulp shows `step=mululp` or `step=addulp`, no factor.

## 8. Exit reasons

### Q: What does the tool print when it exits?
**A: One of:**
- `overflow->inf`: next step would be infinity
- `underflow->0`: next step would be zero
- `nan->nan`: next step would be not-a-number (only reachable on native float/double paths; hand-rolled integer formats never produce NaN)
- `identity(stopped)`: no progress (e.g. 1.0 × 1.0)
- `safety-limit`: hit the 2^120 step cap
- `timeout`: `-t` timer fired
- `cancelled`: DPD/dec128 warning timed out or was declined
- `sigint`: Ctrl+C received

## 9. Language

### Q: Which languages are supported?
**A: Chinese (`--lang zh`) and English (`--lang en`, default). All text including warnings, errors, and Ctrl+C messages is bilingual.**

## 10. Version

### Q: What is the current version?
**A: 1.0-deta1 (beta). Tag `v1.0-deta1`.**

### Q: Is this a stable release?
**A: No.** deta is a test release. Undiscovered edge bugs may exist. The algorithm may misbehave occasionally.
