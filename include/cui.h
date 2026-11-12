// cui.h — console presentation layer: colour policy, display-width alignment,
// section rules, boxed tables, and shared numeric formatters.
//
// Every human-facing print goes through here so the terminal UI stays consistent:
// the same palette, the same column alignment, the same time/number spelling.
// Machine-readable output (--json / --dump-events / --compare) never touches cui.
#pragma once
#if defined(_WIN32) && !defined(__MINGW32__)
#error "ea-FRDT requires MinGW target (x86_64-w64-mingw32 / x86_64-w64-windows-gnu). Do not use MSVC or LLVM's default windows-msvc clang; use MSYS2 clang64."
#endif
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <iostream>
#include <ostream>
#include <sstream>
#include <string>
#include <vector>
#include "color.h"
#include "base_types.h"

namespace fw::cui {

// ---------------------------------------------------------------- colour policy
// One process-wide switch. Off means every paint() call degrades to plain text,
// which keeps redirected output and NO_COLOR sessions free of escape bytes.
inline bool& color_state() { static bool on = true; return on; }
inline bool color_on() { return color_state(); }
inline void set_color(bool on) { color_state() = on; }

inline std::string paint(const char* code, const std::string& s) {
    if (!color_on() || code == nullptr) return s;
    std::string out;
    out.reserve(s.size() + 8);
    out += code;
    out += s;
    out += clr::RST;
    return out;
}

// Semantic styles, not raw escape codes: call sites name the role of the text.
inline std::string title(const std::string& s)  { return paint(clr::CYN, s); }
inline std::string label(const std::string& s)  { return paint(clr::GRY, s); }
inline std::string num(const std::string& s)    { return paint(clr::YEL, s); }
inline std::string ok(const std::string& s)     { return paint(clr::GRN, s); }
inline std::string warn(const std::string& s)   { return paint(clr::YEL, s); }
inline std::string err(const std::string& s)    { return paint(clr::RED, s); }
inline std::string accent(const std::string& s) { return paint(clr::BLU, s); }
inline std::string dim(const std::string& s)    { return paint(clr::DIM, s); }

// ------------------------------------------------------------------ glyphs
inline const char* G_RULE  = "─";
inline const char* G_TITLE = "▌";
 // box drawing
inline const char* G_TL = "┌"; inline const char* G_TM = "┬"; inline const char* G_TR = "┐";
inline const char* G_ML = "├"; inline const char* G_MM = "┼"; inline const char* G_MR = "┤";
inline const char* G_BL = "└"; inline const char* G_BM = "┴"; inline const char* G_BR = "┘";
inline const char* G_V  = "│";

// ------------------------------------------------------------- display width
// CJK / full-width characters occupy two terminal columns; everything else one.
inline int w(const std::string& s) {
    int width = 0;
    for (std::size_t i = 0; i < s.size();) {
        unsigned char c = (unsigned char)s[i];
        if (c < 0x80) { width += 1; i += 1; }
        else if ((c >> 5) == 0x6 && i + 1 < s.size()) { width += 2; i += 2; }
        else if ((c >> 4) == 0xE && i + 2 < s.size()) { width += 2; i += 3; }
        else if ((c >> 3) == 0xF && i + 3 < s.size()) { width += 2; i += 4; }
        else { width += 1; i += 1; }
    }
    return width;
}

inline std::string pad_r(const std::string& s, int width) {
    int cur = w(s);
    return cur >= width ? s : s + std::string((std::size_t)(width - cur), ' ');
}
inline std::string pad_l(const std::string& s, int width) {
    int cur = w(s);
    return cur >= width ? s : std::string((std::size_t)(width - cur), ' ') + s;
}

// Clip to a display width, marking truncation with "~" so a long value can never
// push a column out of alignment.
inline std::string clip(const std::string& s, int width) {
    if (w(s) <= width) return s;
    std::string out;
    int acc = 0;
    for (std::size_t i = 0; i < s.size();) {
        unsigned char c = (unsigned char)s[i];
        std::size_t len = 1;
        if (c < 0x80) len = 1;
        else if ((c >> 5) == 0x6) len = 2;
        else if ((c >> 4) == 0xE) len = 3;
        else if ((c >> 3) == 0xF) len = 4;
        if (i + len > s.size()) len = 1;
        int cw = len > 1 ? 2 : 1;
        if (acc + cw > width - 1) break;
        out.append(s, i, len);
        acc += cw;
        i += len;
    }
    if (width >= 1) out += "~";
    return out;
}

inline int term_width() {
    if (const char* c = std::getenv("COLUMNS")) {
        int v = std::atoi(c);
        if (v >= 48 && v <= 4096) return v - 1;
    }
    return 88;   // conservative default: keeps the rule inside an 90-column console
}

inline std::string rule(int width) {
    if (width < 8) width = 8;
    std::string s;
    s.reserve((std::size_t)width * 3);
    for (int i = 0; i < width; ++i) s += G_RULE;
    return s;
}

// ---------------------------------------------------------------- section head
// "▌ TITLE" + optional indented subtitle + a dim horizontal rule.
inline std::string section(const std::string& head, const std::string& sub = {}) {
    std::string out;
    out += paint(clr::CYN, std::string(G_TITLE) + " " + head);
    out += "\n";
    if (!sub.empty()) out += std::string("  ") + paint(clr::GRY, sub) + "\n";
    out += paint(clr::DIM, rule(term_width()));
    out += "\n";
    return out;
}

// Aligned "  label   value" row; label column width is fixed per block so a
// group of rows reads as a table without drawing one.
inline std::string kv(const std::string& k, const std::string& v, int label_w) {
    return std::string("  ") + paint(clr::GRY, pad_r(k, label_w)) + "  " + v + "\n";
}

// "  NAME   description" row: the name column is measured and padded on plain
// text, then coloured, and every continuation line is indented to the same
// column, so a long description wraps as a block instead of drifting left.
inline std::string row(const std::string& left, const std::string& right, int left_w,
                       const char* left_code = clr::YEL) {
    std::string out = std::string("  ") + paint(left_code, pad_r(left, left_w)) + "  ";
    const std::string indent((std::size_t)(left_w + 4), ' ');
    bool first = true;
    std::size_t pos = 0;
    for (;;) {
        std::size_t nl = right.find('\n', pos);
        std::string seg = right.substr(pos, nl == std::string::npos ? right.size() - pos : nl - pos);
        if (!first) out += indent;
        out += seg;
        out += '\n';
        if (nl == std::string::npos) break;
        first = false;
        pos = nl + 1;
    }
    return out;
}

// ------------------------------------------------------------- formatters
// 12345678 -> "12,345,678"; grouping only, never locale-dependent.
inline std::string group(const std::string& digits) {
    std::string out;
    int n = (int)digits.size();
    for (int i = 0; i < n; ++i) {
        if (i > 0 && (n - i) % 3 == 0) out += ',';
        out += digits[(std::size_t)i];
    }
    return out;
}
inline std::string count(u128 v) { return group(to_string_u128(v)); }
inline std::string count(std::uint64_t v) { return group(std::to_string(v)); }

// Compact throughput: 987 / 8.62K / 51.2M / 1.03G.
inline std::string rate(double v) {
    char buf[32];
    if (v < 1000.0) {
        std::snprintf(buf, sizeof(buf), "%.0f", v);
    } else if (v < 1e6) {
        std::snprintf(buf, sizeof(buf), "%.2fK", v / 1e3);
    } else if (v < 1e9) {
        std::snprintf(buf, sizeof(buf), "%.2fM", v / 1e9 * 1e3);
    } else {
        std::snprintf(buf, sizeof(buf), "%.2fG", v / 1e9);
    }
    return buf;
}

// Wall-clock duration, always the same shape for a given magnitude:
//   812ms   26.384s   3m12.4s   2h03m   1d02h
inline std::string elapsed(double ms) {
    char buf[48];
    if (ms < 10.0) {
        std::snprintf(buf, sizeof(buf), "%.2fms", ms);
        return buf;
    }
    if (ms < 1000.0) {
        std::snprintf(buf, sizeof(buf), "%.0fms", ms);
        return buf;
    }
    long long total_s = (long long)(ms / 1000.0);
    int frac = (int)((long long)ms % 1000);
    if (total_s < 60) {
        std::snprintf(buf, sizeof(buf), "%lld.%03ds", total_s, frac);
    } else if (total_s < 3600) {
        std::snprintf(buf, sizeof(buf), "%lldm%02lld.%01ds",
                      total_s / 60, total_s % 60, frac / 100);
    } else if (total_s < 86400) {
        std::snprintf(buf, sizeof(buf), "%lldh%02lldm",
                      total_s / 3600, (total_s % 3600) / 60);
    } else {
        std::snprintf(buf, sizeof(buf), "%lldd%02lldh",
                      total_s / 86400, (total_s % 86400) / 3600);
    }
    return buf;
}

// Exit-reason styling: normal termination reads green, abnormal reads yellow,
// NaN reads red. Text itself is untouched. The token may be either the short
// machine token ("nan") or the full display string ("nan->nan").
inline std::string reason(const std::string& text, const std::string& token) {
    auto starts = [&](const char* p) { return token.rfind(p, 0) == 0; };
    if (starts("nan")) return err(text);
    if (starts("timeout") || starts("safety")) return warn(text);
    if (starts("overflow") || starts("underflow")) return ok(text);
    if (starts("fixed")) return accent(text);
    return text;
}

// ------------------------------------------------------- terminal control
// Every cursor/erase sequence used by the program is spelled here once.
inline const char* CLEAR      = "\033[K";      // erase from cursor to end of line
inline const char* CLEAR_LINE = "\r\033[K";    // back to column 0, erase the line
inline const char* CLEAR_ALL  = "\r\033[2K";   // back to column 0, erase the whole line
inline std::string up(int lines) { return "\033[" + std::to_string(lines) + "A\r"; }

// Colour a whole line while keeping the terminating newline outside the escape,
// so the next line never starts with a stray reset.
inline std::string paint_line(const char* code, const std::string& s) {
    if (!color_on()) return s;
    std::string body = s;
    bool nl = !body.empty() && body.back() == '\n';
    if (nl) body.pop_back();
    std::string out;
    out.reserve(body.size() + 12);
    out += code;
    out += body;
    out += clr::RST;
    if (nl) out += '\n';
    return out;
}

// Diagnostic sink: wraps an ostream and colours the composed line on destruction.
// Call sites keep their existing `<<` chains, they only name the severity.
class DiagStream {
public:
    DiagStream(const char* code, std::ostream& os) : code_(code), os_(os) {}
    template <class T> DiagStream& operator<<(const T& v) {
        buf_ << v;
        return *this;
    }
    ~DiagStream() { os_ << paint_line(code_, buf_.str()); }
private:
    const char* code_;
    std::ostream& os_;
    std::ostringstream buf_;
};

inline DiagStream err()  { return DiagStream(clr::RED, std::cerr); }   // 错误
inline DiagStream note() { return DiagStream(clr::YEL, std::cerr); }   // 提示 / 警告

// ------------------------------------------------- single-format run banner
// Text is byte-identical to the pre-cui rendering; only styling moved here.
inline std::string walk_header(const std::string& label, const std::string& start_tag,
                               const std::string& step_desc, double delay_ms) {
    char d[32];
    std::snprintf(d, sizeof(d), "%g", delay_ms);
    return accent(label) + " walk: start=" + start_tag + ", " + step_desc
         + ", delay=" + std::string(d) + "ms (0=unlimited)\n";
}

inline std::string bid_header(const std::string& coeff, int exp10, const std::string& hex) {
    return std::string("  ") + dim("[BID]") + " coeff=" + coeff
         + " exp10=" + std::to_string(exp10) + " hex=" + accent(hex) + "\n";
}

inline std::string bid_selfcheck_line(bool pass, const std::string& errs) {
    std::string head = std::string("  ") + dim("[BID-SELFCHECK]") + " ";
    if (pass) return head + ok("OK") + "\n";
    return head + err("MISMATCH" + errs) + "\n";
}

// -------------------------------------------------------------------- table
// Auto-width boxed table. Cells are plain strings (no ANSI): widths are measured
// on plain text, then the header row is coloured so alignment can never drift.
class Table {
public:
    void col(const std::string& head, bool right = false) {
        cols_.push_back({head, right});
    }
    void row(std::vector<std::string> cells) {
        rows_.push_back(std::move(cells));
    }
    void print(std::ostream& os) const {
        if (cols_.empty()) return;
        std::vector<int> wid(cols_.size(), 0);
        for (std::size_t i = 0; i < cols_.size(); ++i) wid[i] = w(cols_[i].head);
        for (const auto& r : rows_)
            for (std::size_t i = 0; i < cols_.size() && i < r.size(); ++i)
                wid[i] = std::max(wid[i], w(r[i]));
        for (auto& x : wid) x += 2;   // one space of padding on each side

        auto hline = [&](const char* l, const char* mid, const char* r) {
            std::string s = l;
            for (std::size_t i = 0; i < wid.size(); ++i) {
                if (i) s += mid;
                for (int k = 0; k < wid[i]; ++k) s += G_RULE;
            }
            s += r;
            return s;
        };
        auto cell = [&](const std::string& text, std::size_t i) {
            int inner = wid[i] - 2;
            std::string t = cols_[i].right ? pad_l(text, inner) : pad_r(text, inner);
            return std::string(" ") + t + " ";
        };
        auto data_row = [&](const std::vector<std::string>& cells, bool head_row) {
            std::string s = G_V;
            for (std::size_t i = 0; i < cols_.size(); ++i) {
                std::string text = i < cells.size() ? cells[i] : std::string();
                std::string c = cell(text, i);
                s += head_row ? paint(clr::BLU, c) : c;
                s += G_V;
            }
            return s;
        };

        os << paint(clr::DIM, hline(G_TL, G_TM, G_TR)) << "\n";
        std::vector<std::string> heads;
        for (const auto& c : cols_) heads.push_back(c.head);
        os << data_row(heads, true) << "\n";
        os << paint(clr::DIM, hline(G_ML, G_MM, G_MR)) << "\n";
        for (const auto& r : rows_) os << data_row(r, false) << "\n";
        os << paint(clr::DIM, hline(G_BL, G_BM, G_BR)) << "\n";
    }
private:
    struct Col { std::string head; bool right; };
    std::vector<Col> cols_;
    std::vector<std::vector<std::string>> rows_;
};

} // namespace fw::cui
