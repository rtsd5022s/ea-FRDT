// fp6.h — FP6 E3M2, hand-rolled bit-level (3 exp / 2 man / bias 3)
#pragma once
#include "engine_sim.h"

namespace fw {

inline const Fmt FP6_FMT{3, 2, 3, "FP6(E3M2)"};

inline void run_fp6(double delay_ms, StartKind kind, StepMode sm, DispFmt df,
                    double start_val, double factor, int digits,
                    ShowBits showbits = ShowBits::OFF, RunResult* quiet = nullptr,
                    std::int64_t refresh_ms = 0) {
    run_sim(FP6_FMT, delay_ms, kind, sm, df, start_val, factor, digits, showbits, quiet, refresh_ms);
}

} // namespace fw
