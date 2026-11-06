// dpd128.h — DPD Decimal128 (12 exp cont / 110 coeff bits / 34 digits / bias 6176)
#pragma once
#if defined(_WIN32) && !defined(__MINGW32__)
#error "ea-FRDT requires MinGW target (x86_64-w64-mingw32 / x86_64-w64-windows-gnu). Do not use MSVC or LLVM's default windows-msvc clang; use MSYS2 clang64."
#endif
#include "engine_dpd.h"

namespace fw {

inline void run_dpd128(double delay_ms, StartKind kind, StepMode sm, DispFmt df,
                       double start_val, double factor, int digits,
                       ShowBits showbits = ShowBits::OFF, RunResult* quiet = nullptr,
                       std::int64_t refresh_ms = 0) {
    run_dpd(dpd_fmt128(), delay_ms, kind, sm, df, start_val, factor, digits, showbits, quiet, refresh_ms);
}

} // namespace fw
