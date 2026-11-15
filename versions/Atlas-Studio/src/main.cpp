#include "flcat/paths.hpp"
#include "flcat/filter.hpp"
#include "flcat/scanner.hpp"
#include "flcat/store.hpp"

#include <algorithm>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <set>
#include <stdexcept>

#ifdef _WIN32
#include "../plugin/online_images.hpp"
#define NOMINMAX
#include <windows.h>
#include <shlobj.h>
#endif

namespace fs = std::filesystem;
using Strings = std::vector<std::string>;

namespace {

const char* help = R"(FL Plugin Atlas · 插件图谱

atlas scan                         扫描 FL 的插件记录和收藏
atlas vendors | purposes           显示每个厂商或用途的预设入口数
atlas artwork                       在 Windows 为缺图条目联网补图
atlas list [--vendor 名称] [--tag 用途]... [--kind Effects|Generators] [--include-missing]
atlas tag --entry 相对路径 [--entry 相对路径]... [--add 用途]... [--remove 用途]...
atlas vendor --entry 相对路径 (--name 新厂商 | --reset)
atlas alias --from 原厂商 (--to 统一名称 | --reset)

所有命令支持 --library "Plugin database 目录" 和 --db "本地索引文件"。
list 的最后一列是 --entry 使用的路径；多个 --tag 取交集。
)";

const std::map<std::string, Strings> command_options{
#ifdef _WIN32
    {"artwork", {}},
#endif
    {"scan", {}}, {"vendors", {}}, {"purposes", {}},
    {"list", {"--vendor", "--tag", "--kind", "--include-missing"}},
    {"tag", {"--entry", "--add", "--remove"}},
    {"vendor", {"--entry", "--name", "--reset"}},
    {"alias", {"--from", "--to", "--reset"}}
};

struct Options {
    std::string command;
    std::map<std::string, Strings> values;

    bool has(const std::string& key) const { return values.count(key) != 0; }
    const Strings& all(const std::string& key) const {
        static const Strings empty;
        return has(key) ? values.at(key) : empty;
    }
    const std::string& one(const std::string& key) const { return values.at(key).front(); }
};

Options parse(const Strings& args) {
    Options opt{args.front(), {}};
    const auto spec = command_options.find(opt.command);
    if (spec == command_options.end()) throw std::runtime_error("未知命令：" + opt.command);
    for (size_t i = 1; i < args.size(); ++i) {
        const std::string key = args[i];
        if (key != "--library" && key != "--db" &&
            std::find(spec->second.begin(), spec->second.end(), key) == spec->second.end())
            throw std::runtime_error("当前命令不支持参数：" + key);
        const bool repeatable = key == "--tag" || key == "--add" || key == "--remove" ||
                                (key == "--entry" && opt.command == "tag");
        if (opt.has(key) && !repeatable) throw std::runtime_error("重复参数：" + key);
        if (key == "--include-missing" || key == "--reset") {
            opt.values[key] = {};
            continue;
        }
        if (++i == args.size() || args[i].find("--") == 0 ||
            args[i].find_first_not_of(" \r\n\t") == std::string::npos)
            throw std::runtime_error("缺少参数值：" + key);
        opt.values[key].push_back(args[i]);
    }
    if (opt.command == "tag") {
        if (!opt.has("--entry") || (!opt.has("--add") && !opt.has("--remove")))
            throw std::runtime_error("tag 需要 --entry 和 --add 或 --remove。");
        for (const auto& tag : opt.all("--add"))
            if (tag == flcat::unclassified ||
                std::find(opt.all("--remove").begin(), opt.all("--remove").end(), tag) != opt.all("--remove").end())
                throw std::runtime_error("不能添加“待分类”或同时添加和移除同一用途。");
    }
    if (opt.command == "vendor" && (!opt.has("--entry") || opt.has("--name") == opt.has("--reset")))
        throw std::runtime_error("vendor 需要 --entry，并选择 --name 或 --reset。");
    if (opt.command == "alias" && (!opt.has("--from") || opt.has("--to") == opt.has("--reset")))
        throw std::runtime_error("alias 需要 --from，并选择 --to 或 --reset。");
    if (opt.has("--kind") && opt.one("--kind") != "Effects" && opt.one("--kind") != "Generators")
        throw std::runtime_error("--kind 可选值为 Effects 或 Generators。");
    return opt;
}

using flcat::documents;
using flcat::data_home;
using flcat::path_id;
volatile std::sig_atomic_t interrupted = 0;
void interrupt(int) { interrupted = 1; }

