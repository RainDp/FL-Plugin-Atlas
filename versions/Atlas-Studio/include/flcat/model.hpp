#pragma once

#include <map>
#include <string>
#include <vector>

namespace flcat {

// Paths and text crossing the catalog boundary are encoded in UTF-8.
struct Entry {
    std::string path;
    std::string plugin_key;
    std::string name;
    std::string kind;
    std::string format;
    std::string vendor;
    std::map<std::string, std::string> tags; // purpose -> evidence
    bool available = true;
    bool identity_conflict = false; // scanner saw contradictory identity evidence
    std::string custom_image;
};

} // namespace flcat
