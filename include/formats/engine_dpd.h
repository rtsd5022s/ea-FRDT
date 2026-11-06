// engine_dpd.h — Shared IEEE 754 DPD decimal-float engine: DpdNum + arithmetic + pack + run_dpd.
// Reused by dpd32, dpd64, dpd128 (see formats/dpd32.h, dpd64.h, dpd128.h).
#pragma once
#if defined(_WIN32) && !defined(__MINGW32__)
#error "ea-FRDT requires MinGW target (x86_64-w64-mingw32 / x86_64-w64-windows-gnu). Do not use MSVC or LLVM's default windows-msvc clang; use MSYS2 clang64."
#endif
#include "engine_dpd_codec.h"

namespace fw {

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
    else if (top_exp < emin) { r.digits = {0}; r.exp10 = 0; }
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

// ---- declet chaining sub-unit: fixed-length DPD bitstring <-> DpdNum with round-trip check ----
// Coefficient zero-padded on the low-significance side to fmt.digits; the leading digit rides in
// the 5-bit combo, the remaining digits group into (digits-1)/3 declets (3 decimal digits -> 10 bits).
inline std::vector<int> dpd_coeff_padded(const DpdNum& n, const DpdFmt& fmt) {
    std::vector<int> d = n.digits;
    while ((int)d.size() < fmt.digits) d.push_back(0);
    while ((int)d.size() > fmt.digits) d.erase(d.begin());
    return d;
}

// Fixed-length MSB-first bit string: S(1) | combo(5) | exp_cont(exp_bits) | declets(10 bits each)
inline std::string dpd_pack_bitstring(const DpdNum& n, const DpdFmt& fmt) {
    std::vector<int> d = dpd_coeff_padded(n, fmt);
    int be = n.exp10 + (int)fmt.bias;
    int E = be & ((1 << fmt.exp_bits) - 1);
    int eMSB = (be >> fmt.exp_bits) & 3;
    std::string out;
    out += n.neg ? '1' : '0';
    out += dpd_combo(d[0], eMSB);
    for (int i = fmt.exp_bits - 1; i >= 0; --i) out += ((E >> i) & 1) ? '1' : '0';
    for (int i = 1; i < (int)d.size(); i += 3) {
        unsigned v = dpd_encode3((unsigned)d[i], (unsigned)d[i+1], (unsigned)d[i+2]);
        for (int b = 9; b >= 0; --b) out += ((v >> b) & 1) ? '1' : '0';
    }
    return out;
}

// Exact reverse of dpd_pack_bitstring.
inline DpdNum dpd_unpack_bits(const std::string& bits, const DpdFmt& fmt) {
    DpdNum r;
    r.neg = (bits[0] == '1');
    char g0=bits[1], g1=bits[2], g2=bits[3], g3=bits[4], g4=bits[5];
    int eMSB, msd;
    if (g0=='0' || g1=='0') {
        eMSB = (g0=='1')*2 + (g1=='1');
        msd  = (g2=='1')*4 + (g3=='1')*2 + (g4=='1');
    } else {
        eMSB = (g2=='1')*2 + (g3=='1');
        msd  = (g4=='1') ? 9 : 8;
    }
    int E = 0, off = 6;
    for (int i = 0; i < fmt.exp_bits; ++i) E = (E << 1) | (bits[off+i]=='1');
    off += fmt.exp_bits;
    r.exp10 = ((eMSB << fmt.exp_bits) | E) - (int)fmt.bias;
    std::vector<int> d; d.push_back(msd);
    int ndec = (fmt.digits - 1) / 3;
    for (int k = 0; k < ndec; ++k) {
        unsigned v = 0;
        for (int b = 0; b < 10; ++b) v = (v << 1) | (bits[off + k*10 + b] == '1');
        unsigned a=0, b=0, c=0;
        dpd_decode3(v, a, b, c);
        d.push_back((int)a); d.push_back((int)b); d.push_back((int)c);
    }
    r.digits = d;
    return r;
}

// True round-trip self-check: pack -> unpack, compare sign / exp10 / fixed-length coefficient.
inline bool dpd_selfcheck(const DpdNum& n, const DpdFmt& fmt, std::string* err = nullptr) {
    std::string bits = dpd_pack_bitstring(n, fmt);
    DpdNum u = dpd_unpack_bits(bits, fmt);
    bool ok = true; std::string e;
    if (u.neg != n.neg) { ok = false; e += " sign"; }
    if (u.exp10 != n.exp10) { ok = false; e += " exp10(" + std::to_string(u.exp10) + "!=" + std::to_string(n.exp10) + ")"; }
    std::vector<int> expect = dpd_coeff_padded(n, fmt);
    if (u.digits != expect) { ok = false; e += " coeff"; }
    if (err) *err = e;
    return ok;
}

inline std::string dpd_pack_bits(const DpdNum& n, const DpdFmt& fmt) {
    std::string b = dpd_pack_bitstring(n, fmt);
    int total = (int)b.size();
    int nb = (total + 7) / 8;
    std::vector<unsigned char> bits(nb, 0);
    for (int i = 0; i < total; ++i)
        if (b[i] == '1') { int by=i/8, o=7-(i%8); bits[by]|=(unsigned char)(1<<o); }
    static const char* hx = "0123456789ABCDEF";
    std::string out;
    for (int i=0;i<nb;++i) { out+=hx[bits[i]>>4]; out+=hx[bits[i]&0xF]; }
    return out;
}

inline std::string dpd_pack_bin(const DpdNum& n, const DpdFmt& fmt) {
    std::string s = dpd_pack_bitstring(n, fmt);
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
        start.digits = {1};
        int emax = (fmt.digits == 7) ? 96 : (fmt.digits == 16) ? 384 : 6144;
        start.exp10 = 1 - emax;
    } else if (kind == StartKind::MaxFinite) {
        start.digits = std::vector<int>(fmt.digits, 9);
        start.exp10 = 9 * (fmt.digits - 1);
    }

