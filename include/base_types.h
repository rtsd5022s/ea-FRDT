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
    enum Reason { OVERFLOW, UNDERFLOW, IDENTITY, SAFETY, TIMEOUT } reason = OVERFLOW;
    double elapsed_ms = 0;
    std::string cur_value;
    u128 cur_steps = 0;
    double cur_elapsed_ms = 0;
    bool done = false;
    std::string name;
};

inline std::atomic<int> g_timeout_sec{0};

} // namespace fw
