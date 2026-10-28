// format_util.cpp — Bit string and numeric formatting implementations
#include <cmath>
#include <stdexcept>
#include "format_util.h"

namespace fw {

std::string bits_to_hex(const std::string& bits) {
    std::string out;
    for (size_t i = 0; i < bits.size(); i += 4) {
        int nib = 0;
        for (int j = 0; j < 4; ++j) {
            nib <<= 1;
            size_t k = i + j;
            if (k < bits.size() && bits[k] == '1') nib |= 1;
        }
        out += "0123456789ABCDEF"[nib];
    }
    return out;
}

std::string raw_to_bits(std::uint64_t raw, int bits) {
    std::string b;
    for (int i = bits - 1; i >= 0; --i) b += ((raw >> i) & 1) ? '1' : '0';
    return b;
}

std::string group_bits(const std::string& b, int expBits, int manBits) {
    return b.substr(0, 1) + " " + b.substr(1, expBits) + " " + b.substr(1 + expBits, manBits);
}

std::string group_dec_bits(const std::string& bits, int exp_cont, int coeff_bits) {
    return bits.substr(0, 1) + " " + bits.substr(1, 5) + " "
         + bits.substr(6, exp_cont) + " " + bits.substr(6 + exp_cont, coeff_bits);
}

std::string format_value(double v, DispFmt fmt, int digits) {
    char buf[160];
    if (!std::isfinite(v)) { std::snprintf(buf, sizeof buf, "%g", v); return buf; }
    char spec[24];
    switch (fmt) {
        case DispFmt::SCI:
            if (digits == -1) std::snprintf(buf, sizeof buf, "%.17e", v);
            else { std::snprintf(spec, sizeof spec, "%%.%de", digits); std::snprintf(buf, sizeof buf, spec, v); }
            break;
        case DispFmt::FULL:
            std::snprintf(buf, sizeof buf, "%.12f", v);
            break;
        case DispFmt::ENG: {
            if (v == 0.0) { std::snprintf(buf, sizeof buf, "0e0"); break; }
            int e = (int)std::floor(std::log10(std::fabs(v)));
            int e3 = (int)std::floor(e / 3.0) * 3;
            double m = v / std::pow(10.0, e3);
            if (digits == -1) std::snprintf(buf, sizeof buf, "%ge%d", m, e3);
            else { std::snprintf(spec, sizeof spec, "%%.%dfe%%d", digits); std::snprintf(buf, sizeof buf, spec, m, e3); }
            break;
        }
    }
    return buf;
}

double parse_number(const std::string& s) {
    if (s.size() >= 2 && (s.substr(0, 2) == "0x" || s.substr(0, 2) == "0X")) {
        const std::string hex = s.substr(2);
        if (hex.size() > 32) throw std::invalid_argument("hex too long");
        for (char c : hex) {
            if (!std::isxdigit((unsigned char)c)) throw std::invalid_argument("bad hex char");
        }
        std::uint64_t bits = std::stoull(hex, nullptr, 16);
        if (hex.size() <= 4) {
            std::uint16_t h = (std::uint16_t)bits;
            float f; std::memcpy(&f, &h, 2);
            return (double)f;
        } else if (hex.size() <= 8) {
            std::uint32_t b = (std::uint32_t)bits;
            float f; std::memcpy(&f, &b, 4);
            return f;
        }
        double v; std::memcpy(&v, &bits, sizeof v);
        return v;
    }
    if (s.size() > 128) throw std::invalid_argument("number too long");
    return std::stod(s);
}

std::string sanitize(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        unsigned char uc = (unsigned char)c;
        if (uc == 0x1B) continue;           // ESC
        if (uc < 0x20) continue;              // C0 control
        if (uc == 0x7F) continue;          // DEL
        out += c;
    }
    return out;
}

} // namespace fw
