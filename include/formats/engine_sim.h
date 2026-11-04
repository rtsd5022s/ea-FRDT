// engine_sim.h — Shared hand-rolled bit-level software engine (parameterized E/M/bias).
// Reused by fp4, fp6, fp8-e4m3, fp8-e5m2, bf16, tf32 (see formats/fp4.h etc.).
#pragma once
#include "common.h"

namespace fw {

// ---------------------------------------------------------------------------
// Parameterized bit-level SVal engine: E exponent bits / M mantissa bits / bias
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

inline SVal step_subulp(SVal x, const Fmt& f) {
    if (x.inf || (x.exp == emin(f) && x.man == 0)) return x;
    SVal r = x;
    --r.man;
    if (r.man < (1ULL << f.M)) {
        if (x.exp > emin(f)) {
            r.exp = x.exp - 1;
            r.man = (1ULL << (f.M + 1)) - 1;
        } else {
            // already subnormal: keep r.man (0..2^M-1), no wrap
            r.exp = emin(f);
        }
    }
    return r;
}

inline void run_sim(const Fmt& f, double delay_ms, StartKind kind, StepMode sm, DispFmt df,
             double start_val, double factor, int digits, ShowBits showbits = ShowBits::OFF,
             RunResult* quiet = nullptr, std::int64_t refresh_ms = 0) {
    SVal start;
    const char* start_tag;
    if (kind == StartKind::Subnormal) { start = SVal{false, emin(f), 1, false}; start_tag = "min-subnormal"; }
    else if (kind == StartKind::MinNormal) { start = SVal{false, emin(f), 1, false}; start_tag = "min"; }
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
            int eb;
            if (v.man < (1ULL << f.M)) eb = 0;
            else eb = v.exp + f.bias;
            for (int i = f.E - 1; i >= 0; --i) b += ((eb >> i) & 1) ? '1' : '0';
            std::uint64_t m = v.man & ((1ULL << f.M) - 1);
            for (int i = f.M - 1; i >= 0; --i) b += ((m >> i) & 1) ? '1' : '0';
            return showbits == ShowBits::HEX ? bits_to_hex(b) : group_bits(b, f.E, f.M);
        };
    }
    // dump callback: write bit-pattern bytes
    int total_bits = 1 + f.E + f.M;
    int b = (total_bits + 7) / 8;
    int nat_bytes;
    if (b <= 1) nat_bytes = 1;
    else if (b <= 2) nat_bytes = 2;
    else if (b <= 4) nat_bytes = 4;
    else if (b <= 8) nat_bytes = 8;
    else nat_bytes = 16;
    int rec_bytes;
    if (g_dump_packed) {
        rec_bytes = (total_bits + 7) / 8;  // tight packed
        g_dump_packed_active = (rec_bytes < nat_bytes) ? 1 : 0;
    } else {
        rec_bytes = nat_bytes;
        g_dump_packed_active = 0;
    }
    auto sim_dump = [&f, rec_bytes](const SVal& v) {
        std::string b;
        b += v.sign ? '1' : '0';
        int eb;
        if (v.man < (1ULL << f.M)) eb = 0;  // subnormal: biased exp = 0
        else eb = v.exp + f.bias;
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
        if (g_dump_endian == 0) {
            for (int i = 0; i < rec_bytes/2; ++i) std::swap(buf[i], buf[rec_bytes-1-i]);
        }
        g_dump_record_size = rec_bytes;  // authoritative width only when records are actually written
        std::fwrite(buf, 1, rec_bytes, g_dump_file);
        g_dump_bytes_written += rec_bytes;
    };
    drive(start,
          [&](SVal v) -> SVal {
              switch (sm) {
                  case StepMode::X102:    return mul(v, encode(factor, f), f);
                  case StepMode::MUL_ULP: { SVal r = mul(v, encode(factor, f), f); return step_mululp(r, f); }
                  case StepMode::ADD_ULP: return step_addulp(v, f);
                  case StepMode::SUB_ULP: return step_subulp(v, f);
              }
              return v;
          },
          [](SVal v) { return v.inf; },
          [](SVal v) { return !v.inf && v.man == 0 && v.exp == 0; },
          [](SVal) { return false; },  // SVal never produces NaN (inf flag covers overflow)
          [&](SVal v) { return to_double(v, f); },
          delay_ms, df, digits, sim_extra, quiet, refresh_ms,
          std::function<void(const SVal&)>(sim_dump));
}

} // namespace fw
