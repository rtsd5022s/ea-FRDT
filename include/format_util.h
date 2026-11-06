// format_util.h — Bit string and numeric formatting helpers
#pragma once
#if defined(_MSC_VER)
#error "ea-FRDT: MSVC is not supported. Use GCC 13+ or Clang 16+ (MSYS2 clang64 on Windows)."
#endif
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
