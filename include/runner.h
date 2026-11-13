// runner.h — ALL concurrent mode: worker pool + live monitor + summary
#pragma once
#if defined(_WIN32) && !defined(__MINGW32__)
#error "ea-FRDT requires MinGW target (x86_64-w64-mingw32 / x86_64-w64-windows-gnu). Do not use MSVC or LLVM's default windows-msvc clang; use MSYS2 clang64."
#endif
#include "common.h"
#include "cui.h"
#include "formats/formats_all.h"
#include "platform.h"
#include <array>
#include <cctype>
#include <ctime>
#include <string_view>
#ifdef _WIN32
#include <conio.h>
#endif

namespace fw {

// All terminal presentation lives in cui.h (width, rules, tables, formatters).

// Comma list -> trimmed tokens. Empty pieces are dropped, so "a,,b" and " a , b "
// read the same as "a,b"; --jobs and the -m all chain both parse through this.
[[nodiscard]] inline std::vector<std::string_view> csv_tokens(std::string_view spec) {
    std::vector<std::string_view> out;
    while (!spec.empty()) {
        auto comma = spec.find(',');
        std::string_view tok = (comma == std::string_view::npos)
                             ? spec
                             : spec.substr(0, comma);
        spec.remove_prefix(tok.size());
        if (!spec.empty()) spec.remove_prefix(1);  // the comma
        auto a = tok.find_first_not_of(" \t");
        if (a == std::string_view::npos) continue;
        auto b = tok.find_last_not_of(" \t");
        out.push_back(tok.substr(a, b - a + 1));
    }
    return out;
}

inline std::vector<int> parse_jobs(std::string_view spec, int hw) {
    std::vector<int> out;
    if (spec.empty()) {
        int n = hw < 1 ? 1 : hw;
        for (int v = 0; v < n; ++v) out.push_back(v);
        return out;
    }
    for (std::string_view tok : csv_tokens(spec)) {
        auto dash = tok.find('-');
        try {
            if (dash != std::string_view::npos) {
                int lo = std::stoi(std::string(tok.substr(0, dash)));
                int hi = std::stoi(std::string(tok.substr(dash + 1)));
                if (lo > hi) std::swap(lo, hi);
                for (int v = lo; v <= hi; ++v) out.push_back(v);
            } else {
                out.push_back(std::stoi(std::string(tok)));
            }
        } catch (...) {
            cui::err() << tr("错误：--jobs 无法解析 \'", "error: --jobs cannot parse \'") << sanitize(std::string(tok))
                      << tr("\'（支持单值/逗号列表/范围）\n", "\' (single/comma/range)\n");
            std::exit(2);
        }
    }
    out.erase(std::remove_if(out.begin(), out.end(), [hw](int v){ return v < 0 || v >= hw; }), out.end());
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    if (out.empty()) {
        cui::err() << tr("错误：--jobs 未解析出有效核心编号（需 0~", "error: --jobs no valid core id (0~") << (hw-1) << ")\n";
        std::exit(2);
    }
    return out;
}

// ---- format registry -------------------------------------------------------
//
// One row per format is the only place a format's display name, its CLI spellings
// and its engine entry point are declared; fmt_name / fmt_canon / run_format all
// read this table instead of three hand-kept switches that could drift apart.
// FmtId order is load-bearing: the default -m all chain walks 0..COUNT and skips
// DPD/Dec128, and worker tables print in this order.
enum class FmtId : int {
    FP4=0, FP6, FP8E4M3, FP8E5M2, FP16, BF16, TF32, FP32,
    Dec32, DPD32, FP64, Dec64, DPD64, X87, FP128, Dec128, DPD128,
    COUNT
};

using Runner = void (*)(double delay_ms, StartKind kind, StepMode sm, DispFmt df,
                        double start_val, double factor, int digits,
                        ShowBits showbits, RunResult* quiet,
                        std::int64_t refresh_ms);

struct FmtSpec {
    FmtId            id;
    const char*      name;          // display name, exactly as printed in tables
    int              bytes;         // natural serialized record width for --dump
    std::string_view aliases[3];    // accepted CLI spellings; empty slot = unused
    Runner           run;           // engine entry point
};

inline constexpr std::array<FmtSpec, 17> kFormats{{
    {FmtId::FP4, "FP4",  1,          {"fp4",                "",                    ""},                  run_fp4},
    {FmtId::FP6, "FP6",  1,          {"fp6",                "",                    ""},                  run_fp6},
    {FmtId::FP8E4M3, "FP8(E4M3)",  1,    {"fp8",                "fp8-e4m3",            "e4m3"},              run_fp8_e4m3},
    {FmtId::FP8E5M2, "FP8(E5M2)",  1,    {"fp8-e5m2",           "e5m2",                ""},                  run_fp8_e5m2},
    {FmtId::FP16, "FP16",  2,         {"f16",                "fp16",                ""},                  run_fp16},
    {FmtId::BF16, "BF16",  2,         {"bf16",               "",                    ""},                  run_bf16},
    {FmtId::TF32, "TF32",  4,         {"tf32",               "",                    ""},                  run_tf32},
    {FmtId::FP32, "FP32",  4,         {"f32",                "fp32",                ""},                  run_fp32},
    {FmtId::Dec32, "Dec32[BID]",  4,   {"dec32",              "decimal32",           ""},                  run_dec32},
    {FmtId::DPD32, "DPD32",  4,        {"dpd32",              "dpd-dec32",           ""},                  run_dpd32},
    {FmtId::FP64, "FP64",  8,         {"f64",                "fp64",                ""},                  run_fp64},
    {FmtId::Dec64, "Dec64[BID]",  8,   {"dec64",              "decimal64",           ""},                  run_dec64},
    {FmtId::DPD64, "DPD64",  8,        {"dpd64",              "dpd-dec64",           ""},                  run_dpd64},
    {FmtId::X87, "x87", 16,          {"x87",                "",                    ""},                  run_x87},
    {FmtId::FP128, "FP128", 16,        {"fp128",              "",                    ""},                  run_fp128},
    {FmtId::Dec128, "Dec128[BID]", 16,  {"dec128",             "decimal128",          ""},                  run_dec128},
    {FmtId::DPD128, "DPD128", 16,       {"dpd128",             "dpd-dec128",          ""},                  run_dpd128},
}};

[[nodiscard]] inline const FmtSpec* fmt_spec(FmtId id) noexcept {
    for (const auto& f : kFormats) if (f.id == id) return &f;
    return nullptr;
}

[[nodiscard]] inline int fmt_record_bytes(FmtId id) noexcept {
    const FmtSpec* f = fmt_spec(id);
    return f ? f->bytes : 0;
}

[[nodiscard]] inline std::string_view fmt_name(FmtId id) noexcept {
    for (const auto& f : kFormats) if (f.id == id) return f.name;
    return "?";
}

[[nodiscard]] inline FmtId fmt_canon(std::string_view in) noexcept {
    std::string t;
    t.reserve(in.size());
    for (char c : in) t += (char)std::tolower((unsigned char)c);
    for (const auto& f : kFormats)
        for (const auto& a : f.aliases)
            if (!a.empty() && t == a) return f.id;
    return FmtId::COUNT;
}

// All params packed into POD, worker thread value-copies, no reference to run_all stack frame
struct RunParams {
    double delay_ms;
    StartKind kind;
    StepMode sm;
    DispFmt df;
    double start_val;
    double factor;
    int digits;
};

inline void run_format(FmtId id, const RunParams& p, RunResult* out,
                       ShowBits showbits = ShowBits::OFF, std::int64_t refresh_ms = 0) {
    if (const FmtSpec* f = fmt_spec(id)) {
        f->run(p.delay_ms, p.kind, p.sm, p.df, p.start_val, p.factor, p.digits,
               showbits, out, refresh_ms);
    }
}

inline double run_all(double delay_ms, StartKind kind, StepMode sm, DispFmt df,
             double start_val, double factor, int digits, std::vector<int> core_ids,
             const std::string& only_csv = {}, std::int64_t refresh_ms = 0, int loop_count = 1, bool quiet = false) {
    int n = (int)core_ids.size();
    if (n < 1) n = 1;

    // Default full chain (excludes DPD32/64/128 and Dec128, require --only to add)
    std::vector<FmtId> chain;
    if (only_csv.empty()) {
        for (int i = 0; i < (int)FmtId::COUNT; ++i) {
            FmtId id = (FmtId)i;
            if (id == FmtId::DPD32 || id == FmtId::DPD64 || id == FmtId::DPD128) continue;
            if (id == FmtId::Dec128) continue;
            chain.push_back(id);
        }
    } else {
        std::string errs;
        for (std::string_view tok : csv_tokens(only_csv)) {
            FmtId id = fmt_canon(tok);
            if (id == FmtId::COUNT) {
                if (!errs.empty()) errs += ", ";
                errs += std::string(tok);
                continue;
            }
            chain.push_back(id);
        }
        if (!errs.empty()) {
            cui::err() << tr("错误：--only 未知格式: ", "error: --only unknown: ") << sanitize(errs) << "\n";
            std::exit(2);
        }
    }
    if (chain.empty()) { cui::err() << tr("错误：--only 未选中格式\n", "error: --only empty\n"); std::exit(2); }

    // Warnings are confirmed in main() before dispatch (unified confirm_warnings)

    RunParams params{delay_ms, kind, sm, df, start_val, factor, digits};

    // Print CPU info
    auto t0 = std::chrono::steady_clock::now();
    int hw_threads = fw::plat::logical_cores();
    int phys_cores = fw::plat::physical_cores();
    if (!g_json) {
        using namespace cui;
        std::cout << "\n" << section(tr("ea-FRDT · ALL 并发模式", "ea-FRDT · ALL concurrent"),
                                     tr("每线程独立跑完整个格式链", "each worker runs the full format chain independently"));
        std::cout << kv("CPU", fw::plat::cpu_brand(), 8);
        std::string cores = std::to_string(hw_threads) + tr(" 逻辑", " logical");
        if (phys_cores > 0 && phys_cores != hw_threads)
            cores += " / " + std::to_string(phys_cores) + tr(" 物理", " physical");
        std::cout << kv(tr("核心", "cores"), cores, 8);
        std::string sf = step_factor_str(sm, factor);
        std::size_t step_pos = sf.find("step=");
        if (step_pos != std::string::npos) sf.erase(step_pos, 5);
        std::cout << kv(tr("步进", "step"), sf, 8);
        std::cout << kv(tr("限速", "delay"),
                        std::to_string((long long)delay_ms) + tr(" ms/步 (0=不限速)", " ms/step (0=unlimited)"), 8);
        std::cout << kv(tr("线程", "workers"), std::to_string(n) + tr(" 个（已绑核）", " (core-pinned)"), 8);
        std::cout << kv(tr("格式链", "chain"), std::to_string(chain.size())
                        + tr(" 个格式", " formats"), 8);
        std::cout << "\n";
    }

    // Per-worker independent state, value-copied params, no reference to stack vector
    struct alignas(64) WorkerSlot {
        RunResult cur;
        std::vector<RunResult> done;
        int loop_done = 0;
        u128 total_steps = 0;
        std::vector<u128> sum_steps;
        char pad[64];
    };
    static_assert(sizeof(WorkerSlot) % 64 == 0, "WorkerSlot cache-line aligned");
    std::vector<WorkerSlot> workers(n);
    std::atomic<bool> stop_mon{false};

    // Worker entry point: all params value-copied
    std::vector<std::thread> pool;
    pool.reserve(n);
    for (int w = 0; w < n; ++w) {
        int core_id = core_ids[w];
        RunParams p = params;           // copy
        std::vector<FmtId> ch = chain;  // copy
        pool.emplace_back([&workers, w, core_id, p, ch, loop_count, t0] {
            fw::plat::bind_thread_to_core(core_id);
            workers[w].sum_steps.assign(ch.size(), 0);
            int loops = 0;
            bool infinite = (loop_count < 0);
            while (infinite || loops < loop_count) {
                ++loops;
                workers[w].loop_done = loops;
                // Global wall-clock check (across formats / across loops): either -t or
                // --max-seconds fires; smallest positive cap wins. Maps to TIMEOUT on exit.
                {
                    int cap = -1;
                    if (g_timeout_sec.load() > 0) cap = g_timeout_sec.load();
                    int mx = g_max_seconds.load();
                    if (mx > 0 && (cap < 0 || mx < cap)) cap = mx;
                    if (cap > 0) {
                        double elapsed_s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
                        if (elapsed_s >= (double)cap) break;
                    }
                }
                workers[w].done.clear();
                for (std::size_t i = 0; i < ch.size(); ++i) {
                    workers[w].cur = RunResult{};
                    workers[w].cur.name = std::string(fmt_name(ch[i]));
                    run_format(ch[i], p, &workers[w].cur);
                    workers[w].done.push_back(workers[w].cur);
                    workers[w].total_steps += workers[w].cur.steps;
                    workers[w].sum_steps[i] += workers[w].cur.steps;
                }
            }
            // Release this worker's TIME_CRITICAL priority and pinned affinity before
            // the thread exits, so no boosted worker lingers after the chain finishes.
            fw::plat::restore_thread_scheduling();
        });
    }

    // Monitor
    std::thread mon([&workers, n, &stop_mon, refresh_ms, quiet, loop_count, core_ids, t0] {
        int last_lines = 0;
        bool header_done = false;
        std::int64_t mon_ms = refresh_ms > 0 ? refresh_ms : 200;
        while (true) {
            std::int64_t remain = mon_ms;
            while (remain > 0 && !stop_mon) {
                std::int64_t chunk = remain < 50 ? remain : 50;
                std::this_thread::sleep_for(std::chrono::milliseconds(chunk));
                remain -= chunk;
            }
            if (stop_mon) break;
            if (quiet) continue;
            // The live header is printed lazily on the first frame, so a run that ends
            // before the first refresh leaves no orphan header behind.
            if (!header_done) {
                std::cout << "  " << cui::label(cui::pad_r(tr("核", "core"), 4)) << " " << cui::dim("│") << " "
                          << cui::label(cui::pad_r(tr("格式", "format"), 12)) << " " << cui::dim("│") << " "
                          << cui::label(cui::pad_l(tr("当前值", "value"), 22)) << " " << cui::dim("│") << " "
                          << cui::label(cui::pad_l(tr("步/秒", "steps/s"), 7)) << " " << cui::dim("│") << " "
                          << cui::label(cui::pad_l(tr("已用", "elapsed"), 9)) << "\n";
                header_done = true;
            }
            if (last_lines > 0) std::cout << cui::up(last_lines);
            int active = 0;
            double total_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            double total_rate = 0.0;
            for (int w = 0; w < n; ++w) {
                const auto& c = workers[w].cur;
                if (c.done) continue;
                double rate = c.cur_elapsed_ms > 0.0 ? (double)c.cur_steps / (c.cur_elapsed_ms / 1000.0) : 0.0;
                total_rate += rate;
                std::cout << cui::CLEAR << "  "
                          << cui::accent(cui::pad_r("w" + std::to_string(core_ids[w]), 4)) << " " << cui::dim("│") << " "
                          << cui::pad_r(cui::clip(c.name, 12), 12) << " " << cui::dim("│") << " "
                          << cui::num(cui::pad_l(cui::clip(c.cur_value.empty() ? "..." : c.cur_value, 22), 22)) << " " << cui::dim("│") << " "
                          << cui::num(cui::pad_l(cui::rate(rate), 7)) << " " << cui::dim("│") << " "
                          << cui::dim(cui::pad_l(cui::elapsed(total_ms), 9));
                if (loop_count != 1) {
                    std::cout << " " << cui::dim("loop=" + std::to_string(workers[w].loop_done)
                                                + (loop_count < 0 ? std::string() : "/" + std::to_string(loop_count)));
                }
                std::cout << "\n";
                ++active;
            }
            std::cout << cui::CLEAR << "  " << cui::warn(cui::pad_r("LIVE", 4)) << " " << cui::dim("│") << " "
                      << cui::dim(cui::pad_r(tr("当前段合计", "segment total"), 12)) << " " << cui::dim("│") << " "
                      << cui::num(cui::pad_l("-", 22)) << " " << cui::dim("│") << " "
                      << cui::num(cui::pad_l(cui::rate(total_rate), 7)) << " " << cui::dim("│") << " "
                      << cui::dim(cui::pad_l(cui::elapsed(total_ms), 9)) << "\n";
            if (active == 0) std::cout << cui::CLEAR << cui::dim(tr("(全部线程已完成)", "(all workers done)")) << "\n";
            for (int i = active + 1; i < last_lines; ++i) std::cout << cui::CLEAR << "\n";
            last_lines = active > 0 ? active + 1 : 2;
            std::cout << std::flush;
        }
    });

    for (auto& t : pool) t.join();
    stop_mon = true;
    mon.join();
    auto t1 = std::chrono::steady_clock::now();
    double wall_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

    if (g_json) {
        // JSON summary: suppress colored table, emit machine-readable JSON
        std::printf("{\"tool\":\"ea-FRDT\",\"version\":\"1.0\",\"mode\":\"all\",\"wall_ms\":%.3f,", wall_ms);
#ifdef __clang__
        const char* cc_name = "clang";
#elif defined(__GNUC__)
        const char* cc_name = "gcc";
#else
        const char* cc_name = "unknown";
#endif
        std::printf("\"system\":{\"hardware_threads\":%u,\"physical_threads\":%d,\"hires_timer\":%s,\"affinity\":%s,\"compiler\":\"%s\",\"cpp_std\":23},",
                    hw_threads, phys_cores,
                    fw::plat::hires_timer_effective() ? "true" : "false",
                    fw::plat::affinity_available() ? "true" : "false",
                    cc_name);
        u128 total_steps = 0;
        for (int w = 0; w < n; ++w) total_steps += workers[w].total_steps;
        double overall_rate = wall_ms > 0.0 ? (double)total_steps / (wall_ms / 1000.0) : 0.0;
        auto reason_json = [](RunResult::Reason rr) -> const char* {
            switch (rr) {
                case RunResult::OVERFLOW:     return "overflow";
                case RunResult::UNDERFLOW:    return "underflow";
                case RunResult::IDENTITY:     return "identity";
                case RunResult::SAFETY:       return "safety";
                case RunResult::TIMEOUT:      return "timeout";
                case RunResult::NOT_A_NUMBER: return "nan";
                case RunResult::FIXED_STEPS:  return "fixed";
            }
            return "unknown";
        };
        // Per-format aggregate across workers (same values the tables print, so the
        // JSON and the human view cannot drift).
        std::printf("\"total_steps\":%s,\"overall_rate\":%.1f,\"formats\":[",
                    to_string_u128(total_steps).c_str(), overall_rate);
        for (std::size_t i = 0; i < workers[0].done.size(); ++i) {
            if (i > 0) std::printf(",");
            const auto& r0 = workers[0].done[i];
            double emin = r0.elapsed_ms, emax = r0.elapsed_ms;
            bool consistent = true;
            for (int w = 1; w < n; ++w) {
                if (i >= workers[w].done.size()) { consistent = false; continue; }
                const auto& r = workers[w].done[i];
                if (!(r.steps == r0.steps) || r.reason != r0.reason) consistent = false;
                if (r.elapsed_ms < emin) emin = r.elapsed_ms;
                if (r.elapsed_ms > emax) emax = r.elapsed_ms;
            }
            std::printf("{\"name\":\"%s\",\"steps\":%s,\"reason\":\"%s\","
                        "\"elapsed_min_ms\":%.1f,\"elapsed_max_ms\":%.1f,"
                        "\"steps_consistent\":%s,\"start_is_fixed_point\":%s}",
                        r0.name.c_str(), to_string_u128(r0.steps).c_str(), reason_json(r0.reason),
                        emin, emax,
                        consistent ? "true" : "false",
                        (r0.reason == RunResult::IDENTITY) ? "true" : "false");
        }
        std::printf("],\"workers\":[");
        for (int w = 0; w < n; ++w) {
            if (w > 0) std::printf(",");
            std::printf("{\"core\":%d,\"formats\":[", core_ids[w]);
            for (std::size_t i = 0; i < workers[w].done.size(); ++i) {
                if (i > 0) std::printf(",");
                const auto& r = workers[w].done[i];
                std::printf("{\"name\":\"%s\",\"steps\":%s,\"elapsed_ms\":%.1f,\"reason\":\"%s\",\"sum_steps\":%s}",
                            r.name.c_str(),
                            to_string_u128(r.steps).c_str(),
                            r.elapsed_ms, reason_json(r.reason),
                            to_string_u128(workers[w].sum_steps[i]).c_str());
            }
            std::printf("]}");
        }
        std::printf("]}\n");
        return wall_ms;
    }

    std::cout << "\n";

    // Summary: one boxed table per worker. The wall clock is a per-worker constant,
    // so it lives in the section subtitle instead of repeating on every row.
    bool any_identity = false;
    for (int w = 0; w < n; ++w) {
        std::cout << cui::section(tr("worker ", "worker ") + std::to_string(core_ids[w]),
                                  tr("总耗时 ", "total ") + cui::elapsed(wall_ms));
        cui::Table t;
        t.col(tr("格式", "Format"), false);
        t.col(tr("单次步数", "loop_steps"), true);
        t.col(tr("累计步数", "all_steps"), true);
        t.col(tr("退出", "exit"), false);
        t.col(tr("耗时ms", "elapsed_ms"), true);
        for (std::size_t i = 0; i < workers[w].done.size(); ++i) {
            const auto& r = workers[w].done[i];
            char elbuf[32];
            std::snprintf(elbuf, sizeof(elbuf), "%.1f", r.elapsed_ms);
            std::string exit_txt = reason_name(r.reason);
            if (r.reason == RunResult::IDENTITY) { exit_txt += "†"; any_identity = true; }
            t.row({r.name,
                   cui::count(r.steps),
                   cui::count(workers[w].sum_steps[i]),
                   exit_txt,
                   elbuf});
        }
        t.print(std::cout);
        if (w + 1 < n) std::cout << "\n";
    }
    if (any_identity)
        std::cout << cui::dim(tr("† 起点即不动点：1.0 × 1.02 在该格式上舍入回自身，连续 16 步无变化后判停（非故障）",
                                 "† start is a fixed point: 1.0 × 1.02 rounds back to itself in that format, so the run stops after 16 unchanged steps (not a fault)"))
                  << "\n";
    u128 total_steps = 0;
    for (int w = 0; w < n; ++w) total_steps += workers[w].total_steps;
    double overall_rate = wall_ms > 0.0 ? (double)total_steps / (wall_ms / 1000.0) : 0.0;
    std::cout << "\n" << cui::num(tr("总计 ", "TOTAL "))
              << cui::num(cui::count(total_steps)) << tr(" 步", " steps")
              << cui::dim("  ·  ") << std::to_string(n) << tr(" worker × ", " workers × ")
              << std::to_string(chain.size()) << tr(" 个格式", " formats")
              << cui::dim("  ·  ") << tr("墙钟 ", "wall ")
              << cui::num(cui::elapsed(wall_ms))
              << cui::dim("  ·  ") << cui::num(cui::rate(overall_rate)) << tr(" 步/秒", " steps/s") << "\n";
    return wall_ms;
}

} // namespace fw
