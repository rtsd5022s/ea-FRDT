// drive.h — Core stepping driver template interface
#pragma once
#include <chrono>
#include <cstdio>
#include <functional>
#include <iostream>
#include <string>
#include <thread>
#include "base_types.h"
#include "lang.h"
#include "format_util.h"

namespace fw {

extern const int CLEAR;

void on_sigint(int);
const char* step_name(StepMode sm);
std::string step_factor_str(StepMode sm, double factor);
const char* reason_name(RunResult::Reason r);

// Template driver (must stay in header for explicit instantiation)
template <class S, class Next, class IsInf, class IsZero, class IsNaN, class Render>
void drive_core(S s, Next next, IsInf isInf, IsZero isZero, IsNaN isNaN, Render render,
                double delay_ms,
                std::function<std::string(const S&)> extra = {},
                RunResult* quiet = nullptr,
                std::int64_t refresh_ms = 0,
                std::function<void(const S&)> dump_cb = nullptr) {
    static const u128 SAFETY_LIMIT = ((u128)1 << 120);
    constexpr size_t INPLACE_MAX = 110;
    constexpr u128 SLOW_INTERVAL = 256;
    u128 step = 0;
    u128 last_slow = 0;
    int ident_streak = 0;
    constexpr int IDENT_LIMIT = 16;
    auto t0 = std::chrono::steady_clock::now();
    auto last_print = t0;
    RunResult::Reason exit_reason = RunResult::OVERFLOW;
    auto finish_line = [&](const std::string& msg) {
        if (quiet) return;
        std::cout << '\r' << "\033[K" << msg << '\n' << std::flush;
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
        if (g_dump_file) {
            if (dump_cb) {
                dump_cb(s);
            } else {
                std::size_t sz = sizeof(S);
                if (g_dump_endian == 1) {
                    char buf[16];
                    for (std::size_t i = 0; i < sz; ++i) buf[i] = ((char*)&s)[sz - 1 - i];
                    std::fwrite(buf, 1, sz, g_dump_file);
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
                if (g_timeout_sec.load() > 0) {
                    double elapsed_s = quiet->cur_elapsed_ms / 1000.0;
                    if (elapsed_s >= (double)g_timeout_sec.load()) {
                        exit_reason = RunResult::TIMEOUT;
                        quiet->steps = step; quiet->reason = RunResult::TIMEOUT; break;
                    }
                }
            }
            if (delay_ms > 0) std::this_thread::sleep_for(std::chrono::duration<double, std::milli>(delay_ms));
            continue;
        }
        if (g_timeout_sec.load() > 0) {
            double elapsed_s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            if (elapsed_s >= (double)g_timeout_sec.load()) {
                finish_line("[step " + to_string_u128(step) + "] timeout (" + std::to_string(g_timeout_sec.load()) + "s), stopping.");
                return;
            }
        }
        std::string line = render(s);
        if (extra) line += " (" + extra(s) + ")";
        std::string out = "[step " + to_string_u128(step) + "] x = " + line;
        auto now = std::chrono::steady_clock::now();
        bool due = (refresh_ms <= 0) ||
                   std::chrono::duration_cast<std::chrono::milliseconds>(now - last_print).count() >= refresh_ms;
        if (due) {
            if (out.size() <= INPLACE_MAX) std::cout << '\r' << "\033[K" << out << std::flush;
            else std::cout << '\r' << "\033[K" << out << '\n' << std::flush;
            last_print = now;
        }
        if (delay_ms > 0) std::this_thread::sleep_for(std::chrono::duration<double, std::milli>(delay_ms));
    }
    if (!quiet) {
        auto t1 = std::chrono::steady_clock::now();
        double elapsed_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        finish_line("[step " + to_string_u128(step) + "] " + reason_name(exit_reason)
                    + "  (" + std::to_string((int)elapsed_ms) + " ms)");
    }
    if (quiet) {
        slow_update();
        auto t1 = std::chrono::steady_clock::now();
        quiet->elapsed_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        quiet->cur_steps = step;
        quiet->cur_elapsed_ms = quiet->elapsed_ms;
        quiet->done = true;
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
