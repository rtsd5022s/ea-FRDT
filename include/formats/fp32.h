// fp32.h — Native IEEE binary32 (float)
#pragma once
#include "engine_native.h"

namespace fw {

inline void run_fp32(double delay_ms, StartKind kind, StepMode sm, DispFmt df,
                     double start_val, double factor, int digits,
                     ShowBits showbits = ShowBits::OFF, RunResult* quiet = nullptr,
                     std::int64_t refresh_ms = 0) {
    run_native<NativeF32>("FP32", delay_ms, kind, sm, df, start_val, factor, digits, showbits, quiet, refresh_ms);
}

} // namespace fw
