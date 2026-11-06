// tf32.h — TF32 E8M10, hand-rolled bit-level (8 exp / 10 man / bias 127)
#pragma once
#if defined(_MSC_VER)
#error "ea-FRDT: MSVC is not supported. Use GCC 13+ or Clang 16+ (MSYS2 clang64 on Windows)."
#endif
#include "engine_sim.h"

namespace fw {

inline const Fmt TF32_FMT{8, 10, 127, "TF32(E8M10)"};

inline void run_tf32(double delay_ms, StartKind kind, StepMode sm, DispFmt df,
                     double start_val, double factor, int digits,
                     ShowBits showbits = ShowBits::OFF, RunResult* quiet = nullptr,
                     std::int64_t refresh_ms = 0) {
    run_sim(TF32_FMT, delay_ms, kind, sm, df, start_val, factor, digits, showbits, quiet, refresh_ms);
}

} // namespace fw