void show(const std::string& command, const std::vector<flcat::Entry>& rows, const Options& opt) {
    flcat::Filter filter;
    if (opt.has("--vendor")) filter.vendor = opt.one("--vendor");
    if (opt.has("--kind")) filter.kind = opt.one("--kind");
    filter.tags = opt.all("--tag");
    filter.include_missing = opt.has("--include-missing");
    std::map<std::string, size_t> counts;
    if (command == "list") std::cout << "插件\t厂商\t用途\t类型\t格式\t可用\t入口路径\n";
    for (const auto& row : rows) {
        if (!filter.matches(row)) continue;
        const std::string vendor = row.vendor.empty() ? flcat::unknown_vendor : row.vendor;
        if (command == "vendors") {
            ++counts[vendor];
        } else if (command == "purposes") {
            if (row.tags.empty()) ++counts[flcat::unclassified];
            for (const auto& tag : row.tags) ++counts[tag.first];
        } else {
            std::string tags;
            for (const auto& tag : row.tags) {
                if (!tags.empty()) tags += "、";
                tags += tag.first;
            }
            std::cout << row.name << '\t' << vendor << '\t' << (tags.empty() ? flcat::unclassified : tags)
                      << '\t' << row.kind << '\t' << row.format << '\t' << (row.available ? "是" : "否")
                      << '\t' << row.path << '\n';
        }
    }
    for (const auto& value : counts) std::cout << value.first << '\t' << value.second << '\n';
}

Strings entry_keys(const std::vector<flcat::Entry>& rows, const Strings& paths) {
    std::map<std::string, std::string> by_path;
    for (const auto& row : rows) by_path.emplace(path_id(fs::u8path(row.path)), row.plugin_key);
    std::set<std::string> keys;
    for (const auto& path : paths) {
        auto match = by_path.find(path_id(fs::u8path(path).lexically_normal()));
        if (match == by_path.end()) throw std::runtime_error("目录中没有此入口：" + path);
        keys.insert(match->second);
    }
    return {keys.begin(), keys.end()};
}

int run(const Strings& args) {
    if (args.empty() || args == Strings{"--help"} || args == Strings{"help"}) {
        std::cout << help;
        return 0;
    }
    const auto opt = parse(args);
    const auto library = fs::weakly_canonical(fs::absolute(opt.has("--library") ? fs::u8path(opt.one("--library"))
        : documents() / "Image-Line/FL Studio/Presets/Plugin database"));
    const auto database = fs::weakly_canonical(fs::absolute(opt.has("--db") ? fs::u8path(opt.one("--db"))
        : data_home() / "FL Plugin Atlas/catalog.sqlite3"));
    const auto id = path_id(library);
    const auto db_id = path_id(database);
    const auto prefix = id.back() == '/' ? id : id + "/";
    if (db_id == id || db_id.compare(0, prefix.size(), prefix) == 0)
        throw std::runtime_error("请选择 FL 插件库之外的位置保存 SQLite 索引。");

    if (opt.command == "scan") {
        std::signal(SIGINT, interrupt);
        const auto rows = flcat::scan(library, [] { return interrupted != 0; });
        if (interrupted) throw std::runtime_error("扫描已取消。");
        fs::create_directories(database.parent_path());
        flcat::Store store(database);
        store.replace_scan(id, rows);
        std::cout << "已索引 " << rows.size() << " 个预设入口。\n" << database.u8string() << '\n';
        return 0;
    }
    if (!fs::is_regular_file(database)) throw std::runtime_error("找不到本地索引，请先运行 atlas scan。");
    flcat::Store store(database);
    if (opt.command == "alias") {
        store.alias_vendor(id, opt.one("--from"), opt.has("--reset") ? "" : opt.one("--to"));
    } else {
        const auto rows = store.list(id);
#ifdef _WIN32
        if(opt.command=="artwork") {
            std::atomic<bool> stop{false};
            auto result=artwork::fill(library,rows,stop);
            std::cout<<"新增图片 "<<result.saved<<"，未匹配 "<<result.missing<<"，失败 "<<result.errors<<'\n';
            return result.errors ? 2 : 0;
        }
#endif
        if (opt.command == "tag" || opt.command == "vendor") {
            const auto keys = entry_keys(rows, opt.all("--entry"));
            if (opt.command == "tag") store.edit_tags(id, keys, opt.all("--add"), opt.all("--remove"));
            else store.set_vendor(id, keys.front(), opt.has("--reset") ? "" : opt.one("--name"));
        } else {
            show(opt.command, rows, opt);
            return 0;
        }
    }
    std::cout << "分类修改已保存。\n";
    return 0;
}

} // namespace

#ifdef _WIN32
int wmain(int argc, wchar_t** argv) {
    SetConsoleOutputCP(CP_UTF8);
#else
int main(int argc, char** argv) {
#endif
    try {
        Strings args;
        for (int i = 1; i < argc; ++i) args.push_back(fs::path(argv[i]).u8string());
        return run(args);
    } catch (const std::exception& error) {
        std::cerr << "插件图谱：" << error.what() << '\n';
        return 1;
    }
}

