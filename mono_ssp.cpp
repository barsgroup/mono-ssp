#include <iostream>
#include <vector>
#include <string>
#include <stdexcept>
#include <chrono>
#include <thread>

#include "backtrace.hpp"
#include "fast_sample.hpp"
#include "perf_symbol_map.hpp"

#define PROJECT_NAME "mono-ssp"

using std::cerr;
using std::cout;
using std::vector;
using std::string;

struct CliArguments {
    bool parsed;
    uint32_t pid;
    bool perf_script;
    bool debug;
    int interval_ms;
    uint32_t count_samples;
    uint32_t tid;
    uint32_t duration_seconds;

    static CliArguments parse(int argc, char** argv) {
        vector<string> args { &argv[1], &argv[argc] };

        bool parsed = true;
        uint32_t pid = 0;
        bool perf_script = false;
        bool debug = false;
        int interval_ms = 10;
        uint32_t count_samples = 0;
        uint32_t tid = 0;
        uint32_t duration_seconds = 0;

        for (auto it = args.begin(); it != args.end(); ++it) {
            if (*it == "--pid") {
                ++it;
                pid = atol(it->c_str());
            } else if (*it == "--tid") {
                ++it;
                tid = atol(it->c_str());
            } else if (*it == "--interval_ms") {
                ++it;
                interval_ms = atol(it->c_str());
            } else if (*it == "--count_samples") {
                ++it;
                count_samples = atol(it->c_str());
            } else if (*it == "--duration_sec") {
                ++it;
                duration_seconds = atol(it->c_str());
            } else if (*it == "--perf_script") {
                perf_script = true;
            } else if (*it == "--debug") {
                debug = true;
            } else {
                cerr << "Unknown arguments: " << *it << "\n";
                parsed = false;
            }
        }

        if (pid == 0) {
            cerr << "--pid must be specified and > 0\n";
            parsed = false;
        }

        if ((count_samples == 0 && duration_seconds == 0) ||
            (count_samples > 0 && duration_seconds > 0)) {
            cerr << "Exactly one of --count_samples and --duration_sec must be specified\n";
            parsed = false;
        }

        return CliArguments {
            parsed,
            pid,
            perf_script,
            debug,
            interval_ms,
            count_samples,
            tid,
            duration_seconds
        };
    }
};

