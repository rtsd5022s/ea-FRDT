// dpd32.h — DPD Decimal32 (6 exp cont / 20 coeff bits / 7 digits / bias 101)
#pragma once
#include "engine_dpd.h"

namespace fw {

inline void run_dpd32(double delay_ms, StartKind kind, StepMode sm, DispFmt df,
                      double start_val, double factor, int digits,
                      ShowBits showbits = ShowBits::OFF, RunResult* quiet = nullptr,
                      std::int64_t refresh_ms = 0) {
    run_dpd(dpd_fmt32(), delay_ms, kind, sm, df, start_val, factor, digits, showbits, quiet, refresh_ms);
}

} // namespace fw
