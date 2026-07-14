#pragma once

#include <cstdint>

#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#include <psapi.h>
#elif defined(__APPLE__)
#include <mach/mach.h>
#include <sys/resource.h>
#else
#include <cstdio>
#include <sys/resource.h>
#include <unistd.h>
#endif

namespace light_ocr::tools {

inline std::uint64_t resident_memory_bytes() noexcept {
#if defined(_WIN32)
  PROCESS_MEMORY_COUNTERS_EX counters{};
  if (!GetProcessMemoryInfo(GetCurrentProcess(),
                            reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),
                            sizeof(counters))) {
    return 0;
  }
  return static_cast<std::uint64_t>(counters.WorkingSetSize);
#elif defined(__APPLE__)
  mach_task_basic_info_data_t info{};
  mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
  if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO,
                reinterpret_cast<task_info_t>(&info), &count) != KERN_SUCCESS) {
    return 0;
  }
  return static_cast<std::uint64_t>(info.resident_size);
#else
  long resident_pages = 0;
  std::FILE* file = std::fopen("/proc/self/statm", "r");
  if (file == nullptr) return 0;
  const int parsed = std::fscanf(file, "%*s %ld", &resident_pages);
  std::fclose(file);
  if (parsed != 1 || resident_pages < 0) return 0;
  const long page_size = sysconf(_SC_PAGESIZE);
  if (page_size <= 0) return 0;
  return static_cast<std::uint64_t>(resident_pages) *
         static_cast<std::uint64_t>(page_size);
#endif
}

inline std::uint64_t peak_resident_memory_bytes() noexcept {
#if defined(_WIN32)
  PROCESS_MEMORY_COUNTERS_EX counters{};
  if (!GetProcessMemoryInfo(GetCurrentProcess(),
                            reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),
                            sizeof(counters))) {
    return 0;
  }
  return static_cast<std::uint64_t>(counters.PeakWorkingSetSize);
#else
  rusage usage{};
  if (getrusage(RUSAGE_SELF, &usage) != 0) return 0;
#if defined(__APPLE__)
  return static_cast<std::uint64_t>(usage.ru_maxrss);
#else
  return static_cast<std::uint64_t>(usage.ru_maxrss) * 1024;
#endif
#endif
}

}  // namespace light_ocr::tools
