#include <string>
#include <utility>
#include <iostream>
#include <optional>

#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <dirent.h>
#include <stdio.h>

#include <sys/types.h>

#include "backtrace.hpp"

using std::optional;
using std::string;
using std::to_string;
using std::move;
using std::vector;

static Result<ProcessSample, string> fail(std::string&& message) {
    return Result<ProcessSample, string>::fail(move(message));
}

Result<vector<uintptr_t>, string> get_threads(uintptr_t pid) {
    vector<uintptr_t> result;

    string proc_path = string("/proc/") + to_string(pid) + "/task";
    DIR* proc_dir = opendir(proc_path.c_str());

    if (proc_dir) {
        struct dirent* entry;
        while ((entry = readdir(proc_dir)) != nullptr) {
            if (entry->d_name[0] == '.') {
                continue;
            }

            int tid = atoi(entry->d_name);
            if (tid != 0) {
                result.push_back(tid);
            }
        }

        closedir(proc_dir);
    } else if (errno == ENOENT) {
        // do nothing
    } else {
        int errno_copy = errno;
        return Result<vector<uintptr_t>, string>::fail(string("opendir(") + proc_path + ") failed: errno = " + to_string(errno) + " message = " + strerror(errno_copy));
    }

    return Result<vector<uintptr_t>, string>::success(move(result));
}

Result<ProcessSample, std::string> sample_process(StringPool& string_pool, PerfSymbolMap& symbol_map, uintptr_t pid, std::optional<uintptr_t> tid) {
    symbol_map.maybeAppend();
    vector<uintptr_t> thread_ids;
    if (tid.has_value()) {
        thread_ids.push_back(*tid);
    } else {
        auto threads_result = get_threads(pid);
        if (!threads_result.isOk()) {
            return fail(string(threads_result.getErrRef()));
        }

        thread_ids = move(threads_result.getOkRef());
    }

    vector<ThreadSample> thread_samples;

    for (uintptr_t tid: thread_ids) {
        auto thread_sample_result = sample_thread(string_pool, symbol_map, tid);
        if (thread_sample_result.isOk()) {
            if (thread_sample_result.getOkRef().has_value()) {
                ThreadSample thread_sample = move(thread_sample_result).getOkRef().value();
                thread_samples.push_back(move(thread_sample));
            }
        } else {
            return fail(string("Tracing thread ") + to_string(tid) + "failed: " + move(thread_sample_result).getErrRef());
        }
    }
    
    ProcessSample result;
    result.pid = pid;
    result.threads = move(thread_samples);

    return Result<ProcessSample, string>::success(move(result));
}
