// format_util.h — Bit string and numeric formatting helpers
#pragma once
#include <cstdint>
#include <string>
#include "base_types.h"

namespace fw {

std::string bits_to_hex(const std::string& bits);
std::string raw_to_bits(std::uint64_t raw, int bits);
std::string group_bits(const std::string& b, int expBits, int manBits);
std::string group_dec_bits(const std::string& bits, int exp_cont, int coeff_bits);
std::string format_value(double v, DispFmt fmt, int digits);
double parse_number(const std::string& s);
std::string sanitize(const std::string& s);  // strip control chars / ANSI escapes for safe output

} // namespace fw
