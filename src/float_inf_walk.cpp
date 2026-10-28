// float_inf_walk.cpp — Overflow walk: grow from 1.0 step-by-step until overflow to +inf
#include "common.h"
#include "fp_formats.h"
#include "decimal.h"
#include "runner.h"
#ifdef _WIN32
#include <windows.h>
#include <conio.h>
#include <io.h>
#include <cstdio>
#include <cmath>
#endif

using namespace fw;

static void print_help() {
    if (g_lang == Lang::EN) {
    std::cout <<
"\033[36m╔══════════════════════════════════════════════════════════════╗\n"
"║   Float Inf Walk — 14-format float overflow walker (C++23)   ║\n"
"╚══════════════════════════════════════════════════════════════╝\033[0m\n"
"\n"
"\033[33mUsage:\033[0m\n"
"  float_inf_walk.exe [options] <delay_ms>\n"
"  float_inf_walk.exe -h | --help\n"
"\n"
"\033[33mModes -m (default f32):\033[0m\n"
"  \033[32mNative\033[0m       f16  f32(default)  f64\n"
"  \033[32mSoftware-bit\033[0m fp4(E2M1)  fp6(E3M2)  fp8-e4m3  fp8-e5m2  bf16  tf32\n"
"  \033[32mPure software\033[0m x87  fp128\n"
"  \033[32mDecimal\033[0m      dec32\033[0m[\033[36mBID\033[0m]  dec64\033[0m[\033[36mBID\033[0m]  dec128\033[0m[\033[36mBID\033[0m]\n"
"                  dpd32\033[0m[\033[36mDPD\033[0m]  dpd64\033[0m[\033[36mDPD\033[0m]  dpd128\033[0m[\033[36mDPD\033[0m]\n"
"  \033[32mall\033[0m        run all formats concurrently, independent state per thread\n"
"\n"
"\033[33mPositional:\033[0m\n"
"  delay_ms        delay per step; 0=unlimited. No arg = show this help.\n"
"\n"
"\033[33mCore options:\033[0m\n"
"  -a, --start <x>    start value (default 1). supports negatives, num/sci/eng/0x Raw/min-normal/max-finite\n"
"  -k, --factor <r>   multiplier per step (default 1.02). >1 to +inf, 0<r<1 to 0, supports sci notation\n"
"  -g, --step <s>     x1.02(default) / mululp(x1+1ULP) / addulp(+1ULP)\n"
"  -m, --mode <m>     float format (see modes above)\n"
"\n"
"\033[33mDisplay:\033[0m\n"
"  -f, --fmt <f>      eng(default) / sci / full\n"
"  -d, --digits <n>   sci/eng decimals (default 6, -1=full, 0~100)\n"
"  --bits <m>         hex / bin / off(default), append bit repr in parens\n"
"  -q, --quiet        quiet: skip live monitor, print summary directly\n"
"  -lang <zh|en>      UI language (default zh)\n"
"\n"
"\033[33mConcurrency (-m all):\033[0m\n"
"  -j, --jobs <spec>  core ids: single(4) / comma(1,3) / range(1-3) / mixed\n"
"  --only <list>      comma-separated format subset (default all 17)\n"
"  -r, --refresh <ms> refresh interval, 0=every step; -m all default 1000\n"
"\n"
"\033[33mControl:\033[0m\n"
"  -t, --timeout <s>  timeout seconds, 0=until inf/underflow (default)\n"
"  -l, --loop <n>     loops: -1=infinite, 1=off(default), >1=N times\n"
"  -s, --subnormal    start from min subnormal (mutually exclusive with -a)\n"
"\n"
"\033[33mExamples:\033[0m\n"
"  float_inf_walk.exe 0                          # f32 from 1.0 x1.02\n"
"  float_inf_walk.exe 0 -a 100 -k 1.1            # start at 100 x1.1\n"
"  float_inf_walk.exe 0 -a 0x3F800000 -m f32     # raw bit start\n"
"  float_inf_walk.exe 0 -a max-finite -k 0.5     # max value descending to 0\n"
"  float_inf_walk.exe 0 -m fp4 -g addulp         # enumerate to inf\n"
"  float_inf_walk.exe 0 -m all --only fp32,dec128 -j 1,3 -q\n"
"  float_inf_walk.exe 0 -m all -l 3 -t 10        # loop 3x, max 10s each\n";
    } else {
    std::cout <<
"\033[36m╔══════════════════════════════════════════════════════════════╗\n"
"║   Float Inf Walk — 17格式浮点溢出行走器 (C++23 / GNU)       ║\n"
"╚══════════════════════════════════════════════════════════════╝\033[0m\n"
"\n"
"\033[33m用法:\033[0m\n"
"  float_inf_walk.exe [选项] <delay_ms>\n"
"  float_inf_walk.exe -h | --help\n"
"\n"
"\033[33m模式 -m（默认 f32）:\033[0m\n"
"  \033[32m原生\033[0m       f16  f32(默认)  f64\n"
"  \033[32m自写位级\033[0m   fp4(E2M1)  fp6(E3M2)  fp8-e4m3  fp8-e5m2  bf16  tf32\n"
"  \033[32m纯软件\033[0m     x87  fp128\n"
"  \033[32m十进制\033[0m     dec32\033[0m[\033[36mBID\033[0m]  dec64\033[0m[\033[36mBID\033[0m]  dec128\033[0m[\033[36mBID\033[0m]\n"
"                  dpd32\033[0m[\033[36mDPD\033[0m]  dpd64\033[0m[\033[36mDPD\033[0m]  dpd128\033[0m[\033[36mDPD\033[0m]\n"
"  \033[32mall\033[0m        并发跑全部格式，每线程独立状态机\n"
"\n"
"\033[33m位置参数:\033[0m\n"
"  delay_ms        每步限速毫秒；0=不限速。无参=只显示帮助。\n"
"\n"
"\033[33m核心选项:\033[0m\n"
"  -a, --start <x>    起点（默认1）。支持负数，数字/科学/工程/0x Raw/min-normal/max-finite\n"
"  -k, --factor <r>   每步倍率（默认1.02）。>1递增到+inf，0<r<1递减到0，支持科学计数法\n"
"  -g, --step <s>     x1.02(默认) / mululp(×1+1ULP) / addulp(+1ULP)\n"
"  -m, --mode <m>     浮点格式（见上方模式列表）\n"
"\n"
"\033[33m显示选项:\033[0m\n"
"  -f, --fmt <f>      eng(默认) / sci / full\n"
"  -d, --digits <n>   sci/eng 小数位（默认6，-1=完整，0~100）\n"
"  --bits <m>         hex / bin / off(默认)，括号内追加位表示\n"
"  -q, --quiet        静默模式：不显示实时监视器，直接出汇总表\n"
"  -lang <zh|en>      界面语言（默认 zh）\n"
"\n"
"\033[33m并发选项（-m all）:\033[0m\n"
"  -j, --jobs <spec>  核心编号：单值(4) / 逗号(1,3) / 范围(1-3) / 混合\n"
"  --only <list>      逗号分隔格式子集（默认全部17个）\n"
"  -r, --refresh <ms> 刷新间隔(ms)，0=每步(默认)；-m all 默认1000\n"
"\n"
"\033[33m控制选项:\033[0m\n"
"  -t, --timeout <s>  超时停止(秒)，0=直到inf/下溢(默认)\n"
"  -l, --loop <n>     循环次数：-1=无限，1=关闭(默认)，>1=N次\n"
"  -s, --subnormal    从最小非规格化数开始（与 -a 互斥）\n"
"\n"
"\033[33m示例:\033[0m\n"
"  float_inf_walk.exe 0                          # f32 从1.0 ×1.02\n"
"  float_inf_walk.exe 0 -a 100 -k 1.1            # 从100起步 ×1.1\n"
"  float_inf_walk.exe 0 -a 0x3F800000 -m f32     # Raw 位串起点\n"
"  float_inf_walk.exe 0 -a max-finite -k 0.5     # 最大值递减到0\n"
"  float_inf_walk.exe 0 -m fp4 -g addulp         # 逐值枚举到 inf\n"
"  float_inf_walk.exe 0 -m all --only fp32,dec128 -j 1,3 -q\n"
"  float_inf_walk.exe 0 -m all -l 3 -t 10        # 循环3次，每次最多10s\n";
    }
}

