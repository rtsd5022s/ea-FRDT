// bf16.h — BF16 E8M7, hand-rolled bit-level (8 exp / 7 man / bias 127)
#pragma once
#include "engine_sim.h"

namespace fw {

inline const Fmt BF16_FMT{8, 7, 127, "BF16(E8M7)"};

inline void run_bf16(double delay_ms, StartKind kind, StepMode sm, DispFmt df,
                     double start_val, double factor, int digits,
                     ShowBits showbits = ShowBits::OFF, RunResult* quiet = nullptr,
                     std::int64_t refresh_ms = 0) {
    run_sim(BF16_FMT, delay_ms, kind, sm, df, start_val, factor, digits, showbits, quiet, refresh_ms);
}

} // namespace fw
