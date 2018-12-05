#pragma once

#include <stdint.h>
#include <vector>
#include <string>
#include <mutex>
#include <memory>

#include "result.hpp"
#include "stringpool.hpp"
#include "perf_symbol_map.hpp"

struct StackFrame {
    static const uint64_t NO_NAME = (uint64_t) -1;

    uintptr_t ip;
    uint64_t name_id;
    uintptr_t name_offset;
};

struct ThreadSample {
    uintptr_t tid;
    double timestamp;
    uint64_t thread_name_id;
    std::vector<StackFrame> frames;
};

struct ProcessSample {
    uintptr_t pid;
    std::vector<ThreadSample> threads;
};

Result<std::optional<ThreadSample>, std::string> sample_thread(StringPool& string_pool, PerfSymbolMap& symbol_map, uintptr_t tid);
Result<ProcessSample, std::string> sample_process(StringPool& string_pool, PerfSymbolMap& symbol_map, uintptr_t pid, std::optional<uintptr_t> tid);
