// fp4.h — FP4 E2M1, hand-rolled bit-level (2 exp / 1 man / bias 1)
#pragma once
#include "engine_sim.h"

namespace fw {

inline const Fmt FP4_FMT{2, 1, 1, "FP4(E2M1)"};

inline void run_fp4(double delay_ms, StartKind kind, StepMode sm, DispFmt df,
                    double start_val, double factor, int digits,
                    ShowBits showbits = ShowBits::OFF, RunResult* quiet = nullptr,
                    std::int64_t refresh_ms = 0) {
    run_sim(FP4_FMT, delay_ms, kind, sm, df, start_val, factor, digits, showbits, quiet, refresh_ms);
}

} // namespace fw
