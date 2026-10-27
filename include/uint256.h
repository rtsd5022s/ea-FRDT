// uint256.h — 纯自研 UInt256 大整数，Schoolbook 乘法
// 纯 uint64_t 无符号运算，无 UB，O2/O3 下编译器自动生成 MUL/ADX 指令
#pragma once
#include <cstdint>

namespace fw {

struct UInt256 {
    std::uint64_t w[4] = {0,0,0,0};
    UInt256() = default;
    UInt256(std::uint64_t v) { w[0]=v; w[1]=w[2]=w[3]=0; }

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

    static UInt256 mul128(std::uint64_t a_lo, std::uint64_t a_hi,
                          std::uint64_t b_lo, std::uint64_t b_hi) {
        UInt256 r;
        std::uint64_t lo, hi;
        mul64(a_lo, b_lo, r.w[0], r.w[1]);
        mul64(a_hi, b_lo, lo, hi);
        std::uint64_t c = r.w[1] + lo;
        r.w[1] = c;
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

    // 除以 10，返回余数（原地修改 this）
    // 2^64 / 10 = 0x1999999999999999, 2^64 % 10 = 6
    std::uint64_t div10() {
        std::uint64_t rem = 0;
        for (int i = 3; i >= 0; --i) {
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
        if (w[3]||w[2]||w[1]) return true;
        return w[0] >= v;
    }
    std::uint64_t to_lo() const { return w[0]; }
};

} // namespace fw