int main(int argc, char** argv) {
#ifdef _WIN32
    fw::plat::set_high_res_timer();
    SetConsoleOutputCP(65001);
    { HANDLE hO = GetStdHandle(STD_OUTPUT_HANDLE); DWORD m; if (GetConsoleMode(hO, &m)) SetConsoleMode(hO, m | ENABLE_VIRTUAL_TERMINAL_PROCESSING); }
    struct PeriodGuard { ~PeriodGuard(){ fw::plat::reset_high_res_timer(); } } _pg;
#endif
    std::signal(SIGINT, on_sigint);
    std::string mode = "f32";
    std::int64_t delay_ms = 0;
    bool has_delay = false;
    StartKind start_kind = StartKind::Default;
    StepMode step_mode = StepMode::X102;
    DispFmt disp_fmt = DispFmt::ENG;
    double start_val = std::nan("");
    double factor = 1.02;
    int digits = 6;
    ShowBits show_bits = ShowBits::OFF;
    std::string jobs_spec;
    std::string only_csv;
    std::int64_t refresh_ms = 0;
    bool quiet = false;
    bool refresh_explicit = false;
    int loop_count = 1;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "-h" || a == "--help") { print_help(); return 0; }
        if (a == "-s" || a == "--subnormal") {
            if (start_kind != StartKind::Default) { std::cerr << tr("错误：特殊起点互斥\n", "error: special start flags are mutually exclusive\n"); return 2; }
            start_kind = StartKind::Subnormal; continue;
        }
        if (a == "--start" || a == "-a") {
            if (i + 1 >= argc) { std::cerr << tr("错误：--start 需要一个值\n", "error: --start requires a value\n"); return 2; }
            std::string sv = argv[++i];
            if (sv == "min-normal" || sv == "minnormal" || sv == "normmin") {
                if (start_kind != StartKind::Default) { std::cerr << tr("错误：特殊起点互斥\n", "error: special start flags are mutually exclusive\n"); return 2; }
                start_kind = StartKind::MinNormal;
            } else if (sv == "max-finite" || sv == "maxfinite" || sv == "infmax") {
                if (start_kind != StartKind::Default) { std::cerr << tr("错误：特殊起点互斥\n", "error: special start flags are mutually exclusive\n"); return 2; }
                start_kind = StartKind::MaxFinite;
            } else {
                try { start_val = parse_number(sv); }
                catch (...) { std::cerr << tr("错误：无法解析起点 '", "error: cannot parse start '") << sv << tr("'\n", "'\n"); return 2; }
                if (std::isnan(start_val) || std::isinf(start_val)) { std::cerr << tr("错误：起点不能为 NaN/Inf\n", "error: start cannot be NaN/Inf\n"); return 2; }
            }
            continue;
        }
        if (a == "--factor" || a == "-k") {
            if (i + 1 >= argc) { std::cerr << tr("错误：--factor 需要一个值\n", "error: --factor requires a value\n"); return 2; }
            try { factor = parse_number(argv[++i]); }
            catch (...) { std::cerr << tr("错误：无法解析倍率\n", "error: cannot parse factor\n"); return 2; }
            if (factor == 0.0) { std::cerr << tr("错误：--factor 不能为 0\n", "error: --factor cannot be 0\n"); return 2; }
            if (factor < 0.0) { std::cerr << tr("错误：--factor 不能为负数\n", "error: --factor cannot be negative\n"); return 2; }
            if (std::isnan(factor) || std::isinf(factor)) { std::cerr << tr("错误：--factor 不能为 NaN/Inf\n", "error: --factor cannot be NaN/Inf\n"); return 2; }
            continue;
        }
        if (a == "--fmt" || a == "-f") {
            if (i + 1 >= argc) { std::cerr << tr("错误：--fmt 需要一个值（eng/sci/full）\n", "error: --fmt requires a value (eng/sci/full)\n"); return 2; }
            std::string f = argv[++i];
            if (f == "eng") disp_fmt = DispFmt::ENG;
            else if (f == "sci") disp_fmt = DispFmt::SCI;
            else if (f == "full") disp_fmt = DispFmt::FULL;
            else { std::cerr << tr("错误：未知显示格式 '", "error: unknown display format '") << f << tr("'\n", "'\n"); return 2; }
            continue;
        }
        if (a == "--digits" || a == "-d") {
            if (i + 1 >= argc) { std::cerr << tr("错误：--digits 需要一个整数\n", "error: --digits requires an integer\n"); return 2; }
            try { digits = std::stoi(argv[++i]); }
            catch (...) { std::cerr << tr("错误：--digits 需为整数\n", "error: --digits must be an integer\n"); return 2; }
            if (digits != -1 && (digits < 0 || digits > 100)) { std::cerr << tr("错误：--digits 应为 -1 或 0~100\n", "error: --digits must be -1 or 0~100\n"); return 2; }
            continue;
        }
        if (a == "--showbits" || a == "--bits") {
            if (i + 1 >= argc) { std::cerr << tr("错误：--showbits 需要一个值（hex/bin/off）\n", "error: --showbits requires a value (hex/bin/off)\n"); return 2; }
            std::string sb = argv[++i];
            if (sb == "hex") show_bits = ShowBits::HEX;
            else if (sb == "bin") show_bits = ShowBits::BIN;
            else if (sb == "off" || sb == "none") show_bits = ShowBits::OFF;
            else { std::cerr << tr("错误：未知 --showbits '", "error: unknown --showbits '") << sb << tr("'\n", "'\n"); return 2; }
            continue;
        }
        if (a == "--jobs" || a == "-j") {
            if (i + 1 >= argc) { std::cerr << tr("错误：--jobs 需要一个值\n", "error: --jobs requires a value\n"); return 2; }
            jobs_spec = argv[++i];
            continue;
        }
        if (a == "--only" || a == "--modes") {
            if (i + 1 >= argc) { std::cerr << tr("错误：--only 需要一个值\n", "error: --only requires a value\n"); return 2; }
            only_csv = argv[++i];
            continue;
        }
        if (a == "--refresh" || a == "-r") {
            if (i + 1 >= argc) { std::cerr << tr("错误：--refresh 需要一个毫秒值\n", "error: --refresh requires a millisecond value\n"); return 2; }
            try { refresh_ms = std::stoll(argv[++i]); }
            catch (...) { std::cerr << tr("错误：--refresh 应为整数毫秒\n", "error: --refresh must be integer ms\n"); return 2; }
            if (refresh_ms < 0) refresh_ms = 0;
            refresh_explicit = true;
            continue;
        }
        if (a == "--timeout" || a == "-t") {
            if (i + 1 >= argc) { std::cerr << tr("错误：--timeout 需要一个秒值\n", "error: --timeout requires a second value\n"); return 2; }
            try { g_timeout_sec.store(std::stoi(argv[++i])); }
            catch (...) { std::cerr << tr("错误：--timeout 应为整数秒\n", "error: --timeout must be integer seconds\n"); return 2; }
            if (g_timeout_sec.load() < 0) g_timeout_sec.store(0);
            continue;
        }
        if (a == "--loop" || a == "-l") {
            if (i + 1 >= argc) { std::cerr << tr("错误：--loop 需要一个值\n", "error: --loop requires a value\n"); return 2; }
            try { loop_count = std::stoi(argv[++i]); }
            catch (...) { std::cerr << tr("错误：--loop 应为整数\n", "error: --loop must be an integer\n"); return 2; }
        if (loop_count == 0) loop_count = 1;  // 0=disabled, equivalent to 1
        if (loop_count < -1) { std::cerr << tr("错误：--loop 不支持 ", "error: --loop does not support ") << loop_count << tr("\n", "\n"); return 2; }
        if (loop_count > 1000000) { std::cerr << tr("错误：--loop 过大（上限 1000000）\n", "error: --loop too large (max 1000000)\n"); return 2; }
            continue;
        }
        if (a == "--step" || a == "-g") {
            if (i + 1 >= argc) { std::cerr << tr("错误：--step 需要一个值\n", "error: --step requires a value\n"); return 2; }
            std::string s = argv[++i];
            if (s == "x1.02" || s == "x102") step_mode = StepMode::X102;
            else if (s == "mululp") step_mode = StepMode::MUL_ULP;
            else if (s == "addulp") step_mode = StepMode::ADD_ULP;
            else { std::cerr << tr("错误：未知步进 '", "error: unknown step '") << s << tr("'\n", "'\n"); return 2; }
            continue;
        }
        if (a == "-q" || a == "--quiet") { quiet = true; continue; }
        if (a == "-lang" || a == "--lang" || a == "--language") {
            if (i + 1 >= argc) { std::cerr << "usage: -lang zh|en\n"; return 2; }
            std::string l = argv[++i];
            if (l == "en" || l == "EN") g_lang = Lang::EN;
            else g_lang = Lang::ZH;
            continue;
        }
        if (a == "-m") {
            if (i + 1 >= argc) { std::cerr << tr("错误：-m 需要一个模式值\n", "error: -m requires a mode value\n"); return 2; }
            mode = argv[++i];
            continue;
        }
        try { delay_ms = std::stoll(a); has_delay = true; }
        catch (...) { std::cerr << tr("错误：无法解析参数 '", "error: cannot parse argument '") << a << tr("'\n", "'\n"); return 2; }
    }

    if (!has_delay) {
        if (mode == "all") delay_ms = 10;
        else { print_help(); return 2; }
    }
    if (delay_ms < 0) delay_ms = 0;
    if (start_kind != StartKind::Default && !std::isnan(start_val)) { std::cerr << tr("错误：特殊起点与数值起点互斥\n", "error: special start and numeric start are mutually exclusive\n"); return 2; }
    if (factor == 0.0) { std::cerr << tr("错误：倍率不能为 0\n", "error: factor cannot be 0\n"); return 2; }
    if (factor < 0.0) { std::cerr << tr("错误：倍率必须为正数\n", "error: factor must be positive\n"); return 2; }
    if (!std::isfinite(factor)) { std::cerr << tr("错误：倍率必须为有限正数\n", "error: factor must be finite positive\n"); return 2; }
    if (!std::isnan(start_val) && !std::isfinite(start_val)) { std::cerr << tr("错误：起点必须为有限数值\n", "error: start must be a finite number\n"); return 2; }

    // DoS guard: infinite loop without timeout warning
    if (loop_count == -1 && g_timeout_sec.load() == 0) {
        std::cerr << clr::YEL << tr("警告：无限循环（-l -1）且无超时（-t 0），可能导致 CPU 占用 100%\n",
                                    "warn: infinite loop (-l -1) with no timeout (-t 0), may cause 100% CPU\n") << clr::RST;
    }
    // DoS guard: extreme small multiplier causing step explosion
    if (factor > 0.0 && factor < 1e-10) {
        std::cerr << clr::YEL << tr("警告：倍率极小（", "warn: factor very small (") << factor
                  << tr("），步数可能爆炸，请确认\n", "), steps may explode, please confirm\n") << clr::RST;
    }

    if (mode == "all") {
        if (!refresh_explicit) refresh_ms = 1000;
        int hw = (int)std::thread::hardware_concurrency();
        auto jobs_list = parse_jobs(jobs_spec, hw);
        run_all(delay_ms, start_kind, step_mode, disp_fmt, start_val, factor, digits, jobs_list, only_csv, refresh_ms, loop_count, quiet);
        return 0;
    }
    if (mode == "f16" || mode == "fp16")            { run_native<NativeF16>("FP16", delay_ms, start_kind, step_mode, disp_fmt, start_val, factor, digits, show_bits, nullptr, refresh_ms); return 0; }
    if (mode == "f32" || mode == "fp32")            { run_native<NativeF32>("FP32", delay_ms, start_kind, step_mode, disp_fmt, start_val, factor, digits, show_bits, nullptr, refresh_ms); return 0; }
    if (mode == "f64" || mode == "fp64")            { run_native<NativeF64>("FP64", delay_ms, start_kind, step_mode, disp_fmt, start_val, factor, digits, show_bits, nullptr, refresh_ms); return 0; }
    if (mode == "x87")                              { run_x87_soft(delay_ms, start_kind, step_mode, disp_fmt, start_val, factor, digits, show_bits, nullptr, refresh_ms); return 0; }
    if (mode == "fp4")        { run_sim({2, 1, 1, "FP4(E2M1)"}, delay_ms, start_kind, step_mode, disp_fmt, start_val, factor, digits, show_bits, nullptr, refresh_ms); return 0; }
    if (mode == "fp6")        { run_sim({3, 2, 3, "FP6(E3M2)"}, delay_ms, start_kind, step_mode, disp_fmt, start_val, factor, digits, show_bits, nullptr, refresh_ms); return 0; }
    if (mode == "fp8" || mode == "fp8-e4m3") { run_sim({4, 3, 7, "FP8(E4M3)"}, delay_ms, start_kind, step_mode, disp_fmt, start_val, factor, digits, show_bits, nullptr, refresh_ms); return 0; }
    if (mode == "fp8-e5m2")   { run_sim({5, 2, 15, "FP8(E5M2)"}, delay_ms, start_kind, step_mode, disp_fmt, start_val, factor, digits, show_bits, nullptr, refresh_ms); return 0; }
    if (mode == "bf16")       { run_sim({8, 7, 127, "BF16(E8M7)"}, delay_ms, start_kind, step_mode, disp_fmt, start_val, factor, digits, show_bits, nullptr, refresh_ms); return 0; }
    if (mode == "tf32")       { run_sim({8, 10, 127, "TF32(E8M10)"}, delay_ms, start_kind, step_mode, disp_fmt, start_val, factor, digits, show_bits, nullptr, refresh_ms); return 0; }
    {
        static const DecFmt f32d{7, 96, "Decimal32"};
        static const DecFmt f64d{16, 384, "Decimal64"};
        if (mode == "dec32" || mode == "decimal32") { run_dec(f32d, delay_ms, start_kind, step_mode, disp_fmt, start_val, factor, digits, show_bits, nullptr, refresh_ms); return 0; }
        if (mode == "dec64" || mode == "decimal64") { run_dec(f64d, delay_ms, start_kind, step_mode, disp_fmt, start_val, factor, digits, show_bits, nullptr, refresh_ms); return 0; }
        if (mode == "dec128" || mode == "decimal128") {
            std::cerr << "\033[33m[!] dec128 走 714521 步（×1.02），每步 UInt256 schoolbook 大整数乘法，实测约 2.6 秒。无 -t 时会跑满全程。\033[0m\n" << std::flush;
            run_dec128(delay_ms, start_kind, step_mode, disp_fmt, start_val, factor, digits, show_bits, nullptr, refresh_ms); return 0;
        }
        if (mode == "dpd32" || mode == "dpd-dec32") { run_dpd(dpd_fmt32(), delay_ms, start_kind, step_mode, disp_fmt, start_val, factor, digits, show_bits, nullptr, refresh_ms); return 0; }
        if (mode == "dpd64" || mode == "dpd-dec64") { run_dpd(dpd_fmt64(), delay_ms, start_kind, step_mode, disp_fmt, start_val, factor, digits, show_bits, nullptr, refresh_ms); return 0; }
        if (mode == "dpd128" || mode == "dpd-dec128") { run_dpd(dpd_fmt128(), delay_ms, start_kind, step_mode, disp_fmt, start_val, factor, digits, show_bits, nullptr, refresh_ms); return 0; }
    }
    if (mode == "fp128") { run_q128_soft(delay_ms, start_kind, step_mode, disp_fmt, start_val, factor, digits, show_bits, nullptr, refresh_ms); return 0; }

    std::cerr << tr("错误：未知模式 '", "error: unknown mode '") << mode << tr("'（见 -h）\n", "' (see -h)\n");
    return 2;
}