int main(int argc, char **argv) {
    auto cli_args = CliArguments::parse(argc, argv);
    if (!cli_args.parsed) {
        cerr << "Usage: mono-ssp --pid PID [--interval_ms 10] (--count_samples 0|--duration_sec 0) [--perf_script] [--debug]\n";
        return 1;
    }

    StringPool string_pool;

    cerr << "Tracing pid " << cli_args.pid << " (debug: " << (cli_args.debug ? "true" : "false") << ") count_samples = " << cli_args.count_samples << "\n";

    PerfSymbolMap symbol_map(string_pool, cli_args.pid);
    // symbol_map.maybeAppend();
    // uintptr_t offsets[] =  { 0x401fd040 - 1, 0x401fd040, 0x401fd040 + 1 };
    // for (uintptr_t offset: offsets) {
    //     auto v = symbol_map.resolve(offset);
    //     if (v.has_value()) {
    //         cerr << std::hex << "0x" << offset << " => " << string_pool.get_by_id(v->name_id) << "+0x" << (offset - v->offset) << std::dec << "\n";
    //     } else {
    //         cerr << std::hex << "0x" << offset << " => " << "-" << std::dec << "\n";
    //     }
    // }

    // for (int i = 0; i < 20; ++i) {
    //     auto start = std::chrono::system_clock::now();
    //     auto res = do_fast_sample(cli_args.pid);

    //     if (res.isOk()) {
    //         cout << "do_fast_sample() OK\n";
    //         for (auto v: res.getOkRef()) {
    //             cout << " IP = " << std::hex << v << std::dec << "\n";
    //         }
    //     } else {
    //         cerr << "do_fast_sample() failed:\n" << res.getErrRef() << "\n";
    //     }
    //     auto end = std::chrono::system_clock::now();
    //     std::chrono::duration<double> elapsed_seconds = end - start;
    //     cout << "do_fast_sample() took " << elapsed_seconds.count() << " seconds\n";
    //     std::this_thread::sleep_for(std::chrono::milliseconds(100));
    // }
    auto sample_start_timestamp = std::chrono::steady_clock::now();
    double sample_start_ms = std::chrono::duration_cast<std::chrono::milliseconds>(sample_start_timestamp.time_since_epoch()).count() * 0.001;

    uint32_t samples_count = 0;
    while (true) {
        auto start = std::chrono::steady_clock::now();
        if (cli_args.count_samples > 0 && samples_count >= cli_args.count_samples) {
            break;
        }

        std::chrono::duration<double> current_duration = start - sample_start_timestamp;
        if (cli_args.duration_seconds > 0 && current_duration >= std::chrono::seconds(cli_args.duration_seconds)) {
            break;
        }
        
        auto trace_result = sample_process(string_pool, symbol_map, cli_args.pid, cli_args.tid == 0 ? std::nullopt : std::make_optional<uintptr_t>(cli_args.tid));
        auto end = std::chrono::steady_clock::now();
        std::chrono::duration<double> elapsed_seconds = end - start;
        if (cli_args.debug) {
            cerr << "sample_process() took " << elapsed_seconds.count() << " seconds\n";
        }
        if (trace_result.isOk()) {
            if (cli_args.debug) {
                cerr << "Trace successful\n";
            }
            auto process_sample = trace_result.getOkRef();
            if (cli_args.debug) {
                cerr << " PID = " << process_sample.pid << "\n";
            }
            for (const auto& t: process_sample.threads) {
                if (cli_args.debug) {
                    cerr << "  Time = " << (t.timestamp - sample_start_ms) << " TID = " << t/*.value()*/.tid << " Name = " << string_pool.get_by_id(t.thread_name_id) << "\n";
                }
                if (cli_args.perf_script) {
                    cout << string_pool.get_by_id(t.thread_name_id) << " " << t.tid << " [000] " << (t.timestamp - sample_start_ms) << ": wall-clock\n";
                }
                for (const auto& f: t/*.value()*/.frames) {
                    if (cli_args.debug) {
                        cerr << "   IP = " << std::hex << f.ip << std::dec;
                    
                        if (f.name_id != StackFrame::NO_NAME) {
                            cerr << " name = " << string_pool.get_by_id(f.name_id) << "+0x" << std::hex << f.name_offset << std::dec;
                        }

                        cerr << "\n";
                    }
                    if (cli_args.perf_script) {
                        cout << "\t    " << std::hex << f.ip << " ";
                        if (f.name_id == StackFrame::NO_NAME) {
                            cout << "unknown";
                        } else {
                            cout << string_pool.get_by_id(f.name_id) << "+0x" << f.name_offset;
                        }
                        cout << " (doesn_matter.so)";
                        cout << std::dec << "\n";
                    }
                }
                if (cli_args.perf_script) {
                    cout << "\n";
                }
            }
        } else {
            if (cli_args.debug) {
                cerr << "Trace failed:\n" << trace_result.getErrRef() << "\n";
            }
        }

        int to_sleep_ms = cli_args.interval_ms - (int)(1000 * elapsed_seconds.count());
        std::this_thread::sleep_for(std::chrono::milliseconds(to_sleep_ms));

        ++samples_count;
    }

    {
        auto current_time = std::chrono::steady_clock::now();
        std::chrono::duration<double> elapsed_seconds = current_time - sample_start_timestamp;
        cerr << "Profile completed. Took " << samples_count << " process samples in " << elapsed_seconds.count() << " seconds\n";
    }

    return 0;
}
