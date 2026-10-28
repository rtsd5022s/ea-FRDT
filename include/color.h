// color.h — ANSI terminal color escape sequences
#pragma once

namespace fw::clr {
inline constexpr const char* RST  = "\033[0m";
inline constexpr const char* DIM  = "\033[2m";
inline constexpr const char* CYN  = "\033[36m";
inline constexpr const char* GRN  = "\033[32m";
inline constexpr const char* YEL  = "\033[33m";
inline constexpr const char* BLU  = "\033[34m";
inline constexpr const char* GRY  = "\033[90m";
inline constexpr const char* RED  = "\033[31m";
} // namespace fw::clr
