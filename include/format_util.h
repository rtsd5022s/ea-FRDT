// format_util.h — Bit string and numeric formatting helpers
#pragma once
#if defined(_WIN32) && !defined(__MINGW32__)
#error "ea-FRDT requires MinGW target (x86_64-w64-mingw32 / x86_64-w64-windows-gnu). Do not use MSVC or LLVM's default windows-msvc clang; use MSYS2 clang64."
#endif
#include <cstdint>
#include <string>
#include <string_view>
#include "base_types.h"

namespace fw {

[[nodiscard]] std::string bits_to_hex(std::string_view bits);
[[nodiscard]] std::string raw_to_bits(std::uint64_t raw, int bits);
[[nodiscard]] std::string group_bits(std::string_view b, int expBits, int manBits);
[[nodiscard]] std::string group_dec_bits(std::string_view bits, int exp_cont, int coeff_bits);
[[nodiscard]] std::string format_value(double v, DispFmt fmt, int digits);
[[nodiscard]] double parse_number(std::string_view s);
[[nodiscard]] std::string sanitize(std::string_view s);  // strip control chars / ANSI escapes for safe output

} // namespace fw
