// engine_bid_codec.h — Shared BID (binary integer decimal) encoding layer.
// Reused by dec32, dec64, dec128 (see formats/dec32.h, dec64.h, dec128.h).
#pragma once
#if defined(_MSC_VER)
#error "ea-FRDT: MSVC is not supported. Use GCC 13+ or Clang 16+ (MSYS2 clang64 on Windows)."
#endif
#include "common.h"

namespace fw {

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
    std::string p = bits;
    while (p.size() % 4 != 0) p = "0" + p;  // left-pad to nibble boundary
    std::string out;
    static const char* h = "0123456789ABCDEF";
    for (size_t i = 0; i < p.size(); i += 4) {
        int v = 0;
        for (size_t k = 0; k < 4; ++k) v = v * 2 + (p[i + k] == '1');
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

} // namespace fw
