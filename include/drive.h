// drive.h — Core stepping driver template interface
#pragma once
#if defined(_WIN32) && !defined(__MINGW32__)
#error "ea-FRDT requires MinGW target (x86_64-w64-mingw32 / x86_64-w64-windows-gnu). Do not use MSVC or LLVM's default windows-msvc clang; use MSYS2 clang64."
#endif
#include <chrono>
#include <cstdio>
#include <functional>
#include <iostream>
#include <string>
#include <thread>
#include "base_types.h"
#include "cui.h"
#include "lang.h"
#include "format_util.h"

namespace fw {

extern const int CLEAR;

void on_sigint(int);
const char* step_name(StepMode sm);
std::string step_factor_str(StepMode sm, double factor);
const char* reason_name(RunResult::Reason r);

// Machine-readable reason token for the JSONL event trace (C2 --dump-events).
static inline const char* reason_token(RunResult::Reason r) {
    switch (r) {
        case RunResult::OVERFLOW: return "overflow";
        case RunResult::UNDERFLOW: return "underflow";
        case RunResult::IDENTITY: return "identity";
        case RunResult::SAFETY: return "safety";
        case RunResult::TIMEOUT: return "timeout";
        case RunResult::NOT_A_NUMBER: return "nan";
        case RunResult::FIXED_STEPS: return "fixed";
    }
    return "?";
}

// Escape a string for inclusion in a JSON double-quoted field (C2 JSONL).
static inline std::string json_escape(const std::string& in) {
    std::string out;
    out.reserve(in.size() + 2);
    for (char c : in) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", (unsigned char)c);
                    out += buf;
                } else {
                    out.push_back(c);
                }
        }
    }
    return out;
}

// Smallest positive wall-clock cap among -t and --max-seconds; -1 when neither is set.
// Either cap firing maps to the existing TIMEOUT reason (no new enum value).
static inline int active_wall_cap() {
    int cap = -1;
    if (g_timeout_sec.load() > 0) cap = g_timeout_sec.load();
    int mx = g_max_seconds.load();
    if (mx > 0 && (cap < 0 || mx < cap)) cap = mx;
    return cap;
}

