// fp8_e5m2.h — FP8 E5M2, hand-rolled bit-level (5 exp / 2 man / bias 15)
#pragma once
#if defined(_WIN32) && !defined(__MINGW32__)
#error "ea-FRDT requires MinGW target (x86_64-w64-mingw32 / x86_64-w64-windows-gnu). Do not use MSVC or LLVM's default windows-msvc clang; use MSYS2 clang64."
#endif
#include "engine_sim.h"

namespace fw {

inline const Fmt FP8_E5M2_FMT{5, 2, 15, "FP8(E5M2)"};

inline void run_fp8_e5m2(double delay_ms, StartKind kind, StepMode sm, DispFmt df,
                         double start_val, double factor, int digits,
                         ShowBits showbits = ShowBits::OFF, RunResult* quiet = nullptr,
                         std::int64_t refresh_ms = 0) {
    run_sim(FP8_E5M2_FMT, delay_ms, kind, sm, df, start_val, factor, digits, showbits, quiet, refresh_ms);
}

} // namespace fw
