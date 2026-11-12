// engine_emit.h — Shared start selection and dump-record emission for the software engines.
// Every pure-software engine (sim / decimal BID / DPD / x87 / fp128) picks the same start
// kinds and writes its records the same way; only the value construction differs.
#pragma once
#if defined(_WIN32) && !defined(__MINGW32__)
#error "ea-FRDT requires MinGW target (x86_64-w64-mingw32 / x86_64-w64-windows-gnu). Do not use MSVC or LLVM's default windows-msvc clang; use MSYS2 clang64."
#endif
#include "common.h"

namespace fw {

// Which start value a run should walk from. The explicit -a kinds win; otherwise an
// explicit --start number is used, and only then the plain 1.0 default.
enum class StartPick { One, Custom, Subnormal, MinNormal, MaxFinite };

[[nodiscard]] inline StartPick pick_start(StartKind kind, double start_val) {
    switch (kind) {
        case StartKind::Subnormal: return StartPick::Subnormal;
        case StartKind::MinNormal: return StartPick::MinNormal;
        case StartKind::MaxFinite: return StartPick::MaxFinite;
        default: break;
    }
    return std::isnan(start_val) ? StartPick::One : StartPick::Custom;
}

// Packs an MSB-first bit string into out[0..n), zero-padding past the end of the string.
inline void bits_into_record(std::string_view bits, char* out, int n) {
    for (int i = 0; i < n; ++i) {
        int byte = 0;
        for (int j = 0; j < 8; ++j) {
            std::size_t idx = (std::size_t)i * 8 + (std::size_t)j;
            byte = (byte << 1) | (idx < bits.size() && bits[idx] == '1' ? 1 : 0);
        }
        out[i] = (char)byte;
    }
}

// Packs a hex bit pattern into at most cap bytes; returns the number of bytes written.
inline int hex_into_record(std::string_view hex, char* out, int cap) {
    auto hexval = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return 0;
    };
    int n = (int)(hex.size() / 2);
    if (n > cap) n = cap;
    for (int i = 0; i < n; ++i) out[i] = (char)((hexval(hex[2 * i]) << 4) | hexval(hex[2 * i + 1]));
    return n;
}

// Appends one record to the dump stream. Buffers are built big-endian (MSB first);
// --dump-endian=le reverses them in place before the usual byte counter update.
inline void dump_record(char* buf, int n) {
    if (n <= 0) return;
    if (g_dump_endian == 0)
        for (int i = 0; i < n / 2; ++i) std::swap(buf[i], buf[n - 1 - i]);
    std::fwrite(buf, 1, (std::size_t)n, g_dump_file);
    g_dump_bytes_written += n;
}

} // namespace fw
