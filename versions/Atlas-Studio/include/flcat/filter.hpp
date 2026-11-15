#pragma once

#include "flcat/model.hpp"

#include <optional>

namespace flcat {

inline constexpr char unknown_vendor[] = "未知厂商";
inline constexpr char unclassified[] = "待分类";

struct Filter {
    std::optional<std::string> vendor;
    std::vector<std::string> tags;
    std::optional<std::string> kind;
    bool include_missing = false;

    bool matches(const Entry& entry) const {
        if ((!entry.available && !include_missing) || (kind && entry.kind != *kind))
            return false;
        if (vendor && *vendor != (entry.vendor.empty() ? unknown_vendor : entry.vendor))
            return false;
        for (const auto& tag : tags) {
            if (tag == unclassified ? !entry.tags.empty() : !entry.tags.count(tag))
                return false;
        }
        return true;
    }
};

} // namespace flcat
