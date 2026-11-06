// fp64.h — Native IEEE binary64 (double)
#pragma once
#if defined(_MSC_VER)
#error "ea-FRDT: MSVC is not supported. Use GCC 13+ or Clang 16+ (MSYS2 clang64 on Windows)."
#endif
#include "engine_native.h"

namespace fw {

inline void run_fp64(double delay_ms, StartKind kind, StepMode sm, DispFmt df,
                     double start_val, double factor, int digits,
                     ShowBits showbits = ShowBits::OFF, RunResult* quiet = nullptr,
                     std::int64_t refresh_ms = 0) {
    run_native<NativeF64>("FP64", delay_ms, kind, sm, df, start_val, factor, digits, showbits, quiet, refresh_ms);
}

} // namespace fw
