// fp128.h — FP128 binary128, pure software, full 112-bit mantissa
#pragma once
#if defined(_WIN32) && !defined(__MINGW32__)
#error "ea-FRDT requires MinGW target (x86_64-w64-mingw32 / x86_64-w64-windows-gnu). Do not use MSVC or LLVM's default windows-msvc clang; use MSYS2 clang64."
#endif
#include "common.h"

namespace fw {

struct Q128 {
    bool sign = false;
    int exp = 0;
    std::uint64_t hi = 0;
    std::uint64_t lo = 0;
    bool inf = false;
    friend bool operator==(const Q128&, const Q128&) = default;
};
constexpr int Q128_EMAX = 16384;
constexpr int Q128_EMIN = -16382;

inline Q128 q128_encode(double v) {
    Q128 r;
    if (v < 0) r.sign = true;
    double av = std::fabs(v);
    if (av == 0.0) return r;
    int e; double m = std::frexp(av, &e);
    int en = e - 1;
    std::uint64_t q = (std::uint64_t)std::floor(m * std::ldexp(1.0, 64) + 0.5);
    if (en > Q128_EMAX) { r.inf = true; return r; }
    if (en < Q128_EMIN) { r.hi = 0; r.lo = 0; r.exp = 0; return r; }
    r.hi = q; r.exp = en; r.lo = 0;
    return r;
}

inline void q128_add128(std::uint64_t b[4], int bitOffset, unsigned __int128 v) {
    int w = bitOffset / 64, sh = bitOffset % 64;
    while (v && w < 4) {
        if (sh) {
            std::uint64_t keep = (std::uint64_t)(v & (((unsigned __int128)1 << (64 - sh)) - 1));
            std::uint64_t put = keep << sh;
            std::uint64_t sum = b[w] + put;
            v = (v >> (64 - sh)) + (sum < put ? 1 : 0);
            b[w] = sum; sh = 0;
        } else {
            b[w] += (std::uint64_t)v;
            v >>= 64;
        }
        ++w;
    }
}

inline Q128 q128_mul(Q128 a, Q128 b) {
    Q128 r;
    if (a.inf || b.inf) { r.inf = true; return r; }
    r.sign = a.sign ^ b.sign;
    std::uint64_t bkt[4] = {0,0,0,0};
    q128_add128(bkt, 96, (unsigned __int128)a.hi * b.hi);
    q128_add128(bkt, 48, (unsigned __int128)a.hi * b.lo);
    q128_add128(bkt, 48, (unsigned __int128)b.hi * a.lo);
    q128_add128(bkt,  0, (unsigned __int128)a.lo * b.lo);
    int p = -1;
    for (int i = 3; i >= 0; --i) if (bkt[i]) { p = i*64 + 63 - __builtin_clzll(bkt[i]); break; }
    int s = p - 111;
    int rb = s - 1;
    bool roundBit = (bkt[rb/64] >> (rb%64)) & 1;
    bool sticky = false;
    for (int i = 0; i < rb/64; ++i) if (bkt[i]) { sticky = true; break; }
    if (!sticky && (rb%64) && (bkt[rb/64] & ((1ULL << (rb%64)) - 1))) sticky = true;
    int w = s / 64, sh = s % 64;
    std::uint64_t g0 = bkt[w] >> sh;
    std::uint64_t g1 = bkt[w+1] >> sh;
    if (sh) { g0 |= bkt[w+1] << (64 - sh); g1 |= bkt[w+2] << (64 - sh); }
    unsigned __int128 R = ((unsigned __int128)g1 << 64) | g0;
    if (roundBit && (sticky || (R & 1))) {
        R += 1;
        if (R >= ((unsigned __int128)1 << 112)) { R >>= 1; ++s; }
    }
    int exp = a.exp + b.exp - 111 + s;
    if (exp > Q128_EMAX) { r.inf = true; return r; }
    if (exp < Q128_EMIN) { r.hi = 0; r.lo = 0; r.exp = 0; return r; }
    r.hi = (std::uint64_t)(R >> 48);
    r.lo = (std::uint64_t)(R & ((1ULL << 48) - 1));
    r.exp = exp;
    return r;
}

inline Q128 q128_addulp(Q128 x) {
    if (x.inf) return x;
    Q128 r = x;
    ++r.hi;
    if (r.hi == 0) { r.hi = 1ULL << 63; ++r.exp; }
    if (r.exp > Q128_EMAX) r.inf = true;
    return r;
}

inline Q128 q128_subulp(Q128 x) {
    if (x.inf || (x.exp == Q128_EMIN && x.hi == 1ULL << 63 && x.lo == 0)) return x;
    Q128 r = x;
    if (r.lo == 0) { r.lo = ~0ULL; --r.hi; } else --r.lo;
    if (r.hi < (1ULL << 63)) {
        if (x.exp > Q128_EMIN) { r.exp = x.exp - 1; r.hi = ~0ULL; r.lo = ~0ULL; }
        else { r.exp = Q128_EMIN; r.hi = (1ULL << 63) | ((1ULL << 63) - 1); r.lo = ~0ULL; }
    }
    return r;
}

inline std::string q128_format(const Q128& v, DispFmt df, int digits) {
    if (v.inf) return "inf";
    if (v.hi == 0) return "0";
    double man_d = (double)v.hi * std::ldexp(1.0, 48) + (double)v.lo;
    double log10v = std::log10(man_d) + (double)(v.exp - 111) * std::log10(2.0);
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

inline std::string q128_bits(const Q128& v) {
    std::string b;
    b += v.sign ? '1' : '0';
    int eb = v.exp + 16383;
    for (int i = 14; i >= 0; --i) b += ((eb >> i) & 1) ? '1' : '0';
    for (int i = 63; i >= 0; --i) b += ((v.hi >> i) & 1) ? '1' : '0';
    for (int i = 47; i >= 0; --i) b += ((v.lo >> i) & 1) ? '1' : '0';
    return b;
}

inline void run_q128_soft(double delay_ms, StartKind kind, StepMode sm, DispFmt df,
                   double start_val, double factor, int digits, ShowBits showbits = ShowBits::OFF,
                   RunResult* quiet = nullptr, std::int64_t refresh_ms = 0) {
    Q128 start;
    std::string tag;
    if (kind == StartKind::MinNormal || kind == StartKind::Subnormal) {
        start = Q128{false, Q128_EMIN, 1ULL << 63, 0, false};
        tag = (kind == StartKind::Subnormal) ? tr("min-subnormal(无denormal,取min-normal)", "min-subnormal(no denormal, use min-normal)") : "min-normalized";
    } else if (kind == StartKind::MaxFinite) {
        start = Q128{false, Q128_EMAX, ~0ULL, ~0ULL, false}; tag = "max-finite";
    } else if (!std::isnan(start_val)) { start = q128_encode(start_val); tag = "custom"; }
    else { start = q128_encode(1.0); tag = "1.0"; }
    Q128 fac = q128_encode(factor);
    if (!quiet) std::cout << cui::walk_header(
        tr("FP128(binary128,纯软件112位)", "FP128(binary128,software 112-bit)"),
        tag, step_factor_str(sm, factor), delay_ms);
    const bool want_bits = showbits != ShowBits::OFF && !quiet;
    auto q_extra = [showbits, want_bits](const Q128& v) -> std::string {
        if (!want_bits) return {};
        std::string b = q128_bits(v);
        return showbits == ShowBits::HEX ? bits_to_hex(b) : group_bits(b, 15, 112);
    };
    // dump callback: fp128 128-bit = 16 bytes
    auto q128_dump = [](const Q128& v) {
        std::string b = q128_bits(v);
        char buf[16] = {0};
        for (int i = 0; i < 16; ++i) {
            int bits = 0;
            for (int j = 0; j < 8; ++j) {
                int idx = i*8 + j;
                bits = (bits << 1) | (idx < (int)b.size() && b[idx] == '1' ? 1 : 0);
            }
            buf[i] = (char)bits;
        }
        if (g_dump_endian == 0) {
            for (int i = 0; i < 8; ++i) std::swap(buf[i], buf[15-i]);
        }
        std::fwrite(buf, 1, 16, g_dump_file);
        g_dump_bytes_written += 16;
    };
    drive_core(start,
          [&](Q128 v) -> Q128 {
              switch (sm) {
                  case StepMode::X102:    return q128_mul(v, fac);
                  case StepMode::MUL_ULP: { Q128 r = q128_mul(v, fac); double ulp = std::ldexp(1.0, r.exp - 63); return q128_mul(r, q128_encode(1.0 + ulp)); }
                  case StepMode::ADD_ULP: return q128_addulp(v);
                  case StepMode::SUB_ULP: return q128_subulp(v);
              }
              return v;
          },
          [](Q128 v) { return v.inf; },
          [](Q128 v) { return !v.inf && v.hi == 0 && v.lo == 0 && v.exp == 0; },
          [](Q128) { return false; },  // Q128 never produces NaN
          [&](const Q128& v) { return q128_format(v, df, digits); },
          delay_ms, q_extra, quiet, refresh_ms,
          q128_dump);
}

inline void run_fp128(double delay_ms, StartKind kind, StepMode sm, DispFmt df,
                      double start_val, double factor, int digits,
                      ShowBits showbits = ShowBits::OFF, RunResult* quiet = nullptr,
                      std::int64_t refresh_ms = 0) {
    run_q128_soft(delay_ms, kind, sm, df, start_val, factor, digits, showbits, quiet, refresh_ms);
}

} // namespace fw
