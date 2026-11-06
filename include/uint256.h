// uint256.h — Single-header hand-rolled 256-bit unsigned big integer
//
// Drop-in:  #include "uint256.h"   (only needs <cstdint>, <cstring>, <string>)
// Pure uint64_t arithmetic, no __int128, no external deps.
// GCC/Clang only (uses __builtin_clzll); MSVC not supported by design.
//
//   UInt256 a(12345), b = UInt256::mul128(lo, hi, lo, hi);
//   a += b;  a *= 10;  std::string s = a.to_string();
//   if (a > b) ...
#pragma once
#if defined(_WIN32) && !defined(__MINGW32__)
#error "ea-FRDT requires MinGW target (x86_64-w64-mingw32 / x86_64-w64-windows-gnu). Do not use MSVC or LLVM's default windows-msvc clang; use MSYS2 clang64."
#endif
#include <cstdint>
#include <cstring>
#include <string>

namespace fw {

struct UInt256 {
    std::uint64_t w[4] = {0,0,0,0};

    // ---- constructors ----
    UInt256() = default;
    UInt256(std::uint64_t v) { w[0]=v; w[1]=w[2]=w[3]=0; }
    UInt256(std::uint64_t w0, std::uint64_t w1, std::uint64_t w2, std::uint64_t w3)
        : w{w0,w1,w2,w3} {}

    // ---- low-level 64x64 -> 128 multiply (schoolbook via 32-bit halves) ----
    static void mul64(std::uint64_t x, std::uint64_t y,
                      std::uint64_t& lo, std::uint64_t& hi) {
        std::uint64_t x0 = x & 0xFFFFFFFFULL, x1 = x >> 32;
        std::uint64_t y0 = y & 0xFFFFFFFFULL, y1 = y >> 32;
        std::uint64_t p0 = x0*y0, p1 = x0*y1, p2 = x1*y0, p3 = x1*y1;
        std::uint64_t mid = (p0>>32) + (p1&0xFFFFFFFFULL) + (p2&0xFFFFFFFFULL);
        lo = (p0 & 0xFFFFFFFFULL) | (mid << 32);
        hi = p3 + (p1>>32) + (p2>>32) + (mid>>32);
    }

    // 128x128 -> 256 multiply
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

    // Full 256x256 -> 256 multiply (truncates to low 256 bits; carry discarded).
    static UInt256 mul(const UInt256& a, const UInt256& b) {
        UInt256 r;
        std::uint64_t lo, hi;
        for (int i = 0; i < 4; ++i) {
            std::uint64_t carry = 0;
            for (int j = 0; j < 4 - i; ++j) {
                mul64(a.w[i], b.w[j], lo, hi);
                std::uint64_t s = r.w[i+j] + lo + carry;
                std::uint64_t c1 = (s < r.w[i+j]) ? 1ULL : 0ULL;
                std::uint64_t c2 = (s < lo) ? 1ULL : 0ULL;
                r.w[i+j] = s;
                carry = hi + c1 + c2;
            }
        }
        return r;
    }

    // ---- small-integer arithmetic ----

    void inc() {
        w[0] += 1;
        for (int i = 0; i < 3; ++i) { if (w[i] != 0) break; w[i+1] += 1; }
    }

    void add_small(std::uint64_t v) {
        std::uint64_t s = w[0] + v;
        std::uint64_t carry = (s < w[0]) ? 1ULL : 0ULL;
        w[0] = s;
        for (int i = 1; i < 4 && carry; ++i) {
            std::uint64_t t = w[i] + carry;
            carry = (t < w[i]) ? 1ULL : 0ULL;
            w[i] = t;
        }
    }

    // Multiply in place by a small integer; returns overflow carry.
    std::uint64_t mul_small(std::uint64_t m) {
        std::uint64_t carry = 0;
        for (int i = 0; i < 4; ++i) {
            std::uint64_t lo, hi;
            mul64(w[i], m, lo, hi);
            lo += carry;
            if (lo < carry) hi += 1;
            w[i] = lo;
            carry = hi;
        }
        return carry;
    }

    // Divide by 10 in place, return remainder (hot path, optimized).
    std::uint64_t div10() {
        std::uint64_t rem = 0;
        for (int i = 3; i >= 0; --i) {
            std::uint64_t q = w[i]/10, r = w[i]%10;
            q += rem * 0x1999999999999999ULL;
            r += rem * 6ULL;
            q += r/10; r %= 10;
            w[i] = q; rem = r;
        }
        return rem;
    }

    // General divide by small integer d (d != 0), in place, return remainder.
    std::uint64_t div_small(std::uint64_t d) {
        std::uint64_t rem = 0;
        for (int i = 3; i >= 0; --i) {
            // (rem << 64) | w[i] divided by d; emulate via two 32-bit halves.
            std::uint64_t q = 0;
            for (int shift = 63; shift >= 0; --shift) {
                std::uint64_t bit = (w[i] >> shift) & 1ULL;
                // rem = rem*2 + bit, then if rem >= d: rem -= d, set q bit
                std::uint64_t new_rem = (rem << 1) | bit;
                q <<= 1;
                if (new_rem >= d) { new_rem -= d; q |= 1ULL; }
                rem = new_rem;
            }
            w[i] = q;
        }
        return rem;
    }

