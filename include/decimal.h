// decimal.h — Decimal float: BID codec + DecNum (dec32/64) + BigDec (dec128)
#pragma once
#include "common.h"
#include "uint256.h"

namespace fw {

// ---------------------------------------------------------------------------
// BID encoding layer (DPD disabled, BID only)
// ---------------------------------------------------------------------------
enum class BidEnc { BID, DPD };

inline std::string bid_ibits(int v, int n) {
    std::string s(n, '0');
    for (int i = n - 1; i >= 0; --i) { s[i] = (v & 1) ? '1' : '0'; v >>= 1; }
    return s;
}

inline std::string bid_dec_to_bin(const std::string& dec) {
    std::string num = dec;
    size_t p = num.find_first_not_of('0');
    num = (p == std::string::npos) ? "0" : num.substr(p);
    std::string bits;
    while (num != "0") {
        std::string q; int rem = 0;
        for (char ch : num) { int cur = rem * 10 + (ch - '0'); q.push_back(char('0' + cur / 2)); rem = cur % 2; }
        size_t qp = q.find_first_not_of('0');
        num = (qp == std::string::npos) ? "0" : q.substr(qp);
        bits.push_back(rem ? '1' : '0');
    }
    if (bits.empty()) return "0";
    std::reverse(bits.begin(), bits.end());
    return bits;
}

inline std::string bid_strip0(std::string s) {
    size_t p = s.find_first_not_of('0');
    return p == std::string::npos ? "0" : s.substr(p);
}

inline std::string bid_bin_to_dec(const std::string& bin) {
    std::string dec = "0";
    for (char b : bin) {
        int carry = (b == '1') ? 1 : 0;
        for (int i = (int)dec.size() - 1; i >= 0; --i) {
            int v = (dec[i] - '0') * 2 + carry;
            dec[i] = char('0' + v % 10);
            carry = v / 10;
        }
        if (carry) dec.insert(dec.begin(), char('0' + carry));
    }
    return bid_strip0(dec);
}

struct BidFmt {
    int exp_cont, coeff_bits, digits;
    long long bias;
    const char* name;
};

inline BidFmt bid_fmt32()  { return {6,  20,  7,  101,  "dec32"}; }
inline BidFmt bid_fmt64()  { return {8,  50,  16, 398,  "dec64"}; }
inline BidFmt bid_fmt128() { return {12, 110, 34, 6176, "dec128"}; }

inline std::string bid_pack(int sign, int expEnc, const std::string& coeffDec, const BidFmt& f) {
    std::string out = sign ? "1" : "0";
    std::string num = bid_strip0(coeffDec.empty() ? "0" : coeffDec);
    while ((int)num.size() < f.digits) num += '0';
    int msd = num[0] - '0';
    int expMSB = (expEnc >> f.exp_cont) & 3;
    std::string combo;
    if (msd <= 7) combo = bid_ibits(expMSB, 2) + bid_ibits(msd, 3);
    else combo = "11" + bid_ibits(expMSB, 2) + (msd == 9 ? "1" : "0");
    out += combo;
    out += bid_ibits(expEnc, f.exp_cont);
    std::string rest = num.substr(1);
    std::string restBin = bid_dec_to_bin(rest);
    while ((int)restBin.size() < f.coeff_bits) restBin = "0" + restBin;
    out += restBin;
    return out;
}

struct BidUnpack {
    int sign = 0, expEnc = 0;
    std::string coeffBin, coeffDec;
    bool inf = false, nan = false;
};

inline BidUnpack bid_unpack(const BidFmt& f, const std::string& bits) {
    BidUnpack r;
    r.sign = bits[0] - '0';
    std::string combo = bits.substr(1, 5);
    if (combo == "11110") { r.inf = true; return r; }
    if (combo == "11111") { r.nan = true; return r; }
    int expMSB, msd;
    if (combo[0] == '0' || combo[1] == '0') {
        expMSB = (combo[0] - '0') * 2 + (combo[1] - '0');
        msd = (combo[2] - '0') * 4 + (combo[3] - '0') * 2 + (combo[4] - '0');
    } else {
        expMSB = (combo[2] - '0') * 2 + (combo[3] - '0');
        msd = (combo[4] == '1') ? 9 : 8;
    }
    int off = 6;
    int cont = 0;
    for (int i = 0; i < f.exp_cont; ++i) cont = cont * 2 + (bits[off + i] - '0');
    off += f.exp_cont;
    r.expEnc = (expMSB << f.exp_cont) | cont;
    std::string restBin;
    for (int g = 0; g < f.coeff_bits; ++g) restBin += bits[off + g];
    r.coeffBin = restBin;
    std::string restDec = (restBin == std::string(f.coeff_bits, '0')) ? "0" : bid_bin_to_dec(restBin);
    int restW = f.digits - 1;
    while ((int)restDec.size() < restW) restDec = "0" + restDec;
    r.coeffDec = bid_strip0(std::to_string(msd) + restDec);
    return r;
}

inline std::string bid_encode(BidEnc enc, int sign, int exp10, const std::string& coeffDec, const BidFmt& f) {
    if (enc == BidEnc::DPD) throw std::runtime_error("DPD encoding disabled (BID only)");
    std::string c = coeffDec.empty() ? "0" : coeffDec;
    return bid_pack(sign, exp10 + (int)f.bias, c, f);
}

inline std::string bid_bits_to_hex(const std::string& bits) {
    std::string out;
    static const char* h = "0123456789ABCDEF";
    for (size_t i = 0; i < bits.size(); i += 4) {
        int v = 0;
        for (size_t k = 0; k < 4 && i + k < bits.size(); ++k) v = v * 2 + (bits[i + k] == '1');
        out.push_back(h[v]);
    }
    return out;
}

inline bool bid_selfcheck(int sign, int exp10, const std::string& coeffDec, const BidFmt& f) {
    std::string bits = bid_encode(BidEnc::BID, sign, exp10, coeffDec, f);
    BidUnpack u = bid_unpack(f, bits);
    bool ok = true;
    std::string errs;
    if (u.sign != sign) { ok = false; errs += " sign(" + std::to_string(u.sign) + "!=" + std::to_string(sign) + ")"; }
    int expBack = u.expEnc - (int)f.bias;
    if (expBack != exp10) { ok = false; errs += " exp10(" + std::to_string(expBack) + "!=" + std::to_string(exp10) + ")"; }
    std::string expect = bid_strip0(coeffDec.empty() ? "0" : coeffDec);
    while ((int)expect.size() < f.digits) expect += '0';
    if (u.coeffDec != expect) { ok = false; errs += " coeff(" + u.coeffDec + "!=" + expect + ")"; }
    std::cout << "  [BID-SELFCHECK] " << (ok ? "OK" : "MISMATCH" + errs) << "\n";
    return ok;
}

// ---------------------------------------------------------------------------
// DecNum dec32/dec64 internal decimal arithmetic
// ---------------------------------------------------------------------------
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
            // P += 1
            P.w[0] += 1;
            for (int i=0;i<3;i++) if (P.w[i]==0) P.w[i+1]+=1; else break;
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

inline void run_dec(const DecFmt& fmt, std::int64_t delay_ms, StartKind kind, StepMode sm, DispFmt df,
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
        std::cout << fmt.name << " walk: start=" << tag << ", " << step_factor_str(sm, factor)
                  << ", delay=" << delay_ms << "ms (0=unlimited)\n";
        BidFmt bf = (fmt.sig <= 7) ? bid_fmt32() : bid_fmt64();
        std::string cb = std::to_string(start.coeff);
        std::string bits = bid_encode(BidEnc::BID, start.neg ? 1 : 0, start.exp10, cb, bf);
        std::cout << "  [BID] coeff=" << cb << " exp10=" << start.exp10
                  << " hex=" << bid_bits_to_hex(bits) << "\n";
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
    drive_core(start,
          [&](DecNum v) -> DecNum {
              switch (sm) {
                  case StepMode::ADD_ULP: return dec_addulp(v, fmt);
                  case StepMode::MUL_ULP: return dec_mululp_step(v, fac, fmt);
                  default:                return dec_mul(v, fac, fmt);
              }
          },
          [](DecNum v) { return v.inf; },
          [](DecNum v) { return v.coeff == 0; },
          [&](const DecNum& v) { return dec_fmt_text(std::to_string(v.coeff), v.exp10, v.neg, df, digits); },
          delay_ms, dec_extra, quiet, refresh_ms);
}

// ---------------------------------------------------------------------------
// BigDec dec128 big integer
// ---------------------------------------------------------------------------
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

inline void run_dec128(std::int64_t delay_ms, StartKind kind, StepMode sm, DispFmt df,
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
    drive_core(start,
          [&](BigDec v) -> BigDec {
              switch (sm) {
                  case StepMode::ADD_ULP: return big_addulp(v, DEC128_SIG, DEC128_EMAX);
                  case StepMode::MUL_ULP: return big_mululp_step(v, fac, DEC128_SIG, DEC128_EMAX);
                  default:                return big_mul(v, fac, DEC128_SIG, DEC128_EMAX);
              }
          },
          [](BigDec v) { return v.inf; },
          [](BigDec v) { return v.coeff.empty() || v.coeff == "0"; },
          [&](const BigDec& v) { return dec_fmt_text(v.coeff, v.exp10, v.neg, df, digits); },
          delay_ms, big_extra, quiet, refresh_ms);
}

} // namespace fw
