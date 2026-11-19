# libeafrdt

ea-FRDT core algorithm as a standalone static library, with C ABI.

## Relationship to the main CLI

- This directory is a **separate, optional** artifact. The CLI executable `ea-FRDT` (`src/`, `include/`) does **not** `#include` or link this library.
- One-way dependency: `eafrdt.h` includes `../include/uint256.h` so both sides share a single 256-bit big-integer type (`fw::UInt256`, aliased as `fw::core::UInt256`). That header is pure `uint64_t` arithmetic with no IO, no globals and no `__int128`, so the archive still builds standalone on Linux; nothing else from `include/` or `src/` is pulled in.
- The root `CMakeLists.txt` builds only the CLI; it does **not** compile or archive `libeafrdt`. The GitHub Actions workflow `.github/workflows/ci.yml` likewise builds and smoke-tests only the CLI.
- Only eight source/documentation files are tracked in git: `eafrdt.h`, `eafrdt.cpp`, `walk_core.h` (internal loop header), `eafrdt.cppm`, `c_api.h`, `c_api.cpp`, `test_capi.cpp`, and this `README.md`. Build outputs (`*.o`, `*.a`, `test_capi.exe`) are ignored by the root `.gitignore` (`*.o`, `*.a`, `*.exe`) and are never committed.
- The library itself contains no IO, threads, ANSI/Windows-specific calls, or globals; the CLI's Windows-only `SetThreadAffinityMask`/`winmm` timer code lives outside this library, so the archive compiles on Linux as well.

## Files

| File | Purpose |
|---|---|
| `eafrdt.h` | C++ public header (namespace `fw::core`); includes the shared `../include/uint256.h` for `UInt256` |
| `eafrdt.cpp` | Encoders/multipliers plus the 17 walk entry points (no IO/threads/globals) |
| `walk_core.h` | Internal generic step loop: terminal check, 16-step identity check, timeout; not part of the public API |
| `eafrdt.cppm` | C++23 module interface (`import eafrdt;`) |
| `c_api.h` | `extern "C"` ABI for C/Rust/other languages |
| `c_api.cpp` | C ABI dispatch |
| `test_capi.cpp` | C ABI baseline verification (17 formats + identity/underflow/NaN guards) |
| `libeafrdt.a` | Build output (git-ignored, not committed); produced by the Build section below |

## Build

The library is built manually (it is not part of the root CMake target). Run the commands below from inside the `libeafrdt/` directory, then compile the two translation units and archive them:

```bash
# Windows (MSYS2 clang64)
C:\msys64\clang64\bin\clang++.exe -std=c++23 -O2 -c eafrdt.cpp -o eafrdt.o
C:\msys64\clang64\bin\clang++.exe -std=c++23 -O2 -c c_api.cpp -o c_api.o
C:\msys64\clang64\bin\llvm-ar.exe rcs libeafrdt.a eafrdt.o c_api.o

# Linux (g++ or clang++)
g++ -std=c++23 -O2 -c eafrdt.cpp -o eafrdt.o
g++ -std=c++23 -O2 -c c_api.cpp -o c_api.o
ar rcs libeafrdt.a eafrdt.o c_api.o
```

Link the minimal C ABI smoke program against the archive:

```bash
# Windows
C:\msys64\clang64\bin\clang++.exe -std=c++23 -O2 test_capi.cpp libeafrdt.a -o test_capi.exe
# Linux
g++ -std=c++23 -O2 test_capi.cpp libeafrdt.a -o test_capi
./test_capi
```

`eafrdt.cppm` declares a C++23 module interface (`import eafrdt;`) for future use; the archive above is built from the traditional TU pair (`eafrdt.cpp` + `c_api.cpp`) and does not compile the module interface.

## C usage

```c
#include "c_api.h"
int reason;
long long steps = eafrdt_walk(EAFRDT_F32, 1.0, 1.02, 0, &reason);
// steps=4481, reason=0 (overflow)
```

## C++ usage

```cpp
#include "eafrdt.h"
using namespace fw::core;
int reason;
long long steps = walk_binary(fmt_f32(), 1.0, 1.02, 0, &reason);
```

## Status and known limitations

Implemented:

- UInt256: the CLI's `include/uint256.h` implementation (pure schoolbook 64-bit arithmetic, no `__int128`), shared instead of duplicated; x87/q128 (fp128) use UInt256 and do not rely on `unsigned __int128`.
- One shared loop (`walk_core.h`) drives every format: it checks overflow/underflow first, then 16 consecutive equal states (`IDENTITY`), then the optional wall-clock timeout. This is the CLI `drive.h` order.
- Underflow to zero now terminates with reason 1 for every format; previously only DPD checked it, so a decreasing walk could spin forever. NaN exits with reason 4 in the native float paths (same as the CLI's `NOT_A_NUMBER`).
- FP16 uses the native `_Float16` path (the same type as the CLI's `include/formats/common.h`), with the SVal emulator as fallback when `__FLT16_MAX__` is unavailable.

Status versus the CLI (measured with `start=1.0`, `factor=1.02`, default X102 multiply walk, `timeout=0`):

| Format | Steps | Reason |
|---|---:|---|
| fp4 / fp6 / fp8-e4m3 / fp8-e5m2 | 16 | identity (3) — multiplier rounds back to the same value |
| f16 | 574 | overflow (0) |
| bf16 | 3,913 | overflow (0) |
| tf32 | 4,623 | overflow (0) |
| f32 | 4,481 | overflow (0) |
| f64 | 35,843 | overflow (0) |
| x87 | 573,521 | overflow (0) |
| fp128 (q128) | 573,521 | overflow (0) |
| dec32 / dpd32 | 11,279 | overflow (0) |
| dec64 / dpd64 | 44,767 | overflow (0) |
| dec128 (BID) | 715,048 | overflow (0) |
| dpd128 | 714,521 | overflow (0) |

`f16`, `bf16`, `tf32`, `f32`, `f64`, dec32/dec64 and dpd128 match the CLI counts exactly. dec128 differs by 527 steps (0.07%) because the library's BID128 coefficient path rounds once at 34 digits while the CLI's engine normalizes differently; this is a structural, not bit-level, parity claim. x87/fp128 remain pure-software simulations.

C ABI reasons (append-only, existing values unchanged):

| Value | Name | Meaning |
|---:|---|---|
| 0 | `EAFRDT_OVERFLOW` | inf reached |
| 1 | `EAFRDT_UNDERFLOW` | value flushed to zero |
| 2 | `EAFRDT_TIMEOUT` | `timeout_sec` elapsed |
| 3 | `EAFRDT_IDENTITY` | 16 consecutive steps left the value unchanged |
| 4 | `EAFRDT_NOT_A_NUMBER` | NaN reached (native float formats only) |

Platform:

- No Windows-specific code in this library; the CLI `ea-FRDT` is Windows-only because of `SetThreadAffinityMask`/`winmm`, while `libeafrdt.a` compiles on Linux.
- **Windows linking:** the MSYS2 `clang64` archive is libc++ based. Link it with `clang++` from the same toolchain, or add the matching libc++ when linking from C. Mixing it with a `ucrt64`/GCC `libstdc++` link fails with `undefined reference to std::__1::chrono::steady_clock::now()`, because `walk_core.h` uses `std::chrono`. The C ABI is C-callable but not ABI-portable across STL runtimes.
- `test_capi.cpp` is the acceptance check: it asserts all 17 step counts and reasons, the fp4 identity guard with `timeout=0`, and the f32 underflow guard; it exits non-zero on any mismatch.
