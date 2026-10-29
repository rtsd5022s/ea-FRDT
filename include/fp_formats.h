// fp_formats.h — Binary float formats: hand-rolled bit-level (SVal) + native (NativeF) + x87 + fp128
#pragma once
#include "common.h"

namespace fw {

// ---------------------------------------------------------------------------
// Hand-rolled bit-level software simulation: parameterized E (exponent bits) / M (mantissa bits) / bias
// ---------------------------------------------------------------------------
struct Fmt { int E, M, bias; const char* name; };

struct SVal {
    bool sign = false;
    int exp = 0;
    std::uint64_t man = 0;
    bool inf = false;
    friend bool operator==(const SVal&, const SVal&) = default;
};

inline int emax(const Fmt& f) { return (1 << f.E) - 1 - f.bias; }
inline int emin(const Fmt& f) { return 1 - f.bias; }

inline SVal encode(double v, const Fmt& f) {
    SVal r;
    if (v < 0) r.sign = true;
    double av = std::fabs(v);
    if (av == 0.0) return r;
    int e; double m = std::frexp(av, &e);
    int e_norm = e - 1;
    std::uint64_t q = (std::uint64_t)std::llround(m * std::ldexp(1.0, f.M + 1));
    if (q == (1ULL << (f.M + 1))) { q = 1ULL << f.M; ++e_norm; }
    if (e_norm > emax(f)) { r.inf = true; return r; }
    if (e_norm < emin(f)) {
        int shift = emin(f) - e_norm;
        double sub = m * std::ldexp(1.0, f.M + 1 - shift);
        r.man = (std::uint64_t)std::llround(sub);
        r.exp = emin(f);
        if (r.man == 0) r.sign = false;
        return r;
    }
    r.exp = e_norm; r.man = q;
    return r;
}

inline SVal mul(SVal a, SVal b, const Fmt& f) {
    SVal r;
    if (a.inf || b.inf) { r.inf = true; return r; }
    r.sign = a.sign ^ b.sign;
    r.exp = a.exp + b.exp;
    std::uint64_t P = a.man * b.man;
    if (P >= (1ULL << (2 * f.M + 1))) { ++r.exp; P >>= 1; }
    std::uint64_t man_raw = P >> f.M;
    std::uint64_t rem = P & ((1ULL << f.M) - 1);
    std::uint64_t half = 1ULL << (f.M - 1);
    if (rem > half || (rem == half && (man_raw & 1ULL))) ++man_raw;
    if (man_raw == (1ULL << (f.M + 1))) { man_raw = 1ULL << f.M; ++r.exp; }
    if (r.exp > emax(f)) { r.inf = true; return r; }
    if (r.exp < emin(f)) {
        int shift = emin(f) - r.exp;
        man_raw >>= shift;
        r.exp = emin(f);
        r.man = man_raw;
        if (r.man == 0) r.sign = false;
        return r;
    }
    r.man = man_raw;
    return r;
}

inline double to_double(const SVal& v, const Fmt& f) {
    if (v.inf) return INFINITY;
    if (v.man == 0) return 0.0;
    int e = (v.exp == emin(f) && v.man < (1ULL << f.M)) ? emin(f) : v.exp;
    double frac = (double)v.man / std::ldexp(1.0, f.M);
    double out = std::ldexp(frac, e);
    return v.sign ? -out : out;
}

inline SVal step_mululp(SVal x, const Fmt& f) {
    if (x.inf) return x;
    double ulp = std::ldexp(1.0, x.exp - f.M);
    SVal fac = encode(1.0 + ulp, f);
    return mul(x, fac, f);
}

inline SVal step_addulp(SVal x, const Fmt& f) {
    if (x.inf) return x;
    SVal r = x;
    ++r.man;
    if (x.exp == emin(f) && x.man < (1ULL << f.M) && r.man == (1ULL << f.M)) {
        r.exp = emin(f) + 1; r.man = 1ULL << f.M;
    } else if (r.man == (1ULL << (f.M + 1))) {
        r.man = 1ULL << f.M; ++r.exp;
    }
    if (r.exp > emax(f)) r.inf = true;
    return r;
}

inline void run_sim(const Fmt& f, double delay_ms, StartKind kind, StepMode sm, DispFmt df,
             double start_val, double factor, int digits, ShowBits showbits = ShowBits::OFF,
             RunResult* quiet = nullptr, std::int64_t refresh_ms = 0) {
    SVal start;
    const char* start_tag;
    if (kind == StartKind::Subnormal) { start = SVal{false, emin(f), 1, false}; start_tag = "min-subnormal"; }
    else if (kind == StartKind::MinNormal) { start = SVal{false, emin(f), 1ULL << f.M, false}; start_tag = "min-normalized"; }
    else if (kind == StartKind::MaxFinite) { start = SVal{false, emax(f), (1ULL << (f.M + 1)) - 1, false}; start_tag = "max-finite"; }
    else if (!std::isnan(start_val)) { start = encode(start_val, f); start_tag = "custom"; }
    else { start = encode(1.0, f); start_tag = "1.0"; }
    if (!quiet) std::cout << f.name << " walk: start=" << start_tag
              << ", " << step_factor_str(sm, factor)
              << ", delay=" << delay_ms << "ms (0=unlimited)\n";
    std::function<std::string(const SVal&)> sim_extra;
    if (showbits != ShowBits::OFF && !quiet) {
        sim_extra = [&](const SVal& v) -> std::string {
            std::string b;
            b += v.sign ? '1' : '0';
            int eb = v.exp + f.bias;
            for (int i = f.E - 1; i >= 0; --i) b += ((eb >> i) & 1) ? '1' : '0';
            std::uint64_t m = v.man & ((1ULL << f.M) - 1);
            for (int i = f.M - 1; i >= 0; --i) b += ((m >> i) & 1) ? '1' : '0';
            return showbits == ShowBits::HEX ? bits_to_hex(b) : group_bits(b, f.E, f.M);
        };
    }
    // dump callback: write bit-pattern bytes
    int total_bits = 1 + f.E + f.M;
    int rec_bytes;
    if (g_dump_packed) {
        rec_bytes = (total_bits + 7) / 8;  // tight packed
    } else {
        // natural width: pad up to 1/2/4/8/16
        int b = (total_bits + 7) / 8;
        if (b <= 1) rec_bytes = 1;
        else if (b <= 2) rec_bytes = 2;
        else if (b <= 4) rec_bytes = 4;
        else if (b <= 8) rec_bytes = 8;
        else rec_bytes = 16;
    }
    auto sim_dump = [&f, rec_bytes](const SVal& v) {
        std::string b;
        b += v.sign ? '1' : '0';
        int eb = v.exp + f.bias;
        for (int i = f.E - 1; i >= 0; --i) b += ((eb >> i) & 1) ? '1' : '0';
        std::uint64_t m = v.man & ((1ULL << f.M) - 1);
        for (int i = f.M - 1; i >= 0; --i) b += ((m >> i) & 1) ? '1' : '0';
        char buf[4] = {0};
        for (int i = 0; i < rec_bytes; ++i) {
            int bits = 0;
            for (int j = 0; j < 8; ++j) {
                int idx = i*8 + j;
                if (idx < (int)b.size()) bits = (bits << 1) | (b[idx] == '1' ? 1 : 0);
                else bits <<= 1;
            }
            buf[i] = (char)bits;
        }
        if (g_dump_endian == 1) {
            for (int i = 0; i < rec_bytes/2; ++i) std::swap(buf[i], buf[rec_bytes-1-i]);
        }
        std::fwrite(buf, 1, rec_bytes, g_dump_file);
        g_dump_bytes_written += rec_bytes;
    };
    drive(start,
          [&](SVal v) -> SVal {
              switch (sm) {
                  case StepMode::X102:    return mul(v, encode(factor, f), f);
                  case StepMode::MUL_ULP: { SVal r = mul(v, encode(factor, f), f); return step_mululp(r, f); }
                  case StepMode::ADD_ULP: return step_addulp(v, f);
              }
              return v;
          },
          [](SVal v) { return v.inf; },
          [](SVal v) { return !v.inf && v.man == 0 && v.exp == 0; },
          [](SVal v) { return false; },  // SVal never produces NaN (inf flag covers overflow)
          [&](SVal v) { return to_double(v, f); },
          delay_ms, df, digits, sim_extra, quiet, refresh_ms,
          std::function<void(const SVal&)>(sim_dump));
}

// ---------------------------------------------------------------------------
// Native type driver
// ---------------------------------------------------------------------------
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
    if (neg) raw -= 1; else raw += 1;
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
    else if (kind == StartKind::MinNormal) { one = std::numeric_limits<T>::min(); start_tag = "min-normalized"; }
    else if (kind == StartKind::MaxFinite) { one = std::numeric_limits<T>::max(); start_tag = "max-finite"; }
    else if (!std::isnan(start_val)) { one = (T)start_val; start_tag = "custom"; }
    else { one = T(1); start_tag = "1.0"; }
    T fac = (T)factor;
    if (!quiet) std::cout << tag << " walk: start=" << start_tag
              << ", " << step_factor_str(sm, factor)
              << ", delay=" << delay_ms << "ms (0=unlimited)\n";
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
              }
              return v;
          },
          [](T v) { return std::isinf((double)v); },
          [](T v) { return v == T(0); },
          [](T v) { return std::isnan((double)v); },
          [](T v) { return (double)v; },
          delay_ms, df, digits, nat_extra, quiet, refresh_ms);
}

