# Contributing

## Building

### Prerequisites

- C++23 compiler: GCC 13+ or Clang 16+. **MSVC is not supported.**
- CMake 3.20+
- On Windows: `winmm` (linked automatically via CMake on WIN32)

### Build commands (CMake)

```bash
cmake -B build
cmake --build build -j
```

CMake defaults to `Release`, enables `-Wall -Wextra`, and names the executable `float_inf_walk`.

### Raw compiler (no CMake)

```bash
# Linux (needs the POSIX threads library)
g++ -std=c++23 -O2 -Wall -Wextra -pthread \
    -Iinclude \
    src/float_inf_walk.cpp src/lang.cpp src/format_util.cpp \
    src/drive.cpp src/platform.cpp \
    -o float_inf_walk

# Windows (MSYS2 clang64 — recommended release, LLVM native, thin LTO)
C:\msys64\clang64\bin\clang++.exe -std=c++23 -Oz -flto=thin \
    -ffunction-sections -fdata-sections -Wl,--gc-sections \
    -static \
    -Iinclude src/*.cpp -lwinmm -o float_inf_walk

# Note: clang64 uses libc++/compiler-rt/lld. Do NOT add -static-libgcc
# -static-libstdc++ (those are GCC flags that pull ucrt64 libraries).
# ucrt64 Clang is GCC-compat, uses GNU ld, and does NOT support thin LTO;
# use clang64 for release builds.
```

## Code layout

- Shared headers and engines live in `include/`; the 14 engine-bound format parameters live in `include/formats/builtin_formats.h`.
- Non-template definitions live in `src/`.
- User-visible bilingual strings go through the `lang.h` `tr()` table; add both the zh and en entries together.
- Windows-specific code is wrapped in `#ifdef _WIN32`, Linux code in `#ifdef __linux__`.
- No third-party dependencies: no libquadmath, Boost, or Eigen.

## Pull requests

1. Fork, branch from `main`.
2. Build with zero warnings (`-Wall -Wextra`). The only accepted compiler warning is `#pragma comment(lib, "winmm.lib")` being ignored on Clang (pre-existing, Windows-only, harmless).
3. Smoke test a native and a hand-rolled format, e.g.:
   ```bash
   ./float_inf_walk 0 -m fp32 -t 5
   ./float_inf_walk 0 -m fp4  -g addulp
   ```
   Optionally validate a dump round trip: `./float_inf_walk 0 -m f64 --dump walk.bin` and inspect it with `python dump_inspect.py walk.bin`.
4. Update the paired CN/EN docs under `docs/` when a flag, default, exit reason, or format behavior changes.
5. Open a PR describing what changed and why.

## Reporting bugs

Use the bug report template. Include: compiler, OS, exact command line, expected vs actual output, and the hex bit pattern or step count when relevant.
