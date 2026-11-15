#include "flcat/scanner.hpp"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace fs = std::filesystem;
using flcat::Entry;

namespace {
void check(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

struct Fixture {
    fs::path root = fs::temp_directory_path() / fs::u8path("atlas-scanner-样本-" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Fixture() { check(fs::create_directory(root), "cannot create fixture"); }
    ~Fixture() { std::error_code ignored; fs::remove_all(root, ignored); }
    void file(const std::string& name, const std::string& data) {
        auto path = root / fs::u8path(name);
        fs::create_directories(path.parent_path());
        std::ofstream stream(path, std::ios::binary);
        stream << data;
        check(bool(stream), "cannot write fixture");
    }
};

template<typename Action> void expect_failure(Action action) {
    try { action(); }
    catch (const std::exception&) { return; }
    throw std::runtime_error("expected scan to fail");
}

void run() {
    Fixture fixture;
    fixture.file("images/Entry.nfo","Bitmap=自定义.png\n");
    fixture.file("images/自定义.png","thumbnail fixture");
    fixture.file("images/Entry.bmp","companion fixture");
    auto pictures=flcat::thumbnail_files(fixture.root,"images/Entry.fst");
    check(pictures.size()==2 && pictures.front().filename().u8string()=="自定义.png","thumbnail preference");
    fixture.file("images/Entry.nfo","Bitmap=../outside.png");
    fixture.file("outside.png","unrelated");
    check(flcat::thumbnail_files(fixture.root,"images/Entry.fst").size()==1,"external thumbnail reference");
    expect_failure([&]{ flcat::scan(fixture.root); });
    const std::string base = "Installed/Effects/VST3/Filter";
    const std::string fields = "ps_file_vendorname_0=音厂\nps_file_vendorname_2=音厂\n"
        "ps_file_category_0=Fx|EQ|Dynamics\nps_file_category_2=Fx|Reverb\n"
        "ps_file_guid_0={A558B1E0-5BA4-4005-A755-E90DC44A220B}\nps_name=Filter\n";
    fixture.file(base + ".fst", "first state");
    fixture.file(base + ".nfo", "\xef\xbb\xbf" + fields);
    fixture.file("Effects/母带/Custom.fst", "different state");
    fixture.file("Effects/母带/Custom.nfo", fields + "Bitmap=custom.png\n");
    fixture.file("Effects/收藏/Original.fst", "first state");
    fixture.file("Effects/收藏/Original.nfo", "Bitmap=Original.png\n");
    fixture.file("Effects/Distortion/Filter.fst", "standalone");
    fixture.file("Effects/插件分类/按厂商/厂商/Skip.fst", "generated");
    fixture.file("Installed/Generators/VST3/Filter.fst", "synth");
    fixture.file("Installed/Generators/VST3/Filter.nfo", "ps_file_vendorname_1=Other\nps_file_category_0=Instrument\n");
    fixture.file("Installed/Generators/CLAP/Multiple.fst", "conflict");
    fixture.file("Installed/Generators/CLAP/Multiple.nfo", "ps_file_vendorname_0=A\nps_file_vendorname_1=B\n");
    fixture.file("Installed/Effects/VST/No NFO.fst", "no info");
    fixture.file("Installed/Effects/New/Filter.fst", "first state");
    fixture.file("Installed/Effects/New/Filter.nfo", "ps_presetfilename=%FLPluginDBPath%\\Installed\\Effects\\VST3\\Filter.fst\n");
    fixture.file("Installed/Effects/VST3/UTF16.fst", "text encoding");
    std::string encoded("\xff\xfe", 2);
    for (char16_t c : std::u16string(u"ps_file_vendorname_0=厂商\r\n")) {
        encoded.push_back(static_cast<char>(c & 255));
        encoded.push_back(static_cast<char>(c >> 8));
    }
    fixture.file("Installed/Effects/VST3/UTF16.nfo", encoded);

    auto records = flcat::scan(fixture.root);
    auto get = [&](const std::string& path) -> const Entry& {
        auto it = std::find_if(records.begin(), records.end(), [&](const Entry& e){ return e.path == path; });
        check(it != records.end(), "missing " + path);
        return *it;
    };
    check(records.size() == 9, "published copies must be excluded");
    const auto& main = get(base + ".fst");
    check(main.vendor == "音厂" && main.kind == "Effects" && main.format == "VST3", "scan fields");
    check(main.tags.count("均衡") && main.tags.count("混响") && main.tags.count("动态处理") &&
          !main.tags.count("压缩") && main.tags.at("混响").find("ps_file_category_2") == 0,
          "all numbered specific categories and evidence");
    const auto& custom = get("Effects/母带/Custom.fst");
    check(custom.name == "Custom" && custom.plugin_key == main.plugin_key && custom.path != main.path &&
          custom.tags.count("母带"),
          "different starting states with the same class ID");
    const auto& copy = get("Effects/收藏/Original.fst");
    check(copy.plugin_key == main.plugin_key && copy.vendor == main.vendor && copy.tags.count("均衡"),
          "byte-identical preset");
    const auto& new_alias = get("Installed/Effects/New/Filter.fst");
    check(new_alias.plugin_key == main.plugin_key && new_alias.format == main.format, "New alias provenance");
    const auto& unknown = get("Effects/Distortion/Filter.fst");
    check(unknown.plugin_key != main.plugin_key && unknown.vendor.empty() && unknown.tags.count("失真"),
          "a matching name is insufficient evidence");
    check(get("Installed/Effects/VST/No NFO.fst").tags.empty(), "unknown purpose");
    check(get("Installed/Generators/CLAP/Multiple.fst").vendor.empty(), "conflicting vendors");
    const auto& synth = get("Installed/Generators/VST3/Filter.fst");
    check(synth.kind == "Generators" && synth.tags.empty() && synth.plugin_key != main.plugin_key,
          "Instrument declares a kind, not a purpose");
    check(get("Installed/Effects/VST3/UTF16.fst").vendor == "厂商", "UTF-16 text");
    int visited = 0;
    expect_failure([&]{ flcat::scan(fixture.root, [&]{ return ++visited == 4; }); });
    check(flcat::scan(fixture.root).size() == records.size(), "cancelled scans are read only");
    fixture.file("Effects/收藏/Rival.fst", "different maker");
    fixture.file("Effects/收藏/Rival.nfo",
        "ps_file_guid_0={A558B1E0-5BA4-4005-A755-E90DC44A220B}\nps_file_vendorname_0=Rival\n");
    records = flcat::scan(fixture.root);
    check(get(base + ".fst").identity_conflict && get("Effects/收藏/Rival.fst").identity_conflict &&
          get(base + ".fst").plugin_key != get("Effects/收藏/Rival.fst").plugin_key,
          "conflicting manufacturers sharing a class ID across branches must stay separate");
    fixture.file("Installed/Effects/VST3/Unusable.fst", "data");
    fs::create_directory(fixture.root / "Installed/Effects/VST3/Unusable.nfo");
    expect_failure([&]{ flcat::scan(fixture.root); });
}
} // namespace

int main() {
    try { run(); }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
