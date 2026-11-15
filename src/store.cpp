#include "flcat/store.hpp"

#if defined(_WIN32) && __has_include(<winsqlite/winsqlite3.h>)
#include <winsqlite/winsqlite3.h>
#elif defined(_WIN32) && __has_include(<winsqlite3.h>)
#include <winsqlite3.h>
#else
#include <sqlite3.h>
#endif

#include <limits>
#include <set>
#include <stdexcept>
#include <utility>

namespace flcat {
namespace {

void check(sqlite3* db, int result) {
    if (result != SQLITE_OK) {
        throw std::runtime_error(db ? sqlite3_errmsg(db) : "Could not open SQLite database");
    }
}

void exec(sqlite3* db, const char* sql) {
    check(db, sqlite3_exec(db, sql, nullptr, nullptr, nullptr));
}

class Transaction {
public:
    Transaction(sqlite3* db, bool write) : db_(db) {
        exec(db_, write ? "BEGIN IMMEDIATE" : "BEGIN");
    }
    ~Transaction() {
        if (pending_) sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
    }
    void commit() {
        exec(db_, "COMMIT");
        pending_ = false;
    }
private:
    sqlite3* db_;
    bool pending_ = true;
};

class Statement {
public:
    Statement(sqlite3* db, const char* sql) : db_(db) {
        check(db_, sqlite3_prepare_v2(db_, sql, -1, &stmt_, nullptr));
    }
    ~Statement() { sqlite3_finalize(stmt_); }
    void bind(int parameter, const std::string& value) {
        if (value.size() > static_cast<size_t>(std::numeric_limits<int>::max())) {
            throw std::length_error("Catalog value exceeds SQLite size limit");
        }
        check(db_, sqlite3_bind_text(stmt_, parameter, value.c_str(), static_cast<int>(value.size()), SQLITE_TRANSIENT));
    }
    void bind(int parameter, bool value) {
        check(db_, sqlite3_bind_int(stmt_, parameter, value));
    }
    bool row() {
        const int result = sqlite3_step(stmt_);
        if (result == SQLITE_ROW) return true;
        if (result == SQLITE_DONE) return false;
        check(db_, result);
        return false;
    }
    void reset() { check(db_, sqlite3_reset(stmt_)); }
    void write() {
        const int result = sqlite3_step(stmt_);
        if (result != SQLITE_DONE) check(db_, result);
        reset();
    }
    std::string text(int column) const {
        const auto* utf8 = sqlite3_column_text(stmt_, column);
        return utf8 ? std::string(reinterpret_cast<const char*>(utf8), sqlite3_column_bytes(stmt_, column)) : std::string{};
    }
    bool flag(int column) const { return sqlite3_column_int(stmt_, column) != 0; }
private:
    sqlite3* db_;
    sqlite3_stmt* stmt_ = nullptr;
};

constexpr char schema[] = R"sql(
CREATE TABLE IF NOT EXISTS entries(
    library TEXT NOT NULL, path TEXT NOT NULL, plugin_key TEXT NOT NULL, name TEXT NOT NULL,
    kind TEXT NOT NULL, format TEXT NOT NULL, vendor_raw TEXT NOT NULL, available INTEGER NOT NULL,
    PRIMARY KEY(library,path));
CREATE INDEX IF NOT EXISTS entries_by_plugin ON entries(library,plugin_key);
CREATE TABLE IF NOT EXISTS scanned_tags(
    library TEXT NOT NULL, path TEXT NOT NULL, tag TEXT NOT NULL, evidence TEXT NOT NULL,
    PRIMARY KEY(library,path,tag),
    FOREIGN KEY(library,path) REFERENCES entries(library,path) ON DELETE CASCADE);
CREATE TABLE IF NOT EXISTS tag_edits(
    library TEXT NOT NULL, plugin_key TEXT NOT NULL, tag TEXT NOT NULL, added INTEGER NOT NULL,
    PRIMARY KEY(library,plugin_key,tag));
CREATE TABLE IF NOT EXISTS vendor_edits(
    library TEXT NOT NULL, plugin_key TEXT NOT NULL, vendor TEXT NOT NULL,
    PRIMARY KEY(library,plugin_key));
CREATE TABLE IF NOT EXISTS vendor_aliases(
    library TEXT NOT NULL, raw TEXT NOT NULL, canonical TEXT NOT NULL, PRIMARY KEY(library,raw));
CREATE TABLE IF NOT EXISTS identity_conflicts(
    library TEXT NOT NULL, path TEXT NOT NULL, PRIMARY KEY(library,path),
    FOREIGN KEY(library,path) REFERENCES entries(library,path) ON DELETE CASCADE);
CREATE TABLE IF NOT EXISTS preview_images(
    library TEXT NOT NULL, plugin_key TEXT NOT NULL, image TEXT NOT NULL, PRIMARY KEY(library,plugin_key));
)sql";

void require_name(const std::string& value) {
    if (value.empty()) throw std::invalid_argument("Catalog identifier cannot be empty");
}

void require_key(Statement& query, const std::string& key) {
    query.bind(2, key);
    if (!query.row()) throw std::invalid_argument("Unknown catalog plugin key");
    query.reset();
}

constexpr char find_key[] = "SELECT 1 FROM entries WHERE library=? AND plugin_key=? LIMIT 1";

bool path_key(const std::string& key) {
    return key.compare(0, 5, "path:") == 0;
}

void migrate_edits(sqlite3* db, const std::string& library, const std::string& old_key,
                   const std::string& new_key) {
    // The confirmed plugin may already have edits from another preset. Keep
    // those choices and transfer previously unrepresented corrections.
    for (const char* sql : {
            "INSERT OR IGNORE INTO tag_edits(library,plugin_key,tag,added)"
            " SELECT library,?3,tag,added FROM tag_edits WHERE library=?1 AND plugin_key=?2",
            "INSERT OR IGNORE INTO vendor_edits(library,plugin_key,vendor)"
            " SELECT library,?3,vendor FROM vendor_edits WHERE library=?1 AND plugin_key=?2",
            "INSERT OR IGNORE INTO preview_images SELECT library,?3,image FROM preview_images WHERE library=?1 AND plugin_key=?2"}) {
        Statement copy(db, sql);
        copy.bind(1, library);
        copy.bind(2, old_key);
        copy.bind(3, new_key);
        copy.write();
    }
    for (const char* sql : {"DELETE FROM tag_edits WHERE library=? AND plugin_key=?",
                            "DELETE FROM vendor_edits WHERE library=? AND plugin_key=?",
                            "DELETE FROM preview_images WHERE library=? AND plugin_key=?"}) {
        Statement clear(db, sql);
        clear.bind(1, library);
        clear.bind(2, old_key);
        clear.write();
    }
}

struct Metadata {
    bool has_available = false;
    std::set<std::string> vendors;
    std::map<std::string,std::string> tags;
};

} // namespace

