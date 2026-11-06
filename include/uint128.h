// uint128.h — Hand-rolled UInt128 big integer, derived from UInt256
// Pure uint64_t unsigned arithmetic, no UB
#pragma once
#include <cstdint>

namespace fw {

struct UInt128 {
    std::uint64_t w[2] = {0,0};
    UInt128() = default;
    UInt128(std::uint64_t v) { w[0]=v; w[1]=0; }

    static void mul64(std::uint64_t x, std::uint64_t y,
                      std::uint64_t& lo, std::uint64_t& hi) {
        std::uint64_t x0 = x & 0xFFFFFFFFULL;
        std::uint64_t x1 = x >> 32;
        std::uint64_t y0 = y & 0xFFFFFFFFULL;
        std::uint64_t y1 = y >> 32;
        std::uint64_t p0 = x0 * y0;
        std::uint64_t p1 = x0 * y1;
        std::uint64_t p2 = x1 * y0;
        std::uint64_t p3 = x1 * y1;
        std::uint64_t mid = (p0 >> 32) + (p1 & 0xFFFFFFFFULL) + (p2 & 0xFFFFFFFFULL);
        lo = (p0 & 0xFFFFFFFFULL) | (mid << 32);
        hi = p3 + (p1 >> 32) + (p2 >> 32) + (mid >> 32);
    }

    // Multiply two 64-bit values, producing a 128-bit result
    static UInt128 mul(std::uint64_t a, std::uint64_t b) {
        UInt128 r;
        mul64(a, b, r.w[0], r.w[1]);
        return r;
    }

    // Divide by 10, return remainder (modifies this in place)
    std::uint64_t div10() {
        std::uint64_t rem = 0;
        for (int i = 1; i >= 0; --i) {
            std::uint64_t q = w[i] / 10;
            std::uint64_t r = w[i] % 10;
            q += rem * 0x1999999999999999ULL;
            r += rem * 6ULL;
            q += r / 10;
            r = r % 10;
            w[i] = q;
            rem = r;
        }
        return rem;
    }

    // ---- Small-integer arithmetic ----

    void inc() {
        w[0] += 1;
        if (w[0] == 0) w[1] += 1;
    }

    void add_small(std::uint64_t v) {
        std::uint64_t s = w[0] + v;
        if (s < w[0]) w[1] += 1;
        w[0] = s;
    }

    // Multiply in place by a small unsigned integer m.
    // Returns the final carry (0 iff result fits in 128 bits).
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

    // ---- Comparison ----

    bool is_zero() const { return (w[0]|w[1]) == 0; }

    int cmp(const UInt128& o) const {
        if (w[1] != o.w[1]) return w[1] < o.w[1] ? -1 : +1;
        if (w[0] != o.w[0]) return w[0] < o.w[0] ? -1 : +1;
        return 0;
    }

    bool eq(const UInt128& o) const { return cmp(o) == 0; }
    bool lt(const UInt128& o) const { return cmp(o) < 0; }
    bool gt(const UInt128& o) const { return cmp(o) > 0; }
    bool le(const UInt128& o) const { return cmp(o) <= 0; }
    bool ge(const UInt128& o) const { return cmp(o) >= 0; }

    // ---- Bit utilities ----
    int bit_length() const {
        if (w[1]) return 64 + (64 - __builtin_clzll(w[1]));
        if (w[0]) return 64 - __builtin_clzll(w[0]);
        return 0;
    }

    // ---- Legacy: compare against a small unsigned integer ----
    bool ge(std::uint64_t v) const {
        if (w[1]) return true;
        return w[0] >= v;
    }
    std::uint64_t to_lo() const { return w[0]; }
};

} // namespace fw
