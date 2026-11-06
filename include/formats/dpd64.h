// dpd64.h — DPD Decimal64 (8 exp cont / 50 coeff bits / 16 digits / bias 398)
#pragma once
#if defined(_MSC_VER)
#error "ea-FRDT: MSVC is not supported. Use GCC 13+ or Clang 16+ (MSYS2 clang64 on Windows)."
#endif
#include "engine_dpd.h"

namespace fw {

inline void run_dpd64(double delay_ms, StartKind kind, StepMode sm, DispFmt df,
                      double start_val, double factor, int digits,
                      ShowBits showbits = ShowBits::OFF, RunResult* quiet = nullptr,
                      std::int64_t refresh_ms = 0) {
    run_dpd(dpd_fmt64(), delay_ms, kind, sm, df, start_val, factor, digits, showbits, quiet, refresh_ms);
}

} // namespace fw
