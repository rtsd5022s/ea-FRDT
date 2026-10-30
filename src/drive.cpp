// drive.cpp — Non-template driver function implementations
#include "drive.h"

namespace fw {

void on_sigint(int) {
    std::cerr << "\r\033[2K"
              << tr("程序提前退出（收到 Ctrl+C）\n", "Interrupted (Ctrl+C)\n");
    std::_Exit(130);
}

const char* step_name(StepMode sm) {
    switch (sm) {
        case StepMode::X102:    return "multiplied";
        case StepMode::MUL_ULP: return "mululp(x1+1ULP)";
        case StepMode::ADD_ULP: return "addulp(+1ULP)";
    }
    return "?";
}

std::string step_factor_str(StepMode sm, double factor) {
    if (sm == StepMode::X102) {
        char buf[32];
        if (factor == (double)(long long)factor)
            std::snprintf(buf, sizeof(buf), "x%lld", (long long)factor);
        else
            std::snprintf(buf, sizeof(buf), "x%g", factor);
        return std::string("step=multiplied, ") + buf;
    }
    return std::string("step=") + step_name(sm);
}

const char* reason_name(RunResult::Reason r) {
    switch (r) {
        case RunResult::OVERFLOW: return "overflow->inf";
        case RunResult::UNDERFLOW: return "underflow->0";
        case RunResult::IDENTITY: return "identity(stopped)";
        case RunResult::SAFETY: return "safety-limit";
        case RunResult::TIMEOUT: return "timeout";
        case RunResult::NOT_A_NUMBER: return "nan->nan";
    }
    return "?";
}

} // namespace fw
