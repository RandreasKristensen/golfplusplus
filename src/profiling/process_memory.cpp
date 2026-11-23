// Current and peak process working set in bytes when the host exposes them.
#include "profiling/frame_accounting.h"

#if defined(_WIN32)
#include <windows.h>
#include <psapi.h>
#elif defined(__APPLE__)
#include <mach/mach.h>
#include <sys/resource.h>
#elif defined(__linux__)
#include <cstdio>
#include <sys/resource.h>
#include <unistd.h>
#endif

process_memory sample_process_memory() {
    process_memory result;
#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS counters{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters))) {
        result.resident_bytes = counters.WorkingSetSize;
        result.peak_resident_bytes = counters.PeakWorkingSetSize;
    }
#elif defined(__APPLE__)
    mach_task_basic_info_data_t info{};
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, reinterpret_cast<task_info_t>(&info), &count) == KERN_SUCCESS) {
        result.resident_bytes = info.resident_size;
    }
    struct rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage) == 0) { result.peak_resident_bytes = usage.ru_maxrss; }
#elif defined(__linux__)
    if (std::FILE* file = std::fopen("/proc/self/statm", "r")) {
        unsigned long pages = 0;
        const long page_bytes = sysconf(_SC_PAGESIZE);
        if (page_bytes > 0 && std::fscanf(file, "%*u %lu", &pages) == 1) {
            result.resident_bytes = static_cast<std::uint64_t>(pages) * page_bytes;
        }
        std::fclose(file);
    }
    struct rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage) == 0) {
        result.peak_resident_bytes = static_cast<std::uint64_t>(usage.ru_maxrss) * 1024;
    }
#endif
    return result;
}