Store::Store(const std::filesystem::path& database) {
    if (database.empty()) throw std::invalid_argument("Missing catalog database path");
    if (database.has_parent_path()) std::filesystem::create_directories(database.parent_path());
    try {
        const int result = sqlite3_open_v2(database.u8string().c_str(), &db_,
                                           SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr);
        check(db_, result);
        check(db_, sqlite3_busy_timeout(db_, 5000));
        exec(db_, "PRAGMA foreign_keys=ON");
        Transaction tx(db_, true);
        exec(db_, schema);
        tx.commit();
    } catch (...) {
        if (db_) sqlite3_close(db_);
        throw;
    }
}

Store::~Store() { sqlite3_close(db_); }

void Store::replace_scan(const std::string& library, const std::vector<Entry>& rows) {
    require_name(library);
    std::lock_guard<std::mutex> lock(mutex_);
    Transaction tx(db_, true);
    Statement stale(db_, "UPDATE entries SET available=0 WHERE library=?");
    stale.bind(1, library);
    stale.write();
    Statement previous(db_, "SELECT e.plugin_key,c.path IS NOT NULL FROM entries e"
                            " LEFT JOIN identity_conflicts c ON c.library=e.library AND c.path=e.path"
                            " WHERE e.library=? AND e.path=?");
    previous.bind(1, library);
    // Replacing an entry also drops its old scanned tags through the foreign key.
    Statement save(db_, "INSERT OR REPLACE INTO entries(library,path,plugin_key,name,kind,format,vendor_raw,available)"
                        " VALUES(?,?,?,?,?,?,?,?)");
    Statement tag(db_, "INSERT INTO scanned_tags(library,path,tag,evidence) VALUES(?,?,?,?)");
    Statement conflict(db_, "INSERT INTO identity_conflicts(library,path) VALUES(?,?)");
    std::set<std::string> seen;
    save.bind(1, library);
    tag.bind(1, library);
    conflict.bind(1, library);
    for (const auto& item : rows) {
        require_name(item.path);
        require_name(item.plugin_key);
        if (!seen.insert(item.path).second) throw std::invalid_argument("Duplicate preset entry path");
        auto key = item.plugin_key;
        previous.bind(2, item.path);
        const bool existed = previous.row();
        const auto old_key = existed ? previous.text(0) : std::string{};
        const bool was_ambiguous = existed && previous.flag(1);
        previous.reset();
        if (!item.identity_conflict && !was_ambiguous && !old_key.empty() && old_key != key) {
            if (path_key(old_key) && !path_key(key)) migrate_edits(db_, library, old_key, key);
            else if (!path_key(old_key) && path_key(key)) key = old_key;
        }
        save.bind(2, item.path);
        save.bind(3, key);
        save.bind(4, item.name);
        save.bind(5, item.kind);
        save.bind(6, item.format);
        save.bind(7, item.vendor);
        save.bind(8, item.available);
        save.write();
        if (item.identity_conflict) {
            conflict.bind(2, item.path);
            conflict.write();
        }
        tag.bind(2, item.path);
        for (const auto& [name, evidence] : item.tags) {
            require_name(name);
            tag.bind(3, name);
            tag.bind(4, evidence);
            tag.write();
        }
    }
    tx.commit();
}

