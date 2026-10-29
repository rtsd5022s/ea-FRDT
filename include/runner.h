// runner.h — ALL concurrent mode: worker pool + live monitor + summary
#pragma once
#include "common.h"
#include "fp_formats.h"
#include "decimal.h"
#include "dpd.h"
#include "platform.h"
#include <ctime>
#ifdef _WIN32
#include <conio.h>
#endif

namespace fw {

// Terminal display width (CJK wide chars count as 2 columns)
inline int disp_width(const std::string& s) {
    int w = 0;
    for (size_t i = 0; i < s.size(); ) {
        unsigned char c = (unsigned char)s[i];
        if (c < 0x80) { w += 1; i += 1; }
        else if ((c >> 5) == 0x6 && i + 1 < s.size()) { w += 2; i += 2; }
        else if ((c >> 4) == 0xE && i + 2 < s.size()) { w += 2; i += 3; }
        else if ((c >> 3) == 0xF && i + 3 < s.size()) { w += 2; i += 4; }
        else { w += 1; i += 1; }
    }
    return w;
}
inline std::string rpad_to(const std::string& s, int width) {
    int w = disp_width(s);
    return w >= width ? s : s + std::string(width - w, ' ');
}
inline std::string lpad_to(const std::string& s, int width) {
    int w = disp_width(s);
    return w >= width ? s : std::string(width - w, ' ') + s;
}

inline std::string fmt_elapsed_wall(double ms) {
    long long s = (long long)(ms / 1000.0);
    long long frac = (long long)ms % 1000;
    std::string f3 = (frac<10?"00":frac<100?"0":"") + std::to_string(frac);
    if (s < 60)       return std::to_string(s) + "." + f3 + "s";
    if (s < 3600)     return std::to_string(s/60) + "m " + std::to_string(s%60) + "." + f3 + "s";
    if (s < 86400)    return std::to_string(s/3600) + "h " + std::to_string((s%3600)/60) + "m " + std::to_string(s%60) + "." + f3 + "s";
    if (s < 2592000)  return std::to_string(s/86400) + "d " + std::to_string((s%86400)/3600) + "h " + std::to_string((s%3600)/60) + "m " + std::to_string(s%60) + "s";
    if (s < 31536000) return std::to_string(s/2592000) + "M " + std::to_string((s%2592000)/86400) + "d " + std::to_string((s%86400)/3600) + "h " + std::to_string((s%3600)/60) + "m";
    return std::to_string(s/31536000) + "Y " + std::to_string((s%31536000)/2592000) + "M " + std::to_string((s%2592000)/86400) + "d " + std::to_string((s%86400)/3600) + "h";
}

inline std::vector<int> parse_jobs(const std::string& spec, int hw) {
    std::vector<int> out;
    if (spec.empty()) {
        int n = hw < 1 ? 1 : hw;
        for (int v = 1; v <= n; ++v) out.push_back(v);
        return out;
    }
    std::string rest = spec;
    while (!rest.empty()) {
        std::string tok;
        auto c = rest.find(',');
        if (c == std::string::npos) { tok = rest; rest.clear(); }
        else { tok = rest.substr(0, c); rest = rest.substr(c + 1); }
        size_t a = tok.find_first_not_of(" \t");
        size_t b = tok.find_last_not_of(" \t");
        if (a == std::string::npos) continue;
        tok = tok.substr(a, b - a + 1);
        auto dash = tok.find('-');
        try {
            if (dash != std::string::npos) {
                int lo = std::stoi(tok.substr(0, dash));
                int hi = std::stoi(tok.substr(dash + 1));
                if (lo > hi) std::swap(lo, hi);
                for (int v = lo; v <= hi; ++v) out.push_back(v);
            } else {
                out.push_back(std::stoi(tok));
            }
        } catch (...) {
            std::cerr << tr("错误：--jobs 无法解析 '", "error: --jobs cannot parse '") << sanitize(tok)
                      << tr("'（支持单值/逗号列表/范围）\n", "' (single/comma/range)\n");
            std::exit(2);
        }
    }
    out.erase(std::remove_if(out.begin(), out.end(), [hw](int v){ return v < 1 || v > hw; }), out.end());
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    if (out.empty()) {
        std::cerr << tr("错误：--jobs 未解析出有效核心编号（需 1~", "error: --jobs no valid core id (1~") << hw << ")\n";
        std::exit(2);
    }
    return out;
}

// Format ID (replaces std::function vector, avoids lambda capture chain)
enum class FmtId : int {
    FP4=0, FP6, FP8E4M3, FP8E5M2, FP16, BF16, TF32, FP32,
    Dec32, DPD32, FP64, Dec64, DPD64, X87, FP128, Dec128, DPD128,
    COUNT
};
inline const char* fmt_name(FmtId id) {
    switch (id) {
        case FmtId::FP4:      return "FP4";
        case FmtId::FP6:      return "FP6";
        case FmtId::FP8E4M3:  return "FP8(E4M3)";
        case FmtId::FP8E5M2:  return "FP8(E5M2)";
        case FmtId::FP16:     return "FP16";
        case FmtId::BF16:     return "BF16";
        case FmtId::TF32:     return "TF32";
        case FmtId::FP32:     return "FP32";
        case FmtId::Dec32:    return "Dec32[BID]";
        case FmtId::FP64:     return "FP64";
        case FmtId::Dec64:    return "Dec64[BID]";
        case FmtId::X87:      return "x87";
        case FmtId::FP128:    return "FP128";
        case FmtId::Dec128:   return "Dec128[BID]";
        case FmtId::DPD32:    return "DPD32";
        case FmtId::DPD64:    return "DPD64";
        case FmtId::DPD128:   return "DPD128";
        default:              return "?";
    }
}
inline FmtId fmt_canon(const std::string& in) {
    std::string t;
    for (char c : in) t += (char)std::tolower((unsigned char)c);
    if (t=="fp4") return FmtId::FP4;
    if (t=="fp6") return FmtId::FP6;
    if (t=="fp8"||t=="fp8-e4m3"||t=="e4m3") return FmtId::FP8E4M3;
    if (t=="fp8-e5m2"||t=="e5m2") return FmtId::FP8E5M2;
    if (t=="f16"||t=="fp16") return FmtId::FP16;
    if (t=="bf16") return FmtId::BF16;
    if (t=="tf32") return FmtId::TF32;
    if (t=="f32"||t=="fp32") return FmtId::FP32;
    if (t=="dec32"||t=="decimal32") return FmtId::Dec32;
    if (t=="f64"||t=="fp64") return FmtId::FP64;
    if (t=="dec64"||t=="decimal64") return FmtId::Dec64;
    if (t=="x87") return FmtId::X87;
    if (t=="fp128") return FmtId::FP128;
    if (t=="dec128"||t=="decimal128") return FmtId::Dec128;
    if (t=="dpd32"||t=="dpd-dec32") return FmtId::DPD32;
    if (t=="dpd64"||t=="dpd-dec64") return FmtId::DPD64;
    if (t=="dpd128"||t=="dpd-dec128") return FmtId::DPD128;
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

inline void run_format(FmtId id, const RunParams& p, RunResult* out) {
    static const DecFmt d32{7, 96, "Decimal32"};
    static const DecFmt d64{16, 384, "Decimal64"};
    switch (id) {
        case FmtId::FP4:      run_sim({2,1,1,"FP4"}, p.delay_ms, p.kind, p.sm, p.df, p.start_val, p.factor, p.digits, ShowBits::OFF, out); break;
        case FmtId::FP6:      run_sim({3,2,3,"FP6"}, p.delay_ms, p.kind, p.sm, p.df, p.start_val, p.factor, p.digits, ShowBits::OFF, out); break;
        case FmtId::FP8E4M3:  run_sim({4,3,7,"FP8"}, p.delay_ms, p.kind, p.sm, p.df, p.start_val, p.factor, p.digits, ShowBits::OFF, out); break;
        case FmtId::FP8E5M2:  run_sim({5,2,15,"FP8e5"}, p.delay_ms, p.kind, p.sm, p.df, p.start_val, p.factor, p.digits, ShowBits::OFF, out); break;
        case FmtId::FP16:     run_native<NativeF16>("FP16", p.delay_ms, p.kind, p.sm, p.df, p.start_val, p.factor, p.digits, ShowBits::OFF, out); break;
        case FmtId::BF16:     run_sim({8,7,127,"BF16"}, p.delay_ms, p.kind, p.sm, p.df, p.start_val, p.factor, p.digits, ShowBits::OFF, out); break;
        case FmtId::TF32:     run_sim({8,10,127,"TF32"}, p.delay_ms, p.kind, p.sm, p.df, p.start_val, p.factor, p.digits, ShowBits::OFF, out); break;
        case FmtId::FP32:     run_native<NativeF32>("FP32", p.delay_ms, p.kind, p.sm, p.df, p.start_val, p.factor, p.digits, ShowBits::OFF, out); break;
        case FmtId::Dec32:    run_dec(d32, p.delay_ms, p.kind, p.sm, p.df, p.start_val, p.factor, p.digits, ShowBits::OFF, out); break;
        case FmtId::FP64:     run_native<NativeF64>("FP64", p.delay_ms, p.kind, p.sm, p.df, p.start_val, p.factor, p.digits, ShowBits::OFF, out); break;
        case FmtId::Dec64:    run_dec(d64, p.delay_ms, p.kind, p.sm, p.df, p.start_val, p.factor, p.digits, ShowBits::OFF, out); break;
        case FmtId::X87:      run_x87_soft(p.delay_ms, p.kind, p.sm, p.df, p.start_val, p.factor, p.digits, ShowBits::OFF, out); break;
        case FmtId::FP128:    run_q128_soft(p.delay_ms, p.kind, p.sm, p.df, p.start_val, p.factor, p.digits, ShowBits::OFF, out); break;
        case FmtId::Dec128:   run_dec128(p.delay_ms, p.kind, p.sm, p.df, p.start_val, p.factor, p.digits, ShowBits::OFF, out); break;
        case FmtId::DPD32:    run_dpd(dpd_fmt32(), p.delay_ms, p.kind, p.sm, p.df, p.start_val, p.factor, p.digits, ShowBits::OFF, out, 0); break;
        case FmtId::DPD64:    run_dpd(dpd_fmt64(), p.delay_ms, p.kind, p.sm, p.df, p.start_val, p.factor, p.digits, ShowBits::OFF, out, 0); break;
        case FmtId::DPD128:   run_dpd(dpd_fmt128(), p.delay_ms, p.kind, p.sm, p.df, p.start_val, p.factor, p.digits, ShowBits::OFF, out, 0); break;
        default: break;
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
        std::string rest = only_csv;
        std::string errs;
        while (!rest.empty()) {
            std::string tok;
            auto comma = rest.find(',');
            if (comma == std::string::npos) { tok = rest; rest.clear(); }
            else { tok = rest.substr(0, comma); rest = rest.substr(comma + 1); }
            size_t a = tok.find_first_not_of(" \t");
            size_t b = tok.find_last_not_of(" \t");
            if (a == std::string::npos) continue;
            tok = tok.substr(a, b - a + 1);
            FmtId id = fmt_canon(tok);
            if (id == FmtId::COUNT) {
                if (!errs.empty()) errs += ", ";
                errs += tok;
                continue;
            }
            chain.push_back(id);
        }
        if (!errs.empty()) {
            std::cerr << tr("错误：--only 未知格式: ", "error: --only unknown: ") << sanitize(errs) << "\n";
            std::exit(2);
        }
    }
    if (chain.empty()) { std::cerr << tr("错误：--only 未选中格式\n", "error: --only empty\n"); std::exit(2); }

    // Warnings are confirmed in main() before dispatch (unified confirm_warnings)

    RunParams params{delay_ms, kind, sm, df, start_val, factor, digits};

    // Print CPU info
    auto t0 = std::chrono::steady_clock::now();
    int hw_threads = fw::plat::logical_cores();
    int phys_cores = fw::plat::physical_cores();
    if (!g_json) {
    std::cout << clr::CYN << tr("=== ALL 并发模式：每线程独立跑完整个格式链 ===", "=== ALL modes: each worker runs the full format chain independently ===") << clr::RST << "\n";
    std::cout << clr::GRY << tr("CPU 核心(逻辑)=", "CPU cores(logical)=") << hw_threads;
    if (phys_cores > 0 && phys_cores != hw_threads)
        std::cout << tr(" (物理=", " (physical=") << phys_cores << ")";
    std::cout << clr::RST << "\n";
    std::cout << clr::GRY << step_factor_str(sm, factor)
              << tr(", 限速=", ", delay=") << delay_ms << tr("ms (0=不限速)", "ms (0=unlimited)")
              << tr(", 线程=", ", workers=") << n
              << tr(", 格式数=", ", chain=") << chain.size() << clr::RST << "\n";
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
                // Global timeout check (across formats / across loops)
                if (g_timeout_sec.load() > 0) {
                    double elapsed_s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
                    if (elapsed_s >= (double)g_timeout_sec.load()) break;
                }
                workers[w].done.clear();
                for (std::size_t i = 0; i < ch.size(); ++i) {
                    workers[w].cur = RunResult{};
                    workers[w].cur.name = fmt_name(ch[i]);
                    run_format(ch[i], p, &workers[w].cur);
                    workers[w].done.push_back(workers[w].cur);
                    workers[w].total_steps += workers[w].cur.steps;
                    workers[w].sum_steps[i] += workers[w].cur.steps;
                }
            }
        });
    }

    // Monitor
    std::thread mon([&workers, n, &stop_mon, refresh_ms, quiet, loop_count, core_ids, t0] {
        int last_lines = 0;
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
            if (last_lines > 0) std::cout << "\033[" << last_lines << "A\r";
            int active = 0;
            double total_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            auto fmt_elapsed = [](double ms) -> std::string {
                long long s = (long long)(ms / 1000.0);
                long long frac = (long long)ms % 1000;
                std::string f3 = (frac<10?"00":frac<100?"0":"") + std::to_string(frac);
                if (s < 60)       return std::to_string(s) + "." + f3 + "s";
                if (s < 3600)     return std::to_string(s/60) + "m " + std::to_string(s%60) + "." + f3 + "s";
                if (s < 86400)    return std::to_string(s/3600) + "h " + std::to_string((s%3600)/60) + "m " + std::to_string(s%60) + "." + f3 + "s";
                if (s < 2592000)  return std::to_string(s/86400) + "d " + std::to_string((s%86400)/3600) + "h " + std::to_string((s%3600)/60) + "m " + std::to_string(s%60) + "s";
                if (s < 31536000) return std::to_string(s/2592000) + "M " + std::to_string((s%2592000)/86400) + "d " + std::to_string((s%86400)/3600) + "h " + std::to_string((s%3600)/60) + "m";
                return std::to_string(s/31536000) + "Y " + std::to_string((s%31536000)/2592000) + "M " + std::to_string((s%2592000)/86400) + "d " + std::to_string((s%86400)/3600) + "h";
            };
            double total_rate = 0.0;
            for (int w = 0; w < n; ++w) {
                const auto& c = workers[w].cur;
                if (c.done) continue;
                double rate = c.cur_elapsed_ms > 0.0 ? (double)c.cur_steps / (c.cur_elapsed_ms / 1000.0) : 0.0;
                total_rate += rate;
                std::cout << "\033[K"
                          << clr::GRN << "[w" << core_ids[w] << "] " << c.name
                          << ": " << (c.cur_value.empty() ? "..." : c.cur_value)
                          << clr::RST << "   " << clr::YEL << (std::uint64_t)rate << " steps/s" << clr::RST
                          << "   " << fmt_elapsed(total_ms);
                if (loop_count != 1) {
                    if (loop_count < 0) std::cout << "   loop=" << workers[w].loop_done;
                    else std::cout << "   loop=" << workers[w].loop_done << "/" << loop_count;
                }
                std::cout << "\n";
                ++active;
            }
            std::cout << "\033[K" << clr::YEL << "LIVE: " << (std::uint64_t)total_rate << " steps/s (current segment)" << clr::RST << "\n";
            if (active == 0) std::cout << "\033[K(all workers done)\n";
            for (int i = active + 1; i < last_lines; ++i) std::cout << "\033[K\n";
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
        u128 total_steps = 0;
        for (int w = 0; w < n; ++w) total_steps += workers[w].total_steps;
        double overall_rate = wall_ms > 0.0 ? (double)total_steps / (wall_ms / 1000.0) : 0.0;
        std::printf("\"total_steps\":%s,\"overall_rate\":%.1f,\"workers\":[",
                    to_string_u128(total_steps).c_str(), overall_rate);
        for (int w = 0; w < n; ++w) {
            if (w > 0) std::printf(",");
            std::printf("{\"core\":%d,\"formats\":[", core_ids[w]);
            for (std::size_t i = 0; i < workers[w].done.size(); ++i) {
                if (i > 0) std::printf(",");
                const auto& r = workers[w].done[i];
                const char* rn = "unknown";
                switch (r.reason) {
                    case RunResult::OVERFLOW:  rn = "overflow"; break;
                    case RunResult::UNDERFLOW: rn = "underflow"; break;
                    case RunResult::IDENTITY:  rn = "identity"; break;
                    case RunResult::SAFETY:    rn = "safety"; break;
                    case RunResult::TIMEOUT:   rn = "timeout"; break;
                    case RunResult::NOT_A_NUMBER: rn = "nan"; break;
                }
                std::printf("{\"name\":\"%s\",\"steps\":%s,\"elapsed_ms\":%.1f,\"reason\":\"%s\",\"sum_steps\":%s}",
                            r.name.c_str(),
                            to_string_u128(r.steps).c_str(),
                            r.elapsed_ms, rn,
                            to_string_u128(workers[w].sum_steps[i]).c_str());
            }
            std::printf("]}");
        }
        std::printf("]}\n");
        return wall_ms;
    }

    std::cout << '\n';

    // Summary table
    for (int w = 0; w < n; ++w) {
        std::cout << clr::CYN << tr("--- worker ", "--- worker ") << core_ids[w] << tr(" ---", " ---") << clr::RST << "\n";
        std::cout << clr::BLU;
        std::cout << rpad_to(tr("格式","Format"),12) << "  "
                  << lpad_to(tr("单次步数","loop_steps"),14) << "  "
                  << lpad_to(tr("累计步数","all_steps"),14) << "  "
                  << rpad_to(tr("退出","exit"),16) << " "
                  << lpad_to(tr("耗时ms","elapsed_ms"),12) << "  "
                  << rpad_to(tr("总耗时","total"),10) << "\n";
        std::cout << clr::RST;
        for (std::size_t i = 0; i < workers[w].done.size(); ++i) {
            const auto& r = workers[w].done[i];
            char elbuf[32]; std::snprintf(elbuf, sizeof(elbuf), "%.1f", r.elapsed_ms);
            std::cout << rpad_to(r.name,12) << "  "
                      << lpad_to(to_string_u128(r.steps),14) << "  "
                      << lpad_to(to_string_u128(workers[w].sum_steps[i]),14) << "  "
                      << rpad_to(reason_name(r.reason),16) << " "
                      << lpad_to(elbuf,12) << "  "
                      << rpad_to(fmt_elapsed_wall(wall_ms),10) << "\n";
        }
    }
    u128 total_steps = 0;
    for (int w = 0; w < n; ++w) total_steps += workers[w].total_steps;
    double overall_rate = wall_ms > 0.0 ? (double)total_steps / (wall_ms / 1000.0) : 0.0;
    std::cout << "\n" << clr::YEL << tr("总计: ", "TOTAL: ") << to_string_u128(total_steps)
              << tr(" 步, 墙钟耗时 ", " steps in wall ") << wall_ms << tr("ms = ", " ms = ")
              << (unsigned long long)overall_rate << tr(" 步/秒", " steps/s") << clr::RST << "\n";
    return wall_ms;
}

} // namespace fw
