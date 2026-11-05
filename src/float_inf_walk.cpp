// float_inf_walk.cpp — Overflow walk: grow from 1.0 step-by-step until overflow to +inf
#include "common.h"
#include "formats/formats_all.h"
#include "runner.h"
#include "platform.h"
#include <fstream>
#include <iterator>
#include <cctype>
#include <cmath>
#include <stdexcept>
#ifdef _WIN32
#include <windows.h>
#include <intrin.h>
#include <conio.h>
#include <io.h>
#include <cstdio>
#include <cmath>
#elif defined(__GNUC__) || defined(__clang__)
#include <cpuid.h>
#endif

using namespace fw;

#ifdef __clang__
#define FW_CC_NAME "clang"
#elif defined(__GNUC__)
#define FW_CC_NAME "gcc"
#else
#define FW_CC_NAME "unknown"
#endif

static const char* tf(bool b) { return b ? "true" : "false"; }

// A1: locate the last record for `key` in a baseline JSON anchor and compare the
// deterministic steps (kept as decimal string, safe beyond 64-bit) and reason.
// Pure stdlib; never changes the process exit code, differences go to stderr only.
struct AnchorCompare {
    bool available = false, matched = true;
    std::string exp_steps, exp_reason;
};
static AnchorCompare compare_anchor(const std::string& path, const std::string& key,
                                    const std::string& got_steps, const std::string& got_reason) {
    AnchorCompare r;
    std::ifstream in(path);
    if (!in) return r;
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::string needle = "\"format\":\"" + key + "\"";
    size_t pos = text.rfind(needle);
    if (pos == std::string::npos) return r;
    r.available = true;
    size_t win = text.find('}', pos);
    if (win == std::string::npos) win = text.size();
    std::string seg = text.substr(pos, win - pos);
    auto grab = [&](const char* field) -> std::string {
        std::string f = "\""; f += field; f += "\":";
        size_t p = seg.find(f);
        if (p == std::string::npos) return "";
        p += f.size();
        while (p < seg.size() && (seg[p] == ' ' || seg[p] == '\t')) ++p;
        if (p < seg.size() && seg[p] == '"') {
            ++p; size_t e = seg.find('"', p);
            return e == std::string::npos ? "" : seg.substr(p, e - p);
        }
        std::string num;
        while (p < seg.size() && std::isdigit((unsigned char)seg[p])) { num += seg[p]; ++p; }
        return num;
    };
    r.exp_steps = grab("steps");
    r.exp_reason = grab("reason");
    r.matched = true;
    if (!r.exp_steps.empty() && r.exp_steps != got_steps) r.matched = false;
    if (!r.exp_reason.empty() && r.exp_reason != got_reason) r.matched = false;
    return r;
}

// Unified warning confirmation: collect all warnings, show once, press y to continue
// level: 0=info([?] blue), 1=warn([!] yellow), 2=critical([!!!] red)
static void print_warnings(const std::vector<std::pair<int,std::string>>& warns, std::ostream& os) {
    for (auto& [lvl, msg] : warns) {
        if (lvl == 0)      os << clr::BLU << "[?] " << msg << "\n" << clr::RST;
        else if (lvl == 2) os << clr::RED << "[!!!] " << msg << "\n" << clr::RST;
        else               os << clr::YEL << "[!] " << msg << "\n" << clr::RST;
    }
}

static bool confirm_warnings(std::vector<std::pair<int,std::string>>& warns) {
    if (warns.empty()) return true;
    // Diagnostic warnings go to stderr so a machine-readable stdout (--json) stays pure JSON.
    if (g_assume_yes) { print_warnings(warns, std::cerr); return true; }
    print_warnings(warns, std::cerr);
    std::cout << clr::YEL << tr("    按 y 继续，10 秒内未操作自动取消... ", "    Press y to continue, auto-cancel in 10s... ") << clr::RST << std::flush;
    bool confirmed = false;
#ifdef _WIN32
    auto t0 = std::chrono::steady_clock::now();
    while (std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - t0).count() < 10) {
        if (_kbhit()) {
            int c = _getch();
            if (c == 'y' || c == 'Y') confirmed = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
    }
#else
    char c = 0;
    std::cin.read(&c, 1);
    if (c == 'y' || c == 'Y') confirmed = true;
#endif
    if (!confirmed) {
        std::cout << "\n" << tr("已取消。\n", "Cancelled.\n");
        return false;
    }
    std::cout << "\n";
    return true;
}

// CRC-32/IEEE 802.3: reflected poly 0xEDB88320, init/xorout 0xFFFFFFFF;
// bit-compatible with zlib / Python binascii.crc32.
static std::uint32_t crc32_update(std::uint32_t crc, const unsigned char* p, std::size_t n) {
    for (std::size_t i = 0; i < n; ++i) {
        crc ^= p[i];
        for (int k = 0; k < 8; ++k)
            crc = (crc >> 1) ^ ((crc & 1u) ? 0xEDB88320u : 0u);
    }
    return crc;
}

// CRC over [base, base+n) of the open dump file; streams in chunks, n<=0 => CRC of empty = 0.
static std::uint32_t crc32_payload(std::FILE* f, long base, long n) {
    std::uint32_t crc = 0xFFFFFFFFu;
    if (n > 0) {
        std::fseek(f, base, SEEK_SET);
        unsigned char buf[4096];
        long remain = n;
        while (remain > 0) {
            std::size_t want = remain < (long)sizeof(buf) ? (std::size_t)remain : sizeof(buf);
            std::size_t got = std::fread(buf, 1, want, f);
            if (got == 0) break;
            crc = crc32_update(crc, buf, got);
            remain -= (long)got;
        }
    }
    return crc ^ 0xFFFFFFFFu;
}

// CRC-32/IEEE 802.3 over an in-memory buffer (used for the header itself).
static std::uint32_t crc32_buffer(const unsigned char* p, std::size_t n) {
    return crc32_update(0xFFFFFFFFu, p, n) ^ 0xFFFFFFFFu;
}

// W2-D2: `--self-test` — pure-logic sanity checks that need no hardware pressure and no
// walk. Covers the deterministic helpers (CRC vectors), the shared codecs (DPD/BID round
// trip) and the parser/sanitizer. Exit 0 only when every check passes; never touches a
// stress run and runs before any mandatory positional/delay validation.
static int run_self_test() {
    int checks = 0, fails = 0;
    auto check = [&](bool ok, const char* name) {
        ++checks;
        if (ok) std::cout << "PASS " << name << "\n";
        else    { ++fails; std::cout << "FAIL " << name << "\n"; }
    };

    // 1) CRC-32/IEEE 802.3 known vectors (zlib-compatible).
    {
        const unsigned char s[] = "123456789";
        check(crc32_buffer(s, 9) == 0xCBF43926u, "crc32(\"123456789\")==0xCBF43926");
        check(crc32_buffer(nullptr, 0) == 0u, "crc32(empty)==0");
    }
    // 2) DPD triplet codec: every 3-digit BCD value 0..999 must survive encode->decode.
    {
        bool ok = true;
        for (unsigned d = 0; d < 1000; ++d) {
            unsigned d2 = d / 100, d1 = (d / 10) % 10, d0 = d % 10;
            unsigned code = fw::dpd_encode3(d2, d1, d0);
            unsigned a = 99, b = 99, c = 99;
            fw::dpd_decode3(code, a, b, c);
            if (a != d2 || b != d1 || c != d0) { ok = false; break; }
        }
        check(ok, "dpd encode3/decode3 round-trip for 0..999");
    }
    // 3) Shared BID codec skeleton. Packed bits must match the schema width and survive
    //    sign/exponent round-trip. The raw coeff display helpers (bid_dec_to_bin /
    //    bid_bin_to_dec) are deliberately NOT gated: their decimal-digit vs binary-shift
    //     conventions differ and they feed the display-only coeff string, not the integer
    //     arithmetic core; the startup diagnostic bid_selfcheck() comparator likewise uses a
    //     different trailing-zero normalization and remains a non-blocking print.
    {
        bool width = true, meta = true;
        fw::BidFmt fs[] = { fw::bid_fmt32(), fw::bid_fmt64(), fw::bid_fmt128() };
        for (const auto& f : fs) {
            std::string bits = fw::bid_pack(0, 0, "12345", f);
            std::size_t expect_bits = 1u + 5u + (std::size_t)f.exp_cont + (std::size_t)f.coeff_bits;
            if (bits.size() != expect_bits) width = false;
            fw::BidUnpack u = fw::bid_unpack(f, bits);
            if (u.sign != 0 || u.expEnc != 0) meta = false;
        }
        check(width, "BID packed bit width matches schema (sign+combo+exp+coeff)");
        check(meta, "BID pack->unpack preserves sign and encoded exponent");
    }
    // 4) Numeric parser: 32-bit IEEE hex literal round; sanitizer strips ESC/C0.
    {
        double v = 0.0;
        bool parsed = true;
        try { v = fw::parse_number("0x3F800000"); } catch (...) { parsed = false; }
        check(parsed && std::abs(v - 1.0) < 1e-7, "parse_number(0x3F800000) ~= 1.0");
        std::string dirty = "x\x1b[31mok";
        std::string clean = fw::sanitize(dirty);
        check(clean.find('\x1b') == std::string::npos, "sanitize removes ESC escape sequences");
    }

    std::cout << "SELF-TEST CHECKS=" << checks << " FAILS=" << fails << "\n";
    return fails == 0 ? 0 : 1;
}

