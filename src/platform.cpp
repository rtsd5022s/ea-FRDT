#include "platform.h"
#include <cstdlib>

#ifdef _WIN32
#include <windows.h>
#include <mmsystem.h>
#ifdef _MSC_VER
#pragma comment(lib, "winmm.lib")
#endif

namespace fw::plat {

// Ref-counted multimedia timer period: set/reset must stay paired so an early
// teardown (SIGINT) and the normal scope guard never over-release the period.
static int g_period_depth = 0;

void set_thread_affinity(int core_id) {
    SetThreadAffinityMask(GetCurrentThread(), (DWORD_PTR)(1ULL << core_id));
}

void bind_thread_to_core(int core_id) {
    set_thread_affinity(core_id);
    boost_thread_priority();
}

void boost_thread_priority() {
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
}

void restore_thread_scheduling() {
    // Full-system affinity mask; THREAD_PRIORITY_NORMAL lets the scheduler run it
    // like any ordinary thread once the pressure run has finished.
    SetThreadAffinityMask(GetCurrentThread(), (DWORD_PTR)-1);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_NORMAL);
}

void set_high_res_timer() {
    if (g_period_depth++ == 0) timeBeginPeriod(1);
}

void reset_high_res_timer() {
    if (g_period_depth > 0) {
        --g_period_depth;
        if (g_period_depth == 0) timeEndPeriod(1);
    }
}

void teardown_on_exit() {
    reset_high_res_timer();
    restore_thread_scheduling();
}

int logical_cores() {
    SYSTEM_INFO si; GetSystemInfo(&si);
    return si.dwNumberOfProcessors;
}

int physical_cores() {
    DWORD len = 0;
    GetLogicalProcessorInformation(nullptr, &len);
    if (len == 0) return 0;
    auto* buf = (SYSTEM_LOGICAL_PROCESSOR_INFORMATION*)std::malloc(len);
    if (!buf) return 0;
    int n = 0;
    if (GetLogicalProcessorInformation(buf, &len))
        for (DWORD i = 0; i < len / sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION); ++i)
            if (buf[i].Relationship == RelationProcessorCore) ++n;
    std::free(buf);
    return n;
}

bool hires_timer_effective() { return true; }
bool affinity_available()     { return true; }

} // namespace fw::plat

#else
#include <pthread.h>
#include <sched.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>

namespace fw::plat {

void set_thread_affinity(int core_id) {
    cpu_set_t cs; CPU_ZERO(&cs);
    CPU_SET(core_id, &cs);
    // Best-effort: containers/VMs may restrict affinity; ignore EINVAL/EPERM
    // rather than crashing the run.
    pthread_setaffinity_np(pthread_self(), sizeof(cs), &cs);
}

void bind_thread_to_core(int core_id) {
    set_thread_affinity(core_id);
    boost_thread_priority();
}

void boost_thread_priority() {
    // Best-effort raise, strictly weaker than Windows TIME_CRITICAL:
    //   1. SCHED_BATCH (no root, no RT): lowers wakeup latency for CPU-bound
    //      threads and prevents HZ accounting from penalising long-running tasks.
    //   2. Fallback: nice(-5) — needs CAP_SYS_NICE, silently ignored if denied.
    // Containers without CAP_SYS_NICE get neither; that is acceptable.
    struct sched_param sp; std::memset(&sp, 0, sizeof(sp));
    sp.sched_priority = 0;
    if (pthread_setschedparam(pthread_self(), SCHED_BATCH, &sp) == 0) return;
    errno = 0;
    if (::nice(-5) == -1 && errno != 0) { /* not permitted, stay at nice 0 */ }
}

void restore_thread_scheduling() {
    cpu_set_t cs; CPU_ZERO(&cs);
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    if (n > 0) for (long i = 0; i < n; ++i) CPU_SET(i, &cs);
    pthread_setaffinity_np(pthread_self(), sizeof(cs), &cs);
    // Roll back SCHED_BATCH / nice(-5) to ordinary SCHED_OTHER / nice 0.
    struct sched_param sp; std::memset(&sp, 0, sizeof(sp));
    pthread_setschedparam(pthread_self(), SCHED_OTHER, &sp);
    errno = 0;
    if (::nice(0) == -1 && errno != 0) { /* ignored */ }
}

void set_high_res_timer() {}  // Linux has no global multimedia-timer knob
void reset_high_res_timer() {}

void teardown_on_exit() {
    reset_high_res_timer();
    restore_thread_scheduling();
}

int logical_cores() { return (int)sysconf(_SC_NPROCESSORS_ONLN); }
int physical_cores() { return 0; } // Not portable on Linux, return 0 to skip display

bool hires_timer_effective() { return false; }  // sleep_for resolution = HZ (1-4ms), not 1ms
bool affinity_available()     { return true; }

} // namespace fw::plat
#endif
