# libeafrdt

ea-FRDT core algorithm as a standalone static library, with C ABI.

## Relationship to the main CLI

- This directory is a **separate, optional** artifact. The CLI executable `float_inf_walk` (`src/`, `include/`) does **not** `#include` or link this library.
- The root `CMakeLists.txt` builds only the CLI; it does **not** compile or archive `libeafrdt`. The GitHub Actions workflow `.github/workflows/ci.yml` likewise builds and smoke-tests only the CLI.
- Only seven source/documentation files are tracked in git: `eafrdt.h`, `eafrdt.cpp`, `eafrdt.cppm`, `c_api.h`, `c_api.cpp`, `test_capi.cpp`, and this `README.md`. Build outputs (`*.o`, `*.a`, `test_capi.exe`) are ignored by the root `.gitignore` (`*.o`, `*.a`, `*.exe`) and are never committed.
- The library itself contains no IO, threads, ANSI/Windows-specific calls, or globals; the CLI's Windows-only `SetThreadAffinityMask`/`winmm` timer code lives outside this library, so the archive compiles on Linux as well.

## Files

| File | Purpose |
|---|---|
| `eafrdt.h` | C++ public header (namespace `fw::core`) |
| `eafrdt.cpp` | Walk implementations (17 formats, no IO/threads/globals) |
| `eafrdt.cppm` | C++23 module interface (`import eafrdt;`) |
| `c_api.h` | `extern "C"` ABI for C/Rust/other languages |
| `c_api.cpp` | C ABI dispatch |
| `test_capi.cpp` | Minimal C ABI smoke program |
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

- UInt256: pure schoolbook 64-bit arithmetic, no `__int128`; x87/q128 (fp128) use UInt256 and do not rely on `unsigned __int128`.
- DPD/BID walks produce correct relative step counts (dec32=11279, dec64=44767, matching the recorded baseline).

Known limitation (do not treat as CLI parity):

- `f32`/`f64` in this library currently route through the parameterized SVal emulator rather than the hardware-native `float`/`double` path used by the CLI; their absolute step counts are not yet aligned with the main program's native walk and should be compared only as relative/structural results.

Platform:

- No Windows-specific code in this library; the CLI `float_inf_walk` is Windows-only because of `SetThreadAffinityMask`/`winmm`, while `libeafrdt.a` compiles on Linux.
