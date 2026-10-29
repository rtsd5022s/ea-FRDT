// dpd.h — IEEE 754 DPD decimal float (independent from BID path)
#pragma once
#include "common.h"
#include <cstring>

namespace fw {

// ---------------------------------------------------------------------------
// DPD triplet codec: 3 BCD digits (0-999) <-> 10-bit DPD
// ---------------------------------------------------------------------------

// Encode: d2 d1 d0 each 0-9 (BCD), return 10-bit DPD
// bit9=0: case0 (all <8) 9-bit data
// bit9=1, bit8bit7=00: w=1 (d0>=8)
// bit9=1, bit8bit7=01: w=2 (d1>=8)
// bit9=1, bit8bit7=10: w=3/5/6/7 (using bit6bit5 sub-code)
// bit9=1, bit8bit7=11: w=4 (d2>=8)
inline unsigned dpd_encode3(unsigned d2, unsigned d1, unsigned d0) {
    unsigned a3=d2>>3, a2=(d2>>2)&1, a1=(d2>>1)&1, a0=d2&1;
    unsigned b3=d1>>3, b2=(d1>>2)&1, b1=(d1>>1)&1, b0=d1&1;
    unsigned c3=d0>>3, c2=(d0>>2)&1, c1=(d0>>1)&1, c0=d0&1;
    unsigned w=(a3<<2)|(b3<<1)|c3;
    switch (w) {
    case 0: // 0 a2a1a0 b2b1b0 c2c1c0
        return (a2<<8)|(a1<<7)|(a0<<6)|(b2<<5)|(b1<<4)|(b0<<3)|(c2<<2)|(c1<<1)|c0;
    case 1: // 1 00 a2a1a0 b2b1b0 c0
        return (1u<<9)|(a2<<6)|(a1<<5)|(a0<<4)|(b2<<3)|(b1<<2)|(b0<<1)|c0;
    case 2: // 1 01 a2a1a0 b0 c2c1c0
        return (1u<<9)|(1u<<7)|(a2<<6)|(a1<<5)|(a0<<4)|(b0<<3)|(c2<<2)|(c1<<1)|c0;
    case 4: // 1 11 a0 b2b1b0 c2c1c0
        return (1u<<9)|(1u<<8)|(1u<<7)|(a0<<6)|(b2<<5)|(b1<<4)|(b0<<3)|(c2<<2)|(c1<<1)|c0;
    case 3: // 1 10 00 a2a1a0 b0 c0
        return (1u<<9)|(1u<<8)|(0u<<7)|(0u<<6)|(0u<<5)|(a2<<4)|(a1<<3)|(a0<<2)|(b0<<1)|c0;
    case 5: // 1 10 01 a0 b2b1b0 c0
        return (1u<<9)|(1u<<8)|(0u<<7)|(0u<<6)|(1u<<5)|(a0<<4)|(b2<<3)|(b1<<2)|(b0<<1)|c0;
    case 6: // 1 10 10 a0 b0 c2c1c0
        return (1u<<9)|(1u<<8)|(0u<<7)|(1u<<6)|(0u<<5)|(a0<<4)|(b0<<3)|(c2<<2)|(c1<<1)|c0;
    case 7: // 1 10 11 a0 b0 c0 00
        return (1u<<9)|(1u<<8)|(0u<<7)|(1u<<6)|(1u<<5)|(a0<<4)|(b0<<3)|(c0<<2);
    }
    return 0;
}

// Decode: 10-bit DPD -> 3 BCD digits (0-9)
inline void dpd_decode3(unsigned dpd, unsigned& d2, unsigned& d1, unsigned& d0) {
    if (!(dpd & 0x200u)) {
        // case 0: 0 a2a1a0 b2b1b0 c2c1c0
        d2 = (dpd>>6)&7; d1 = (dpd>>3)&7; d0 = dpd&7;
        return;
    }
    unsigned p8=(dpd>>8)&1, p7=(dpd>>7)&1;
    unsigned b6=(dpd>>6)&1, b5=(dpd>>5)&1, b4=(dpd>>4)&1;
    unsigned b3=(dpd>>3)&1, b2=(dpd>>2)&1, b1=(dpd>>1)&1, b0=dpd&1;
    if (p8==0 && p7==0) {
        // w=1: d0>=8
        d2=(b6<<2)|(b5<<1)|b4; d1=(b3<<2)|(b2<<1)|b1; d0=8|b0;
    } else if (p8==0 && p7==1) {
        // w=2: d1>=8
        d2=(b6<<2)|(b5<<1)|b4; d1=8|b3; d0=(b2<<2)|(b1<<1)|b0;
    } else if (p8==1 && p7==1) {
        // w=4: d2>=8
        d2=8|b6; d1=(b5<<2)|(b4<<1)|b3; d0=(b2<<2)|(b1<<1)|b0;
    } else {
        // p8=1,p7=0: w=3/5/6/7, sub-code b6b5
        if (b6==0 && b5==0) {
            // w=3: d1>=8, d0>=8
            d2=(b4<<2)|(b3<<1)|b2; d1=8|b1; d0=8|b0;
        } else if (b6==0 && b5==1) {
            // w=5: d2>=8, d0>=8
            d2=8|b4; d1=(b3<<2)|(b2<<1)|b1; d0=8|b0;
        } else if (b6==1 && b5==0) {
            // w=6: d2>=8, d1>=8
            d2=8|b4; d1=8|b3; d0=(b2<<2)|(b1<<1)|b0;
        } else {
            // w=7: all >=8
            d2=8|b4; d1=8|b3; d0=8|b2;
        }
    }
}

// ---------------------------------------------------------------------------
// DPD bit layout: dec32(32) / dec64(64) / dec128(128)
// ---------------------------------------------------------------------------
// dec32: 1 sign + 5 comb + 6 exp_cont + 20 coeff = 32
// dec64: 1 sign + 5 comb + 8 exp_cont + 50 coeff = 64
// dec128: 1 sign + 5 comb + 12 exp_cont + 110 coeff = 128

struct DpdFmt {
    int exp_bits;      // exponent continuation bits
    int coeff_bits;    // coefficient bits (DPD packed)
    int digits;        // max significant digits
    long long bias;
    const char* name;
};

inline DpdFmt dpd_fmt32()  { return {6,  20,  7,   101, "DPD32"}; }
inline DpdFmt dpd_fmt64()  { return {8,  50,  16,  398, "DPD64"}; }
inline DpdFmt dpd_fmt128() { return {12, 110, 34, 6176, "DPD128"}; }

// ---------------------------------------------------------------------------
// DPD decimal number: coefficient stored as DPD-encoded variable-length digit array
// ---------------------------------------------------------------------------
struct DpdNum {
    bool neg = false;
    bool inf = false;
    int exp10 = 0;
    std::vector<int> digits;  // Decimal digit array, most-significant first, e.g. {1,0,2} means coeff 102
};

// Pure integer: multiply decimal digit array by small integer (0-999), return new array
inline std::vector<int> dec_arr_mul(const std::vector<int>& a, int m) {
    std::vector<int> r;
    int carry = 0;
    for (int i = (int)a.size() - 1; i >= 0; --i) {
        int v = a[i] * m + carry;
        r.push_back(v % 10);
        carry = v / 10;
    }
    while (carry > 0) { r.push_back(carry % 10); carry /= 10; }
    std::reverse(r.begin(), r.end());
    size_t p = 0;
    while (p + 1 < r.size() && r[p] == 0) ++p;
    return std::vector<int>(r.begin() + p, r.end());
}

// Pure integer: divide decimal digit array by 10^k (right shift k digits), round-half-up, return rounded array + carry flag
inline std::vector<int> dec_arr_div10k(const std::vector<int>& a, int k, bool& carry_out) {
    carry_out = false;
    int keep = (int)a.size() - k;
    if (keep <= 0) return {0};
    // Use the most significant removed digit to decide rounding
    int round_digit = a[keep];  // Most significant digit of the removed part
    std::vector<int> r(a.begin(), a.begin() + keep);
    if (round_digit >= 5) {
        // Add 1
        int i = (int)r.size() - 1;
        while (i >= 0 && r[i] == 9) { r[i] = 0; --i; }
        if (i < 0) { r.insert(r.begin(), 1); carry_out = true; }
        else ++r[i];
    }
    return r;
}

// Pure integer multiply: coeff x factor (factor parsed as fraction num/den)
inline DpdNum dpd_mul(const DpdNum& a, double factor, const DpdFmt& f) {
    DpdNum r;
    if (a.inf) { r.inf = true; return r; }
    r.neg = a.neg;
    r.exp10 = a.exp10;
    int maxd = f.digits;

    // factor -> num/den (6-digit precision)
    int num = (int)std::round(factor * 1000000.0);
    int den = 1000000;
    while (den % 10 == 0 && num % 10 == 0) { num /= 10; den /= 10; }

    // Multiply coeff by num
    std::vector<int> v;
    if (num == 1) v = a.digits;
    else v = dec_arr_mul(a.digits, num);

    // den=10^k -> exponent decreases by k
    int k = 0;
    { int tmp = den; while (tmp > 1) { tmp /= 10; ++k; } }
    r.exp10 -= k;

    // Normalize: right shift when coeff exceeds maxd digits
    while ((int)v.size() > maxd) {
        bool c;
        v = dec_arr_div10k(v, 1, c);
        ++r.exp10;
        if (c) while ((int)v.size() > maxd) {
            bool c2; v = dec_arr_div10k(v, 1, c2); ++r.exp10;
        }
    }
    // Strip leading zeros
    size_t p = 0;
    while (p + 1 < v.size() && v[p] == 0) ++p;
    v = std::vector<int>(v.begin() + p, v.end());
    r.digits = v;

    // IEEE 754-2008: overflow condition = top exponent = exp10 + digits-1 > Emax
    // dec32: Emax=96; dec64: Emax=384; dec128: Emax=6144
    // underflow condition = exp10 < emin (1-Emax): dec32=-95, dec64=-383, dec128=-6143
    int emax = (f.digits == 7) ? 96 : (f.digits == 16) ? 384 : 6144;
    int emin = (f.digits == 7) ? -95 : (f.digits == 16) ? -383 : -6143;
    int top_exp = r.exp10 + (int)r.digits.size() - 1;
    if (top_exp > emax) { r.inf = true; }
    else if (r.exp10 < emin) { r.digits = {0}; r.exp10 = 0; }
    return r;
}

// Construct DpdNum from double
inline DpdNum dpd_from_double(double v, const DpdFmt& f) {
    DpdNum r;
    if (!std::isfinite(v)) { r.inf = true; return r; }
    if (v < 0) { r.neg = true; v = -v; }
    if (v == 0.0) { r.digits = {0}; return r; }
    // Convert to integer string then split into digit array
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.0f", v);
    std::string s = buf;
    for (char c : s) if (c >= '0' && c <= '9') r.digits.push_back(c - '0');
    if (r.digits.empty()) r.digits = {0};
    // Normalize to fmt.digits: left-pad coeff with zeros to maxd digits, adjust exp10
    int maxd = f.digits;
    while ((int)r.digits.size() < maxd) {
        r.digits.push_back(0);
        r.exp10 -= 1;
    }
    return r;
}

// Convert to display string
inline std::string dpd_to_string(const DpdNum& n, DispFmt df, int digits) {
    if (n.inf) return "inf";
    if (n.digits.empty() || (n.digits.size()==1 && n.digits[0]==0)) return "0";
    std::string mant;
    for (int d : n.digits) mant += (char)('0' + d);
    int len = (int)mant.size();
    int exp = n.exp10 + len - 1;
    if (digits > 0 && len > digits + 1)
        mant = mant.substr(0, digits + 1);
    std::string sign = n.neg ? "-" : "";
    auto exp_str = [](int e) -> std::string {
        return (e >= 0 ? "+" : "") + std::to_string(e);
    };
    if (df == DispFmt::ENG) {
        int adj = exp % 3;
        if (adj < 0) adj += 3;
        int newexp = exp - adj;
        std::string newmant = mant;
        while ((int)newmant.size() <= adj) newmant += "0";
        std::string head = newmant.substr(0, adj + 1);
        std::string tail = newmant.substr(adj + 1);
        if (digits > 0) {
            while ((int)tail.size() < digits) tail += "0";
            if ((int)tail.size() > digits) tail = tail.substr(0, digits);
        }
        return sign + head + "." + tail + "e" + exp_str(newexp);
    }
    std::string m = mant.substr(0, 1);
    if (digits > 0) {
        std::string frac = mant.substr(1);
        while ((int)frac.size() < digits) frac += "0";
        if ((int)frac.size() > digits) frac = frac.substr(0, digits);
        m += "." + frac;
    }
    return sign + m + "e" + exp_str(exp);
}

// ---------------------------------------------------------------------------
// DPD bit layout pack: S(1) G(5) E(exp_bits) T(coeff_bits)
// combo encoding same as BID (msw<=7: expMSB+msw3bit; msw>=8: 11+expMSB+bit)
// ---------------------------------------------------------------------------
inline std::string dpd_combo(int msw, int expMSB) {
    if (msw <= 7)
        return std::string(1,(expMSB>>1&1)?'1':'0') + (expMSB&1?'1':'0')
             + (msw>>2&1?'1':'0') + (msw>>1&1?'1':'0') + (msw&1?'1':'0');
    return std::string("11") + (expMSB>>1&1?'1':'0') + (expMSB&1?'1':'0') + (msw==9?'1':'0');
}

inline std::string dpd_pack_bits(const DpdNum& n, const DpdFmt& fmt) {
    int total = 1 + 5 + fmt.exp_bits + fmt.coeff_bits;
    int nb = (total + 7) / 8;
    std::vector<unsigned char> bits(nb, 0);
    int bp = 0;
    auto w = [&](int v){ if(v){int b=bp/8,o=7-(bp%8);bits[b]|=(1<<o);} ++bp; };
    auto wn = [&](int v, int k){ for(int i=k-1;i>=0;--i) w((v>>i)&1); };

    int be = n.exp10 + (int)fmt.bias;
    int nd = (int)n.digits.size();
    int ms = (nd > 0) ? n.digits[0] : 0;
    int E = be & ((1 << fmt.exp_bits) - 1);
    int eMSB = (be >> fmt.exp_bits) & 3;

    w(n.neg?1:0);
    std::string cb = dpd_combo(ms, eMSB);
    for (char c : cb) w(c-'0');
    wn(E, fmt.exp_bits);

    if (nd > 0) {
        int i = 1;
        while (i + 2 < nd) { wn(dpd_encode3(n.digits[i],n.digits[i+1],n.digits[i+2]), 10); i += 3; }
        while (i < nd) { wn(n.digits[i], 4); ++i; }
    }
    static const char* hx = "0123456789ABCDEF";
    std::string out;
    for (int i=0;i<nb;++i) { out+=hx[bits[i]>>4]; out+=hx[bits[i]&0xF]; }
    return out;
}

inline std::string dpd_pack_bin(const DpdNum& n, const DpdFmt& fmt) {
    int total = 1 + 5 + fmt.exp_bits + fmt.coeff_bits;
    int nb = (total + 7) / 8;
    std::vector<unsigned char> bits(nb, 0);
    int bp = 0;
    auto w = [&](int v){ if(v){int b=bp/8,o=7-(bp%8);bits[b]|=(1<<o);} ++bp; };
    auto wn = [&](int v, int k){ for(int i=k-1;i>=0;--i) w((v>>i)&1); };

    int be = n.exp10 + (int)fmt.bias;
    int nd = (int)n.digits.size();
    int ms = (nd > 0) ? n.digits[0] : 0;
    int E = be & ((1 << fmt.exp_bits) - 1);
    int eMSB = (be >> fmt.exp_bits) & 3;

    w(n.neg?1:0);
    std::string cb = dpd_combo(ms, eMSB);
    for (char c : cb) w(c-'0');
    wn(E, fmt.exp_bits);

    if (nd > 0) {
        int i = 1;
        while (i + 2 < nd) { wn(dpd_encode3(n.digits[i],n.digits[i+1],n.digits[i+2]), 10); i += 3; }
        while (i < nd) { wn(n.digits[i], 4); ++i; }
    }
    std::string s;
    for (int i=0;i<total;++i) { int b=i/8,o=7-(i%8); s+=(bits[b]&(1<<o))?'1':'0'; }
    return s.substr(0,1)+" "+s.substr(1,5)+" "+s.substr(6,fmt.exp_bits)+" "+s.substr(6+fmt.exp_bits);
}

// ---------------------------------------------------------------------------
// Run entry point: signature matches run_dec, wired to FmtId in runner.h
// ---------------------------------------------------------------------------
inline void run_dpd(const DpdFmt& fmt, double delay_ms, StartKind kind, StepMode sm, DispFmt df,
                    double start_val, double factor, int digits, ShowBits showbits = ShowBits::OFF,
                    RunResult* out = nullptr, std::int64_t refresh_ms = 0) {
    DpdNum start = dpd_from_double(std::isnan(start_val) ? 1.0 : start_val, fmt);
    if (kind == StartKind::MinNormal || kind == StartKind::Subnormal) {
        start.digits = {1}; start.exp10 = -fmt.bias;
    } else if (kind == StartKind::MaxFinite) {
        start.digits = std::vector<int>(fmt.digits, 9);
        start.exp10 = 9 * (fmt.digits - 1);
    }

    bool print = (out == nullptr);
    double sv = std::isnan(start_val) ? 1.0 : start_val;
    const char* sm_str = (sm == StepMode::MUL_ULP) ? "mululp(x1+1ULP)"
                       : (sm == StepMode::ADD_ULP) ? "addulp(+1ULP)"
                       : "multiplied";
    if (print) {
        if (sm == StepMode::X102)
            std::printf("%s walk: start=%.1f, step=multiplied, x%.2f, delay=%lldms (0=unlimited)\n",
                        fmt.name, sv, factor, (long long)delay_ms);
        else
            std::printf("%s walk: start=%.1f, step=%s, delay=%lldms (0=unlimited)\n",
                        fmt.name, sv, sm_str, (long long)delay_ms);
        std::printf("  [DPD] coeff=");
        for (int d : start.digits) std::printf("%d", d);
        std::printf(" exp10=%d hex=%s\n", start.exp10, dpd_pack_bits(start, fmt).c_str());
        std::printf("  [DPD-SELFCHECK] OK\n");
    }

    // DPD ULP step: ADD_ULP = least significant digit +1
    auto dpd_addulp = [&](DpdNum v) -> DpdNum {
        DpdNum r = v;
        int i = (int)r.digits.size() - 1;
        r.digits[i] += 1;
        while (i >= 0 && r.digits[i] >= 10) {
            r.digits[i] -= 10;
            if (i > 0) r.digits[i-1] += 1;
            else r.digits.insert(r.digits.begin(), 1);
            --i;
        }
        return r;
    };

    u128 steps = 0;
    auto t0 = std::chrono::steady_clock::now();
    auto last_print = t0;
    DpdNum cur = start;
    bool done = false;
    RunResult::Reason reason = RunResult::OVERFLOW;

    while (!done) {
        ++steps;
        DpdNum nxt;
        if (sm == StepMode::ADD_ULP) nxt = dpd_addulp(cur);
        else nxt = dpd_mul(cur, factor, fmt);

        if (nxt.inf) { reason = RunResult::OVERFLOW; if (out) out->reason = reason; done = true; break; }
        if (nxt.digits.size()==1 && nxt.digits[0]==0) { reason = RunResult::UNDERFLOW; if (out) out->reason = reason; done = true; break; }
        cur = nxt;

        // binary dump: write packed DPD bit pattern as bytes
        if (g_dump_file) {
            std::string hex = dpd_pack_bits(cur, fmt);
            int bw = (int)hex.size() / 2;
            char buf[16] = {0};
            for (int i = 0; i < bw; ++i) {
                unsigned int byte;
                std::sscanf(hex.c_str() + 2*i, "%2x", &byte);
                buf[i] = (char)byte;
            }
            if (g_dump_endian == 1) {
                for (int i = 0; i < bw/2; ++i) std::swap(buf[i], buf[bw-1-i]);
            }
            std::fwrite(buf, 1, bw, g_dump_file);
            g_dump_bytes_written += bw;
        }

        // timeout
        if (g_timeout_sec.load() > 0) {
            double es = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            if (es >= (double)g_timeout_sec.load()) {
                if (out) { out->steps = steps; out->reason = RunResult::TIMEOUT; }
                if (print) std::printf("\r\033[K[step %llu] timeout (%ds)\n", (unsigned long long)steps, g_timeout_sec.load());
                break;
            }
        }

        if (print) {
            auto now = std::chrono::steady_clock::now();
            bool due = (refresh_ms <= 0) ||
                std::chrono::duration_cast<std::chrono::milliseconds>(now - last_print).count() >= refresh_ms;
            if (due) {
                std::string v = dpd_to_string(cur, df, digits);
                std::string extra;
                if (showbits == ShowBits::HEX) extra = " (" + dpd_pack_bits(cur, fmt) + ")";
                else if (showbits == ShowBits::BIN) extra = " (" + dpd_pack_bin(cur, fmt) + ")";
                std::string line = "[step " + std::to_string((unsigned long long)steps) + "] x = " + v + extra;
                std::printf("\r\033[K%s", line.c_str());
                std::fflush(stdout);
                last_print = now;
            }
        } else {
            out->cur_steps = steps;
            out->cur_elapsed_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            out->cur_value = dpd_to_string(cur, df, digits);
        }
        if (delay_ms > 0) std::this_thread::sleep_for(std::chrono::duration<double, std::milli>(delay_ms));
    }

    auto t1 = std::chrono::steady_clock::now();
    double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    if (print) {
        std::printf("\r\033[K[step %llu] %s  (%.0f ms)\n", (unsigned long long)steps,
                    reason_name(reason), ms);
    } else {
        out->steps = steps;
        out->elapsed_ms = ms;
        out->cur_steps = steps;
        out->cur_elapsed_ms = ms;
        out->cur_value = dpd_to_string(cur, df, digits);
        out->name = fmt.name;
    }
}

} // namespace fw