static void print_help() {
    if (g_lang == Lang::EN) {
    std::cout <<
"\033[36m╔══════════════════════════════════════════════════════════════╗\n"
"║   ea-FRDT — Silicon-level Devil Hardware Stress Test (C++23)  ║\n"
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
"  -g, --step <s>     x1.02(default) / mululp(x1+1ULP) / addulp(+1ULP) / subulp(-1ULP)\n"
"  -m, --mode <m>     float format (see modes above)\n"
"\n"
"\033[33mDisplay:\033[0m\n"
"  -f, --fmt <f>      eng(default) / sci / full\n"
"  -d, --digits <n>   sci/eng decimals (default 6, -1=full, 0~100)\n"
"  --bits <m>         hex / bin / off(default), append bit repr in parens\n"
"  -q, --quiet        quiet: skip live monitor, print summary directly\n"
"  --json             machine-readable JSON output (no ANSI, for benchmarking)\n"
"  --dump <file>      binary serialize every step raw bytes to file\n"
"  --dump-endian <e>  byte order: little(default, x86 native)/big\n"
"  --dump-packed      request tight records; flag set only when width actually shrinks (e.g. tf32 3B<4B)\n"
"  --dump-raw / --dump-no-header  omit the 256B header; continuous raw bit stream from offset 0 (single format only)\n"
"  -lang <zh|en>      UI language (default zh)\n"
"\n"
"\033[33mConcurrency (-m all):\033[0m\n"
"  -j, --jobs <spec>  core ids: single(4) / comma(1,3) / range(1-3) / mixed\n"
"  --only <list>      comma-separated format subset (default all 17)\n"
"  -r, --refresh <ms> refresh interval, 0=every step; -m all default 1000\n"
"\n"
"\033[33mControl:\033[0m\n"
"  -t, --timeout <s>  timeout seconds, 0=until inf/underflow (default)\n"
"  -l, --loop <n>     loops (-m all only): -1=infinite, 1=off(default), >1=N times\n"
"  -y, --yes          skip interactive critical-warning confirmation (non-interactive/CI)\n"
"  --compare <file>  compare deterministic steps/reason against a baseline JSON; diff on stderr, exit code unchanged\n"
"  --steps <n>       stop after exactly N steps (default 0=disabled; complements -t wall-clock)\n"
"  --pin <core>      single-format opt-in: bind this thread to one logical core (default off)\n"
"  --boost           single-format opt-in: raise thread priority (default off; no-op on Linux)\n"
"  --dump-events <f> single-format JSONL trace: start/exit event records (default off)\n"
"  --self-test       run pure-logic codec/CRC/parser checks and exit; no stress run\n"
"  -s, --subnormal    start from min subnormal (mutually exclusive with -a)\n"
"\n"
"\033[33mAdvanced opt-in (default off; single format unless noted):\033[0m\n"
"  --max-seconds <s>  hard wall-clock cap in seconds; fires as timeout; independent of -t\n"
"  --preset iron / --iron  priority boost + 60s safety cap; never auto-pins; no temperature readout or automatic throttling\n"
"  --pairwise A,B    minimal same-start dual-engine compare (whitelist dec32,dpd32 / dec64,dpd64 / f32,tf32)\n"
"  --sample-window <ms>  quiet/JSON-only steps/s segments: min/avg/p95/max distribution (0=off)\n"
"  --probe boundaries  standalone fixed-step batch table: quick formats x four boundary starts\n"
"\n"
"\033[33mExamples:\033[0m\n"
"  float_inf_walk.exe 0                          # f32 from 1.0 x1.02\n"
"  float_inf_walk.exe 0 -a 100 -k 1.1            # start at 100 x1.1\n"
"  float_inf_walk.exe 0 -a 0x3F800000 -m f32     # raw bit start\n"
"  float_inf_walk.exe 0 -a max-finite -k 0.5     # max value descending to 0\n"
"  float_inf_walk.exe 0 -m fp4 -g addulp         # enumerate to inf\n"
"  float_inf_walk.exe 0 -m all --only fp32,dec128 -j 1,3 -q\n"
"  float_inf_walk.exe 0 -m all -l 3 -t 10        # loop 3x, max 10s each\n"
"  float_inf_walk.exe 0 -m f32 --json            # JSON output for scripts\n"
"  float_inf_walk.exe 0 -m f64 --dump walk.bin\n";
    } else {
    std::cout <<
"\033[36m╔══════════════════════════════════════════════════════════════╗\n"
"║   ea-FRDT — 面向硅片魔鬼高压硬件测试 (C++23)              ║\n"
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
"  -g, --step <s>     x1.02(默认) / mululp(×1+1ULP) / addulp(+1ULP) / subulp(-1ULP)\n"
"  -m, --mode <m>     浮点格式（见上方模式列表）\n"
"\n"
"\033[33m显示选项:\033[0m\n"
"  -f, --fmt <f>      eng(默认) / sci / full\n"
"  -d, --digits <n>   sci/eng 小数位（默认6，-1=完整，0~100）\n"
"  --bits <m>         hex / bin / off(默认)，括号内追加位表示\n"
"  -q, --quiet        静默模式：不显示实时监视器，直接出汇总表\n"
"  --json             机器可读 JSON 输出（无 ANSI，供评测脚本采集）\n"
"  --dump <file>      二进制序列化每步原始字节到文件\n"
"  --dump-endian <e>  字节序：little(默认,x86原生)/big\n"
"  --dump-packed      请求紧凑记录；仅在实际缩宽时置位（如 tf32 写3字节<4字节，整字宽格式不缩减）\n"
"  --dump-raw / --dump-no-header  省略256字节头：从偏移0直接写连续原始位流（仅单格式）\n"
"  -lang <zh|en>      界面语言（默认 zh）\n"
"\n"
"\033[33m并发选项（-m all）:\033[0m\n"
"  -j, --jobs <spec>  核心编号：单值(4) / 逗号(1,3) / 范围(1-3) / 混合\n"
"  --only <list>      逗号分隔格式子集（默认全部17个）\n"
"  -r, --refresh <ms> 刷新间隔(ms)，0=每步(默认)；-m all 默认1000\n"
"\n"
"\033[33m控制选项:\033[0m\n"
"  -t, --timeout <s>  超时停止(秒)，0=直到inf/下溢(默认)\n"
"  -l, --loop <n>     循环次数（仅 -m all 有效）：-1=无限，1=关闭(默认)，>1=N次\n"
"  -y, --yes          跳过 critical 警告交互确认（非交互/CI 用）\n"
"  --compare <file>  与基线 JSON 比对确定性 steps/reason；差异仅走 stderr，退出码不变\n"
"  --steps <n>       固定跑 N 步即停（默认0=关闭；与 -t 墙钟超时互补）\n"
"  --pin <core>      单格式可选：把本线程绑定到指定逻辑核（默认关闭）\n"
"  --boost           单格式可选：提升线程优先级（默认关闭；Linux 无提级则为 no-op）\n"
"  --dump-events <f> 单格式 JSONL 轨迹：start/exit 事件记录（默认关闭）\n"
"  --self-test       运行纯逻辑编解码/CRC/解析自检后退出，不进入压测\n"
"  -s, --subnormal    从最小非规格化数开始（与 -a 互斥）\n"
"\n"
"\033[33m高级可选（默认关闭；除注明外仅单格式）:\033[0m\n"
"  --max-seconds <s> 墙钟硬熔断上限(秒)，到点以 timeout 退出；与 -t 独立\n"
"  --preset iron / --iron  开启提级 + 60s 安全上限；不自动绑核；不读温度/节流、不自动降频\n"
"  --pairwise A,B    最小同起点双引擎对照（白名单 dec32,dpd32 / dec64,dpd64 / f32,tf32）\n"
"  --sample-window <ms>  仅静默/JSON：按墙钟窗统计 steps/s 的 min/avg/p95/max 分布（0=关）\n"
"  --probe boundaries  独立固定步批量表：快格式 × 四类边界起点\n"
"\n"
"\033[33m示例:\033[0m\n"
"  float_inf_walk.exe 0                          # f32 从1.0 ×1.02\n"
"  float_inf_walk.exe 0 -a 100 -k 1.1            # 从100起步 ×1.1\n"
"  float_inf_walk.exe 0 -a 0x3F800000 -m f32     # Raw 位串起点\n"
"  float_inf_walk.exe 0 -a max-finite -k 0.5     # 最大值递减到0\n"
"  float_inf_walk.exe 0 -m fp4 -g addulp         # 逐值枚举到 inf\n"
"  float_inf_walk.exe 0 -m all --only fp32,dec128 -j 1,3 -q\n"
"  float_inf_walk.exe 0 -m all -l 3 -t 10        # 循环3次，每次最多10s\n"
"  float_inf_walk.exe 0 -m f32 --json            # JSON 输出供脚本采集\n"
"  float_inf_walk.exe 0 -m f64 --dump walk.bin\n";
    }
}

