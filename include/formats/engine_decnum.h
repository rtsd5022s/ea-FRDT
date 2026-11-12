// engine_decnum.h — Shared 64-bit coefficient decimal (BID) engine: DecNum + run_dec.
// Reused by dec32, dec64 (see formats/dec32.h, dec64.h).
#pragma once
#if defined(_WIN32) && !defined(__MINGW32__)
#error "ea-FRDT requires MinGW target (x86_64-w64-mingw32 / x86_64-w64-windows-gnu). Do not use MSVC or LLVM's default windows-msvc clang; use MSYS2 clang64."
#endif
#include "engine_bid_codec.h"
#include "../uint256.h"

namespace fw {

struct DecNum {
    bool neg = false;
    unsigned long long coeff = 0;
    int exp10 = 0;
    bool inf = false;
    friend bool operator==(const DecNum&, const DecNum&) = default;
};

struct DecFmt {
    int sig, emax;
    const char* name;
};

inline unsigned long long dec_pow10(int n) {
    unsigned long long v = 1;
    while (n-- > 0) v *= 10;
    return v;
}

inline unsigned long long dec_round(UInt256 P, int& exp, unsigned long long maxc) {
    while (P.ge(maxc)) {
        unsigned long long r = P.div10();
        if (r > 5 || (r == 5 && (P.to_lo() & 1ULL))) {
            P.inc();
        }
        ++exp;
        if (P.ge(maxc) && P.to_lo()==maxc && !P.w[1] && !P.w[2] && !P.w[3]) {
            P = UInt256(maxc / 10); ++exp;
        }
    }
    return P.to_lo();
}

inline DecNum dec_mul(const DecNum& a, const DecNum& b, const DecFmt& f) {
    DecNum r;
    if (a.inf || b.inf) { r.inf = true; return r; }
    r.neg = a.neg ^ b.neg;
    UInt256 P = UInt256::mul128(a.coeff, 0, b.coeff, 0);
    int exp = a.exp10 + b.exp10;
    r.coeff = dec_round(P, exp, dec_pow10(f.sig));
    int cdigits = 0;
    for (unsigned long long t = r.coeff; t > 0; t /= 10) ++cdigits;
    if (cdigits > 0 && exp + cdigits - 1 > f.emax) { r.inf = true; return r; }
    if (exp + cdigits <= -f.emax + 1) { r.coeff = 0; r.exp10 = 0; return r; }
    r.exp10 = exp;
    return r;
}

inline DecNum dec_addulp(const DecNum& v, const DecFmt& f) {
    DecNum r = v;
    if (r.inf || r.coeff == 0) return r;
    r.coeff += 1;
    unsigned long long maxc = dec_pow10(f.sig);
    if (r.coeff >= maxc) { r.coeff = maxc / 10; r.exp10 += 1; }
    if (r.exp10 + (f.sig - 1) > f.emax) r.inf = true;
    return r;
}

inline DecNum dec_subulp(const DecNum& v, const DecFmt& f) {
    DecNum r = v;
    if (r.inf || r.coeff == 0) return r;
    r.coeff -= 1;
    [[maybe_unused]] unsigned long long maxc = dec_pow10(f.sig);
    if (r.coeff == 0) { r.exp10 -= 1; }  // underflow
    if (r.exp10 + (int)std::to_string(r.coeff).size() - 1 < 1 - f.emax) r.coeff = 0;
    return r;
}

inline DecNum dec_mululp_step(const DecNum& v, const DecNum& fac, const DecFmt& f) {
    DecNum r = dec_mul(v, fac, f);
    return dec_addulp(r, f);
}

inline DecNum dec_from_double(double v, int sig) {
    DecNum r;
    if (v < 0) r.neg = true;
    double av = std::fabs(v);
    if (av == 0.0) return r;
    int e = (int)std::floor(std::log10(av));
    double m = av / std::pow(10.0, e);
    unsigned long long c = std::llround(m * (double)dec_pow10(sig - 1));
    int exp = e - (sig - 1);
    unsigned long long maxc = dec_pow10(sig);
    if (c == maxc) { c = maxc / 10; ++exp; }
    r.coeff = c; r.exp10 = exp;
    return r;
}

inline std::string dec_fmt_text(const std::string& coeffStr, int exp10,
                         bool neg, DispFmt df, int digits) {
    std::string body;
    if (coeffStr.empty() || coeffStr == "0") {
        body = "0";
    } else if (df == DispFmt::FULL) {
        if (exp10 >= 0) body = coeffStr + std::string(exp10, '0') + ".0";
        else {
            int d = -exp10;
            if (d < (int)coeffStr.size()) body = coeffStr.substr(0, coeffStr.size() - d) + "." + coeffStr.substr(coeffStr.size() - d);
            else body = "0." + std::string(d - coeffStr.size(), '0') + coeffStr;
        }
    } else {
        const int E = exp10 + ((int)coeffStr.size() - 1);
        const std::string& num = coeffStr;
        if (df == DispFmt::SCI) {
            std::string tail;
            int n = (digits == -1) ? (int)num.size() - 1 : digits;
            for (int i = 1; i <= n; ++i) tail += (i < (int)num.size()) ? num[i] : '0';
            body = num.substr(0, 1) + (tail.empty() ? "" : "." + tail) + "e" + (E >= 0 ? "+" : "") + std::to_string(E);
        } else {
            const int E3 = (E >= 0) ? (E / 3) * 3 : -(((-E + 2) / 3) * 3);
            const int shift = E - E3;
            std::string intPart = num.substr(0, shift + 1);
            std::string fracSrc = num.substr(shift + 1);
            std::string tail;
            int n = (digits == -1) ? (int)fracSrc.size() : digits;
            for (int i = 0; i < n; ++i) tail += (i < (int)fracSrc.size()) ? fracSrc[i] : '0';
            body = intPart + (tail.empty() ? "" : "." + tail) + "e" + (E3 >= 0 ? "+" : "") + std::to_string(E3);
        }
    }
    return (neg && body != "0") ? "-" + body : body;
}

inline void run_dec(const DecFmt& fmt, double delay_ms, StartKind kind, StepMode sm, DispFmt df,
             double start_val, double factor, int digits, ShowBits showbits = ShowBits::OFF,
             RunResult* quiet = nullptr, std::int64_t refresh_ms = 0) {
    DecNum start;
    const char* tag;
    if (kind == StartKind::MinNormal) { start = DecNum{false, 1, -(fmt.emax - 1), false}; tag = "min-normalized"; }
    else if (kind == StartKind::MaxFinite) {
        start = DecNum{false, dec_pow10(fmt.sig) - 1, fmt.emax - (fmt.sig - 1), false};
        tag = "max-finite";
    }
    else if (!std::isnan(start_val)) { start = dec_from_double(start_val, fmt.sig); tag = "custom"; }
    else { start = dec_from_double(1.0, fmt.sig); tag = "1.0"; }
    DecNum fac = dec_from_double(factor, fmt.sig);
    if (!quiet) {
        std::cout << cui::walk_header(fmt.name, tag, step_factor_str(sm, factor), delay_ms);
        BidFmt bf = (fmt.sig <= 7) ? bid_fmt32() : bid_fmt64();
        std::string cb = std::to_string(start.coeff);
        std::string bits = bid_encode(BidEnc::BID, start.neg ? 1 : 0, start.exp10, cb, bf);
        std::cout << cui::bid_header(cb, start.exp10, bid_bits_to_hex(bits));
        bid_selfcheck(start.neg ? 1 : 0, start.exp10, cb, bf);
    }
    BidFmt dbf = (fmt.sig <= 7) ? bid_fmt32() : bid_fmt64();
    std::function<std::string(const DecNum&)> dec_extra;
    if (showbits != ShowBits::OFF && !quiet) {
        dec_extra = [showbits, dbf](const DecNum& v) -> std::string {
            std::string bits = bid_encode(BidEnc::BID, v.neg ? 1 : 0, v.exp10,
                                          std::to_string(v.coeff), dbf);
            return showbits == ShowBits::HEX ? bid_bits_to_hex(bits)
                   : group_dec_bits(bits, dbf.exp_cont, dbf.coeff_bits);
        };
    }
    // dump callback: write packed BID bit-pattern bytes (4 for dec32, 8 for dec64)
    auto dec_dump = [dbf](const DecNum& v) {
        std::string bits = bid_encode(BidEnc::BID, v.neg ? 1 : 0, v.exp10,
                                      std::to_string(v.coeff), dbf);
        std::string hex = bid_bits_to_hex(bits);
        int bw = (int)hex.size() / 2;
        char buf[8] = {0};
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
          [&](DecNum v) -> DecNum {
              switch (sm) {
                  case StepMode::ADD_ULP: return dec_addulp(v, fmt);
                  case StepMode::SUB_ULP: return dec_subulp(v, fmt);
                  case StepMode::MUL_ULP: return dec_mululp_step(v, fac, fmt);
                  default:                return dec_mul(v, fac, fmt);
              }
          },
          [](DecNum v) { return v.inf; },
          [](DecNum v) { return v.coeff == 0; },
          [](DecNum) { return false; },  // DecNum never produces NaN
          [&](const DecNum& v) { return dec_fmt_text(std::to_string(v.coeff), v.exp10, v.neg, df, digits); },
          delay_ms, dec_extra, quiet, refresh_ms, std::function<void(const DecNum&)>(dec_dump));
}

} // namespace fw
