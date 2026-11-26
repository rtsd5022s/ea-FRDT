// cli_main.cpp — minimal DLL-linked host for libeafrdt.
//
// This is deliberately not the full ea-FRDT CLI: it exercises the C ABI that
// ships in libeafrdt.dll. Use the standalone ea-FRDT.exe for the complete
// option surface (dump/JSON/multithreading/etc.).
#include "c_api.h"

#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

struct FormatRow {
    const char* name;
    enum eafrdt_fmt id;
};

static const FormatRow kFormats[] = {
    {"fp4",        EAFRDT_FP4},
    {"fp6",        EAFRDT_FP6},
    {"fp8-e4m3",   EAFRDT_FP8_E4M3},
    {"fp8e4m3",    EAFRDT_FP8_E4M3},
    {"fp8-e5m2",   EAFRDT_FP8_E5M2},
    {"fp8e5m2",    EAFRDT_FP8_E5M2},
    {"f16",        EAFRDT_FP16},
    {"fp16",       EAFRDT_FP16},
    {"bf16",       EAFRDT_BF16},
    {"tf32",       EAFRDT_TF32},
    {"f32",        EAFRDT_F32},
    {"fp32",       EAFRDT_F32},
    {"f64",        EAFRDT_F64},
    {"fp64",       EAFRDT_F64},
    {"x87",        EAFRDT_X87},
    {"fp128",      EAFRDT_Q128},
    {"q128",       EAFRDT_Q128},
    {"dec32",      EAFRDT_BID32},
    {"bid32",      EAFRDT_BID32},
    {"dec64",      EAFRDT_BID64},
    {"bid64",      EAFRDT_BID64},
    {"dec128",     EAFRDT_BID128},
    {"bid128",     EAFRDT_BID128},
    {"dpd32",      EAFRDT_DPD32},
    {"dpd64",      EAFRDT_DPD64},
    {"dpd128",     EAFRDT_DPD128},
};

static void usage(const char* argv0) {
    std::fprintf(stderr,
        "usage: %s <format> [start=1.0] [multiplier=1.02] [timeout_sec=0]\n"
        "       %s --list\n"
        "       %s --version\n"
        "formats: fp4 fp6 fp8-e4m3 fp8-e5m2 f16 bf16 tf32 f32 f64 x87 "
        "fp128 dec32 dec64 dec128 dpd32 dpd64 dpd128\n",
        argv0, argv0, argv0);
}

static const FormatRow* find_format(const char* name) {
    for (const auto& row : kFormats) {
        if (std::strcmp(name, row.name) == 0) return &row;
    }
    return nullptr;
}

static bool parse_double(const char* text, double* out) {
    errno = 0;
    char* end = nullptr;
    const double value = std::strtod(text, &end);
    if (errno != 0 || end == text || *end != '\0') return false;
    *out = value;
    return true;
}

static const char* reason_name(int reason) {
    switch (reason) {
    case EAFRDT_OVERFLOW: return "overflow";
    case EAFRDT_UNDERFLOW: return "underflow";
    case EAFRDT_TIMEOUT: return "timeout";
    case EAFRDT_IDENTITY: return "identity";
    case EAFRDT_NOT_A_NUMBER: return "nan";
    default: return "unknown";
    }
}

int main(int argc, char** argv) {
    if (argc == 2 && std::strcmp(argv[1], "--version") == 0) {
        std::puts(eafrdt_version());
        return 0;
    }
    if (argc == 2 && std::strcmp(argv[1], "--list") == 0) {
        for (const auto& row : kFormats) {
            if (std::strcmp(row.name, "fp8e4m3") == 0 ||
                std::strcmp(row.name, "fp8e5m2") == 0 ||
                std::strcmp(row.name, "fp16") == 0 ||
                std::strcmp(row.name, "fp32") == 0 ||
                std::strcmp(row.name, "fp64") == 0 ||
                std::strcmp(row.name, "q128") == 0 ||
                std::strcmp(row.name, "bid32") == 0 ||
                std::strcmp(row.name, "bid64") == 0 ||
                std::strcmp(row.name, "bid128") == 0) {
                continue;  // aliases
            }
            std::puts(row.name);
        }
        return 0;
    }
    if (argc < 2 || argc > 5) {
        usage(argv[0]);
        return 2;
    }

    const FormatRow* format = find_format(argv[1]);
    if (!format) {
        std::fprintf(stderr, "unknown format: %s\n", argv[1]);
        usage(argv[0]);
        return 2;
    }

    double start = 1.0;
    double multiplier = 1.02;
    double timeout_sec = 0.0;
    if ((argc >= 3 && !parse_double(argv[2], &start)) ||
        (argc >= 4 && !parse_double(argv[3], &multiplier)) ||
        (argc >= 5 && !parse_double(argv[4], &timeout_sec))) {
        std::fprintf(stderr, "invalid numeric argument\n");
        return 2;
    }
    if (!std::isfinite(start) || !std::isfinite(multiplier) ||
        !std::isfinite(timeout_sec) || multiplier == 0.0 || timeout_sec < 0.0) {
        std::fprintf(stderr, "start/multiplier must be finite; multiplier != 0; timeout >= 0\n");
        return 2;
    }

    int reason = -1;
    const long long steps = eafrdt_walk(format->id, start, multiplier, timeout_sec, &reason);
    std::printf("%s\tsteps=%lld\treason=%d (%s)\n",
                format->name, steps, reason, reason_name(reason));
    return 0;
}
