// base_types.h — Core enums, u128 step counter, RunResult struct, global state
#pragma once
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <string>

namespace fw {

enum class StepMode { X102, MUL_ULP, ADD_ULP };
enum class DispFmt { ENG, SCI, FULL };
enum class StartKind { Default, Subnormal, MinNormal, MaxFinite };
enum class ShowBits { OFF, HEX, BIN };

using u128 = unsigned __int128;

inline std::string to_string_u128(u128 v) {
    if (v == 0) return "0";
    std::string out;
    while (v > 0) { out += (char)('0' + (int)(v % 10)); v /= 10; }
    std::reverse(out.begin(), out.end());
    return out;
}

struct RunResult {
    u128 steps = 0;
    enum Reason { OVERFLOW, UNDERFLOW, IDENTITY, SAFETY, TIMEOUT, NOT_A_NUMBER } reason = OVERFLOW;
    double elapsed_ms = 0;
    std::string cur_value;
    u128 cur_steps = 0;
    double cur_elapsed_ms = 0;
    bool done = false;
    std::string name;
};

inline std::atomic<int> g_timeout_sec{0};
inline bool g_json = false;
inline RunResult g_result;
inline std::string g_fmt_name;
inline double g_json_factor = 1.02;
inline double g_json_start = 1.0;
inline int g_json_step_mode = 0;
inline int g_json_digits = 6;
inline double g_json_delay_ms = 0;
inline int g_json_loop = 1;
inline int g_json_workers = 1;
#include <cstdio>
inline std::FILE* g_dump_file = nullptr;
inline int g_dump_align = 8;  // 1/2/4/8/16 bytes per record
inline int g_dump_endian = 0; // 0=little (native x86), 1=big
inline void (*g_dump_cb)(const void* data, std::size_t len) = nullptr;

} // namespace fw
