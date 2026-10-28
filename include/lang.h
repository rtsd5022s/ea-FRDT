// lang.h — Bilingual translation interface (zh/en)
#pragma once
#include <string>

namespace fw {

enum class Lang { ZH, EN };
extern Lang g_lang;

std::string tr(const char* zh, const char* en);

} // namespace fw
