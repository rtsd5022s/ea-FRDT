// base_types.h — Core enums, u128 step counter, RunResult struct, global state
#pragma once
#if defined(_WIN32) && !defined(__MINGW32__)
#error "ea-FRDT requires MinGW target (x86_64-w64-mingw32 / x86_64-w64-windows-gnu). Do not use MSVC or LLVM's default windows-msvc clang; use MSYS2 clang64."
#endif
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace fw {

enum class StepMode { X102, MUL_ULP, ADD_ULP, SUB_ULP };
enum class DispFmt { ENG, SCI, FULL };
enum class StartKind { Default, Subnormal, MinNormal, MaxFinite };
enum class ShowBits { OFF, HEX, BIN };

using u128 = unsigned __int128;

[[nodiscard]] inline std::string to_string_u128(u128 v) {
    if (v == 0) return "0";
    std::string out;
    out.reserve(39);  // 2^128-1 has 39 decimal digits
    while (v > 0) { out += (char)('0' + (int)(v % 10)); v /= 10; }
    std::reverse(out.begin(), out.end());
    return out;
}

struct RunResult {
    u128 steps = 0;
    enum Reason { OVERFLOW, UNDERFLOW, IDENTITY, SAFETY, TIMEOUT, NOT_A_NUMBER, FIXED_STEPS } reason = OVERFLOW;
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
inline std::string g_compare_path;   // --compare <baseline.json>: deterministic steps/reason anchor (A1)
inline std::string g_compare_key;     // format token used to locate the anchor record
inline u128 g_fixed_steps = 0;        // --steps N: stop after exactly N steps (0=disabled, natural termination)
inline int  g_pin_core = -1;          // --pin <core>: single-format optional affinity opt-in (-1=unset)
inline bool g_boost_enabled = false;  // --boost: single-format optional priority raise opt-in
inline std::FILE* g_events_file = nullptr; // --dump-events <file>: JSONL start/exit event trace (single format)
inline std::atomic<int> g_max_seconds{0};  // --max-seconds <s>: hard wall-clock cap (0=off); fires TIMEOUT, independent of -t
inline bool g_iron_preset = false;         // --preset iron / --iron: boost-on + safety cap; never auto-pins, single format only
// W3-B5: --sample-window <ms> throughput distribution over wall-clock segments (0=off; quiet/JSON path only).
inline long long g_sample_window_ms = 0;
inline std::vector<double> g_rate_segments;
inline double g_seg_prev_ms = 0.0;
inline u128   g_seg_prev_steps = 0;
inline double g_rate_min = 0.0, g_rate_avg = 0.0, g_rate_p95 = 0.0, g_rate_max = 0.0;
inline int    g_rate_n = 0;
inline std::FILE* g_dump_file = nullptr;
inline bool g_dump_records_enabled = false; // record writes on; disabled for -m all (mixed-width concurrent)
inline int g_dump_endian = 0; // 0=little (native x86), 1=big
inline int g_dump_packed = 0; // 0=natural width (default), 1=request packed tight
inline int g_dump_packed_active = 0; // 1 only when packed actually shrinks a record (e.g. tf32 3B<4B); else 0
inline bool g_assume_yes = false; // --yes/-y: skip interactive critical-warning confirmation (non-interactive/CI)
inline bool g_dump_raw = false; // 1=omit 256B header; continuous raw bit stream from offset 0 (single format only)
inline const char* g_dump_path = nullptr; // open path; discarded when -m all rejects raw
inline long g_dump_bytes_written = 0;
inline int g_dump_record_size = 0;   // bytes per record (set per format)
inline int g_dump_format_id = 0;     // format identifier (set per format)
inline double g_dump_factor = 1.02;  // step factor for header (IEEE binary64)
inline int g_dump_start_kind = 0;    // 0=numeric/default,1=subnormal,2=min-normal,3=max-finite
inline int g_dump_step_mode = 0;      // 0=x1.02,1=mululp,2=addulp,3=subulp
inline double g_dump_start_value = 1.0; // numeric start value, IEEE binary64

} // namespace fw
