#pragma once
#if defined(_WIN32) && !defined(__MINGW32__)
#error "ea-FRDT requires MinGW target (x86_64-w64-mingw32 / x86_64-w64-windows-gnu). Do not use MSVC or LLVM's default windows-msvc clang; use MSYS2 clang64."
#endif

namespace fw::plat {

// Concurrent worker: pin to a core AND raise priority (existing ALL-mode behavior).
void bind_thread_to_core(int core_id);

// Affinity only: restrict the calling thread to one logical core (single-format --pin, W2-B1).
void set_thread_affinity(int core_id);

// Raise only the calling thread priority (single-format opt-in boost, W2-B1).
void boost_thread_priority();

// Restore calling thread affinity to the online-full mask and priority to NORMAL.
// Called at worker end / normal exit / best-effort SIGINT teardown so a boosted
// thread never survives as TIME_CRITICAL after the run finishes.
void restore_thread_scheduling();

// High-resolution multimedia timer (Windows timeBeginPeriod(1)); ref-counted.
void set_high_res_timer();
void reset_high_res_timer();

// Best-effort process-level teardown: release timer period and scheduling boost.
// Safe to call more than once; no-op on non-Windows where these knobs do not exist.
void teardown_on_exit();

int  logical_cores();
int  physical_cores();

// CPU brand string from CPUID leaf 0x80000002..0x80000004 (e.g. "Intel(R) Core(TM) i5-10400 CPU @ 2.90GHz").
// Static buffer, never throws, returns "unknown" if CPUID leaves are absent.
const char* cpu_brand();

// Capability probes for the JSON "system" block (A4). Never throw.
bool hires_timer_effective();   // true only where timeBeginPeriod exists
bool affinity_available();      // true where thread affinity can be requested

} // namespace fw::plat
