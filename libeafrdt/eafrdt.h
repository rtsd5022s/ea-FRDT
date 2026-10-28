// eafrdt.h — ea-FRDT core algorithm library, platform-independent
// Namespace: fw::core. No IO, no threads, no ANSI, no globals.
#pragma once
#include <cstdint>
#include <cstddef>
#include <string>

namespace fw::core {

// ===========================================================================
// UInt256 — hand-rolled 256-bit unsigned big integer (schoolbook multiply)
// Pure uint64_t arithmetic, no __int128 dependency.
// ===========================================================================
struct UInt256 {
    std::uint64_t w[4] = {0,0,0,0};
    UInt256() = default;
    UInt256(std::uint64_t v) { w[0]=v; w[1]=w[2]=w[3]=0; }

    static void mul64(std::uint64_t x, std::uint64_t y,
                      std::uint64_t& lo, std::uint64_t& hi) {
        std::uint64_t x0 = x & 0xFFFFFFFFULL, x1 = x >> 32;
        std::uint64_t y0 = y & 0xFFFFFFFFULL, y1 = y >> 32;
        std::uint64_t p0 = x0*y0, p1 = x0*y1, p2 = x1*y0, p3 = x1*y1;
        std::uint64_t mid = (p0>>32) + (p1&0xFFFFFFFFULL) + (p2&0xFFFFFFFFULL);
        lo = (p0 & 0xFFFFFFFFULL) | (mid << 32);
        hi = p3 + (p1>>32) + (p2>>32) + (mid>>32);
    }

    // Multiply two 128-bit numbers (a_hi:a_lo) * (b_hi:b_lo) -> 256-bit
    static UInt256 mul128(std::uint64_t a_lo, std::uint64_t a_hi,
                          std::uint64_t b_lo, std::uint64_t b_hi) {
        UInt256 r;
        std::uint64_t lo, hi;
        mul64(a_lo, b_lo, r.w[0], r.w[1]);
        mul64(a_hi, b_lo, lo, hi);
        std::uint64_t c = r.w[1] + lo; r.w[1] = c;
        r.w[2] = hi + (c < r.w[1] ? 1ULL : 0ULL);
        mul64(a_lo, b_hi, lo, hi);
        c = r.w[1] + lo;
        std::uint64_t carry1 = (c < r.w[1]) ? 1ULL : 0ULL;
        r.w[1] = c;
        std::uint64_t c2 = r.w[2] + hi + carry1;
        r.w[2] = c2;
        r.w[3] = (c2 < r.w[2] + carry1) ? 1ULL : 0ULL;
        mul64(a_hi, b_hi, lo, hi);
        c = r.w[2] + lo;
        std::uint64_t carry2 = (c < r.w[2]) ? 1ULL : 0ULL;
        r.w[2] = c;
        r.w[3] += hi + carry2;
        return r;
    }

    // Divide by 10 in place, return remainder. 2^64/10=0x1999..., 2^64%10=6.
    std::uint64_t div10() {
        std::uint64_t rem = 0;
        for (int i = 3; i >= 0; --i) {
            std::uint64_t q = w[i]/10, r = w[i]%10;
            q += rem * 0x1999999999999999ULL;
            r += rem * 6ULL;
            q += r/10; r = r%10;
            w[i] = q; rem = r;
        }
        return rem;
    }

