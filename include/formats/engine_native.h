// engine_native.h — Shared native floating-point driver (nextafterT + run_native template).
// Reused by fp16, fp32, fp64 (see formats/fp16.h, fp32.h, fp64.h).
#pragma once
#if defined(_WIN32) && !defined(__MINGW32__)
#error "ea-FRDT requires MinGW target (x86_64-w64-mingw32 / x86_64-w64-windows-gnu). Do not use MSVC or LLVM's default windows-msvc clang; use MSYS2 clang64."
#endif
#include "common.h"

namespace fw {

template <class T>
T nextafterT(T x, T y) {
    if (std::isinf(static_cast<double>(x))) return x;
    if (x == T(0)) {
        std::uint64_t raw = (y > T(0)) ? 1 : (std::uint64_t(1) << (sizeof(T)*8 - 1)) + 1;
        T r; std::memcpy(&r, &raw, sizeof(T));
        return r;
    }
    std::uint64_t raw = 0;
    std::memcpy(&raw, &x, sizeof(T));
    bool neg = (raw >> (sizeof(T)*8 - 1)) & 1;
    bool go_up = (y > x);
    if (neg) {
        // negative: more negative = raw increases, toward zero = raw decreases
        if (go_up) raw -= 1; else raw += 1;
    } else {
        if (go_up) raw += 1; else raw -= 1;
    }
    T r; std::memcpy(&r, &raw, sizeof(T));
    return r;
}

template <class T>
void run_native(const char* tag, double delay_ms, StartKind kind, StepMode sm, DispFmt df,
                double start_val, double factor, int digits, ShowBits showbits = ShowBits::OFF,
                RunResult* quiet = nullptr, std::int64_t refresh_ms = 0) {
    T one;
    const char* start_tag;
    if (kind == StartKind::Subnormal) { one = std::numeric_limits<T>::denorm_min(); start_tag = "min-subnormal"; }
    else if (kind == StartKind::MinNormal) {
        // IEEE minimum *normalized* positive value (not denorm_min); aligned with the decimal engine.
        // Use the exact binary16 normal minimum (2^-14) for _Float16, since the toolchain's
        // numeric_limits<_Float16>::min() can be incomplete/degenerate.
        if constexpr (sizeof(T) == 2) one = (T)6.103515625e-5;  // 2^-14, binary16 min normal
        else one = std::numeric_limits<T>::min();
        start_tag = "min-normal";
    }
    else if (kind == StartKind::MaxFinite) { one = std::numeric_limits<T>::max(); start_tag = "max-finite"; }
    else if (!std::isnan(start_val)) { one = (T)start_val; start_tag = "custom"; }
    else { one = T(1); start_tag = "1.0"; }
    T fac = (T)factor;
    if (!quiet) std::cout << cui::walk_header(tag, start_tag, step_factor_str(sm, factor), delay_ms);
    std::function<std::string(const T&)> nat_extra;
    if (showbits != ShowBits::OFF && !quiet && sizeof(T) <= 8) {
        nat_extra = [showbits](const T& v) -> std::string {
            std::uint64_t raw = 0; std::memcpy(&raw, &v, sizeof(T));
            std::string b = raw_to_bits(raw, (int)sizeof(T) * 8);
            if (showbits == ShowBits::HEX) return bits_to_hex(b);
            int expBits, manBits;
            if constexpr (sizeof(T) == 2)      { expBits = 5;  manBits = 10; }
            else if constexpr (sizeof(T) == 4) { expBits = 8;  manBits = 23; }
            else                              { expBits = 11; manBits = 52; }
            return group_bits(b, expBits, manBits);
        };
    }
    drive(one,
          [=](T v) -> T {
              const T up = std::numeric_limits<T>::infinity();
              switch (sm) {
                  case StepMode::X102:    return T(v * fac);
                  case StepMode::MUL_ULP: { T ulp = (T)(nextafterT(v, up) - v); return (T)(v * fac * (T(1) + ulp)); }
                  case StepMode::ADD_ULP: return nextafterT(v, up);
                  case StepMode::SUB_ULP: return nextafterT(v, -up);
              }
              return v;
          },
          [](T v) { return std::isinf((double)v); },
          [](T v) { return v == T(0); },
          [](T v) { return std::isnan((double)v); },
          [](T v) { return (double)v; },
          delay_ms, df, digits, nat_extra, quiet, refresh_ms);
}

} // namespace fw
