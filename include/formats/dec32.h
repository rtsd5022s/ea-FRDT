// dec32.h — Decimal32 (IEEE 754 BID, 7 sig digits / Emax 96)
#pragma once
#if defined(_MSC_VER)
#error "ea-FRDT: MSVC is not supported. Use GCC 13+ or Clang 16+ (MSYS2 clang64 on Windows)."
#endif
#include "engine_decnum.h"

namespace fw {

inline const DecFmt DEC32_FMT{7, 96, "Decimal32"};

inline void run_dec32(double delay_ms, StartKind kind, StepMode sm, DispFmt df,
                      double start_val, double factor, int digits,
                      ShowBits showbits = ShowBits::OFF, RunResult* quiet = nullptr,
                      std::int64_t refresh_ms = 0) {
    run_dec(DEC32_FMT, delay_ms, kind, sm, df, start_val, factor, digits, showbits, quiet, refresh_ms);
}

} // namespace fw