    bool print = (out == nullptr);
    double sv = std::isnan(start_val) ? 1.0 : start_val;
    std::string sc_err;
    bool sc_ok = dpd_selfcheck(start, fmt, &sc_err);
    if (!sc_ok) std::fprintf(stderr, "  [DPD-SELFCHECK] MISMATCH%s\n", sc_err.c_str());
    if (print) {
        std::printf("%s walk: start=%.1f, %s, delay=%lldms (0=unlimited)\n",
                    fmt.name, sv, step_factor_str(sm, factor).c_str(), (long long)delay_ms);
        std::printf("  [DPD] coeff=");
        for (int d : start.digits) std::printf("%d", d);
        std::printf(" exp10=%d hex=%s\n", start.exp10, dpd_pack_bits(start, fmt).c_str());
        std::printf("  [DPD-SELFCHECK] %s\n", sc_ok ? "OK" : "MISMATCH");
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

    auto dpd_subulp = [&](DpdNum v) -> DpdNum {
        DpdNum r = v;
        if (r.digits.size() == 1 && r.digits[0] == 0) return r;
        int i = (int)r.digits.size() - 1;
        r.digits[i] -= 1;
        while (i >= 0 && r.digits[i] < 0) {
            r.digits[i] += 10;
            if (i > 0) r.digits[i-1] -= 1;
            --i;
        }
        // strip leading zeros
        size_t first = 0;
        while (first < r.digits.size() - 1 && r.digits[first] == 0) ++first;
        r.digits.erase(r.digits.begin(), r.digits.begin() + first);
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
        else if (sm == StepMode::SUB_ULP) nxt = dpd_subulp(cur);
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
                auto hexval = [](char c) -> int {
                    if (c >= '0' && c <= '9') return c - '0';
                    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                    return 0;
                };
                buf[i] = (char)((hexval(hex[2*i]) << 4) | hexval(hex[2*i+1]));
            }
            if (g_dump_endian == 0) {
                for (int i = 0; i < bw/2; ++i) std::swap(buf[i], buf[bw-1-i]);
            }
            std::fwrite(buf, 1, bw, g_dump_file);
            g_dump_bytes_written += bw;
        }

        // timeout
        if (g_timeout_sec.load() > 0) {
            double es = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            if (es >= (double)g_timeout_sec.load()) {
                reason = RunResult::TIMEOUT;
                if (out) { out->steps = steps; out->reason = reason; }
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
