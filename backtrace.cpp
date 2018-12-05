#include <string>
#include <utility>
#include <iostream>
#include <optional>

#include <libunwind.h>
#include <libunwind-ptrace.h>
// #include <time.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <dirent.h>
#include <stdio.h>

#include <sys/ptrace.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "backtrace.hpp"

using std::optional;
using std::string;
using std::to_string;
using std::move;

static Result<optional<ThreadSample>, string> fail(std::string&& message) {
    return Result<optional<ThreadSample>, string>::fail(move(message));
}

class PtraceDetachGuard {
    pid_t pid;
public:
    int stopped_signal = 0;
    PtraceDetachGuard(pid_t pid): pid(pid) {}
    PtraceDetachGuard(const PtraceDetachGuard&) = delete;
    ~PtraceDetachGuard() {
        if (pid) {
            int rc = ptrace(PTRACE_DETACH, pid, 0, stopped_signal);
            if (rc != 0) {
                std::cerr << ("ptrace(PTRACE_DETACH) failed with errno = " + to_string(errno) + " message = " + strerror(errno)) << "\n";
            }
            pid = 0;
        }
    }
};

class UnwAddrSpaceGuard {
    unw_addr_space_t data;
public:
    UnwAddrSpaceGuard(unw_addr_space_t data): data(data) {}
    UnwAddrSpaceGuard(const UnwAddrSpaceGuard&) = delete;
    ~UnwAddrSpaceGuard() {
        if (data) {
            unw_destroy_addr_space(data);
            data = nullptr;
        }
    }
};

class UptInfoGuard {
    struct UPT_info* data;
public:
    UptInfoGuard(struct UPT_info* data): data(data) {}
    UptInfoGuard(const UptInfoGuard&) = delete;
    ~UptInfoGuard() {
        if (data) {
            _UPT_destroy(data);
            data = nullptr;
        }
    }
};

Result<optional<ThreadSample>, string> sample_thread(StringPool& string_pool, PerfSymbolMap& symbol_map, uintptr_t tid_) {
    pid_t target_pid = tid_;
    int rc;

    rc = ptrace(PTRACE_SEIZE, target_pid, 0, 0);
    if (rc != 0) {
        if (errno == ESRCH) {
            // LWP does not exist
            return Result<optional<ThreadSample>, string>::success(std::nullopt);
        }

        return fail("ptrace(PTRACE_SEIZE) failed with errno = " + to_string(errno) + " message = " + strerror(errno));
    }

    PtraceDetachGuard ptrace_detach_guard(target_pid);

    rc = ptrace(PTRACE_INTERRUPT, target_pid, 0, 0);
    if (rc != 0) {
        return fail("ptrace(PTRACE_INTERRUPT) failed with errno = " + to_string(errno) + " message = " + strerror(errno));
    }

    int signal_delivery_stop_signal = 0;

    {
        int status;
        pid_t tmp = waitpid(target_pid, &status, WCONTINUED);
        if (tmp != target_pid) {
            return fail("waitpid() failed with ret = " + to_string(tmp) + " errno = " + to_string(errno) + " message = " + strerror(errno));
        }

        if (!WIFSTOPPED(status)) {
            return fail("waitpid() returned bad status: !WIFSTOPPED(status); status = " + to_string(status));
        }

        if (WSTOPSIG(status)) {
            // Process stopped via signal delivery
            signal_delivery_stop_signal = WSTOPSIG(status);
            ptrace_detach_guard.stopped_signal = signal_delivery_stop_signal;
        }
    }

    double timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count() * 0.001;

    uint64_t thread_name_id;
    {
        string path = string("/proc/") + to_string(target_pid) + "/comm";
        std::ifstream stream(path.c_str());
        string thread_name;
        getline(stream, thread_name);
        for (auto& c: thread_name) {
            if (c == ' ') {
                c = '_';
            }
        }
        thread_name_id = string_pool.intern(thread_name);
    }

    unw_addr_space_t address_space = unw_create_addr_space (&_UPT_accessors, 0);
    if (address_space == nullptr) {
        return fail("unw_create_addr_space() failed");
    }
    UnwAddrSpaceGuard address_space_guard(address_space);

    struct UPT_info* unw_ptrace_cb = (struct UPT_info*) _UPT_create (target_pid);
    if (unw_ptrace_cb == nullptr) {
        return fail("_UPT_create() failed");
    }
    UptInfoGuard unw_ptrace_cb_guard (unw_ptrace_cb);

    unw_cursor_t cursor;
    rc = unw_init_remote (&cursor, address_space, unw_ptrace_cb);
    if (rc < 0) {
        return fail("unw_init_remote() failed with ret = " + to_string(rc));
    }
    
    ThreadSample thread_sample;
    thread_sample.tid = target_pid;
    thread_sample.timestamp = timestamp;
    thread_sample.thread_name_id = thread_name_id;
    
    while (true) {
        unw_word_t ip, sp;
        rc = unw_get_reg(&cursor, UNW_REG_IP, &ip);
        if (rc < 0) {
            return fail("unw_get_reg(UNG_REG_IP) failed with ret = " + to_string(rc));
        }

        rc = unw_get_reg(&cursor, UNW_REG_SP, &sp);
        if (rc < 0) {
            return fail("unw_get_reg(UNG_REG_SP) failed with ret = " + to_string(rc));
        }

        uint64_t name_id = StackFrame::NO_NAME;
        uintptr_t name_offset = 0;

        {
            unw_word_t ip_offset;
            char buf[1024];

            rc = unw_get_proc_name(&cursor, &buf[0], sizeof(buf), &ip_offset);
            if (rc == 0 || rc == UNW_ENOMEM) {
                name_id = string_pool.intern(std::string_view(buf));
                name_offset = ip_offset;
            } else {
                auto symbol = symbol_map.resolve(ip);
                if (symbol.has_value()) {
                    name_id = symbol->name_id;
                    name_offset = symbol->offset + symbol->length - ip;
                }
            }
        }

        thread_sample.frames.push_back(StackFrame { ip, name_id, name_offset });

        rc = unw_step(&cursor);
        if (rc == 0) {
            // 0 means end of call chain
            break;
        }

        if (rc < 0) {
            return fail("unw_step() failed with ret = " + to_string(rc));
        }
    }

    return Result<optional<ThreadSample>, string>::success(move(thread_sample));
}
