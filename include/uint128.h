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

    bool ge(std::uint64_t v) const {
        if (w[1]) return true;
        return w[0] >= v;
    }
    std::uint64_t to_lo() const { return w[0]; }
};

} // namespace fw