std::vector<Entry> Store::list(const std::string& library) const {
    std::lock_guard<std::mutex> lock(mutex_);
    Transaction tx(db_, false);
    Statement query(db_, R"sql(
        SELECT e.path,e.plugin_key,e.name,e.kind,e.format,COALESCE(v.vendor,a.canonical,e.vendor_raw),
               e.available,t.tag,t.evidence,c.path IS NOT NULL,i.image
        FROM entries e
        LEFT JOIN preview_images i ON i.library=e.library AND i.plugin_key=e.plugin_key
        LEFT JOIN scanned_tags t ON t.library=e.library AND t.path=e.path
        LEFT JOIN vendor_edits v ON v.library=e.library AND v.plugin_key=e.plugin_key
        LEFT JOIN vendor_aliases a ON a.library=e.library AND a.raw=e.vendor_raw
        LEFT JOIN identity_conflicts c ON c.library=e.library AND c.path=e.path
        WHERE e.library=? ORDER BY e.path,t.tag
    )sql");
    query.bind(1, library);
    std::vector<Entry> entries;
    std::map<std::string, Metadata> plugins;
    while (query.row()) {
        const auto path = query.text(0);
        if (entries.empty() || entries.back().path != path) {
            entries.push_back({path, query.text(1), query.text(2), query.text(3), query.text(4),
                               query.text(5), {}, query.flag(6), query.flag(9)});
            auto& info = plugins[entries.back().plugin_key];
            entries.back().custom_image=query.text(10);
            if (entries.back().available) info.has_available = true;
        }
        const auto tag = query.text(7);
        if (!tag.empty()) entries.back().tags.emplace(tag, query.text(8));
    }
    // Only confirmed siblings share automatic metadata. Keep the last known
    // metadata when the whole plugin temporarily disappears from the scan.
    for (const auto& entry : entries) {
        auto& info = plugins.at(entry.plugin_key);
        if (info.has_available && !entry.available) continue;
        if (!entry.vendor.empty()) info.vendors.insert(entry.vendor);
        info.tags.insert(entry.tags.begin(), entry.tags.end());
    }
    Statement edits(db_, "SELECT plugin_key,tag,added FROM tag_edits WHERE library=?");
    edits.bind(1, library);
    while (edits.row()) {
        auto plugin = plugins.find(edits.text(0));
        if (plugin == plugins.end()) continue;
        auto& tags = plugin->second.tags;
        if (edits.flag(2)) tags[edits.text(1)] = "manual";
        else tags.erase(edits.text(1));
    }
    for (auto& entry : entries) {
        const auto& info = plugins.at(entry.plugin_key);
        entry.vendor = info.vendors.size() == 1 ? *info.vendors.begin() : std::string{};
        entry.tags = info.tags;
    }
    tx.commit();
    return entries;
}

