// lang.h — Bilingual translation interface (zh/en)
#pragma once
#if defined(_WIN32) && !defined(__MINGW32__)
#error "ea-FRDT requires MinGW target (x86_64-w64-mingw32 / x86_64-w64-windows-gnu). Do not use MSVC or LLVM's default windows-msvc clang; use MSYS2 clang64."
#endif
#include <string>

namespace fw {

enum class Lang { ZH, EN };
extern Lang g_lang;

std::string tr(const char* zh, const char* en);

} // namespace fw
