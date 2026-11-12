// uint128.h — Single-header hand-rolled 128-bit unsigned big integer
//
// Drop-in:  #include "uint128.h"   (only needs <cstdint>, <string>)
// Pure uint64_t arithmetic, no __int128.
// GCC/Clang only (uses __builtin_clzll); MSVC not supported by design.
#pragma once
#if defined(_WIN32) && !defined(__MINGW32__)
#error "ea-FRDT requires MinGW target (x86_64-w64-mingw32 / x86_64-w64-windows-gnu). Do not use MSVC or LLVM's default windows-msvc clang; use MSYS2 clang64."
#endif
#include <compare>
#include <cstdint>
#include <string>

namespace fw {

struct UInt128 {
    std::uint64_t w[2] = {0,0};

    UInt128() = default;
    UInt128(std::uint64_t v) { w[0]=v; w[1]=0; }
    UInt128(std::uint64_t lo, std::uint64_t hi) : w{lo,hi} {}

    static void mul64(std::uint64_t x, std::uint64_t y,
                      std::uint64_t& lo, std::uint64_t& hi) {
        std::uint64_t x0 = x & 0xFFFFFFFFULL, x1 = x >> 32;
        std::uint64_t y0 = y & 0xFFFFFFFFULL, y1 = y >> 32;
        std::uint64_t p0 = x0*y0, p1 = x0*y1, p2 = x1*y0, p3 = x1*y1;
        std::uint64_t mid = (p0>>32) + (p1&0xFFFFFFFFULL) + (p2&0xFFFFFFFFULL);
        lo = (p0 & 0xFFFFFFFFULL) | (mid << 32);
        hi = p3 + (p1>>32) + (p2>>32) + (mid>>32);
    }

    static UInt128 mul(std::uint64_t a, std::uint64_t b) {
        UInt128 r; mul64(a, b, r.w[0], r.w[1]); return r;
    }

    // 128x128 -> 128 multiply (truncates to low 128 bits).
    static UInt128 mul(const UInt128& a, const UInt128& b) {
        UInt128 r;
        std::uint64_t lo, hi;
        // a.w[0]*b.w[0] -> r.w[0],r.w[1]
        mul64(a.w[0], b.w[0], r.w[0], r.w[1]);
        // a.w[0]*b.w[1] -> adds to r.w[1]
        mul64(a.w[0], b.w[1], lo, hi);
        r.w[1] += lo; // hi discarded (overflow above 128 bits)
        // a.w[1]*b.w[0] -> adds to r.w[1]
        mul64(a.w[1], b.w[0], lo, hi);
        r.w[1] += lo;
        return r;
    }

    // ---- small-integer arithmetic ----
    void inc() { w[0] += 1; if (w[0] == 0) w[1] += 1; }

    void add_small(std::uint64_t v) {
        std::uint64_t s = w[0] + v;
        if (s < w[0]) w[1] += 1;
        w[0] = s;
    }

    std::uint64_t mul_small(std::uint64_t m) {
        std::uint64_t carry = 0;
        for (int i = 0; i < 2; ++i) {
            std::uint64_t lo, hi;
            mul64(w[i], m, lo, hi);
            lo += carry;
            if (lo < carry) hi += 1;
            w[i] = lo;
            carry = hi;
        }
        return carry;
    }

    std::uint64_t div10() {
        std::uint64_t rem = 0;
        for (int i = 1; i >= 0; --i) {
            std::uint64_t q = w[i]/10, r = w[i]%10;
            q += rem * 0x1999999999999999ULL;
            r += rem * 6ULL;
            q += r/10; r %= 10;
            w[i] = q; rem = r;
        }
        return rem;
    }

    std::uint64_t div_small(std::uint64_t d) {
        std::uint64_t rem = 0;
        for (int i = 1; i >= 0; --i) {
            std::uint64_t q = 0;
            for (int shift = 63; shift >= 0; --shift) {
                std::uint64_t bit = (w[i] >> shift) & 1ULL;
                std::uint64_t nr = (rem << 1) | bit;
                q <<= 1;
                if (nr >= d) { nr -= d; q |= 1ULL; }
                rem = nr;
            }
            w[i] = q;
        }
        return rem;
    }

    // ---- comparison ----
    [[nodiscard]] constexpr bool is_zero() const { return (w[0]|w[1]) == 0; }

