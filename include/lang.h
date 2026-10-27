// lang.h — 双语翻译接口
#pragma once
#include <string>

namespace fw {

enum class Lang { ZH, EN };
extern Lang g_lang;

std::string tr(const char* zh, const char* en);

} // namespace fw
