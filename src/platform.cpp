#include "platform.h"
#include <cstdlib>

#ifdef _WIN32
#include <windows.h>
#include <mmsystem.h>
#pragma comment(lib, "winmm.lib")

namespace fw::plat {

void bind_thread_to_core(int core_id) {
    SetThreadAffinityMask(GetCurrentThread(), (DWORD_PTR)(1ULL << core_id));
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
}

void set_high_res_timer() { timeBeginPeriod(1); }
void reset_high_res_timer() { timeEndPeriod(1); }

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

} // namespace fw::plat

#else
#include <pthread.h>
#include <sched.h>
#include <unistd.h>

namespace fw::plat {

void bind_thread_to_core(int core_id) {
    cpu_set_t cs; CPU_ZERO(&cs);
    CPU_SET(core_id, &cs);
    pthread_setaffinity_np(pthread_self(), sizeof(cs), &cs);
}

void set_high_res_timer() {}
void reset_high_res_timer() {}

int logical_cores() { return (int)sysconf(_SC_NPROCESSORS_ONLN); }
int physical_cores() { return 0; } // Not precise on Linux, return 0 to skip display

} // namespace fw::plat
#endif
