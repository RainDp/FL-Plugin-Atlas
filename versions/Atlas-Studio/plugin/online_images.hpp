#pragma once
#include "flcat/model.hpp"
#include <atomic>
#include <filesystem>

namespace artwork {
std::filesystem::path cached(const flcat::Entry& entry);
struct Report { int saved=0, missing=0, errors=0; };
Report fill(const std::filesystem::path& library, const std::vector<flcat::Entry>& entries,
            const std::atomic<bool>& stop);
}
