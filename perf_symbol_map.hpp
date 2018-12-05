#pragma once

#include <string>
#include <map>
#include <iostream>
#include <fstream>

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>


#include "stringpool.hpp"

struct PerfSymbolInfo {
    uintptr_t offset;
    uintptr_t length;
    uint64_t name_id;
};

struct PerfSymbolMap {
    StringPool& string_pool;
    std::string path;
    uint64_t last_length = 0;
    std::map<uintptr_t, PerfSymbolInfo> symbols;

    PerfSymbolMap(StringPool& string_pool, uintptr_t pid) : string_pool(string_pool) {
        path = std::string("/tmp/perf-") + std::to_string(pid) + ".map";
    }

    void maybeAppend() {
        struct stat statbuf;
        int rc = lstat(path.c_str(), &statbuf);
        // std::cerr << "lstat(" << path << ") = " << rc << "\n";
        if (rc != 0) {
            return;
        }
        uintptr_t file_size = statbuf.st_size;
        // std::cerr << "file_size = " << file_size << ", last_length = " << last_length << "\n";
        if (last_length < file_size) {
            std::ifstream in(path.c_str());
            in.seekg(last_length);
            in >> std::hex;
            uintptr_t offset, length;
            while (in >> offset >> length) {
                std::string name;
                in.ignore(1, ' ');
                std::getline(in, name);
                uint64_t name_id = string_pool.intern(name);
                // std::cerr << "got off=" << std::hex << offset << " len=" << length << " name='" << name << "'\n" << std::dec;
                symbols[offset] = PerfSymbolInfo { offset, length, name_id };
            }

            last_length = file_size;
        }
    }

    std::optional<PerfSymbolInfo> resolve(uintptr_t offset) {
        if (symbols.empty()) {
            return std::nullopt;
        }

        auto it = symbols.upper_bound(offset);
        --it;
        const auto& symbol = it->second;
        if (symbol.offset <= offset && offset < symbol.offset + symbol.length) {
            return symbol;
        } else {
            return std::nullopt;
        }
    }
};
