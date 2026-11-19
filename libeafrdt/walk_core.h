// walk_core.h — internal generic step loop shared by every libeafrdt walk.
// No IO, no threads, no globals. Not part of the public C++ API.
#pragma once
#include <chrono>

namespace fw::core::detail {

// Exit reasons, mirroring c_api.h's eafrdt_reason (which appends IDENTITY = 3).
inline constexpr int kOverflow   = 0;
inline constexpr int kUnderflow  = 1;
inline constexpr int kTimeout    = 2;
inline constexpr int kIdentity   = 3;
inline constexpr int kNotANumber = 4;

// CLI parity: drive.h commits to 16 consecutive equal states (IDENT_LIMIT).
inline constexpr int kIdentityLimit = 16;

// Advance until the next state is terminal, repeats 16 times, or the wall-clock
// budget expires. Order of checks matches include/drive.h: overflow/underflow
// first, then identity, then timeout.
//
//   Step     : State(State)      -> next state
//   Terminal : int(State)        -> -1 continue, else 0 overflow / 1 underflow /
//                                   4 NaN
//   Equal    : bool(State,State) -> state equality (for identity detection)
template <class State, class Step, class Terminal, class Equal>
long long walk_until(State start, Step step, Terminal terminal, Equal equal,
                     double timeout_sec, int* reason) {
    const auto t0 = std::chrono::steady_clock::now();
    State cur = start;
    long long steps = 0;
    int ident_streak = 0;
    while (true) {
        State next = step(cur);
        ++steps;
        int term = terminal(next);
        if (term >= 0) {
            if (reason) *reason = term;
            return steps;
        }
        if (equal(next, cur)) {
            if (++ident_streak >= kIdentityLimit) {
                if (reason) *reason = kIdentity;
                return steps;
            }
            continue;  // state already equal; keep `cur` as is
        }
        cur = next;
        ident_streak = 0;
        if (timeout_sec > 0.0) {
            const double elapsed = std::chrono::duration<double>(
                std::chrono::steady_clock::now() - t0).count();
            if (elapsed >= timeout_sec) {
                if (reason) *reason = kTimeout;
                return steps;
            }
        }
    }
}

} // namespace fw::core::detail