// Template driver (must stay in header for explicit instantiation)
template <class S, class Next, class IsInf, class IsZero, class IsNaN, class Render>
void drive_core(S s, Next next, IsInf isInf, IsZero isZero, IsNaN isNaN, Render render,
                double delay_ms,
                std::function<std::string(const S&)> extra = {},
                RunResult* quiet = nullptr,
                std::int64_t refresh_ms = 0,
                std::function<void(const S&)> dump_cb = nullptr) {
    static const u128 SAFETY_LIMIT = ((u128)1 << 120);
    [[maybe_unused]] constexpr size_t INPLACE_MAX = 110;
    constexpr u128 SLOW_INTERVAL = 256;
    u128 step = 0;
    u128 last_slow = 0;
    int ident_streak = 0;
    constexpr int IDENT_LIMIT = 16;
    auto t0 = std::chrono::steady_clock::now();
    auto last_print = t0;
    if (g_events_file)
        std::fprintf(g_events_file, "{\"event\":\"start\",\"format\":\"%s\"}\n", g_fmt_name.c_str());
    RunResult::Reason exit_reason = RunResult::OVERFLOW;
    auto finish_line = [&](const std::string& msg) {
        if (quiet) return;
        std::cout << cui::CLEAR_LINE << msg << '\n' << std::flush;
    };
    auto slow_update = [&]() {
        if (!quiet) return;
        quiet->cur_elapsed_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        quiet->cur_value = render(s);
        last_slow = step;
    };
    while (true) {
        S nxt = next(s);
        ++step;
        if (isInf(nxt)) {
            exit_reason = RunResult::OVERFLOW;
            if (quiet) { quiet->steps = step; quiet->reason = RunResult::OVERFLOW; quiet->cur_value = render(s); }
            break;
        }
        if (isZero(nxt)) {
            exit_reason = RunResult::UNDERFLOW;
            if (quiet) { quiet->steps = step; quiet->reason = RunResult::UNDERFLOW; quiet->cur_value = "0"; }
            break;
        }
        if (isNaN(nxt)) {
            exit_reason = RunResult::NOT_A_NUMBER;
            if (quiet) { quiet->steps = step; quiet->reason = RunResult::NOT_A_NUMBER; quiet->cur_value = render(s); }
            break;
        }
        if (!(nxt == s)) { s = nxt; ident_streak = 0; }
        else {
            if (++ident_streak >= IDENT_LIMIT) {
                exit_reason = RunResult::IDENTITY;
                if (quiet) { quiet->steps = step; quiet->reason = RunResult::IDENTITY; quiet->cur_value = render(s); }
                break;
            }
        }
        if (step >= SAFETY_LIMIT) {
            exit_reason = RunResult::SAFETY;
            if (quiet) { quiet->steps = step; quiet->reason = RunResult::SAFETY; }
            break;
        }
        // W2-A3: optional fixed-step cap (default 0 = disabled; never changes natural termination).
        if (g_fixed_steps > 0 && step >= g_fixed_steps) {
            exit_reason = RunResult::FIXED_STEPS;
            if (quiet) { quiet->steps = step; quiet->reason = RunResult::FIXED_STEPS; quiet->cur_value = render(s); }
            break;
        }
        if (g_dump_file && g_dump_records_enabled) {
            if (dump_cb) {
                dump_cb(s);
            } else {
                std::size_t sz = sizeof(S);
                if (g_dump_endian == 1) {
                    std::array<char, sizeof(S)> buf{};
                    for (std::size_t i = 0; i < sz; ++i) buf[i] = ((char*)&s)[sz - 1 - i];
                    std::fwrite(buf.data(), 1, sz, g_dump_file);
                } else {
                    std::fwrite(&s, 1, sz, g_dump_file);
                }
                g_dump_bytes_written += sz;
            }
        }
        if (quiet) {
            quiet->cur_steps = step;
            if (step - last_slow >= SLOW_INTERVAL) {
                slow_update();
                // W3-B5: collect wall-clock segment steps/s samples when --sample-window is on.
                if (g_sample_window_ms > 0) {
                    double ems = quiet->cur_elapsed_ms;
                    if (g_seg_prev_ms <= 0.0) {
                        g_seg_prev_ms = ems; g_seg_prev_steps = quiet->cur_steps;
                    } else if (ems - g_seg_prev_ms >= (double)g_sample_window_ms) {
                        double dt = (ems - g_seg_prev_ms) / 1000.0;
                        if (dt > 0.0)
                            g_rate_segments.push_back((double)(quiet->cur_steps - g_seg_prev_steps) / dt);
                        g_seg_prev_ms = ems; g_seg_prev_steps = quiet->cur_steps;
                    }
                }
                int cap = active_wall_cap();
                if (cap > 0) {
                    double elapsed_s = quiet->cur_elapsed_ms / 1000.0;
                    if (elapsed_s >= (double)cap) {
                        exit_reason = RunResult::TIMEOUT;
                        quiet->steps = step; quiet->reason = RunResult::TIMEOUT; break;
                    }
                }
            }
            if (delay_ms > 0) std::this_thread::sleep_for(std::chrono::duration<double, std::milli>(delay_ms));
            continue;
        }
        int cap = active_wall_cap();
        if (cap > 0) {
            double elapsed_s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            if (elapsed_s >= (double)cap) {
                finish_line("[step " + to_string_u128(step) + "] timeout (" + std::to_string(cap) + "s), stopping.");
                return;
            }
        }
        std::string line = render(s);
        if (extra) line += " (" + extra(s) + ")";
        std::string out = cui::dim("[step " + to_string_u128(step) + "]") + " x = " + line;
        auto now = std::chrono::steady_clock::now();
        bool due = (refresh_ms <= 0) ||
                   std::chrono::duration_cast<std::chrono::milliseconds>(now - last_print).count() >= refresh_ms;
        if (due) {
            std::cout << cui::CLEAR_LINE << out << std::flush;
            last_print = now;
        }
        if (delay_ms > 0) std::this_thread::sleep_for(std::chrono::duration<double, std::milli>(delay_ms));
    }
    if (!quiet) {
        auto t1 = std::chrono::steady_clock::now();
        double elapsed_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        std::string rn = reason_name(exit_reason);
        finish_line(cui::dim("[step " + to_string_u128(step) + "]") + " "
                    + cui::reason(rn, rn) + "  (" + std::to_string((int)elapsed_ms) + " ms)");
    }
    if (quiet) {
        // Do NOT call slow_update() here: it re-renders s and would overwrite the terminal value set
        // by the exit branch (e.g. UNDERFLOW -> "0"). Only refresh elapsed/counters below.
        auto t1 = std::chrono::steady_clock::now();
        quiet->elapsed_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        quiet->cur_steps = step;
        quiet->cur_elapsed_ms = quiet->elapsed_ms;
        quiet->done = true;
        if (g_events_file) {
            std::fprintf(g_events_file,
                "{\"event\":\"exit\",\"reason\":\"%s\",\"steps\":%s,\"value\":\"%s\",\"elapsed_ms\":%.3f}\n",
                reason_token(exit_reason), to_string_u128(quiet->steps).c_str(),
                json_escape(quiet->cur_value).c_str(), quiet->elapsed_ms);
            std::fflush(g_events_file);
        }
        // W3-B5: finalize throughput distribution over collected segments (min/avg/p95/max).
        if (g_sample_window_ms > 0 && !g_rate_segments.empty()) {
            std::sort(g_rate_segments.begin(), g_rate_segments.end());
            g_rate_n = (int)g_rate_segments.size();
            double sum = 0.0;
            for (double v : g_rate_segments) sum += v;
            g_rate_min = g_rate_segments.front();
            g_rate_max = g_rate_segments.back();
            g_rate_avg = sum / (double)g_rate_n;
            std::size_t idx = (95u * (unsigned)g_rate_n + 99u) / 100u;
            if (idx < 1) idx = 1;
            --idx;
            if (idx >= (std::size_t)g_rate_n) idx = (std::size_t)g_rate_n - 1;
            g_rate_p95 = g_rate_segments[idx];
        }
    }
}

template <class S, class Next, class IsInf, class IsZero, class IsNaN, class Disp>
void drive(S s, Next next, IsInf isInf, IsZero isZero, IsNaN isNaN, Disp disp,
           double delay_ms, DispFmt df, int digits,
           std::function<std::string(const S&)> extra = {}, RunResult* quiet = nullptr,
           std::int64_t refresh_ms = 0,
           std::function<void(const S&)> dump_cb = nullptr) {
    drive_core(s, next, isInf, isZero, isNaN,
               [&](const S& v) { return format_value(disp(v), df, digits); },
               delay_ms, extra, quiet, refresh_ms, dump_cb);
}

} // namespace fw