void Store::edit_tags(const std::string& library, const std::vector<std::string>& keys,
                      const std::vector<std::string>& add, const std::vector<std::string>& remove) {
    std::lock_guard<std::mutex> lock(mutex_);
    Transaction tx(db_, true);
    Statement key_query(db_, find_key);
    key_query.bind(1, library);
    Statement edit(db_, "INSERT OR REPLACE INTO tag_edits(library,plugin_key,tag,added) VALUES(?,?,?,?)");
    edit.bind(1, library);
    for (const auto& key : keys) {
        require_key(key_query, key);
        edit.bind(2, key);
        for (bool added : {true, false}) {
            edit.bind(4, added);
            for (const auto& tag : added ? add : remove) {
                require_name(tag);
                edit.bind(3, tag);
                edit.write();
            }
        }
    }
    tx.commit();
}

void Store::set_vendor(const std::string& library, const std::string& key, const std::string& vendor) {
    std::lock_guard<std::mutex> lock(mutex_);
    Transaction tx(db_, true);
    Statement key_query(db_, find_key);
    key_query.bind(1, library);
    require_key(key_query, key);
    Statement edit(db_, vendor.empty() ? "DELETE FROM vendor_edits WHERE library=? AND plugin_key=?"
                                       : "INSERT OR REPLACE INTO vendor_edits(library,plugin_key,vendor) VALUES(?,?,?)");
    edit.bind(1, library);
    edit.bind(2, key);
    if (!vendor.empty()) edit.bind(3, vendor);
    edit.write();
    tx.commit();
}

void Store::alias_vendor(const std::string& library, const std::string& raw, const std::string& canonical) {
    require_name(library);
    require_name(raw);
    std::lock_guard<std::mutex> lock(mutex_);
    Transaction tx(db_, true);
    Statement clear(db_, "DELETE FROM vendor_aliases WHERE library=? AND raw=?");
    clear.bind(1, library);
    clear.bind(2, raw);
    clear.write();
    if (!canonical.empty() && raw != canonical) {
        Statement target(db_, "SELECT canonical FROM vendor_aliases WHERE library=? AND raw=?");
        target.bind(1, library);
        target.bind(2, canonical);
        const auto resolved = target.row() ? target.text(0) : canonical;
        target.reset();
        if (resolved == raw) throw std::invalid_argument("Vendor alias would form a cycle");
        Statement dependencies(db_, "UPDATE vendor_aliases SET canonical=? WHERE library=? AND canonical=?");
        dependencies.bind(1, resolved);
        dependencies.bind(2, library);
        dependencies.bind(3, raw);
        dependencies.write();
        Statement edit(db_, "INSERT INTO vendor_aliases(library,raw,canonical) VALUES(?,?,?)");
        edit.bind(1, library);
        edit.bind(2, raw);
        edit.bind(3, resolved);
        edit.write();
    }
    tx.commit();
}

void Store::set_image(const std::string& library, const std::string& key, const std::string& image) {
    std::lock_guard<std::mutex> lock(mutex_);
    Transaction tx(db_,true);
    Statement existing(db_,find_key);
    existing.bind(1,library);
    require_key(existing,key);
    Statement edit(db_,image.empty() ? "DELETE FROM preview_images WHERE library=? AND plugin_key=?"
        : "INSERT OR REPLACE INTO preview_images VALUES(?,?,?)");
    edit.bind(1,library);
    edit.bind(2,key);
    if (!image.empty()) edit.bind(3,image);
    edit.write();
    tx.commit();
}
} // namespace flcat
