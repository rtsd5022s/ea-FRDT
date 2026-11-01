// dec64.h — Decimal64 (IEEE 754 BID, 16 sig digits / Emax 384)
#pragma once
#include "engine_decnum.h"

namespace fw {

inline const DecFmt DEC64_FMT{16, 384, "Decimal64"};

inline void run_dec64(double delay_ms, StartKind kind, StepMode sm, DispFmt df,
                      double start_val, double factor, int digits,
                      ShowBits showbits = ShowBits::OFF, RunResult* quiet = nullptr,
                      std::int64_t refresh_ms = 0) {
    run_dec(DEC64_FMT, delay_ms, kind, sm, df, start_val, factor, digits, showbits, quiet, refresh_ms);
}

} // namespace fw
