// lang.cpp — Bilingual translation implementation
#include "lang.h"

namespace fw {

Lang g_lang = Lang::ZH;

std::string tr(const char* zh, const char* en) {
    return g_lang == Lang::ZH ? zh : en;
}

} // namespace fw
