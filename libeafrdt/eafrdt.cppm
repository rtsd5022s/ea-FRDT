// eafrdt.cppm — C++23 module interface for libeafrdt
// Usage: import eafrdt;
module;
#include <cstdint>
export module eafrdt;

namespace fw::core {

// Re-export the core types. Implementation is in libeafrdt.a.
struct UInt256 {
    std::uint64_t w[4] = {0,0,0,0};
    UInt256() = default;
    UInt256(std::uint64_t v);
    static void mul64(std::uint64_t x, std::uint64_t y,
                      std::uint64_t& lo, std::uint64_t& hi);
    static UInt256 mul128(std::uint64_t a_lo, std::uint64_t a_hi,
                          std::uint64_t b_lo, std::uint64_t b_hi);
    std::uint64_t div10();
    bool ge(std::uint64_t v) const;
    std::uint64_t to_lo() const;
};

struct Fmt { int E, M, bias; };
struct SVal { bool sign=false; int exp=0; std::uint64_t man=0; bool inf=false; };
struct X87 { bool sign=false; int exp=0; std::uint64_t man=0; bool inf=false; };
struct Q128 { bool sign=false; int exp=0; std::uint64_t hi=0, lo=0; bool inf=false; };
struct DecFmt { int sig, emax; };
struct DecNum { bool neg=false; std::uint64_t coeff=0; int exp10=0; bool inf=false; };
struct DpdFmt { int exp_bits, coeff_bits, digits; long long bias; };
struct DpdNum { bool neg=false, inf=false; int exp10=0; std::uint8_t digits[34]; int ndigits=0; };

export long long walk_binary(const Fmt& f, double start, double mult,
                             double timeout_sec, int* reason);
export long long walk_x87(double start, double mult, double timeout_sec, int* reason);
export long long walk_q128(double start, double mult, double timeout_sec, int* reason);
export long long walk_bid(const DecFmt& f, double start, double mult,
                          double timeout_sec, int* reason);
export long long walk_bid128(double start, double mult, double timeout_sec, int* reason);
export long long walk_dpd(const DpdFmt& f, double start, double mult,
                          double timeout_sec, int* reason);

} // namespace fw::core