    // ---- comparison ----
    bool is_zero() const { return (w[0]|w[1]|w[2]|w[3]) == 0; }

    int cmp(const UInt256& o) const {
        for (int i = 3; i >= 0; --i)
            if (w[i] != o.w[i]) return w[i] < o.w[i] ? -1 : +1;
        return 0;
    }
    bool eq(const UInt256& o) const { return cmp(o) == 0; }
    bool lt(const UInt256& o) const { return cmp(o) < 0; }
    bool gt(const UInt256& o) const { return cmp(o) > 0; }
    bool le(const UInt256& o) const { return cmp(o) <= 0; }
    bool ge(const UInt256& o) const { return cmp(o) >= 0; }

    bool ge(std::uint64_t v) const {
        if (w[3]||w[2]||w[1]) return true;
        return w[0] >= v;
    }

    // ---- bit utilities ----
    int bit_length() const {
        for (int i = 3; i >= 0; --i)
            if (w[i]) return i*64 + (63 - __builtin_clzll(w[i])) + 1;
        return 0;
    }

    // Logical right shift by n bits (n < 256).
    UInt256 shr(int n) const {
        UInt256 r;
        if (n <= 0) return *this;
        if (n >= 256) return r;
        int limbs = n / 64, bits = n % 64;
        for (int i = 0; i < 4 - limbs; ++i) {
            r.w[i] = w[i+limbs] >> bits;
            if (bits && i+limbs+1 < 4) r.w[i] |= w[i+limbs+1] << (64 - bits);
        }
        return r;
    }

    // Logical left shift by n bits (n < 256).
    UInt256 shl(int n) const {
        UInt256 r;
        if (n <= 0) return *this;
        if (n >= 256) return r;
        int limbs = n / 64, bits = n % 64;
        for (int i = 3; i >= limbs; --i) {
            r.w[i] = w[i-limbs] << bits;
            if (bits && i-limbs-1 >= 0) r.w[i] |= w[i-limbs-1] >> (64 - bits);
        }
        return r;
    }

    // ---- string output ----
    std::string to_string() const {
        if (is_zero()) return "0";
        UInt256 t = *this;
        char buf[80]; // 256 bits ~ 78 decimal digits max
        int pos = sizeof(buf) - 1;
        buf[pos] = '\0';
        while (!t.is_zero()) {
            std::uint64_t r = t.div10();
            buf[--pos] = (char)('0' + r);
        }
        return std::string(&buf[pos]);
    }

    std::string to_hex_string() const {
        static const char* h = "0123456789abcdef";
        char buf[65];
        buf[64] = '\0';
        for (int i = 3; i >= 0; --i) {
            std::uint64_t v = w[i];
            int base = (3 - i) * 16;
            for (int k = 15; k >= 0; --k) {
                buf[base + k] = h[v & 0xF];
                v >>= 4;
            }
        }
        int s = 0;
        while (s < 63 && buf[s] == '0') ++s;
        return std::string(&buf[s]);
    }

    // ---- parse ----
    static UInt256 from_dec_string(const char* s) {
        UInt256 r;
        for (; *s; ++s) {
            if (*s < '0' || *s > '9') break;
            r.mul_small(10);
            r.add_small((std::uint64_t)(*s - '0'));
        }
        return r;
    }

    std::uint64_t to_lo() const { return w[0]; }
};

// ---- free-function operators for ergonomic use ----
inline bool operator==(const UInt256& a, const UInt256& b) { return a.eq(b); }
inline bool operator!=(const UInt256& a, const UInt256& b) { return !a.eq(b); }
inline bool operator< (const UInt256& a, const UInt256& b) { return a.lt(b); }
inline bool operator> (const UInt256& a, const UInt256& b) { return a.gt(b); }
inline bool operator<=(const UInt256& a, const UInt256& b) { return a.le(b); }
inline bool operator>=(const UInt256& a, const UInt256& b) { return a.ge(b); }

inline UInt256 operator+(const UInt256& a, const UInt256& b) {
    UInt256 r;
    std::uint64_t carry = 0;
    for (int i = 0; i < 4; ++i) {
        std::uint64_t s1 = a.w[i] + b.w[i];
        std::uint64_t c1 = (s1 < a.w[i]) ? 1ULL : 0ULL;
        std::uint64_t s = s1 + carry;
        std::uint64_t c2 = (s < s1) ? 1ULL : 0ULL;
        r.w[i] = s;
        carry = c1 + c2;
    }
    return r;
}
inline UInt256& operator+=(UInt256& a, const UInt256& b) {
    std::uint64_t carry = 0;
    for (int i = 0; i < 4; ++i) {
        std::uint64_t s = a.w[i] + b.w[i] + carry;
        carry = (s < a.w[i]) ? 1ULL : 0ULL;
        a.w[i] = s;
    }
    return a;
}
inline UInt256 operator*(const UInt256& a, const UInt256& b) { return UInt256::mul(a, b); }

} // namespace fw
