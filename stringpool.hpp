#pragma once

#include <vector>
#include <string>
#include <unordered_map>
#include <string_view>
#include <memory>
#include <utility>
#include <shared_mutex>

struct StringPool {
    std::shared_mutex mutex;
    std::vector<std::unique_ptr<std::string>> strings;
    std::unordered_map<std::string_view, uint64_t> map;

    uint64_t intern(std::string_view data) {
        std::shared_lock<std::shared_mutex> shared_lock(mutex);
        auto it = map.find(data);
        if (it != map.end()) {
            return it->second;
        } else {
            shared_lock.unlock();

            std::unique_lock<std::shared_mutex> unique_lock(mutex);
            it = map.find(data);
            if (it != map.end()) {
                return it->second;
            } else {
                uint64_t new_id = strings.size();
                map[data] = new_id;
                strings.push_back(std::make_unique<std::string>(data));
                return new_id;
            }
        }
    }

    std::string_view get_by_id(uint64_t name_id) {
        return *strings[name_id];
    }
};