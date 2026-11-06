// lang.h — Bilingual translation interface (zh/en)
#pragma once
#if defined(_MSC_VER)
#error "ea-FRDT: MSVC is not supported. Use GCC 13+ or Clang 16+ (MSYS2 clang64 on Windows)."
#endif
#include <string>

namespace fw {

enum class Lang { ZH, EN };
extern Lang g_lang;

std::string tr(const char* zh, const char* en);

} // namespace fw
