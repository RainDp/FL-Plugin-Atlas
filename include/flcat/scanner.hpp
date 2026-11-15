#pragma once

#include "flcat/model.hpp"

#include <filesystem>
#include <functional>

namespace flcat {

// Candidate local thumbnails in the preset directory, in FL preference order.
std::vector<std::filesystem::path> thumbnail_files(const std::filesystem::path& root,
                                                   const std::string& preset);

// Returns one entry per loadable preset path. Failure or cancellation throws
// before returning a replacement snapshot. The database is opened read-only.
std::vector<Entry> scan(const std::filesystem::path& root,
                        const std::function<bool()>& cancelled = {});

} // namespace flcat