    [[nodiscard]] constexpr int cmp(const UInt128& o) const {
        if (w[1] != o.w[1]) return w[1] < o.w[1] ? -1 : +1;
        if (w[0] != o.w[0]) return w[0] < o.w[0] ? -1 : +1;
        return 0;
    }
    [[nodiscard]] constexpr bool eq(const UInt128& o) const { return cmp(o) == 0; }
    [[nodiscard]] constexpr bool lt(const UInt128& o) const { return cmp(o) < 0; }
    [[nodiscard]] constexpr bool gt(const UInt128& o) const { return cmp(o) > 0; }
    [[nodiscard]] constexpr bool le(const UInt128& o) const { return cmp(o) <= 0; }
    [[nodiscard]] constexpr bool ge(const UInt128& o) const { return cmp(o) >= 0; }

    [[nodiscard]] constexpr bool ge(std::uint64_t v) const {
        if (w[1]) return true;
        return w[0] >= v;
    }

    // ---- bit utilities ----
    [[nodiscard]] int bit_length() const {
        if (w[1]) return 64 + (64 - __builtin_clzll(w[1]));
        if (w[0]) return 64 - __builtin_clzll(w[0]);
        return 0;
    }

    [[nodiscard]] UInt128 shr(int n) const {
        UInt128 r;
        if (n <= 0) return *this;
        if (n >= 128) return r;
        int limbs = n / 64, bits = n % 64;
        for (int i = 0; i < 2 - limbs; ++i) {
            r.w[i] = w[i+limbs] >> bits;
            if (bits && i+limbs+1 < 2) r.w[i] |= w[i+limbs+1] << (64 - bits);
        }
        return r;
    }

    [[nodiscard]] UInt128 shl(int n) const {
        UInt128 r;
        if (n <= 0) return *this;
        if (n >= 128) return r;
        int limbs = n / 64, bits = n % 64;
        for (int i = 1; i >= limbs; --i) {
            r.w[i] = w[i-limbs] << bits;
            if (bits && i-limbs-1 >= 0) r.w[i] |= w[i-limbs-1] >> (64 - bits);
        }
        return r;
    }

    // ---- string ----
    [[nodiscard]] std::string to_string() const {
        if (is_zero()) return "0";
        UInt128 t = *this;
        char buf[45];
        int pos = sizeof(buf) - 1;
        buf[pos] = '\0';
        while (!t.is_zero()) {
            std::uint64_t r = t.div10();
            buf[--pos] = (char)('0' + r);
        }
        return std::string(&buf[pos]);
    }

    [[nodiscard]] std::string to_hex_string() const {
        static const char* h = "0123456789abcdef";
        char buf[33];
        buf[32] = '\0';
        for (int i = 1; i >= 0; --i) {
            std::uint64_t v = w[i];
            int base = (1 - i) * 16;
            for (int k = 15; k >= 0; --k) {
                buf[base + k] = h[v & 0xF];
                v >>= 4;
            }
        }
        int s = 0;
        while (s < 31 && buf[s] == '0') ++s;
        return std::string(&buf[s]);
    }

    [[nodiscard]] static UInt128 from_dec_string(const char* s) {
        UInt128 r;
        for (; *s; ++s) {
            if (*s < '0' || *s > '9') break;
            r.mul_small(10);
            r.add_small((std::uint64_t)(*s - '0'));
        }
        return r;
    }

    [[nodiscard]] constexpr std::uint64_t to_lo() const { return w[0]; }
};

// C++20 three-way compare: <, >, <=, >= and != are rewritten from this; == stays
// explicit because <=> alone does not synthesise it for a non-defaulted class.
inline std::strong_ordering operator<=>(const UInt128& a, const UInt128& b) noexcept {
    return a.cmp(b) <=> 0;
}
inline bool operator==(const UInt128& a, const UInt128& b) noexcept { return a.eq(b); }

inline UInt128 operator+(const UInt128& a, const UInt128& b) {
    UInt128 r;
    std::uint64_t carry = 0;
    for (int i = 0; i < 2; ++i) {
        std::uint64_t s1 = a.w[i] + b.w[i];
        std::uint64_t c1 = (s1 < a.w[i]) ? 1ULL : 0ULL;
        std::uint64_t s = s1 + carry;
        std::uint64_t c2 = (s < s1) ? 1ULL : 0ULL;
        r.w[i] = s;
        carry = c1 + c2;
    }
    return r;
}
inline UInt128& operator+=(UInt128& a, const UInt128& b) {
    std::uint64_t carry = 0;
    for (int i = 0; i < 2; ++i) {
        std::uint64_t s = a.w[i] + b.w[i] + carry;
        carry = (s < a.w[i]) ? 1ULL : 0ULL;
        a.w[i] = s;
    }
    return a;
}
inline UInt128 operator*(const UInt128& a, const UInt128& b) { return UInt128::mul(a, b); }

} // namespace fw
