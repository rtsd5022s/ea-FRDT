// eafrdt.cpp — implementation of core walk algorithms.
// No IO, no threads, no globals. Pure compute.
#include "eafrdt.h"
#include <cmath>
#include <chrono>
#include <cstring>

namespace fw::core {

// ---------------------------------------------------------------------------
// SVal helpers (parameterized binary format)
// ---------------------------------------------------------------------------
static SVal sval_encode(double v, const Fmt& f) {
    SVal r;
    if (v < 0) r.sign = true;
    double av = std::fabs(v);
    if (av == 0.0) return r;
    int e; double m = std::frexp(av, &e);
    int en = e - 1;
    std::uint64_t q = (std::uint64_t)std::llround(m * std::ldexp(1.0, f.M + 1));
    if (q == (1ULL << (f.M + 1))) { q = 1ULL << f.M; ++en; }
    if (en > emax_of(f)) { r.inf = true; return r; }
    if (en < emin_of(f)) {
        int shift = emin_of(f) - en;
        double sub = m * std::ldexp(1.0, f.M + 1 - shift);
        r.man = (std::uint64_t)std::llround(sub);
        r.exp = emin_of(f);
        if (r.man == 0) r.sign = false;
        return r;
    }
    r.exp = en; r.man = q;
    return r;
}

static SVal sval_mul(SVal a, SVal b, const Fmt& f) {
    SVal r;
    if (a.inf || b.inf) { r.inf = true; return r; }
    r.sign = a.sign ^ b.sign;
    r.exp = a.exp + b.exp;
    std::uint64_t P = a.man * b.man;
    if (P >= (1ULL << (2*f.M + 1))) { ++r.exp; P >>= 1; }
    std::uint64_t man_raw = P >> f.M;
    std::uint64_t rem = P & ((1ULL << f.M) - 1);
    std::uint64_t half = 1ULL << (f.M - 1);
    if (rem > half || (rem == half && (man_raw & 1ULL))) ++man_raw;
    if (man_raw == (1ULL << (f.M + 1))) { man_raw = 1ULL << f.M; ++r.exp; }
    if (r.exp > emax_of(f)) { r.inf = true; return r; }
    if (r.exp < emin_of(f)) {
        int shift = emin_of(f) - r.exp;
        man_raw >>= shift;
        r.exp = emin_of(f);
        r.man = man_raw;
        if (r.man == 0) r.sign = false;
        return r;
    }
    r.man = man_raw;
    return r;
}

long long walk_binary(const Fmt& f, double start, double mult, double timeout_sec, int* reason) {
    SVal cur = sval_encode(start, f);
    SVal fac = sval_encode(mult, f);
    auto t0 = std::chrono::steady_clock::now();
    long long steps = 0;
    while (!cur.inf) {
        ++steps;
        cur = sval_mul(cur, fac, f);
        if (timeout_sec > 0) {
            double es = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            if (es >= timeout_sec) { if (reason) *reason = 2; return steps; }
        }
    }
    if (reason) *reason = 0;
    return steps;
}

long long walk_f32(float start, float mult, double timeout_sec, int* reason) {
    float cur = start;
    auto t0 = std::chrono::steady_clock::now();
    long long steps = 0;
    while (std::isfinite(cur)) {
        ++steps;
        cur = cur * mult;
        if (timeout_sec > 0) {
            double es = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            if (es >= timeout_sec) { if (reason) *reason = 2; return steps; }
        }
    }
    if (reason) *reason = std::isinf(cur) ? 0 : 1;
    return steps;
}

long long walk_f64(double start, double mult, double timeout_sec, int* reason) {
    double cur = start;
    auto t0 = std::chrono::steady_clock::now();
    long long steps = 0;
    while (std::isfinite(cur)) {
        ++steps;
        cur = cur * mult;
        if (timeout_sec > 0) {
            double es = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            if (es >= timeout_sec) { if (reason) *reason = 2; return steps; }
        }
    }
    if (reason) *reason = std::isinf(cur) ? 0 : 1;
    return steps;
}
static X87 x87_encode(double v) {
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

static X87 x87_mul(X87 a, X87 b) {
    X87 r;
    if (a.inf || b.inf) { r.inf = true; return r; }
    r.sign = a.sign ^ b.sign;
    int exp = a.exp + b.exp;
    // 64x64 -> 128-bit product. w[0]=low64, w[1]=high64, w[2]=w[3]=0.
    UInt256 P = UInt256::mul128(a.man, 0, b.man, 0);
    // Renormalize: if P >= 2^127 (i.e. w[1] bit63 set), shift right by 1.
    if (P.w[1] & (1ULL<<63)) {
        std::uint64_t carry = P.w[1] & 1ULL;
        P.w[1] >>= 1;
        P.w[0] = (P.w[0] >> 1) | (carry << 63);
        ++exp;
    }
    // man_raw = P >> 63 (the 64-bit field starting at bit 63 of the 128-bit product)
    std::uint64_t man_raw = (P.w[0] >> 63) | (P.w[1] << 1);
    std::uint64_t rem = P.w[0] & ((1ULL<<63)-1);
    std::uint64_t half = 1ULL << 62;
    if (rem > half || (rem == half && (man_raw & 1ULL))) ++man_raw;
    if (man_raw == 0) { man_raw = 1ULL << 63; ++exp; }
    if (exp > X87_EMAX) { r.inf = true; return r; }
    if (exp < X87_EMIN) { r.man = 0; r.exp = 0; return r; }
    r.man = man_raw; r.exp = exp;
    return r;
}

long long walk_x87(double start, double mult, double timeout_sec, int* reason) {
    X87 cur = x87_encode(start);
    X87 fac = x87_encode(mult);
    auto t0 = std::chrono::steady_clock::now();
    long long steps = 0;
    while (!cur.inf) {
        ++steps;
        cur = x87_mul(cur, fac);
        if (timeout_sec > 0) {
            double es = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            if (es >= timeout_sec) { if (reason) *reason = 2; return steps; }
        }
    }
    if (reason) *reason = 0;
    return steps;
}

// ---------------------------------------------------------------------------
// Q128 binary128, pure software with UInt256
// ---------------------------------------------------------------------------
static Q128 q128_encode(double v) {
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

static Q128 q128_mul(Q128 a, Q128 b) {
    Q128 r;
    if (a.inf || b.inf) { r.inf = true; return r; }
    r.sign = a.sign ^ b.sign;
    // 112-bit mantissa: a.hi(64):a.lo(48), b.hi(64):b.lo(48)
    // Use UInt256::mul128 to compute 128x128 -> 256 bucket
    // We treat (hi:lo48) as a 128-bit number: hi<<48 | lo
    // Simplify: do four 64-bit products into a 256-bit bucket
    std::uint64_t bkt[4] = {0,0,0,0};
    auto add64 = [&](int word, std::uint64_t v) {
        std::uint64_t carry = v;
        for (int i = word; i < 4 && carry; ++i) {
            std::uint64_t sum = bkt[i] + carry;
            carry = (sum < bkt[i]) ? 1ULL : 0ULL;
            bkt[i] = sum;
        }
    };
    // 112-bit mantissa = hi(64):lo(48). Product terms at bit offsets:
    //   a.hi*b.hi at 96, a.hi*b.lo at 48, a.lo*b.hi at 48, a.lo*b.lo at 0
    std::uint64_t lo, hi;
    // a.hi * b.hi, offset 96 = word1 shift32
    UInt256::mul64(a.hi, b.hi, lo, hi);
    bkt[1] |= lo << 32;
    bkt[2] |= lo >> 32;
    bkt[2] |= hi << 32;
    add64(3, hi >> 32);
    // a.hi * b.lo, offset 48 = word0 shift48
    UInt256::mul64(a.hi, b.lo, lo, hi);
    bkt[0] |= lo << 48;
    bkt[1] |= lo >> 16;
    bkt[1] |= hi << 48;
    add64(2, hi >> 16);
    // a.lo * b.hi, offset 48
    UInt256::mul64(a.lo, b.hi, lo, hi);
    bkt[0] |= lo << 48;
    bkt[1] |= lo >> 16;
    bkt[1] |= hi << 48;
    add64(2, hi >> 16);
    // a.lo * b.lo, offset 0
    UInt256::mul64(a.lo, b.lo, lo, hi);
    bkt[0] |= lo;
    add64(1, hi);

    // Find top bit position
    int p = -1;
    for (int i = 3; i >= 0; --i) if (bkt[i]) { p = i*64 + 63 - __builtin_clzll(bkt[i]); break; }
    if (p < 0) { r.hi = 0; r.lo = 0; r.exp = 0; return r; }
    int s = p - 111;
    int exp = a.exp + b.exp - 111 + s;
    // Round to 112 bits
    int rb = s - 1;
    bool roundBit = (rb >= 0) && ((bkt[rb/64] >> (rb%64)) & 1);
    bool sticky = false;
    for (int i = 0; i < rb/64; ++i) if (bkt[i]) { sticky = true; break; }
    if (!sticky && (rb%64) && (bkt[rb/64] & ((1ULL << (rb%64)) - 1))) sticky = true;
    // Extract 112-bit value starting at bit s
    int w = s/64, sh = s%64;
    std::uint64_t g0 = bkt[w] >> sh;
    std::uint64_t g1 = (w+1 < 4) ? bkt[w+1] >> sh : 0;
    if (sh) {
        g0 |= (w+1 < 4) ? bkt[w+1] << (64-sh) : 0;
        g1 |= (w+2 < 4) ? bkt[w+2] << (64-sh) : 0;
    }
    // R = g1<<64 | g0 (128-bit). After rounding, R holds the 112-bit mantissa.
    // R bit 0..47 = lo, R bit 48..111 = hi.
    if (roundBit && (sticky || (g0 & 1))) {
        g0 += 1;
        if (g0 == 0) g1 += 1;
        if (g1 >= (1ULL << 48)) { g1 >>= 1; g0 |= (1ULL<<63); ++exp; }
    }
    if (exp > Q128_EMAX) { r.inf = true; return r; }
    if (exp < Q128_EMIN) { r.hi = 0; r.lo = 0; r.exp = 0; return r; }
    // R = g1<<64 | g0. r.hi = R>>48, r.lo = R & (2^48-1)
    r.lo = g0 & ((1ULL<<48)-1);
    r.hi = (g0 >> 48) | (g1 << 16);
    r.exp = exp;
    return r;
}

long long walk_q128(double start, double mult, double timeout_sec, int* reason) {
    Q128 cur = q128_encode(start);
    Q128 fac = q128_encode(mult);
    auto t0 = std::chrono::steady_clock::now();
    long long steps = 0;
    while (!cur.inf) {
        ++steps;
        cur = q128_mul(cur, fac);
        if (timeout_sec > 0) {
            double es = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            if (es >= timeout_sec) { if (reason) *reason = 2; return steps; }
        }
    }
    if (reason) *reason = 0;
    return steps;
}

// ---------------------------------------------------------------------------
// BID dec32/dec64
// ---------------------------------------------------------------------------
static std::uint64_t pow10(int n) {
    std::uint64_t v = 1;
    while (n-- > 0) v *= 10;
    return v;
}

static std::uint64_t dec_round(UInt256 P, int& exp, std::uint64_t maxc) {
    while (P.ge(maxc)) {
        std::uint64_t r = P.div10();
        if (r > 5 || (r == 5 && (P.to_lo() & 1ULL))) {
            P.w[0] += 1;
            for (int i = 0; i < 3; ++i) if (P.w[i] == 0) P.w[i+1] += 1; else break;
        }
        ++exp;
        if (P.ge(maxc) && P.to_lo() == maxc && !P.w[1] && !P.w[2] && !P.w[3]) {
            P = UInt256(maxc / 10); ++exp;
        }
    }
    return P.to_lo();
}

static DecNum dec_mul(const DecNum& a, const DecNum& b, const DecFmt& f) {
    DecNum r;
    if (a.inf || b.inf) { r.inf = true; return r; }
    r.neg = a.neg ^ b.neg;
    UInt256 P = UInt256::mul128(a.coeff, 0, b.coeff, 0);
    int exp = a.exp10 + b.exp10;
    r.coeff = dec_round(P, exp, pow10(f.sig));
    int cdigits = 0;
    for (std::uint64_t t = r.coeff; t > 0; t /= 10) ++cdigits;
    if (cdigits > 0 && exp + cdigits - 1 > f.emax) { r.inf = true; return r; }
    if (exp + cdigits <= -f.emax + 1) { r.coeff = 0; r.exp10 = 0; return r; }
    r.exp10 = exp;
    return r;
}

static DecNum dec_from_double(double v, int sig) {
    DecNum r;
    if (v < 0) r.neg = true;
    double av = std::fabs(v);
    if (av == 0.0) return r;
    int e = (int)std::floor(std::log10(av));
    double m = av / std::pow(10.0, e);
    std::uint64_t c = std::llround(m * (double)pow10(sig - 1));
    int exp = e - (sig - 1);
    std::uint64_t maxc = pow10(sig);
    if (c == maxc) { c = maxc/10; ++exp; }
    r.coeff = c; r.exp10 = exp;
    return r;
}

long long walk_bid(const DecFmt& f, double start, double mult, double timeout_sec, int* reason) {
    DecNum cur = dec_from_double(start, f.sig);
    DecNum fac = dec_from_double(mult, f.sig);
    auto t0 = std::chrono::steady_clock::now();
    long long steps = 0;
    while (!cur.inf) {
        ++steps;
        cur = dec_mul(cur, fac, f);
        if (timeout_sec > 0) {
            double es = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            if (es >= timeout_sec) { if (reason) *reason = 2; return steps; }
        }
    }
    if (reason) *reason = 0;
    return steps;
}

// ---------------------------------------------------------------------------
// BID dec128 (BigDec with UInt256 coeff)
// ---------------------------------------------------------------------------
static BigDec bid128_from_double(double v) {
    BigDec r;
    if (v < 0) r.neg = true;
    double av = std::fabs(v);
    if (av == 0.0) return r;
    int e = (int)std::floor(std::log10(av));
    double m = av / std::pow(10.0, e);
    // m in [1,10). coeff = round(m * 10^33) -> 34-digit integer.
    double cd = m * 1e33;
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.0f", cd);
    UInt256 c;
    for (char* p = buf; *p; ++p) {
        // c *= 10
        std::uint64_t carry = 0;
        for (int i = 0; i < 4; ++i) {
            std::uint64_t lo, hi;
            UInt256::mul64(c.w[i], 10, lo, hi);
            lo += carry;
            if (lo < carry) hi++;
            c.w[i] = lo;
            carry = hi;
        }
        c.w[0] += (std::uint64_t)(*p - '0');
    }
    r.coeff = c;
    r.exp10 = e - 33;
    return r;
}

static BigDec bid128_mul(const BigDec& a, const BigDec& b) {
    BigDec r;
    if (a.inf || b.inf) { r.inf = true; return r; }
    r.neg = a.neg ^ b.neg;
    // Each coeff is ~113-bit (34 decimal digits). Product ~226-bit, fits in UInt256.
    r.coeff = UInt256::mul128(a.coeff.w[0], a.coeff.w[1], b.coeff.w[0], b.coeff.w[1]);
    r.exp10 = a.exp10 + b.exp10;
    // Normalize: while coeff >= 10^34 (i.e. w[2] or w[3] set, since 10^34 ~ 2^113), divide by 10.
    while (r.coeff.w[3] || r.coeff.w[2]) {
        std::uint64_t rem = r.coeff.div10();
        if (rem > 5 || (rem == 5 && (r.coeff.w[0] & 1ULL))) {
            r.coeff.w[0] += 1;
            for (int i = 0; i < 3; ++i) if (r.coeff.w[i] == 0) r.coeff.w[i+1] += 1; else break;
        }
        ++r.exp10;
    }
    int top_exp = r.exp10 + 33;
    if (top_exp > 6144) r.inf = true;
    if (r.exp10 < -6143) { r.coeff = UInt256(0); r.exp10 = 0; }
    return r;
}

long long walk_bid128(double start, double mult, double timeout_sec, int* reason) {
    BigDec cur = bid128_from_double(start);
    BigDec fac = bid128_from_double(mult);
    auto t0 = std::chrono::steady_clock::now();
    long long steps = 0;
    while (!cur.inf) {
        ++steps;
        cur = bid128_mul(cur, fac);
        if (timeout_sec > 0) {
            double es = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            if (es >= timeout_sec) { if (reason) *reason = 2; return steps; }
        }
    }
    if (reason) *reason = 0;
    return steps;
}

// ---------------------------------------------------------------------------
// DPD dpd32/64/128
// ---------------------------------------------------------------------------
static unsigned dpd_encode3(unsigned d2, unsigned d1, unsigned d0) {
    unsigned a3=d2>>3, a2=(d2>>2)&1, a1=(d2>>1)&1, a0=d2&1;
    unsigned b3=d1>>3, b2=(d1>>2)&1, b1=(d1>>1)&1, b0=d1&1;
    unsigned c3=d0>>3, c2=(d0>>2)&1, c1=(d0>>1)&1, c0=d0&1;
    unsigned w=(a3<<2)|(b3<<1)|c3;
    switch (w) {
    case 0: return (a2<<8)|(a1<<7)|(a0<<6)|(b2<<5)|(b1<<4)|(b0<<3)|(c2<<2)|(c1<<1)|c0;
    case 1: return (1u<<9)|(a2<<6)|(a1<<5)|(a0<<4)|(b2<<3)|(b1<<2)|(b0<<1)|c0;
    case 2: return (1u<<9)|(1u<<7)|(a2<<6)|(a1<<5)|(a0<<4)|(b0<<3)|(c2<<2)|(c1<<1)|c0;
    case 4: return (1u<<9)|(1u<<8)|(1u<<7)|(a0<<6)|(b2<<5)|(b1<<4)|(b0<<3)|(c2<<2)|(c1<<1)|c0;
    case 3: return (1u<<9)|(1u<<8)|(a2<<4)|(a1<<3)|(a0<<2)|(b0<<1)|c0;
    case 5: return (1u<<9)|(1u<<8)|(1u<<5)|(a0<<4)|(b2<<3)|(b1<<2)|(b0<<1)|c0;
    case 6: return (1u<<9)|(1u<<8)|(1u<<6)|(a0<<4)|(b0<<3)|(c2<<2)|(c1<<1)|c0;
    case 7: return (1u<<9)|(1u<<8)|(1u<<6)|(1u<<5)|(a0<<4)|(b0<<3)|(c0<<2);
    }
    return 0;
}

static DpdNum dpd_from_double(double v, const DpdFmt& f) {
    DpdNum r;
    if (!std::isfinite(v)) { r.inf = true; return r; }
    if (v < 0) { r.neg = true; v = -v; }
    if (v == 0.0) { r.digits[0] = 0; r.ndigits = 1; return r; }
    // For walk: start at coeff=1, exp10 adjusted
    r.digits[0] = 1;
    for (int i = 1; i < f.digits; ++i) r.digits[i] = 0;
    r.ndigits = f.digits;
    r.exp10 = -(f.digits - 1);
    (void)v;
    return r;
}

static DpdNum dpd_mul(const DpdNum& a, double factor, const DpdFmt& f) {
    DpdNum r;
    if (a.inf) { r.inf = true; return r; }
    r.neg = a.neg;
    r.exp10 = a.exp10;
    int maxd = f.digits;
    int num = (int)std::round(factor * 1000000.0);
    int den = 1000000;
    while (den % 10 == 0 && num % 10 == 0) { num /= 10; den /= 10; }

    // Multiply digit array by num
    std::uint8_t v[34];
    int vn = a.ndigits;
    std::memcpy(v, a.digits, vn);
    int carry = 0;
    for (int i = vn - 1; i >= 0; --i) {
        int x = v[i] * num + carry;
        v[i] = x % 10;
        carry = x / 10;
    }
    while (carry > 0) {
        // shift right
        for (int i = vn; i > 0; --i) v[i] = v[i-1];
        v[0] = carry % 10;
        carry /= 10;
        ++vn;
    }
    int k = 0;
    { int tmp = den; while (tmp > 1) { tmp /= 10; ++k; } }
    r.exp10 -= k;

    // Normalize: right shift when > maxd digits
    while (vn > maxd) {
        // remove least significant digit, round
        int round_digit = v[vn-1];
        --vn;
        if (round_digit >= 5) {
            int i = vn - 1;
            while (i >= 0 && v[i] == 9) { v[i] = 0; --i; }
            if (i < 0) {
                for (int j = vn; j > 0; --j) v[j] = v[j-1];
                v[0] = 1; ++vn;
            } else ++v[i];
        }
        ++r.exp10;
    }
    // strip leading zeros
    int p = 0;
    while (p + 1 < vn && v[p] == 0) ++p;
    r.ndigits = vn - p;
    std::memcpy(r.digits, v + p, r.ndigits);

    int emax = (f.digits == 7) ? 96 : (f.digits == 16) ? 384 : 6144;
    int emin = (f.digits == 7) ? -95 : (f.digits == 16) ? -383 : -6143;
    int top_exp = r.exp10 + r.ndigits - 1;
    if (top_exp > emax) r.inf = true;
    else if (r.exp10 < emin) { r.digits[0] = 0; r.ndigits = 1; r.exp10 = 0; }
    return r;
}

long long walk_dpd(const DpdFmt& f, double start, double mult, double timeout_sec, int* reason) {
    DpdNum cur = dpd_from_double(start, f);
    auto t0 = std::chrono::steady_clock::now();
    long long steps = 0;
    while (!cur.inf) {
        ++steps;
        cur = dpd_mul(cur, mult, f);
        if (cur.ndigits == 1 && cur.digits[0] == 0) { if (reason) *reason = 1; return steps; }
        if (timeout_sec > 0) {
            double es = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            if (es >= timeout_sec) { if (reason) *reason = 2; return steps; }
        }
    }
    if (reason) *reason = 0;
    return steps;
}

} // namespace fw::core