// ---------------------------------------------------------------------------
// x87 80-bit extended precision, pure software
// ---------------------------------------------------------------------------
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
    // dump callback: x87 80-bit = 10 bytes, padded to 16
    auto x87_dump = [](const X87& v) {
        std::string b = x87_bits(v);
        char buf[16] = {0};
        for (int i = 0; i < 10; ++i) {
            int bits = 0;
            for (int j = 0; j < 8; ++j) {
                int idx = i*8 + j;
                bits = (bits << 1) | (idx < (int)b.size() && b[idx] == '1' ? 1 : 0);
            }
            buf[i] = (char)bits;
        }
        if (g_dump_endian == 1) {
            for (int i = 0; i < 5; ++i) std::swap(buf[i], buf[9-i]);
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
              }
              return v;
          },
          [](X87 v) { return v.inf; },
          [](X87 v) { return !v.inf && v.man == 0 && v.exp == 0; },
          [](X87 v) { return false; },  // X87 never produces NaN
          [&](const X87& v) { return x87_format(v, df, digits); },
          delay_ms, x87_extra, quiet, refresh_ms,
          std::function<void(const X87&)>(x87_dump));
}

// ---------------------------------------------------------------------------
// FP128 binary128, pure software, full 112-bit mantissa
// ---------------------------------------------------------------------------
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
    if (!quiet) std::cout << tr("FP128(binary128,纯软件112位) ", "FP128(binary128,software 112-bit) ") << "walk: start=" << tag << ", " << step_factor_str(sm, factor)
              << ", delay=" << delay_ms << "ms (0=unlimited)\n";
    std::function<std::string(const Q128&)> q_extra;
    if (showbits != ShowBits::OFF && !quiet) {
        q_extra = [showbits](const Q128& v) -> std::string {
            std::string b = q128_bits(v);
            return showbits == ShowBits::HEX ? bits_to_hex(b) : group_bits(b, 15, 112);
        };
    }
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
        if (g_dump_endian == 1) {
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
              }
              return v;
          },
          [](Q128 v) { return v.inf; },
          [](Q128 v) { return !v.inf && v.hi == 0 && v.lo == 0 && v.exp == 0; },
          [](Q128 v) { return false; },  // Q128 never produces NaN
          [&](const Q128& v) { return q128_format(v, df, digits); },
          delay_ms, q_extra, quiet, refresh_ms,
          std::function<void(const Q128&)>(q128_dump));
}

} // namespace fw