    bool ge(std::uint64_t v) const {
        if (w[3]||w[2]||w[1]) return true;
        return w[0] >= v;
    }
    std::uint64_t to_lo() const { return w[0]; }
    bool is_zero() const { return !w[0] && !w[1] && !w[2] && !w[3]; }
};

// ===========================================================================
// Binary floating-point emulation: parameterized E/M/bias (fp4..tf32)
// ===========================================================================
struct Fmt { int E, M, bias; };

struct SVal {
    bool sign = false;
    int exp = 0;
    std::uint64_t man = 0;
    bool inf = false;
};

constexpr int emax_of(const Fmt& f) { return (1 << f.E) - 1 - f.bias; }
constexpr int emin_of(const Fmt& f) { return 1 - f.bias; }

// Format descriptors
inline Fmt fmt_fp4()    { return {3,  2,  7};   } // E2M1? kept as 3/2 for legacy
inline Fmt fmt_fp6()    { return {3,  3,  7};   }
inline Fmt fmt_fp8e4m3(){ return {4,  3,  7};   }
inline Fmt fmt_fp8e5m2(){ return {5,  2,  15};  }
inline Fmt fmt_fp16()   { return {5,  10, 15};  }
inline Fmt fmt_bf16()   { return {8,  7,  127}; }
inline Fmt fmt_tf32()   { return {8,  10, 127}; }
inline Fmt fmt_f32()    { return {8,  23, 127}; }
inline Fmt fmt_f64()    { return {11, 52, 1023};}

// ===========================================================================
// x87 80-bit extended precision, pure software (no __int128)
// ===========================================================================
struct X87 {
    bool sign = false;
    int exp = 0;
    std::uint64_t man = 0;
    bool inf = false;
};
constexpr int X87_EMAX = 16384;
constexpr int X87_EMIN = -16382;

// ===========================================================================
// FP128 binary128, pure software 112-bit mantissa (uses UInt256, no __int128)
// ===========================================================================
struct Q128 {
    bool sign = false;
    int exp = 0;
    std::uint64_t hi = 0;  // top 64 bits of 112-bit mantissa
    std::uint64_t lo = 0;  // low 48 bits
    bool inf = false;
};
constexpr int Q128_EMAX = 16384;
constexpr int Q128_EMIN = -16382;

// ===========================================================================
// Decimal BID (Binary Integer Decimal): dec32/dec64 via uint64 coeff,
// dec128 via UInt256 coeff. IEEE 754-2008.
// ===========================================================================
struct DecFmt {
    int sig;       // significant digits (7/16/34)
    int emax;      // 96/384/6144
    int bias;
};
inline DecFmt bid_fmt32()  { return {7,   96,  101};  }
inline DecFmt bid_fmt64()  { return {16,  384, 398};  }
inline DecFmt bid_fmt128() { return {34,  6144,6176}; }

struct DecNum {
    bool neg = false;
    std::uint64_t coeff = 0;  // for dec32/dec64
    int exp10 = 0;
    bool inf = false;
};

// dec128: coefficient as UInt256
struct BigDec {
    bool neg = false;
    UInt256 coeff;
    int exp10 = 0;
    bool inf = false;
};

// ===========================================================================
// DPD (Densely Packed Decimal): dec32/64/128, digit-array coefficient
// ===========================================================================
struct DpdFmt {
    int exp_bits;
    int coeff_bits;
    int digits;
    long long bias;
};
inline DpdFmt dpd_fmt32()  { return {6,  20,  7,   101}; }
inline DpdFmt dpd_fmt64()  { return {8,  50,  16,  398}; }
inline DpdFmt dpd_fmt128() { return {12, 110, 34, 6176}; }

struct DpdNum {
    bool neg = false;
    bool inf = false;
    int exp10 = 0;
    std::uint8_t digits[34]; // max 34 significant digits, MSB first
    int ndigits = 0;
};

// ===========================================================================
// Step-mode walk: from start, apply multiplier until overflow/underflow.
// Returns step count; *reason_out gets exit reason.
// reason: 0=overflow, 1=underflow, 2=timeout
// ===========================================================================

// Walk a parameterized binary format.
// multiplier e.g. 1.02; returns step count to reach inf.
long long walk_binary(const Fmt& f, double start, double multiplier,
                      double timeout_sec, int* reason_out);

// Walk x87 80-bit.
long long walk_x87(double start, double multiplier, double timeout_sec, int* reason_out);

// Walk fp128 112-bit.
long long walk_q128(double start, double multiplier, double timeout_sec, int* reason_out);

// Walk BID dec32/dec64 (uint64 coeff).
long long walk_bid(const DecFmt& f, double start, double multiplier,
                   double timeout_sec, int* reason_out);

// Walk BID dec128 (UInt256 coeff).
long long walk_bid128(double start, double multiplier, double timeout_sec, int* reason_out);

// Walk DPD dpd32/64/128.
long long walk_dpd(const DpdFmt& f, double start, double multiplier,
                   double timeout_sec, int* reason_out);

} // namespace fw::core
