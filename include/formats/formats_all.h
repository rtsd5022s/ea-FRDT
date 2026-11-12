// formats_all.h — Aggregator: every format entry point in one include.
// Four shared engines (native / sim / decnum / dpd) are bound to 14 parameter sets in
// builtin_formats.h; x87, fp128 and dec128 carry their own arithmetic and stay separate.
#pragma once
#if defined(_WIN32) && !defined(__MINGW32__)
#error "ea-FRDT requires MinGW target (x86_64-w64-mingw32 / x86_64-w64-windows-gnu). Do not use MSVC or LLVM's default windows-msvc clang; use MSYS2 clang64."
#endif

// 14 engine-bound formats: fp4 fp6 fp8-e4m3 fp8-e5m2 bf16 tf32 fp16 fp32 fp64 dec32 dec64
// dpd32 dpd64 dpd128
#include "builtin_formats.h"

// Self-contained extended precision
#include "x87.h"
#include "fp128.h"
#include "dec128.h"
