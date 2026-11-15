#include "flcat/store.hpp"

#include <chrono>
#include <future>
#include <iostream>
#include <stdexcept>
#include <utility>

namespace {
namespace fs = std::filesystem;
using flcat::Entry;
using flcat::Store;

void expect(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

class Sandbox {
public:
    Sandbox() {
        const auto id = std::chrono::steady_clock::now().time_since_epoch().count();
        for (int attempt = 0; ; ++attempt) {
            auto candidate = fs::temp_directory_path() / ("atlas-store-test-" + std::to_string(id) + "-" + std::to_string(attempt));
            if (fs::create_directory(candidate)) { path = std::move(candidate); break; }
        }
    }
    ~Sandbox() {
        std::error_code ignored;
        fs::remove_all(path, ignored);
    }
    fs::path path;
};

void test_store(const fs::path& file) {
    Store first(file), second(file);
    const std::string library = u8"测试库";
    Entry eq{"Installed/Effects/EQ.fst", "eq-id", "EQ", "effect", "VST3", "FF",
             {{u8"均衡", "Fx|EQ"}}, true};
    Entry soft = eq;
    soft.path = "Effects/Voice.fst";
    soft.vendor.clear();
    soft.tags.clear();
    Entry other{"Installed/Effects/Other.fst", "other-id", "EQ", "effect", "VST3", "", {}, true};

    first.replace_scan(library, {eq, soft, other});
    first.replace_scan("other-library", {eq});
    auto rows = second.list(library);
    expect(rows.size() == 3, "same-name and preset entries must remain distinct");
    expect(rows[0].vendor == "FF" && rows[0].tags.count(u8"均衡"), "associated presets inherit catalog metadata");
    expect(rows[2].vendor.empty() && rows[2].tags.empty(), "unknown metadata stays empty");

    first.alias_vendor(library, "FF", "FabFilter");
    first.set_image(library,"eq-id","截图.png");
    expect(second.list(library)[0].custom_image=="截图.png" &&
           first.list("other-library")[0].custom_image.empty(),"preview image isolation");
    first.edit_tags(library, {"eq-id","other-id"}, {u8"母带"}, {});
    second.edit_tags(library, {"eq-id"}, {u8"人声"}, {u8"均衡"});
    second.set_vendor(library, "other-id", "Other vendor");
    first.replace_scan(library, {eq, soft, other});
    rows = second.list(library);
    for (std::size_t i : {0u, 1u}) {
        expect(rows[i].vendor == "FabFilter", "vendor alias lost");
        expect(rows[i].custom_image=="截图.png","rescan lost custom preview");
        expect(rows[i].tags.size() == 2 && rows[i].tags.count(u8"母带") && rows[i].tags.count(u8"人声"),
               "rescan lost manual additions or restored excluded automatic purpose");
    }
    expect(rows[2].vendor == "Other vendor" && rows[2].tags.count(u8"母带"), "batch edit did not reach all plugins");
    expect(first.list("other-library")[0].vendor == "FF" && first.list("other-library")[0].tags.size() == 1,
           "edits leaked across libraries");
    first.set_vendor(library, "other-id", "");
    expect(second.list(library)[2].vendor.empty(), "removing a correction must reveal raw metadata");

    bool failed = false;
    try { first.replace_scan(library, {other, other}); }
    catch (const std::invalid_argument&) { failed = true; }
    expect(failed && second.list(library)[0].available, "partial scan was committed");
    failed = false;
    try { first.edit_tags(library, {"eq-id","unknown-key"}, {"partial"}, {}); }
    catch (const std::invalid_argument&) { failed = true; }
    expect(failed && !second.list(library)[0].tags.count("partial"), "partial bulk edit was committed");

    first.replace_scan(library, {});
    rows = second.list(library);
    expect(rows.size() == 3 && !rows[1].available && rows[1].tags.count(u8"母带"),
           "offline scan erased edits or left stale entries available");
    first.replace_scan(library, {eq, soft, other});
    auto edit = std::async(std::launch::async, [&] { first.edit_tags(library, {"eq-id"}, {"background"}, {}); });
    second.edit_tags(library, {"eq-id"}, {"foreground"}, {});
    edit.get();
    Store reopened(file);
    auto saved = reopened.list(library)[0];
    expect(saved.available && saved.tags.count("background") && saved.tags.count("foreground"),
           "independent instances overwrote each other's changes");

    eq.tags = {{"fresh", "Updated category"}};
    first.alias_vendor(library, "FF", "");
    first.replace_scan(library, {eq});
    saved = second.list(library)[1];
    expect(saved.vendor == "FF" && saved.tags.count("fresh"), "new scan metadata not visible");
    expect(!second.list(library)[0].available, "missing sibling not marked offline");
    eq.tags.clear();
    first.replace_scan(library, {eq});
    expect(!second.list(library)[1].tags.count("fresh"), "obsolete scanned purpose survived replacement");

    soft.vendor = "Conflicting vendor";
    first.replace_scan(library, {eq, soft});
    expect(second.list(library)[0].vendor.empty(), "conflicting vendors were attributed automatically");
    second.set_vendor(library, "eq-id", "Corrected");
    expect(first.list(library)[1].vendor == "Corrected", "manual vendor failed to resolve ambiguous metadata");
    first.alias_vendor(library, "Corrected", "Override this alias");
    expect(second.list(library)[1].vendor == "Corrected", "alias replaced an explicit vendor correction");
}

void test_identity(const fs::path& file) {
    Store store(file);
    Entry item{"Installed/Foo.fst", "path:installed/foo.fst", "Foo", "Effects", "", "", {}, true};
    store.replace_scan("identities", {item});
    store.edit_tags("identities", {item.plugin_key}, {"mine"}, {"suppressed"});
    store.set_vendor("identities", item.plugin_key, "Manual");

    const auto weak = item.plugin_key;
    item.plugin_key = "guid:Effects:a";
    item.tags.emplace("suppressed", "category");
    store.replace_scan("identities", {item});
    auto found = store.list("identities")[0];
    expect(found.plugin_key == item.plugin_key && found.vendor == "Manual" && found.tags.count("mine")
           && !found.tags.count("suppressed"), "weak identity upgrade lost edits");
    item.plugin_key = weak;
    store.replace_scan("identities", {item});
    found = store.list("identities")[0];
    expect(found.plugin_key == "guid:Effects:a" && found.tags.count("mine"), "metadata loss orphaned edits");

    item.identity_conflict = true;
    store.replace_scan("identities", {item});
    found = store.list("identities")[0];
    expect(found.plugin_key == weak && found.vendor.empty() && !found.tags.count("mine") && found.identity_conflict,
           "ambiguous metadata was linked to the historical plugin");
    store.edit_tags("identities", {weak}, {"ambiguous-only"}, {});
    item.identity_conflict = false;
    item.plugin_key = "guid:Effects:a";
    store.replace_scan("identities", {item});
    found = store.list("identities")[0];
    expect(found.tags.count("mine") && !found.tags.count("ambiguous-only") && !found.identity_conflict,
           "resolved identity lost its corrections or inherited ambiguous edits");
    item.plugin_key = "guid:Effects:b";
    store.replace_scan("identities", {item});
    expect(!store.list("identities")[0].tags.count("mine"), "replaced plugin inherited the old plugin's edits");
}
} // namespace

int main() {
    try {
        Sandbox sandbox;
        const auto db = sandbox.path / fs::u8path(u8"插件") / "atlas.sqlite";
        test_store(db);
        test_identity(db);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
