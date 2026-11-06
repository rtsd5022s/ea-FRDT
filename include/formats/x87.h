// x87.h — x87 80-bit extended precision, pure software (1 sign + 15 exp + 64 man / explicit leading bit)
#pragma once
#if defined(_WIN32) && !defined(__MINGW32__)
#error "ea-FRDT requires MinGW target (x86_64-w64-mingw32 / x86_64-w64-windows-gnu). Do not use MSVC or LLVM's default windows-msvc clang; use MSYS2 clang64."
#endif
#include "common.h"

namespace fw {

struct X87 {
    bool sign = false;
    int exp = 0;
    std::uint64_t man = 0;
    bool inf = false;
    friend bool operator==(const X87&, const X87&) = default;
};
constexpr int X87_EMAX = 16384;
constexpr int X87_EMIN = -16382;

inline X87 x87_encode(double v) {
    X87 r;
    if (v < 0) r.sign = true;
    double av = std::fabs(v);
    if (av == 0.0) return r;
    int e; double m = std::frexp(av, &e);
    int en = e - 1;
    std::uint64_t q = (std::uint64_t)std::floor(m * std::ldexp(1.0, 64) + 0.5);
    if (en > X87_EMAX) { r.inf = true; return r; }
    if (en < X87_EMIN) { r.man = 0; r.exp = 0; return r; }
    r.man = q; r.exp = en;
    return r;
}

inline X87 x87_mul(X87 a, X87 b) {
    X87 r;
    if (a.inf || b.inf) { r.inf = true; return r; }
    r.sign = a.sign ^ b.sign;
    int exp = a.exp + b.exp;
    using u128 = unsigned __int128;
    u128 P = (u128)a.man * (u128)b.man;
    if (P >= ((u128)1 << 127)) { P >>= 1; ++exp; }
    std::uint64_t man_raw = (std::uint64_t)(P >> 63);
    std::uint64_t rem = (std::uint64_t)(P & (((u128)1 << 63) - 1));
    std::uint64_t half = 1ULL << 62;
    if (rem > half || (rem == half && (man_raw & 1ULL))) ++man_raw;
    if (man_raw == 0) { man_raw = 1ULL << 63; ++exp; }
    if (exp > X87_EMAX) { r.inf = true; return r; }
    if (exp < X87_EMIN) { r.man = 0; r.exp = 0; return r; }
    r.man = man_raw; r.exp = exp;
    return r;
}

inline X87 x87_addulp(X87 x) {
    if (x.inf) return x;
    X87 r = x;
    ++r.man;
    if (r.man == 0) { r.man = 1ULL << 63; ++r.exp; }
    if (r.exp > X87_EMAX) r.inf = true;
    return r;
}

inline X87 x87_subulp(X87 x) {
    if (x.inf || (x.exp == X87_EMIN && x.man == 1ULL << 63)) return x;
    X87 r = x;
    --r.man;
    if (r.man < (1ULL << 63)) {
        if (x.exp > X87_EMIN) { r.exp = x.exp - 1; r.man = ~0ULL; }
        else { r.exp = X87_EMIN; r.man = (1ULL << 63) | ((1ULL << 63) - 1); }
    }
    return r;
}

inline std::string x87_bits(const X87& v) {
    std::string b;
    b += v.sign ? '1' : '0';
    int eb = v.exp + 16383;
    for (int i = 14; i >= 0; --i) b += ((eb >> i) & 1) ? '1' : '0';
    for (int i = 63; i >= 0; --i) b += ((v.man >> i) & 1) ? '1' : '0';
    return b;
}

inline std::string x87_format(const X87& v, DispFmt df, int digits) {
    if (v.inf) return "inf";
    if (v.man == 0) return "0";
    double man_d = (double)v.man;
    double log10v = std::log10(man_d) + (double)(v.exp - 63) * std::log10(2.0);
    int e10 = (int)std::floor(log10v);
    double mant = std::pow(10.0, log10v - e10);
    if (mant >= 10.0) { mant /= 10.0; e10 += 1; }
    int d = digits < 0 ? 6 : digits;
    std::ostringstream os;
    os << std::setprecision(d);
    if (df == DispFmt::ENG) {
        int adj = e10 % 3; if (adj < 0) adj += 3;
        mant *= std::pow(10.0, adj); e10 -= adj;
        os << std::fixed << mant << "e" << e10;
    } else {
        os << std::scientific << mant << "e" << e10;
    }
    return v.sign ? "-" + os.str() : os.str();
}

inline void run_x87_soft(double delay_ms, StartKind kind, StepMode sm, DispFmt df,
                  double start_val, double factor, int digits, ShowBits showbits = ShowBits::OFF,
                  RunResult* quiet = nullptr, std::int64_t refresh_ms = 0) {
    X87 start;
    std::string tag;
    if (kind == StartKind::MinNormal || kind == StartKind::Subnormal) {
        start = X87{false, X87_EMIN, 1ULL << 63, false};
        tag = (kind == StartKind::Subnormal) ? tr("min-subnormal(无denormal,取min-normal)", "min-subnormal(no denormal, use min-normal)") : "min-normalized";
    } else if (kind == StartKind::MaxFinite) {
        start = X87{false, X87_EMAX, ~0ULL, false}; tag = "max-finite";
    } else if (!std::isnan(start_val)) { start = x87_encode(start_val); tag = "custom"; }
    else { start = x87_encode(1.0); tag = "1.0"; }
    X87 fac = x87_encode(factor);
    if (!quiet) std::cout << tr("x87(80位,纯软件) ", "x87(80-bit,software) ") << "walk: start=" << tag << ", " << step_factor_str(sm, factor)
              << ", delay=" << delay_ms << "ms (0=unlimited)\n";
    std::function<std::string(const X87&)> x87_extra;
    if (showbits != ShowBits::OFF && !quiet) {
        x87_extra = [showbits](const X87& v) -> std::string {
            std::string b = x87_bits(v);
            return showbits == ShowBits::HEX ? bits_to_hex(b) : group_bits(b, 15, 64);
        };
    }
    // dump callback: x87 80-bit value in a fixed 16-byte slot.
    // 10 bytes carry the valid 80-bit pattern; the remaining 6 bytes are zero-filled.
    // LE: valid bytes in low half (little-endian order), upper 6 bytes zero;
    // BE: lower 6 bytes zero, valid bytes in high half (big-endian order);
    // BE record == reverse(LE record), preserving the global byte-reversal invariant.
    auto x87_dump = [](const X87& v) {
        std::string b = x87_bits(v);
        char c[10] = {0};
        for (int i = 0; i < 10; ++i) {
            int bits = 0;
            for (int j = 0; j < 8; ++j) {
                int idx = i*8 + j;
                bits = (bits << 1) | (idx < (int)b.size() && b[idx] == '1' ? 1 : 0);
            }
            c[i] = (char)bits;
        }
        char buf[16] = {0};
        if (g_dump_endian == 1) {                 // BE: low 6 zero, high 10 = MSB-first valid bytes
            for (int i = 0; i < 10; ++i) buf[6 + i] = c[i];
        } else {                                  // LE: low 10 = little-endian valid bytes, high 6 zero
            for (int i = 0; i < 10; ++i) buf[i] = c[9 - i];
        }
        std::fwrite(buf, 1, 16, g_dump_file);
        g_dump_bytes_written += 16;
    };
    drive_core(start,
          [&](X87 v) -> X87 {
              switch (sm) {
                  case StepMode::X102:    return x87_mul(v, fac);
                  case StepMode::MUL_ULP: { X87 r = x87_mul(v, fac); double ulp = std::ldexp(1.0, r.exp - 63); return x87_mul(r, x87_encode(1.0 + ulp)); }
                  case StepMode::ADD_ULP: return x87_addulp(v);
                  case StepMode::SUB_ULP: return x87_subulp(v);
              }
              return v;
          },
          [](X87 v) { return v.inf; },
          [](X87 v) { return !v.inf && v.man == 0 && v.exp == 0; },
          [](X87) { return false; },  // X87 never produces NaN
          [&](const X87& v) { return x87_format(v, df, digits); },
          delay_ms, x87_extra, quiet, refresh_ms,
          std::function<void(const X87&)>(x87_dump));
}

inline void run_x87(double delay_ms, StartKind kind, StepMode sm, DispFmt df,
                    double start_val, double factor, int digits,
                    ShowBits showbits = ShowBits::OFF, RunResult* quiet = nullptr,
                    std::int64_t refresh_ms = 0) {
    run_x87_soft(delay_ms, kind, sm, df, start_val, factor, digits, showbits, quiet, refresh_ms);
}

} // namespace fw