int main(int argc, char** argv) {
#ifdef _WIN32
    fw::plat::set_high_res_timer();
    SetConsoleOutputCP(65001);
    { HANDLE hO = GetStdHandle(STD_OUTPUT_HANDLE); DWORD m; if (GetConsoleMode(hO, &m)) SetConsoleMode(hO, m | ENABLE_VIRTUAL_TERMINAL_PROCESSING); }
    struct PeriodGuard { ~PeriodGuard(){ fw::plat::reset_high_res_timer(); } } _pg;
#endif
    std::signal(SIGINT, [](int s) {
        on_sigint(s);
        // Best-effort release of the high-resolution timer period and any boosted
        // thread priority/affinity before the interrupted process terminates.
        fw::plat::teardown_on_exit();
        std::_Exit(130);
    });
    std::atexit([](){ if (g_dump_file) {
        std::fflush(g_dump_file);
        if (g_dump_raw) { std::fclose(g_dump_file); g_dump_file = nullptr; return; } // raw: no header/CRC; flush and close only
        long payload_n = g_dump_bytes_written;
        // payload CRC-32/IEEE over records at offset 256 .. EOF
        std::uint32_t payload_crc = crc32_payload(g_dump_file, 256, payload_n);

        // ---- v2 fixed 256-byte header, all integers little-endian ----
        // 0x00 char[8] magic "EAFRDT2\0"
        // 0x08 u8 schema=2 (fixed 256B header) ; 0x09 reserved
        // 0x0A u8 payload_endian(0 LE/1 BE) ; 0x0B u8 packed(0 natural/1 tight)
        // 0x0C u32 flags(bit2=empty) ; 0x10 u32 header_crc32 ; 0x14 u32 payload_crc32
        // 0x18 u64 payload_bytes ; 0x20 u64 record_count ; 0x28 u32 record_size
        // 0x2C u8 format_id ;0x2D u8 step_mode ;0x2E u8 start_kind ;0x2F u8 reason(0xFF=unknown)
        // 0x30 f64 start_value ; 0x38 f64 factor
        // 0x40 i64 timestamp_unix_s ; 0x48 u64 elapsed_us
        // 0x50 u32 timeout_sec ;0x54 i32 loop_count ;0x58 f64 delay_ms
        // 0x60 char[32] format_name ; 0x80 char[32] tool_version ; 0xA0..0xFF reserved zero
        std::rewind(g_dump_file);
        unsigned char hdr[256] = {0};
        std::memcpy(hdr+0x00, "EAFRDT2", 7);
        hdr[0x08] = 2;  // schema v2: fixed 256-byte header; 0x09 reserved zero
        hdr[0x0A] = (unsigned char)g_dump_endian;
        hdr[0x0B] = (unsigned char)g_dump_packed_active;
        {
            std::uint32_t flags = (payload_n == 0) ? 0x4u : 0u;  // bit2: empty (no records)
            std::memcpy(hdr+0x0C, &flags, 4);
        }
        std::memcpy(hdr+0x14, &payload_crc, 4);
        std::uint64_t payload_bytes = (std::uint64_t)payload_n;
        std::memcpy(hdr+0x18, &payload_bytes, 8);
        std::uint32_t rsize = (std::uint32_t)g_dump_record_size;
        std::uint64_t record_count = (rsize > 0) ? (std::uint64_t)(payload_n / (long)rsize) : 0ull;
        std::memcpy(hdr+0x20, &record_count, 8);
        std::memcpy(hdr+0x28, &rsize, 4);
        hdr[0x2C] = (unsigned char)g_dump_format_id;
        hdr[0x2D] = (unsigned char)g_dump_step_mode;
        hdr[0x2E] = (unsigned char)g_dump_start_kind;
        hdr[0x2F] = (payload_n > 0) ? (unsigned char)g_result.reason : (unsigned char)0xFF;
        {
            double sv = g_dump_start_value;
            double fac = g_dump_factor;
            std::memcpy(hdr+0x30, &sv, 8);
            std::memcpy(hdr+0x38, &fac, 8);
            std::int64_t ts = (std::int64_t)std::time(nullptr);
            std::memcpy(hdr+0x40, &ts, 8);
            std::uint64_t elapsed_us = (std::uint64_t)(g_result.elapsed_ms * 1000.0);
            std::memcpy(hdr+0x48, &elapsed_us, 8);
            std::uint32_t timeout_s = (std::uint32_t)g_timeout_sec.load();
            std::memcpy(hdr+0x50, &timeout_s, 4);
            std::int32_t loop = g_json_loop;
            std::memcpy(hdr+0x54, &loop, 4);
            double delay_ms = g_json_delay_ms;
            std::memcpy(hdr+0x58, &delay_ms, 8);
        }
        {
            std::memset(hdr+0x60, 0, 32);
            std::size_t nc = g_fmt_name.size() < 31 ? g_fmt_name.size() : 31;
            std::memcpy(hdr+0x60, g_fmt_name.data(), nc);
            const char* ver = "v1.0.6";
            std::memset(hdr+0x80, 0, 32);
            std::memcpy(hdr+0x80, ver, std::strlen(ver));
        }
        // header CRC32 over the 256 bytes with the header_crc slot (0x10..0x13) kept zeroed
        std::uint32_t header_crc = crc32_buffer(hdr, 256);
        std::memcpy(hdr+0x10, &header_crc, 4);

        std::fwrite(hdr, 1, 256, g_dump_file);
        std::fflush(g_dump_file);
#ifdef _WIN32
        _chsize(_fileno(g_dump_file), 256 + payload_n);
#endif
        std::fclose(g_dump_file); g_dump_file = nullptr; } });
    std::atexit([](){
        if (!g_json) return;
        if (g_fmt_name == "all") return;  // run_all emits its own JSON summary
        const char* rn = "unknown";
        switch (g_result.reason) {
            case RunResult::OVERFLOW:  rn = "overflow"; break;
            case RunResult::UNDERFLOW: rn = "underflow"; break;
            case RunResult::IDENTITY:  rn = "identity"; break;
            case RunResult::SAFETY:    rn = "safety"; break;
            case RunResult::TIMEOUT:   rn = "timeout"; break;
            case RunResult::NOT_A_NUMBER: rn = "nan"; break;
            case RunResult::FIXED_STEPS: rn = "fixed"; break;
        }
        const char* sm = "x1.02";
        if (g_json_step_mode == 1) sm = "mululp";
        else if (g_json_step_mode == 2) sm = "addulp";
        else if (g_json_step_mode == 3) sm = "subulp";
        double rate = g_result.elapsed_ms > 0.0 ? (double)g_result.steps / (g_result.elapsed_ms / 1000.0) : 0.0;
        auto now = std::time(nullptr);
        // A1: deterministic anchor comparison (steps/reason only; rate/wall are host-dependent).
        std::string bc;
        if (!g_compare_path.empty()) {
            AnchorCompare ac = compare_anchor(g_compare_path, g_compare_key,
                                              to_string_u128(g_result.steps), rn);
            if (!ac.available) {
                bc = "{\"available\":false}";
            } else {
                if (!ac.matched) {
                    std::cerr << "[compare] " << g_compare_key
                              << " MISMATCH exp_steps=" << ac.exp_steps
                              << " exp_reason=" << ac.exp_reason
                              << " got_steps=" << to_string_u128(g_result.steps)
                              << " got_reason=" << rn << "\n";
                }
                bc = "{\"available\":true,\"matched\":";
                bc += ac.matched ? "true" : "false";
                bc += ",\"expected_steps\":\"" + ac.exp_steps + "\"";
                bc += ",\"expected_reason\":\"" + ac.exp_reason + "\"}";
            }
        } else {
            bc = "{\"available\":false}";
        }
        // W3-B5: optional throughput block; empty string when --sample-window is off so the
        // default JSON shape stays byte-identical.
        std::string tp_field;
        if (g_sample_window_ms > 0) {
            char tb[256];
            std::snprintf(tb, sizeof(tb),
                "\"throughput\":{\"window_ms\":%lld,\"samples\":%d,\"min\":%.1f,\"avg\":%.1f,\"p95\":%.1f,\"max\":%.1f},",
                g_sample_window_ms, g_rate_n, g_rate_min, g_rate_avg, g_rate_p95, g_rate_max);
            tp_field = tb;
        }
        std::printf(
            "{"
            "\"tool\":\"ea-FRDT\","
            "\"version\":\"1.0\","
            "\"format\":\"%s\","
            "\"steps\":%s,"
            "\"elapsed_ms\":%.3f,"
            "\"elapsed_s\":%.6f,"
            "\"steps_per_sec\":%.1f,"
            "\"reason\":\"%s\","
            "\"final_value\":\"%s\","
            "\"params\":{"
              "\"start\":%.17g,"
              "\"factor\":%.17g,"
              "\"step_mode\":\"%s\","
              "\"digits\":%d,"
              "\"delay_ms\":%.17g,"
              "\"timeout_s\":%d,"
              "\"loop\":%d,"
              "\"fixed_steps\":%s,"
              "\"max_seconds\":%d,"
              "\"preset\":\"%s\""
            "},"
            "\"system\":{"
              "\"hardware_threads\":%u,"
              "\"physical_threads\":%d,"
              "\"hires_timer\":%s,"
              "\"affinity\":%s,"
              "\"compiler\":\"%s\","
              "\"cpp_std\":23"
            "},"
            "%s"
            "\"baseline_compare\":%s,"
            "\"timestamp\":%ld"
            "}\n",
            g_result.name.empty() ? g_fmt_name.c_str() : g_result.name.c_str(),
            to_string_u128(g_result.steps).c_str(),
            g_result.elapsed_ms,
            g_result.elapsed_ms / 1000.0,
            rate,
            rn,
            g_result.cur_value.empty() ? "" : g_result.cur_value.c_str(),
            g_json_start,
            g_json_factor,
            sm,
            g_json_digits,
            g_json_delay_ms,
            g_timeout_sec.load(),
            g_json_loop,
            to_string_u128(g_fixed_steps).c_str(),
            g_max_seconds.load(),
            g_iron_preset ? "iron" : "default",
            (unsigned)std::thread::hardware_concurrency(),
            fw::plat::physical_cores(),
            tf(fw::plat::hires_timer_effective()),
            tf(fw::plat::affinity_available()),
            FW_CC_NAME,
            tp_field.c_str(),
            bc.c_str(),
            (long)now
        );
    });
    // C2 --dump-events: close the JSONL trace after the run; normal exit only (SIGINT skips atexit).
    std::atexit([](){ if (g_events_file) {
        std::fflush(g_events_file);
        std::fclose(g_events_file);
        g_events_file = nullptr;
    } });
    std::string mode = "f32";
    double delay_ms = 0;
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
    std::string pairwise_csv;   // W3-C3: minimal dual-engine/format compare pair "A,B"
    std::string probe_target;   // W3-C1: boundary probe batch table target (currently only "boundaries")
    std::int64_t refresh_ms = 0;
    bool quiet = false;
    bool refresh_explicit = false;
    int loop_count = 1;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "-h" || a == "--help") { print_help(); return 0; }
        if (a == "--self-test") { return run_self_test(); }
        if (a == "-s" || a == "--subnormal") {
            if (start_kind != StartKind::Default) { std::cerr << tr("错误：特殊起点互斥\n", "error: special start flags are mutually exclusive\n"); return 2; }
            start_kind = StartKind::Subnormal; continue;
        }
        if (a == "--start" || a == "-a") {
            if (i + 1 >= argc) { std::cerr << tr("错误：--start 需要一个值\n", "error: --start requires a value\n"); return 2; }
            std::string sv = argv[++i];
            if (sv == "min-normal" || sv == "minnormal" || sv == "normmin" || sv == "min") {
                if (start_kind != StartKind::Default) { std::cerr << tr("错误：特殊起点互斥\n", "error: special start flags are mutually exclusive\n"); return 2; }
                start_kind = StartKind::MinNormal;
            } else if (sv == "max-finite" || sv == "maxfinite" || sv == "infmax" || sv == "max") {
                if (start_kind != StartKind::Default) { std::cerr << tr("错误：特殊起点互斥\n", "error: special start flags are mutually exclusive\n"); return 2; }
                start_kind = StartKind::MaxFinite;
            } else {
                try { start_val = parse_number(sv); }
                catch (...) { std::cerr << tr("错误：无法解析起点 '", "error: cannot parse start '") << sanitize(sv) << tr("'\n", "'\n"); return 2; }
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
            else if (s == "subulp") step_mode = StepMode::SUB_ULP;
            else { std::cerr << tr("错误：未知步进 '", "error: unknown step '") << sanitize(s) << tr("'\n", "'\n"); return 2; }
            continue;
        }
        if (a == "-y" || a == "--yes") { g_assume_yes = true; continue; }
        if (a == "--compare") {
            if (i + 1 >= argc) { std::cerr << tr("错误：--compare 需要一个基线 JSON 文件\n",
                                                 "error: --compare requires a baseline JSON file\n"); return 2; }
            g_compare_path = argv[++i];
            continue;
        }
        if (a == "--pairwise" || a == "--crosscheck") {
            if (i + 1 >= argc) { std::cerr << tr("错误：--pairwise 需要两个逗号分隔格式 A,B\n",
                                                 "error: --pairwise requires two comma-separated formats A,B\n"); return 2; }
            pairwise_csv = argv[++i];
            continue;
        }
        if (a == "--probe") {
            if (i + 1 >= argc) { std::cerr << tr("错误：--probe 需要目标（当前仅 boundaries）\n",
                                                 "error: --probe requires a target (currently only boundaries)\n"); return 2; }
            probe_target = argv[++i];
            continue;
        }
        if (a == "--steps") {
            if (i + 1 >= argc) { std::cerr << tr("错误：--steps 需要一个非负整数步数\n",
                                                 "error: --steps requires a non-negative integer step count\n"); return 2; }
            std::string sv = argv[++i];
            try {
                std::size_t pos = 0;
                unsigned long long n = std::stoull(sv, &pos);
                if (pos != sv.size()) throw std::invalid_argument("trailing chars");
                if (n > 1000000000ULL) { std::cerr << tr("错误：--steps 上限 1000000000\n",
                                                         "error: --steps capped at 1000000000\n"); return 2; }
                g_fixed_steps = (u128)n;
            } catch (...) { std::cerr << tr("错误：--steps 应为非负整数\n",
                                            "error: --steps must be a non-negative integer\n"); return 2; }
            continue;
        }
        if (a == "--pin") {
            if (i + 1 >= argc) { std::cerr << tr("错误：--pin 需要一个逻辑核编号\n",
                                                 "error: --pin requires a logical core id\n"); return 2; }
            try { g_pin_core = std::stoi(argv[++i]); }
            catch (...) { std::cerr << tr("错误：--pin 应为整数核编号\n", "error: --pin must be an integer core id\n"); return 2; }
            continue;
        }
        if (a == "--boost") { g_boost_enabled = true; continue; }
        if (a == "--max-seconds") {
            if (i + 1 >= argc) { std::cerr << tr("错误：--max-seconds 需要一个非负整数秒值\n",
                                                 "error: --max-seconds requires a non-negative integer second value\n"); return 2; }
            try { g_max_seconds.store(std::stoi(argv[++i])); }
            catch (...) { std::cerr << tr("错误：--max-seconds 应为整数秒\n",
                                          "error: --max-seconds must be integer seconds\n"); return 2; }
            if (g_max_seconds.load() < 0) g_max_seconds.store(0);
            continue;
        }
        if (a == "--sample-window") {
            if (i + 1 >= argc) { std::cerr << tr("错误：--sample-window 需要一个毫秒窗口值\n",
                                                 "error: --sample-window requires a millisecond window value\n"); return 2; }
            try { g_sample_window_ms = std::stoll(argv[++i]); }
            catch (...) { std::cerr << tr("错误：--sample-window 应为整数毫秒\n",
                                          "error: --sample-window must be integer milliseconds\n"); return 2; }
            if (g_sample_window_ms < 0) g_sample_window_ms = 0;
            continue;
        }
        if (a == "--preset" || a == "--iron") {
            std::string preset_name = "iron";
            if (a == "--preset") {
                if (i + 1 >= argc) { std::cerr << tr("错误：--preset 需要预设名（当前仅 iron）\n",
                                                     "error: --preset requires a preset name (currently only iron)\n"); return 2; }
                preset_name = argv[++i];
                if (preset_name != "iron") { std::cerr << tr("错误：未知预设 '", "error: unknown preset '")
                                                       << sanitize(preset_name) << tr("'（当前仅 iron）\n", "' (currently only iron)\n"); return 2; }
            }
            // iron: enable the optional priority raise, but never auto-pin; a safety wall-clock
            // cap defaults to 60s below once the single-format guard accepts this run.
            g_iron_preset = true;
            g_boost_enabled = true;
            continue;
        }
        if (a == "--dump-events") {
            if (i + 1 >= argc) { std::cerr << tr("错误：--dump-events 需要一个 JSONL 文件路径\n",
                                                 "error: --dump-events requires a JSONL file path\n"); return 2; }
            g_events_file = std::fopen(argv[++i], "w");
            if (!g_events_file) { std::cerr << tr("错误：无法打开事件轨迹文件\n",
                                                  "error: cannot open event trace file\n"); return 2; }
            quiet = true;
            continue;
        }
        if (a == "-q" || a == "--quiet") { quiet = true; continue; }
        if (a == "--json") { g_json = true; quiet = true; continue; }
        if (a == "--dump") {
            if (i + 1 >= argc) { std::cerr << tr("错误：--dump 需要一个文件路径\n", "error: --dump requires a file path\n"); return 2; }
            g_dump_path = argv[++i];
            g_dump_file = std::fopen(g_dump_path, "wb+"); // wb+: truncate/write, but re-readable for payload CRC at exit
            if (!g_dump_file) { std::cerr << tr("错误：无法打开 dump 文件\n", "error: cannot open dump file\n"); return 2; }
            // header-space reserve (if any) is applied after parsing, once g_dump_raw is finalized
            g_dump_records_enabled = true;            // single-format path writes records
            quiet = true;
            continue;
        }
        if (a == "--dump-endian") {
            if (i + 1 >= argc) { std::cerr << tr("错误：--dump-endian 需要 little/big\n", "error: --dump-endian requires little/big\n"); return 2; }
            std::string e = argv[++i];
            if (e == "little" || e == "le" || e == "cpu") g_dump_endian = 0;
            else if (e == "big" || e == "be") g_dump_endian = 1;
            else { std::cerr << tr("错误：--dump-endian 仅支持 little/big\n", "error: --dump-endian only supports little/big\n"); return 2; }
            continue;
        }
        if (a == "--dump-packed") { g_dump_packed = 1; continue; }
        if (a == "--dump-raw" || a == "--dump-no-header") { g_dump_raw = true; continue; }
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
            g_fmt_name = mode;
            continue;
        }
        try { delay_ms = std::stod(a); has_delay = true; }
        catch (...) { std::cerr << tr("错误：无法解析参数 '", "error: cannot parse argument '") << sanitize(a) << tr("'\n", "'\n"); return 2; }
    }

    // W3-C3: minimal same-start dual-engine/format pairwise compare (opt-in, default off).
    // Two whitelisted formats run from the same default start/factor into silent RunResults;
    // reports steps/reason/width agreement. It does not touch the drive hot loop, the dump
    // pipeline, or the natural-termination baseline; independent early path, returns before
    // the single-format atexit JSON shell.
    if (!pairwise_csv.empty()) {
        std::vector<std::string> toks;
        {
            std::string rest = pairwise_csv;
            while (!rest.empty()) {
                std::string t;
                auto c = rest.find(',');
                if (c == std::string::npos) { t = rest; rest.clear(); }
                else { t = rest.substr(0, c); rest = rest.substr(c + 1); }
                size_t a = t.find_first_not_of(" \t");
                size_t b = t.find_last_not_of(" \t");
                if (a != std::string::npos) toks.push_back(t.substr(a, b - a + 1));
            }
        }
        if (toks.size() != 2) {
            std::cerr << tr("错误：--pairwise 需要恰好两个格式 A,B\n",
                            "error: --pairwise needs exactly two formats A,B\n"); return 2;
        }
        FmtId pa = fmt_canon(toks[0]);
        FmtId pb = fmt_canon(toks[1]);
        if (pa == FmtId::COUNT || pb == FmtId::COUNT) {
            std::cerr << tr("错误：--pairwise 含未知格式\n",
                            "error: --pairwise contains an unknown format\n"); return 2;
        }
        auto in_pair = [&](FmtId x, FmtId y) {
            return (pa == x && pb == y) || (pa == y && pb == x);
        };
        bool allowed = in_pair(FmtId::Dec32, FmtId::DPD32) ||
                        in_pair(FmtId::Dec64, FmtId::DPD64) ||
                        in_pair(FmtId::FP32, FmtId::TF32);
        if (!allowed) {
            std::cerr << tr("错误：--pairwise 最小白名单仅 dec32,dpd32 / dec64,dpd64 / f32,tf32\n",
                            "error: --pairwise minimal whitelist: dec32,dpd32 / dec64,dpd64 / f32,tf32\n"); return 2;
        }
        if (mode == "all" || loop_count != 1 || !only_csv.empty() || !jobs_spec.empty() ||
            g_fixed_steps > 0 || g_dump_file || g_pin_core >= 0 || g_boost_enabled || g_events_file) {
            std::cerr << tr("错误：--pairwise 与 -m all/--loop/--only/-j/--steps/--dump/--pin/--boost/--dump-events 互斥\n",
                            "error: --pairwise conflicts with -m all/--loop/--only/-j/--steps/--dump/--pin/--boost/--dump-events\n"); return 2;
        }
        auto pair_tok = [](RunResult::Reason r) -> const char* {
            switch (r) {
                case RunResult::OVERFLOW:    return "overflow";
                case RunResult::UNDERFLOW:   return "underflow";
                case RunResult::IDENTITY:     return "identity";
                case RunResult::SAFETY:       return "safety";
                case RunResult::TIMEOUT:      return "timeout";
                case RunResult::NOT_A_NUMBER: return "nan";
                case RunResult::FIXED_STEPS: return "fixed";
            }
            return "?";
        };
        struct Meta { const char* engine; int bits; int bytes; };
        auto meta = [](FmtId id) -> Meta {
            switch (id) {
                case FmtId::Dec32: return {"BID", 32, 4};
                case FmtId::DPD32: return {"DPD", 32, 4};
                case FmtId::Dec64: return {"BID", 64, 8};
                case FmtId::DPD64: return {"DPD", 64, 8};
                case FmtId::FP32:  return {"IEEE754", 32, 4};
                case FmtId::TF32:  return {"TF32-trunc", 19, 4};
                default: return {"?", 0, 0};
            }
        };
        bool as_json = g_json;
        g_json = false; // this block owns stdout; suppress the single-format atexit JSON shell
        RunParams pp{0.0, StartKind::Default, StepMode::X102, DispFmt::ENG, 1.0, 1.02, 6};
        RunResult ra{}, rb{};
        run_format(pa, pp, &ra);
        run_format(pb, pp, &rb);
        bool match = (ra.steps == rb.steps && ra.reason == rb.reason);
        if (as_json) {
            std::printf(
                "{\"tool\":\"ea-FRDT\",\"mode\":\"pairwise\",\"match\":%s,"
                "\"pair\":[\"%s\",\"%s\"],"
                "\"engines\":["
                "{\"format\":\"%s\",\"engine\":\"%s\",\"bits\":%d,\"bytes\":%d,\"steps\":%s,\"reason\":\"%s\",\"elapsed_ms\":%.3f},"
                "{\"format\":\"%s\",\"engine\":\"%s\",\"bits\":%d,\"bytes\":%d,\"steps\":%s,\"reason\":\"%s\",\"elapsed_ms\":%.3f}"
                "]}\n",
                match ? "true" : "false",
                toks[0].c_str(), toks[1].c_str(),
                toks[0].c_str(), meta(pa).engine, meta(pa).bits, meta(pa).bytes,
                to_string_u128(ra.steps).c_str(), pair_tok(ra.reason), ra.elapsed_ms,
                toks[1].c_str(), meta(pb).engine, meta(pb).bits, meta(pb).bytes,
                to_string_u128(rb.steps).c_str(), pair_tok(rb.reason), rb.elapsed_ms);
        } else {
            std::cout << "=== pairwise compare: same start 1.0, factor 1.02 ===\n";
            auto row = [&](FmtId id, const RunResult& r, const std::string& label) {
                Meta m = meta(id);
                std::cout << "  " << label << " [" << m.engine << "] bits=" << m.bits
                          << " bytes=" << m.bytes << " steps=" << to_string_u128(r.steps)
                          << " reason=" << pair_tok(r.reason) << " elapsed_ms=" << r.elapsed_ms << "\n";
            };
            row(pa, ra, toks[0]);
            row(pb, rb, toks[1]);
            std::cout << "  verdict: " << (match ? "MATCH (same steps and reason)" : "DIFF (steps/reason differ)") << "\n";
        }
        return 0;
    }

    // W3-C1: --probe boundaries batch table (opt-in, default off). Fast fixed-step sweep of
    // whitelisted quick formats x boundary starts; independent early path, never runs the
    // natural-termination walk and therefore cannot move the 17-step baseline.
    if (!probe_target.empty()) {
        if (probe_target != "boundaries") {
            std::cerr << tr("错误：--probe 仅支持 boundaries\n", "error: --probe supports only 'boundaries'\n"); return 2;
        }
        if (mode == "all" || loop_count != 1 || !only_csv.empty() || !jobs_spec.empty() ||
            g_fixed_steps > 0 || g_dump_file || g_pin_core >= 0 || g_boost_enabled || g_events_file ||
            !pairwise_csv.empty() || g_sample_window_ms > 0) {
            std::cerr << tr("错误：--probe boundaries 为独立模式，与 all/loop/only/jobs/steps/dump/pin/boost/events/pairwise/sample-window 互斥\n",
                            "error: --probe boundaries is standalone and conflicts with all/loop/only/jobs/steps/dump/pin/boost/events/pairwise/sample-window\n"); return 2;
        }
        struct PCell { const char* tok; const char* btag; u128 steps; RunResult::Reason reason; double ms; bool empty; };
        struct PDef { FmtId id; const char* tok; };
        const PDef fdefs[] = {
            {FmtId::FP4,"fp4"}, {FmtId::FP6,"fp6"}, {FmtId::FP8E4M3,"fp8-e4m3"}, {FmtId::FP8E5M2,"fp8-e5m2"},
            {FmtId::FP16,"f16"}, {FmtId::BF16,"bf16"}, {FmtId::TF32,"tf32"}, {FmtId::FP32,"f32"}
        };
        struct BDef { StartKind kind; const char* tag; };
        const BDef bdefs[] = {
            {StartKind::Default,"default"}, {StartKind::MinNormal,"min-normal"},
            {StartKind::MaxFinite,"max-finite"}, {StartKind::Subnormal,"subnormal"}
        };
        auto p_tok = [](RunResult::Reason r) -> const char* {
            switch (r) {
                case RunResult::OVERFLOW:    return "overflow";
                case RunResult::UNDERFLOW:   return "underflow";
                case RunResult::IDENTITY:    return "identity";
                case RunResult::SAFETY:       return "safety";
                case RunResult::TIMEOUT:      return "timeout";
                case RunResult::NOT_A_NUMBER: return "nan";
                case RunResult::FIXED_STEPS: return "fixed";
            }
            return "?";
        };
        bool as_json = g_json;
        g_json = false; // standalone path owns stdout; suppress the single-format atexit JSON shell
        constexpr unsigned long long PROBE_STEPS = 32;
        g_fixed_steps = PROBE_STEPS;
        std::vector<PCell> cells;
        cells.reserve(sizeof(fdefs)/sizeof(fdefs[0]) * sizeof(bdefs)/sizeof(bdefs[0]));
        for (const auto& fd : fdefs) {
            for (const auto& bd : bdefs) {
                RunParams pp{0.0, bd.kind, StepMode::X102, DispFmt::ENG, 1.0, 1.02, 6};
                RunResult r{};
                run_format(fd.id, pp, &r);
                cells.push_back({fd.tok, bd.tag, r.steps, r.reason, r.elapsed_ms, r.cur_value.empty()});
            }
        }
        g_fixed_steps = 0;
        if (as_json) {
            std::printf("{\"tool\":\"ea-FRDT\",\"mode\":\"probe\",\"target\":\"boundaries\",\"fixed_steps\":%llu,\"cells\":[",
                        PROBE_STEPS);
            for (std::size_t i = 0; i < cells.size(); ++i) {
                if (i) std::printf(",");
                const auto& c = cells[i];
                std::printf("{\"format\":\"%s\",\"boundary\":\"%s\",\"steps\":%s,\"reason\":\"%s\",\"elapsed_ms\":%.3f,\"value_ok\":%s}",
                            c.tok, c.btag, to_string_u128(c.steps).c_str(), p_tok(c.reason), c.ms,
                            c.empty ? "false" : "true");
            }
            std::printf("]}\n");
        } else {
            std::cout << "=== probe: boundaries (fixed 32 steps/cell; standalone, not the natural-termination baseline) ===\n";
            for (const auto& c : cells) {
                std::cout << "  " << c.tok << " [" << c.btag << "] steps=" << to_string_u128(c.steps)
                          << " reason=" << p_tok(c.reason) << " elapsed_ms=" << c.ms
                          << " value_ok=" << (c.empty ? "false" : "true") << "\n";
            }
            std::cout << "cells=" << cells.size() << "\n";
        }
        return 0;
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

    // Mode-compatibility guards: --loop / --only / --jobs are consumed only by -m all.
    // Reject them early in single mode instead of silently ignoring (or misreporting) them.
    if (mode != "all" && loop_count != 1) {
        g_json = false; // suppress the atexit single-format JSON shell for an early rejection
        std::cerr << tr("错误：--loop/-l 仅在 -m all 并发模式有效；单格式请移除 -l\n",
                        "error: --loop/-l applies only to -m all; remove -l for a single format\n");
        return 2;
    }
    if (mode != "all" && (!only_csv.empty() || !jobs_spec.empty())) {
        g_json = false; // suppress the atexit single-format JSON shell for an early rejection
        std::cerr << tr("错误：--only 与 --jobs/-j 仅在 -m all 并发模式有效；单格式请移除\n",
                        "error: --only and --jobs/-j apply only to -m all; remove them for a single format\n");
        return 2;
    }
    // W2-B1/C2 / W3-B3: --pin / --boost / --preset iron / --dump-events are single-format
    // opt-ins; the concurrent -m all path assigns workers and its own scheduling, so reject
    // rather than silently drop. --max-seconds remains a generic wall-clock cap usable in all.
    if (mode == "all" && (g_pin_core >= 0 || g_boost_enabled || g_iron_preset || g_events_file)) {
        g_json = false;
        if (g_events_file) { std::fclose(g_events_file); g_events_file = nullptr; }
        std::cerr << tr("错误：--pin / --boost / --preset iron / --dump-events 仅在单格式有效；-m all 请移除\n",
                        "error: --pin / --boost / --preset iron / --dump-events apply only to a single format; remove them for -m all\n");
        return 2;
    }
    // W3-B3: iron safety cap defaults to 60s when the user did not set --max-seconds explicitly.
    // Only reached for an accepted single-format run (the -m all rejection returned above).
    if (g_iron_preset && g_max_seconds.load() <= 0) g_max_seconds.store(60);
    if (g_pin_core >= 0) {
        int hw = fw::plat::logical_cores();
        if (hw > 0 && (g_pin_core < 0 || g_pin_core >= hw)) {
            std::cerr << tr("错误：--pin 核号越界（本机逻辑核=", "error: --pin core out of range (logical cores=")
                      << hw << tr("；合法 0..", "; valid 0..") << (hw - 1) << ")\n";
            return 2;
        }
    }
    if (!std::isnan(start_val) && !std::isfinite(start_val)) { std::cerr << tr("错误：起点必须为有限数值\n", "error: start must be a finite number\n"); return 2; }

    g_json_factor = factor;
    g_dump_factor = factor;
    g_compare_key = mode;  // anchor record is matched by the same format token
    g_json_start = std::isnan(start_val) ? 1.0 : start_val;
    g_json_step_mode = (int)step_mode;
    g_json_digits = digits;
    g_json_delay_ms = delay_ms;
    g_json_loop = (mode == "all") ? loop_count : 1; // loop executes only in -m all; single format reports honest 1
    // v2 dump header metadata
    g_dump_step_mode = (int)step_mode;
    g_dump_start_kind = (int)start_kind;
    g_dump_start_value = std::isnan(start_val) ? 1.0 : start_val;

    // Collect all warnings for unified confirmation
    // level: 0=[?] blue info, 1=[!] yellow warn, 2=[!!!] red critical
    std::vector<std::pair<int,std::string>> warns;
    if (loop_count == -1 && g_timeout_sec.load() == 0) {
        warns.push_back({1, tr("警告：无限循环（-l -1）且无超时（-t 0），可能导致 CPU 占用 100%",
                               "warn: infinite loop (-l -1) with no timeout (-t 0), may cause 100% CPU")});
    }
    if (factor > 0.0 && factor < 1e-10) {
        char buf[128];
        std::snprintf(buf, sizeof(buf), "%g", factor);
        warns.push_back({1, tr("警告：倍率极小（", "warn: factor very small (") + buf + tr("），步数可能爆炸", "), steps may explode")});
    }
    // DPD / dec128 warnings: check single mode or all+--only
    bool is_dpd = (mode == "dpd32" || mode == "dpd64" || mode == "dpd128" ||
                   mode == "dpd-dec32" || mode == "dpd-dec64" || mode == "dpd-dec128");
    bool is_dec128 = (mode == "dec128" || mode == "decimal128");
    if (mode == "all") {
        if (only_csv.empty()) {
            // Running all 17 formats: DPD32/64/128 and dec128 are included.
            is_dpd = true;
            is_dec128 = true;
        } else {
            std::string s = only_csv;
            for (auto& c : s) c = (char)tolower(c);
            if (s.find("dpd") != std::string::npos) is_dpd = true;
            if (s.find("dec128") != std::string::npos) is_dec128 = true;
        }
    }
    if (is_dpd) {
        warns.push_back({2, tr("DPD 是纯软件十进制数组运算：逐位乘法+进位+舍入，无硬件十进制浮点单元，性能可能降低 20-30 倍，低端 CPU 满载时间更长。包含格式：DPD32/DPD64/DPD128。",
                               "DPD is pure software decimal-array arithmetic: per-digit multiply+carry+round, no hardware decimal FPU, performance may drop 20-30x, weak CPUs stay loaded longer. Includes DPD32/DPD64/DPD128.")});
    }
    if (is_dec128) {
        warns.push_back({2, tr("dec128 每步 UInt256 schoolbook 大整数乘法，性能比 DPD128 暴跌 2-3 倍以上。",
                               "dec128 each step uses UInt256 schoolbook big-integer multiply, performance drops 2-3x vs DPD128.")});
    }
    if (mode == "all") {
        // Quiet/non-interactive mode skips the warning prompt entirely; interactive
        // concurrent runs still see the yellow/red warnings and get a y/n gate.
        if (!quiet && !confirm_warnings(warns)) return 1;
        if (g_dump_file) {
            g_dump_records_enabled = false; // mixed-width concurrent workers would interleave incompatible record sizes
            if (g_dump_raw) {
                // raw stream has no header to fall back on; discard the empty file rather than leave a zero-byte stub
                std::fclose(g_dump_file); g_dump_file = nullptr;
                std::remove(g_dump_path);
                std::cerr << tr("提示：-m all 为并发多格式模式，不支持 --dump-raw/--dump-no-header 裸流（记录宽度交织），已放弃该输出文件。\n",
                                "note: -m all is concurrent multi-format; --dump-raw/--dump-no-header stream is unsupported (mixed record widths); the output file was discarded.\n");
            } else {
                // v2 mode: keep the file as an empty header stub (flags bit2=empty, payload_bytes=0)
                std::cerr << tr("提示：-m all 为并发多格式模式，--dump 不序列化记录（避免不同记录宽度交织），仅写出空头部。\n",
                                "note: -m all is concurrent multi-format; --dump records are not serialized (mixed record widths); only an empty header stub is written.\n");
            }
        }
        if (!refresh_explicit) refresh_ms = 1000;
        int hw = (int)std::thread::hardware_concurrency();
        auto jobs_list = parse_jobs(jobs_spec, hw);
        run_all(delay_ms, start_kind, step_mode, disp_fmt, start_val, factor, digits, jobs_list, only_csv, refresh_ms, loop_count, quiet);
        return 0;
    }
    // Single-format path: warnings are NOT printed here. The user has explicitly chosen
    // this format/parameters; yellow/red warnings only surface in concurrent -m all mode
    // via the interactive confirm_warnings gate above.
    // W3-B3: iron preset disclosure on stderr only (stdout stays pure JSON under --json).
    // It raises priority and applies a wall-clock cap, but never auto-pins and never reads a
    // temperature/throttle sensor (no portable Windows API here; no automatic frequency drop).
    if (g_iron_preset) {
        std::cerr << tr("[iron] 提级开启；墙钟安全上限 ", "[iron] priority boost on; wall-clock cap ")
                  << g_max_seconds.load()
                  << tr(" s；未自动绑核（保留逻辑核给系统）；不读取温度/节流状态、不自动降频，请确保散热。\n",
                        " s; not auto-pinned (cores reserved for the OS); no temperature/throttle readout and no automatic throttling — ensure adequate cooling.\n");
    }
    // dump metadata: format_id and record_size
    if (g_dump_file) {
        if (!g_dump_raw) std::fseek(g_dump_file, 256, SEEK_SET); // reserve v2 header space; raw stream starts at offset 0
        if (mode=="fp4")       { g_dump_format_id=0;  g_dump_record_size=1; }
        else if (mode=="fp6") { g_dump_format_id=1;  g_dump_record_size=1; }
        else if (mode=="fp8"||mode=="fp8-e4m3") { g_dump_format_id=2; g_dump_record_size=1; }
        else if (mode=="fp8-e5m2") { g_dump_format_id=3; g_dump_record_size=1; }
        else if (mode=="f16"||mode=="fp16") { g_dump_format_id=4; g_dump_record_size=2; }
        else if (mode=="bf16") { g_dump_format_id=5; g_dump_record_size=2; }
        else if (mode=="tf32") { g_dump_format_id=6; g_dump_record_size=4; }
        else if (mode=="f32"||mode=="fp32") { g_dump_format_id=7; g_dump_record_size=4; }
        else if (mode=="dec32"||mode=="decimal32") { g_dump_format_id=8; g_dump_record_size=4; }
        else if (mode=="dpd32"||mode=="dpd-dec32") { g_dump_format_id=9; g_dump_record_size=4; }
        else if (mode=="f64"||mode=="fp64") { g_dump_format_id=10; g_dump_record_size=8; }
        else if (mode=="dec64"||mode=="decimal64") { g_dump_format_id=11; g_dump_record_size=8; }
        else if (mode=="dpd64"||mode=="dpd-dec64") { g_dump_format_id=12; g_dump_record_size=8; }
        else if (mode=="x87")  { g_dump_format_id=13; g_dump_record_size=16; } // 16B slot: 10B valid + 6B zero padding
        else if (mode=="fp128"){ g_dump_format_id=14; g_dump_record_size=16; }
        else if (mode=="dec128"||mode=="decimal128") { g_dump_format_id=15; g_dump_record_size=16; }
        else if (mode=="dpd128"||mode=="dpd-dec128") { g_dump_format_id=16; g_dump_record_size=16; }
    }
    // W2-B1: optional single-format affinity/priority inside a scope guard; every early
    // `return 0` runs the destructor and restores full affinity plus NORMAL priority.
    struct SingleSchedGuard {
        bool applied = false;
        ~SingleSchedGuard() { if (applied) fw::plat::restore_thread_scheduling(); }
    } sched_guard;
    if (g_pin_core >= 0) { fw::plat::set_thread_affinity(g_pin_core); sched_guard.applied = true; }
    if (g_boost_enabled)  { fw::plat::boost_thread_priority(); sched_guard.applied = true; }

    // W3-B5: reset throughput samples before the single-format walk (process runs one walk).
    g_rate_segments.clear(); g_rate_n = 0;
    g_seg_prev_ms = 0.0; g_seg_prev_steps = 0;
    g_rate_min = g_rate_avg = g_rate_p95 = g_rate_max = 0.0;
    RunResult* q = (quiet || g_json || g_dump_file || g_events_file ? &g_result : nullptr);
    if (mode == "f16" || mode == "fp16")            { run_fp16(delay_ms, start_kind, step_mode, disp_fmt, start_val, factor, digits, show_bits, q, refresh_ms); return 0; }
    if (mode == "f32" || mode == "fp32")            { run_fp32(delay_ms, start_kind, step_mode, disp_fmt, start_val, factor, digits, show_bits, q, refresh_ms); return 0; }
    if (mode == "f64" || mode == "fp64")            { run_fp64(delay_ms, start_kind, step_mode, disp_fmt, start_val, factor, digits, show_bits, q, refresh_ms); return 0; }
    if (mode == "x87")                              { run_x87(delay_ms, start_kind, step_mode, disp_fmt, start_val, factor, digits, show_bits, q, refresh_ms); return 0; }
    if (mode == "fp4")        { run_fp4(delay_ms, start_kind, step_mode, disp_fmt, start_val, factor, digits, show_bits, q, refresh_ms); return 0; }
    if (mode == "fp6")        { run_fp6(delay_ms, start_kind, step_mode, disp_fmt, start_val, factor, digits, show_bits, q, refresh_ms); return 0; }
    if (mode == "fp8" || mode == "fp8-e4m3") { run_fp8_e4m3(delay_ms, start_kind, step_mode, disp_fmt, start_val, factor, digits, show_bits, q, refresh_ms); return 0; }
    if (mode == "fp8-e5m2")   { run_fp8_e5m2(delay_ms, start_kind, step_mode, disp_fmt, start_val, factor, digits, show_bits, q, refresh_ms); return 0; }
    if (mode == "bf16")       { run_bf16(delay_ms, start_kind, step_mode, disp_fmt, start_val, factor, digits, show_bits, q, refresh_ms); return 0; }
    if (mode == "tf32")       { run_tf32(delay_ms, start_kind, step_mode, disp_fmt, start_val, factor, digits, show_bits, q, refresh_ms); return 0; }
    if (mode == "dec32" || mode == "decimal32") { run_dec32(delay_ms, start_kind, step_mode, disp_fmt, start_val, factor, digits, show_bits, q, refresh_ms); return 0; }
    if (mode == "dec64" || mode == "decimal64") { run_dec64(delay_ms, start_kind, step_mode, disp_fmt, start_val, factor, digits, show_bits, q, refresh_ms); return 0; }
    if (mode == "dec128" || mode == "decimal128") { run_dec128(delay_ms, start_kind, step_mode, disp_fmt, start_val, factor, digits, show_bits, q, refresh_ms); return 0; }
    if (mode == "dpd32" || mode == "dpd-dec32") { run_dpd32(delay_ms, start_kind, step_mode, disp_fmt, start_val, factor, digits, show_bits, q, refresh_ms); return 0; }
    if (mode == "dpd64" || mode == "dpd-dec64") { run_dpd64(delay_ms, start_kind, step_mode, disp_fmt, start_val, factor, digits, show_bits, q, refresh_ms); return 0; }
    if (mode == "dpd128" || mode == "dpd-dec128") { run_dpd128(delay_ms, start_kind, step_mode, disp_fmt, start_val, factor, digits, show_bits, q, refresh_ms); return 0; }
    if (mode == "fp128") { run_fp128(delay_ms, start_kind, step_mode, disp_fmt, start_val, factor, digits, show_bits, q, refresh_ms); return 0; }

    std::cerr << tr("错误：未知模式 '", "error: unknown mode '") << sanitize(mode) << tr("'（见 -h）\n", "' (see -h)\n");
    return 2;
}
