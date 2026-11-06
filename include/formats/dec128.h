// dec128.h — Decimal128 (IEEE 754 BID, 34 sig digits / Emax 6144), pure-software big-integer coefficient.
#pragma once
#if defined(_MSC_VER)
#error "ea-FRDT: MSVC is not supported. Use GCC 13+ or Clang 16+ (MSYS2 clang64 on Windows)."
#endif
#include "engine_decnum.h"   // brings engine_bid_codec.h + dec_fmt_text

namespace fw {

constexpr int DEC128_SIG = 34;
constexpr int DEC128_EMAX = 6144;

struct BigDec {
    bool neg = false;
    std::string coeff;
    int exp10 = 0;
    bool inf = false;
    friend bool operator==(const BigDec& a, const BigDec& b) {
        return a.inf == b.inf && a.neg == b.neg && a.exp10 == b.exp10 && a.coeff == b.coeff;
    }
};

inline std::string big_mul_str(const std::string& A, const std::string& B) {
    const int n = (int)A.size(), m = (int)B.size();
    std::vector<int> res(n + m, 0);
    for (int i = n - 1; i >= 0; --i) {
        int carry = 0;
        for (int j = m - 1; j >= 0; --j) {
            int s = res[i + j + 1] + (A[i] - '0') * (B[j] - '0') + carry;
            res[i + j + 1] = s % 10;
            carry = s / 10;
        }
        res[i] = carry;
    }
    size_t k = 0;
    while (k + 1 < res.size() && res[k] == 0) ++k;
    std::string out;
    for (; k < res.size(); ++k) out.push_back((char)('0' + res[k]));
    return out;
}

inline std::string big_round(const std::string& P, int sig, int& exp) {
    if ((int)P.size() <= sig) return P;
    std::string keep = P.substr(0, sig);
    int next = P[sig] - '0';
    bool sticky = false;
    for (size_t i = sig + 1; i < P.size(); ++i) if (P[i] != '0') { sticky = true; break; }
    bool up = (next > 5) || (next == 5 && (sticky || ((keep.back() - '0') & 1)));
    if (up) {
        int i = sig - 1;
        while (i >= 0) {
            if (keep[i] == '9') { keep[i] = '0'; --i; }
            else { ++keep[i]; break; }
        }
        if (i < 0) keep = "1" + keep;
    }
    exp += (int)P.size() - sig;
    if ((int)keep.size() > sig) { ++exp; keep.erase(0, 1); }
    return keep;
}

inline BigDec big_mul(const BigDec& a, const BigDec& b, int sig, int emax) {
    BigDec r;
    if (a.inf || b.inf) { r.inf = true; return r; }
    r.neg = a.neg ^ b.neg;
    std::string P = big_mul_str(a.coeff, b.coeff);
    int exp = a.exp10 + b.exp10;
    r.coeff = big_round(P, sig, exp);
    std::string cc = r.coeff; int ee = exp;
    while (cc.size() > 1 && cc.back() == '0') { cc.pop_back(); ++ee; }
    if (!cc.empty() && cc != "0" && ee + (int)cc.size() - 1 > emax) { r.inf = true; return r; }
    if (ee + (int)cc.size() <= -emax + 1) { r.coeff = "0"; r.exp10 = 0; return r; }
    r.exp10 = exp;
    return r;
}

inline BigDec big_addulp(const BigDec& v, int sig, int emax) {
    BigDec r = v;
    if (r.inf || r.coeff == "0") return r;
    std::string c = r.coeff;
    int carry = 1;
    for (int i = (int)c.size() - 1; i >= 0 && carry; --i) {
        int d = (c[i] - '0') + carry;
        c[i] = (char)('0' + d % 10);
        carry = d / 10;
    }
    if (carry) c = "1" + c;
    if ((int)c.size() > sig) { r.coeff = "1" + std::string(sig - 1, '0'); r.exp10 += 1; }
    else r.coeff = c;
    if (r.exp10 + sig - 1 > emax) r.inf = true;
    return r;
}

inline BigDec big_subulp(const BigDec& v, [[maybe_unused]] int sig, int emax) {
    BigDec r = v;
    if (r.inf || r.coeff == "0") return r;
    std::string c = r.coeff;
    int borrow = 1;
    for (int i = (int)c.size() - 1; i >= 0 && borrow; --i) {
        int d = (c[i] - '0') - borrow;
        if (d < 0) { d += 10; borrow = 1; } else borrow = 0;
        c[i] = (char)('0' + d);
    }
    // strip leading zeros
    size_t first = c.find_first_not_of('0');
    if (first == std::string::npos) { r.coeff = "0"; return r; }
    c = c.substr(first);
    // 33 nines * 10^-33 = 0.999...9 — correct, don't pad or shift exp
    r.coeff = c;
    if (r.exp10 + (int)c.size() - 1 < 1 - emax) r.coeff = "0";
    return r;
}

inline BigDec big_mululp_step(const BigDec& v, const BigDec& fac, int sig, int emax) {
    BigDec r = big_mul(v, fac, sig, emax);
    return big_addulp(r, sig, emax);
}

inline BigDec big_from_double(double v, int sig) {
    BigDec r;
    if (v < 0) r.neg = true;
    double av = std::fabs(v);
    if (av == 0.0) return r;
    int e = (int)std::floor(std::log10(av));
    double m = av / std::pow(10.0, e);
    long long hi = std::llround(m * 1.0e16);
    std::string coeff = std::to_string(hi);
    while ((int)coeff.size() < sig) coeff.push_back('0');
    if ((int)coeff.size() > sig) coeff = coeff.substr(0, sig);
    r.coeff = coeff;
    r.exp10 = e - (sig - 1);
    return r;
}

inline void run_dec128(double delay_ms, StartKind kind, StepMode sm, DispFmt df,
                double start_val, double factor, int digits, ShowBits showbits = ShowBits::OFF,
                RunResult* quiet = nullptr, std::int64_t refresh_ms = 0) {
    BigDec start;
    const char* tag;
    if (kind == StartKind::MinNormal) { start = BigDec{false, "1", -(DEC128_EMAX - 1), false}; tag = "min-normalized"; }
    else if (kind == StartKind::MaxFinite) {
        start = BigDec{false, std::string(DEC128_SIG, '9'), DEC128_EMAX - (DEC128_SIG - 1), false};
        tag = "max-finite";
    }
    else if (!std::isnan(start_val)) { start = big_from_double(start_val, DEC128_SIG); tag = "custom"; }
    else { start = big_from_double(1.0, DEC128_SIG); tag = "1.0"; }
    BigDec fac = big_from_double(factor, DEC128_SIG);
    if (!quiet) {
        std::cout << "Decimal128 walk: start=" << tag << ", " << step_factor_str(sm, factor)
                  << ", delay=" << delay_ms << "ms (0=unlimited)\n";
        BidFmt bf = bid_fmt128();
        std::string bits = bid_encode(BidEnc::BID, start.neg ? 1 : 0, start.exp10, start.coeff, bf);
        std::cout << "  [BID] coeff=" << start.coeff << " exp10=" << start.exp10
                  << " hex=" << bid_bits_to_hex(bits) << "\n";
        bid_selfcheck(start.neg ? 1 : 0, start.exp10, start.coeff, bf);
    }
    std::function<std::string(const BigDec&)> big_extra;
    if (showbits != ShowBits::OFF && !quiet) {
        big_extra = [showbits](const BigDec& v) -> std::string {
            BidFmt bf = bid_fmt128();
            std::string bits = bid_encode(BidEnc::BID, v.neg ? 1 : 0, v.exp10, v.coeff, bf);
            return showbits == ShowBits::HEX ? bid_bits_to_hex(bits)
                   : group_dec_bits(bits, bf.exp_cont, bf.coeff_bits);
        };
    }
    // dump callback: write packed BID128 bit-pattern (16 bytes)
    auto big_dump = [](const BigDec& v) {
        BidFmt bf = bid_fmt128();
        std::string bits = bid_encode(BidEnc::BID, v.neg ? 1 : 0, v.exp10, v.coeff, bf);
        std::string hex = bid_bits_to_hex(bits);
        int bw = (int)hex.size() / 2;
        char buf[16] = {0};
        auto hexval = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return 0;
        };
        for (int i = 0; i < bw; ++i)
            buf[i] = (char)((hexval(hex[2*i]) << 4) | hexval(hex[2*i+1]));
        if (g_dump_endian == 0) {
            for (int i = 0; i < bw/2; ++i) std::swap(buf[i], buf[bw-1-i]);
        }
        std::fwrite(buf, 1, bw, g_dump_file);
        g_dump_bytes_written += bw;
    };
    drive_core(start,
          [&](BigDec v) -> BigDec {
              switch (sm) {
                  case StepMode::ADD_ULP: return big_addulp(v, DEC128_SIG, DEC128_EMAX);
                  case StepMode::SUB_ULP: return big_subulp(v, DEC128_SIG, DEC128_EMAX);
                  case StepMode::MUL_ULP: return big_mululp_step(v, fac, DEC128_SIG, DEC128_EMAX);
                  default:                return big_mul(v, fac, DEC128_SIG, DEC128_EMAX);
              }
          },
          [](BigDec v) { return v.inf; },
          [](BigDec v) { return v.coeff.empty() || v.coeff == "0"; },
          [](BigDec) { return false; },  // BigDec never produces NaN
          [&](const BigDec& v) { return dec_fmt_text(v.coeff, v.exp10, v.neg, df, digits); },
          delay_ms, big_extra, quiet, refresh_ms, std::function<void(const BigDec&)>(big_dump));
}

} // namespace fw
