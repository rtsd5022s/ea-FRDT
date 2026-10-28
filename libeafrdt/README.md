# libeafrdt

ea-FRDT core algorithm as a standalone static library, with C ABI.

## Files

| File | Purpose |
|---|---|
| `eafrdt.h` | C++ public header (namespace `fw::core`) |
| `eafrdt.cpp` | Walk implementations (17 formats, no IO/threads/globals) |
| `eafrdt.cppm` | C++23 module interface (`import eafrdt;`) |
| `c_api.h` | `extern "C"` ABI for C/Rust/other languages |
| `c_api.cpp` | C ABI dispatch |
| `libeafrdt.a` | Prebuilt static archive (clang64, -O2) |

## Build

```
C:\msys64\clang64\bin\clang++.exe -std=c++23 -O2 -c eafrdt.cpp -o eafrdt.o
C:\msys64\clang64\bin\clang++.exe -std=c++23 -O2 -c c_api.cpp -o c_api.o
C:\msys64\clang64\bin\llvm-ar.exe rcs libeafrdt.a eafrdt.o c_api.o
```

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

## Status

- UInt256: pure schoolbook, no `__int128`.
- x87/q128: rewritten to use UInt256 (removed `unsigned __int128` from q128_mul).
- f32/f64 currently route through the parameterized SVal emulator (not hardware native). Step counts need alignment with the main program's native path.
- DPD/BID walks produce correct relative step counts (dec32=11279, dec64=44767, matching baseline).
- No Windows-specific code in this library. The main `ea-FRDT.exe` is Windows-only because of `SetThreadAffinityMask`/`winmm`, but `libeafrdt.a` itself compiles on Linux.
