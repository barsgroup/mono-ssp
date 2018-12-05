#include <string>
#include <utility>
#include <iostream>
#include <optional>

#include <libunwind.h>
#include <libunwind-ptrace.h>
// #include <time.h>
#include <stdlib.h>
#include <stdint.h>
#include <errno.h>
#include <string.h>
#include <dirent.h>
#include <stdio.h>

#include <sys/ptrace.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/user.h>

#include "result.hpp"
#include "fast_sample.hpp"


using std::optional;
using std::string;
using std::to_string;
using std::move;
using std::vector;

class PtraceDetachGuard {
    pid_t pid;
public:
    PtraceDetachGuard(pid_t pid): pid(pid) {}
    PtraceDetachGuard(const PtraceDetachGuard&) = delete;
    ~PtraceDetachGuard() {
        if (pid) {
            int rc = ptrace(PTRACE_DETACH, pid, 0, 0);
            if (rc != 0) {
                std::cerr << ("ptrace(PTRACE_DETACH) failed with errno = " + to_string(errno) + " message = " + strerror(errno)) << "\n";
            }
            pid = 0;
        }
    }
};


Result<std::vector<uintptr_t>, std::string> do_fast_sample(uintptr_t pid) {

    pid_t target_pid = pid;
    int rc;

    rc = ptrace(PTRACE_SEIZE, target_pid, 0, 0);
    if (rc != 0) {
        if (errno == ESRCH) {
            // LWP does not exist
            return ResultInit::ok(std::vector<uintptr_t>{});
        }

        return ResultInit::err(std::string() + "ptrace(PTRACE_SEIZE) failed with errno = " + to_string(errno) + " message = " + strerror(errno));
    }

    PtraceDetachGuard ptrace_detach_guard(target_pid);

    rc = ptrace(PTRACE_INTERRUPT, target_pid, 0, 0);
    if (rc != 0) {
        return ResultInit::err(std::string() + "ptrace(PTRACE_INTERRUPT) failed with errno = " + to_string(errno) + " message = " + strerror(errno));
    }

    {
        int status;
        pid_t tmp = waitpid(target_pid, &status, WCONTINUED);
        if (tmp != target_pid) {
            return ResultInit::err(std::string() + "waitpid() failed with ret = " + to_string(tmp) + " errno = " + to_string(errno) + " message = " + strerror(errno));
        }

        if (!WIFSTOPPED(status)) {
            return ResultInit::err(std::string() + "waitpid() returned bad status: !WIFSTOPPED(status); status = " + to_string(status));
        }
    }

    struct user_regs_struct regs;
    ptrace(PTRACE_GETREGS, target_pid, NULL, &regs);

    vector<uintptr_t> result;
    result.push_back(regs.rip);
    result.push_back(regs.rsp);
    result.push_back(regs.rbp);

    return ResultInit::ok(move(result));

    // printf( “%lX\n”, regs.rip );
    
    // return ResultInit::err(std::string("not implemented"));
}