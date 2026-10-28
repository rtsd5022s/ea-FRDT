# Contributing

## Building

### Prerequisites

- C++23 compiler: GCC 13+ or Clang 16+. **MSVC is not supported.**
- CMake 3.20+
- On Windows: `winmm` (already linked via CMake)

### Build commands

```bash
cmake -B build
cmake --build build -j
```

### Raw compiler (no CMake)

```bash
# Linux
g++ -std=c++23 -O2 -Wall -Wextra -Iinclude src/*.cpp -o float_inf_walk -pthread

# Windows (MSYS2 clang64 — recommended, LLVM native)
C:\msys64\clang64\bin\clang++.exe -std=c++23 -Oz -flto=thin \
    -ffunction-sections -fdata-sections -Wl,--gc-sections \
    -static \
    -Iinclude src/*.cpp -lwinmm -o ea-FRDT.exe

# Note: clang64 uses libc++/compiler-rt/lld. Do NOT add -static-libgcc
# -static-libstdc++ (those are GCC flags that pull ucrt64 libraries).

# Windows (MSYS2 ucrt64 — GCC-based, Clang is compat-only)
# Note: ucrt64 Clang uses GNU ld, does NOT support thin LTO.
# Use clang64 for release builds.
C:\msys64\ucrt64\bin\clang++.exe -std=c++23 -O2 \
    -static -static-libgcc -static-libstdc++ \
    -Iinclude src/*.cpp -lwinmm -o ea-FRDT.exe
```

## Code style

- Headers in `include/`, implementation in `src/`.
- All comments in English.
- Bilingual UI strings go through `lang.h` (`tr()` table). Add both zh and en.
- Windows-specific code wrapped in `#ifdef _WIN32`, Linux in `#ifdef __linux__`.
- No third-party dependencies. No libquadmath, Boost, Eigen.

## Pull requests

1. Fork, branch from `main`.
2. Build with zero warnings (`-Wall -Wextra`).
3. Run the smoke test: `./float_inf_walk 0 -m fp32 -t 5`.
4. Open a PR describing what changed and why.

## Reporting bugs

Use the bug report template. Include: compiler, OS, exact command line, expected vs actual output, and the hex bit pattern if relevant.
