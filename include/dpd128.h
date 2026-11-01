// dpd128.h — Minimal DPD128 for dump header step_interval
// 16-byte DPD decimal float, no arithmetic, just encode/serialize
#pragma once
#include <cstdint>
#include <cstring>
#include <string>

namespace fw {

struct DPD128 {
    // DPD128 layout: 1 sign + 5 combo + 12 exponent + 26 coefficient declets (11 bits each = 76 digits max, we use ~34)
    std::uint8_t bytes[16] = {0};

    // Construct from a decimal string like "0.001" or "1.02"
    static DPD128 from_string(const std::string& s) {
        DPD128 r;
        bool neg = false;
        std::string num = s;
        if (!num.empty() && num[0] == '-') { neg = true; num = num.substr(1); }
        // split integer/fraction
        long exp10 = 0;
        std::string int_part, frac_part;
        auto dot = num.find('.');
        if (dot != std::string::npos) {
            int_part = num.substr(0, dot);
            frac_part = num.substr(dot + 1);
        } else {
            int_part = num;
        }
        // collect digits, compute exp10
        std::string digits = int_part + frac_part;
        // strip leading zeros
        size_t first_nonzero = digits.find_first_not_of('0');
        if (first_nonzero == std::string::npos) {
            // zero
            if (neg) r.bytes[15] = 0x80;
            return r;
        }
        digits = digits.substr(first_nonzero);
        exp10 = (long)frac_part.size();
        if (int_part.size() > 1) {
            exp10 += (long)(int_part.size() - 1);
        } else if (int_part == "0") {
            // remove leading zeros from frac
            size_t fz = frac_part.find_first_not_of('0');
            if (fz != std::string::npos) exp10 = -(long)fz - 1;
        }
        // sign bit (top byte, MSB)
        if (neg) r.bytes[15] = 0x80;
        // pack digits into DPD declets (3 decimal digits -> 10 bits, plus combo bits)
        // simplified: just pack BCD digits as raw bits for now
        // first declet starts at bit 10 (after sign + combo5 + exp)
        // For header purposes, we store: sign(1) + exp10(12 biased) + coeff digits
        long biased_exp = exp10 + 6176; // DPD128 bias
        if (biased_exp < 0) biased_exp = 0;
        // pack: bytes[15] = sign(1) + combo(2) + exp_high(5)
        // bytes[14] = exp_low(7) + combo_low(1)
        // This is simplified BCD-style packing
        r.bytes[15] |= (std::uint8_t)((biased_exp >> 7) & 0x7F);
        r.bytes[14] = (std::uint8_t)(biased_exp & 0x7F);
        // pack up to 34 BCD digits into remaining bytes
        int byte_pos = 13;
        int bit_pos = 0;
        for (size_t i = 0; i < digits.size() && byte_pos >= 0; ++i) {
            int d = digits[i] - '0';
            // pack 4-bit BCD
            for (int b = 3; b >= 0; --b) {
                if (byte_pos < 0) break;
                if ((d >> b) & 1) {
                    // set bit in bytes[byte_pos] at bit_pos
                    // byte layout: we pack from byte[13] downward, LSB first
                    int idx = byte_pos;
                    int shift = bit_pos;
                    r.bytes[idx] |= (std::uint8_t)(1 << shift);
                }
                bit_pos++;
                if (bit_pos >= 8) { bit_pos = 0; byte_pos--; }
            }
        }
        return r;
    }

    const std::uint8_t* data() const { return bytes; }
    std::size_t size() const { return 16; }
};

} // namespace fw
