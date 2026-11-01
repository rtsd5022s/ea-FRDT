// fp8_e4m3.h — FP8 E4M3, hand-rolled bit-level (4 exp / 3 man / bias 7)
#pragma once
#include "engine_sim.h"

namespace fw {

inline const Fmt FP8_E4M3_FMT{4, 3, 7, "FP8(E4M3)"};

inline void run_fp8_e4m3(double delay_ms, StartKind kind, StepMode sm, DispFmt df,
                         double start_val, double factor, int digits,
                         ShowBits showbits = ShowBits::OFF, RunResult* quiet = nullptr,
                         std::int64_t refresh_ms = 0) {
    run_sim(FP8_E4M3_FMT, delay_ms, kind, sm, df, start_val, factor, digits, showbits, quiet, refresh_ms);
}

} // namespace fw
