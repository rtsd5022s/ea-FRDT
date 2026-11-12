// builtin_formats.h — Parameter bindings for the 14 table-driven formats.
// Each entry is one line of constants plus a thin forwarding call into a shared
// engine; the CLI/registry side therefore needs no per-format header at all.
// Self-contained algorithms (x87, fp128, dec128) keep their own headers.
#pragma once
#if defined(_WIN32) && !defined(__MINGW32__)
#error "ea-FRDT requires MinGW target (x86_64-w64-mingw32 / x86_64-w64-windows-gnu). Do not use MSVC or LLVM's default windows-msvc clang; use MSYS2 clang64."
#endif
#include "engine_sim.h"
#include "engine_native.h"
#include "engine_decnum.h"
#include "engine_dpd.h"

namespace fw {

// ---- hand-rolled bit-level binary (engine_sim.h): E exponent / M mantissa / bias ----
inline constexpr Fmt FP4_FMT      {2, 1,  1,   "FP4(E2M1)"};
inline constexpr Fmt FP6_FMT      {3, 2,  3,   "FP6(E3M2)"};
inline constexpr Fmt FP8_E4M3_FMT {4, 3,  7,   "FP8(E4M3)"};
inline constexpr Fmt FP8_E5M2_FMT {5, 2,  15,  "FP8(E5M2)"};
inline constexpr Fmt BF16_FMT     {8, 7,  127, "BF16(E8M7)"};
inline constexpr Fmt TF32_FMT     {8, 10, 127, "TF32(E8M10)"};

// ---- BID decimal (engine_decnum.h): sig digits / Emax ----
inline constexpr DecFmt DEC32_FMT {7,  96,  "Decimal32"};
inline constexpr DecFmt DEC64_FMT {16, 384, "Decimal64"};

inline void run_fp4(double delay_ms, StartKind kind, StepMode sm, DispFmt df,
                    double start_val, double factor, int digits,
                    ShowBits showbits = ShowBits::OFF, RunResult* quiet = nullptr,
                    std::int64_t refresh_ms = 0) {
    run_sim(FP4_FMT, delay_ms, kind, sm, df, start_val, factor, digits, showbits, quiet, refresh_ms);
}

inline void run_fp6(double delay_ms, StartKind kind, StepMode sm, DispFmt df,
                    double start_val, double factor, int digits,
                    ShowBits showbits = ShowBits::OFF, RunResult* quiet = nullptr,
                    std::int64_t refresh_ms = 0) {
    run_sim(FP6_FMT, delay_ms, kind, sm, df, start_val, factor, digits, showbits, quiet, refresh_ms);
}

inline void run_fp8_e4m3(double delay_ms, StartKind kind, StepMode sm, DispFmt df,
                         double start_val, double factor, int digits,
                         ShowBits showbits = ShowBits::OFF, RunResult* quiet = nullptr,
                         std::int64_t refresh_ms = 0) {
    run_sim(FP8_E4M3_FMT, delay_ms, kind, sm, df, start_val, factor, digits, showbits, quiet, refresh_ms);
}

inline void run_fp8_e5m2(double delay_ms, StartKind kind, StepMode sm, DispFmt df,
                         double start_val, double factor, int digits,
                         ShowBits showbits = ShowBits::OFF, RunResult* quiet = nullptr,
                         std::int64_t refresh_ms = 0) {
    run_sim(FP8_E5M2_FMT, delay_ms, kind, sm, df, start_val, factor, digits, showbits, quiet, refresh_ms);
}

inline void run_bf16(double delay_ms, StartKind kind, StepMode sm, DispFmt df,
                     double start_val, double factor, int digits,
                     ShowBits showbits = ShowBits::OFF, RunResult* quiet = nullptr,
                     std::int64_t refresh_ms = 0) {
    run_sim(BF16_FMT, delay_ms, kind, sm, df, start_val, factor, digits, showbits, quiet, refresh_ms);
}

inline void run_tf32(double delay_ms, StartKind kind, StepMode sm, DispFmt df,
                     double start_val, double factor, int digits,
                     ShowBits showbits = ShowBits::OFF, RunResult* quiet = nullptr,
                     std::int64_t refresh_ms = 0) {
    run_sim(TF32_FMT, delay_ms, kind, sm, df, start_val, factor, digits, showbits, quiet, refresh_ms);
}

// ---- native IEEE types (engine_native.h) ----
inline void run_fp16(double delay_ms, StartKind kind, StepMode sm, DispFmt df,
                     double start_val, double factor, int digits,
                     ShowBits showbits = ShowBits::OFF, RunResult* quiet = nullptr,
                     std::int64_t refresh_ms = 0) {
    run_native<NativeF16>("FP16", delay_ms, kind, sm, df, start_val, factor, digits, showbits, quiet, refresh_ms);
}

inline void run_fp32(double delay_ms, StartKind kind, StepMode sm, DispFmt df,
                     double start_val, double factor, int digits,
                     ShowBits showbits = ShowBits::OFF, RunResult* quiet = nullptr,
                     std::int64_t refresh_ms = 0) {
    run_native<NativeF32>("FP32", delay_ms, kind, sm, df, start_val, factor, digits, showbits, quiet, refresh_ms);
}

inline void run_fp64(double delay_ms, StartKind kind, StepMode sm, DispFmt df,
                     double start_val, double factor, int digits,
                     ShowBits showbits = ShowBits::OFF, RunResult* quiet = nullptr,
                     std::int64_t refresh_ms = 0) {
    run_native<NativeF64>("FP64", delay_ms, kind, sm, df, start_val, factor, digits, showbits, quiet, refresh_ms);
}

// ---- BID decimal (engine_decnum.h) ----
inline void run_dec32(double delay_ms, StartKind kind, StepMode sm, DispFmt df,
                      double start_val, double factor, int digits,
                      ShowBits showbits = ShowBits::OFF, RunResult* quiet = nullptr,
                      std::int64_t refresh_ms = 0) {
    run_dec(DEC32_FMT, delay_ms, kind, sm, df, start_val, factor, digits, showbits, quiet, refresh_ms);
}

inline void run_dec64(double delay_ms, StartKind kind, StepMode sm, DispFmt df,
                      double start_val, double factor, int digits,
                      ShowBits showbits = ShowBits::OFF, RunResult* quiet = nullptr,
                      std::int64_t refresh_ms = 0) {
    run_dec(DEC64_FMT, delay_ms, kind, sm, df, start_val, factor, digits, showbits, quiet, refresh_ms);
}

// ---- DPD decimal (engine_dpd.h) ----
inline void run_dpd32(double delay_ms, StartKind kind, StepMode sm, DispFmt df,
                      double start_val, double factor, int digits,
                      ShowBits showbits = ShowBits::OFF, RunResult* quiet = nullptr,
                      std::int64_t refresh_ms = 0) {
    run_dpd(dpd_fmt32(), delay_ms, kind, sm, df, start_val, factor, digits, showbits, quiet, refresh_ms);
}

inline void run_dpd64(double delay_ms, StartKind kind, StepMode sm, DispFmt df,
                      double start_val, double factor, int digits,
                      ShowBits showbits = ShowBits::OFF, RunResult* quiet = nullptr,
                      std::int64_t refresh_ms = 0) {
    run_dpd(dpd_fmt64(), delay_ms, kind, sm, df, start_val, factor, digits, showbits, quiet, refresh_ms);
}

inline void run_dpd128(double delay_ms, StartKind kind, StepMode sm, DispFmt df,
                       double start_val, double factor, int digits,
                       ShowBits showbits = ShowBits::OFF, RunResult* quiet = nullptr,
                       std::int64_t refresh_ms = 0) {
    run_dpd(dpd_fmt128(), delay_ms, kind, sm, df, start_val, factor, digits, showbits, quiet, refresh_ms);
}

} // namespace fw
