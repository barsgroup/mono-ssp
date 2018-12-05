#pragma once

#include <vector>
#include <string>
#include <stdint.h>

#include "result.hpp"

Result<std::vector<uintptr_t>, std::string> do_fast_sample(uintptr_t pid);