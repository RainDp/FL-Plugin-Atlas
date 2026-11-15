#pragma once

#include "flcat/model.hpp"

#include <filesystem>
#include <mutex>

struct sqlite3;

namespace flcat {

class Store {
public:
    explicit Store(const std::filesystem::path& database);
    ~Store();
    Store(const Store&) = delete;
    Store& operator=(const Store&) = delete;

    // Commit a complete scan. An absent entry stays in the catalog as unavailable.
    void replace_scan(const std::string& library, const std::vector<Entry>& rows);
    std::vector<Entry> list(const std::string& library) const;
    void edit_tags(const std::string& library, const std::vector<std::string>& keys,
                   const std::vector<std::string>& add, const std::vector<std::string>& remove);
    // An empty correction restores the scanned value; an empty canonical name removes an alias.
    void set_vendor(const std::string& library, const std::string& key, const std::string& vendor);
    void alias_vendor(const std::string& library, const std::string& raw, const std::string& canonical);
    void set_image(const std::string& library, const std::string& key, const std::string& image);

private:
    sqlite3* db_ = nullptr;
    mutable std::mutex mutex_;
};

} // namespace flcat
