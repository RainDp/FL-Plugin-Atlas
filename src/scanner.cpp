#include "flcat/scanner.hpp"

#include <algorithm>
#include <array>
#include <fstream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string_view>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace flcat {
namespace {
namespace fs = std::filesystem;
using Fields = std::map<std::string, std::string>;
using Cancel = std::function<bool()>;

void checkpoint(const Cancel& cancelled) {
    if (cancelled && cancelled()) throw std::runtime_error("扫描已取消");
}

std::string lower(std::string text) {
    for (char& ch : text) {
        if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
    }
    return text;
}

std::string trim(const std::string& text) {
    const auto start = text.find_first_not_of(" \r\n\t");
    if (start == std::string::npos) return {};
    return text.substr(start, text.find_last_not_of(" \r\n\t") - start + 1);
}

bool linked(const fs::path& path) {
#ifdef _WIN32
    // Junctions may appear as ordinary directories to std::filesystem.
    const auto attrs = GetFileAttributesW(path.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES) {
        throw fs::filesystem_error("无法检查文件", path,
            std::error_code(static_cast<int>(GetLastError()), std::system_category()));
    }
    return (attrs & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
#else
    return fs::is_symlink(fs::symlink_status(path));
#endif
}

std::ifstream open_file(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("无法读取文件：" + path.u8string());
    file.exceptions(std::ios::badbit);
    return file;
}

std::string utf8(const std::u16string& text) {
    std::string result;
    for (size_t i = 0; i < text.size(); ++i) {
        unsigned code = text[i];
        if (code >= 0xd800 && code <= 0xdbff) {
            if (++i == text.size() || text[i] < 0xdc00 || text[i] > 0xdfff)
                throw std::runtime_error("无效的 UTF-16 元数据");
            code = 0x10000 + ((code - 0xd800) << 10) + (text[i] - 0xdc00);
        } else if (code >= 0xdc00 && code <= 0xdfff) {
            throw std::runtime_error("无效的 UTF-16 元数据");
        }
        if (code < 0x80) { result.push_back(static_cast<char>(code)); continue; }
        unsigned trailing = code < 0x800 ? 1 : (code < 0x10000 ? 2 : 3);
        const unsigned lead[] = {0, 0xc0, 0xe0, 0xf0};
        result.push_back(static_cast<char>(lead[trailing] | (code >> (6 * trailing))));
        while (trailing != 0) result.push_back(static_cast<char>(0x80 | ((code >> (6 * --trailing)) & 0x3f)));
    }
    return result;
}

std::string decode(std::string bytes) {
    if (bytes.compare(0, 3, "\xef\xbb\xbf") == 0) return bytes.substr(3);
    const bool little = bytes.compare(0, 2, "\xff\xfe") == 0;
    if (little || bytes.compare(0, 2, "\xfe\xff") == 0) {
        if (bytes.size() % 2) throw std::runtime_error("无效的 UTF-16 元数据");
        std::u16string text;
        for (size_t i = 2; i < bytes.size(); i += 2) {
            auto lo = static_cast<unsigned char>(bytes[i + (little ? 0 : 1)]);
            auto hi = static_cast<unsigned char>(bytes[i + (little ? 1 : 0)]);
            text.push_back(static_cast<char16_t>((hi << 8) | lo));
        }
        return utf8(text);
    }
#ifdef _WIN32
    // Older records may use the current Windows ANSI code page.
    if (!bytes.empty() && !MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(),
                                               static_cast<int>(bytes.size()), nullptr, 0)) {
        int size = MultiByteToWideChar(CP_ACP, 0, bytes.data(), static_cast<int>(bytes.size()), nullptr, 0);
        if (!size) throw std::runtime_error("无法解码扫描记录");
        std::wstring wide(size, L'\0');
        MultiByteToWideChar(CP_ACP, 0, bytes.data(), static_cast<int>(bytes.size()), wide.data(), size);
        return utf8(std::u16string(wide.begin(), wide.end()));
    }
#endif
    return bytes;
}

Fields read_fields(fs::path path) {
    path.replace_extension(".nfo");
    if (!fs::exists(fs::symlink_status(path)) || linked(path)) return {};
    if (!fs::is_regular_file(path)) throw std::runtime_error("元数据不是普通文件：" + path.u8string());
    auto file = open_file(path);
    const std::string raw{std::istreambuf_iterator<char>{file}, {}};
    std::istringstream lines(decode(raw));
    Fields fields;
    for (std::string line; std::getline(lines, line);) {
        auto eq = line.find('=');
        if (eq != std::string::npos)
            fields[lower(trim(line.substr(0, eq)))] = trim(line.substr(eq + 1));
    }
    return fields;
}

bool numbered(const std::string& key, std::string_view prefix) {
    return key.compare(0, prefix.size(), prefix) == 0 && key.size() > prefix.size()
        && key.find_first_not_of("0123456789", prefix.size()) == std::string::npos;
}

const Fields purposes = {
    {"eq","均衡"}, {"equalizer","均衡"}, {"dynamics","动态处理"}, {"compressor","压缩"},
    {"compression","压缩"}, {"reverb","混响"}, {"delay","延迟"}, {"distortion","失真"},
    {"synth","合成器"}, {"instrument synth","合成器"}, {"sampler","采样器"},
    {"mastering","母带"}, {"vocals","人声"}, {"analyzer","分析"}, {"analysis","分析"},
    {"filter","滤波"}, {"modulation","调制"}, {"flanger","镶边"}, {"pitch shift","移调"},
    {"restoration","音频修复"}, {"channel strip","通道条"}, {"spatial","空间处理"},
    {"drum","鼓"}, {"gain","增益"}, {"limiter","限幅"}
};

// Stable plugin-class IDs, where FL supplies one. Reject malformed or null GUIDs.
std::string guid(std::string value) {
    value = lower(trim(value));
    if (value.size() == 38 && value.front() == '{' && value.back() == '}')
        value = value.substr(1, 36);
    if (value.size() == 36) {
        for (size_t i : {8, 13, 18, 23}) if (value[i] != '-') return {};
        value.erase(std::remove(value.begin(), value.end(), '-'), value.end());
    }
    if (value.size() != 32 || value.find_first_not_of("0123456789abcdef") != value.npos ||
        value.find_first_not_of('0') == value.npos) return {};
    return value;
}

struct Record {
    Entry entry;
    bool installed;
    bool conflicted;
    std::string reference;
    std::uintmax_t size;
};

Record read_record(const fs::path& file, const fs::path& root, const fs::path& branch,
                   const std::string& kind, bool installed) {
    Fields fields = read_fields(file);
    Entry entry;
    entry.path = file.lexically_relative(root).generic_u8string();
    entry.kind = kind;
    entry.name = installed && !fields["ps_name"].empty() ? fields["ps_name"] : file.stem().u8string();
    const auto relative = file.lexically_relative(branch);
    if (installed && relative.has_parent_path()) entry.format = relative.begin()->u8string();
    if (lower(entry.format) == "new") entry.format.clear();
    std::set<std::string> vendors, guids, strings;
    for (const auto& [key, value] : fields) {
        if (value.empty()) continue;
        if (numbered(key, "ps_file_vendorname_")) vendors.insert(value);
        if (numbered(key, "ps_file_guid_")) {
            auto id = guid(value);
            if (!id.empty()) guids.insert(id);
        }
        if (numbered(key, "ps_file_idstring_")) strings.insert(value);
        if (numbered(key, "ps_file_category_")) {
            std::istringstream tokens(value);
            for (std::string token; std::getline(tokens, token, '|');) {
                const auto tag = purposes.find(lower(trim(token)));
                if (tag != purposes.end()) entry.tags.emplace(tag->second, key + "=" + value);
            }
        }
    }
    bool conflicted = vendors.size() > 1 || guids.size() > 1 || strings.size() > 1;
    if (vendors.size() == 1) entry.vendor = *vendors.begin();
    if (!conflicted && guids.size() == 1) entry.plugin_key = "guid:" + kind + ":" + *guids.begin();
    else if (!conflicted && strings.size() == 1)
        entry.plugin_key = "idstring:" + kind + ":" + *strings.begin();
    else entry.plugin_key = "path:" + entry.path;

    if (!installed) {
        for (const auto& folder : relative.parent_path()) {
            auto raw = folder.u8string();
            auto name = lower(raw);
            if (name == "synth classic" || name == "synth special") name = "synth";
            const auto tag = purposes.find(name);
            if (tag != purposes.end()) entry.tags.emplace(tag->second, "收藏目录：" + raw);
            else if (std::any_of(purposes.begin(), purposes.end(),
                          [&](const auto& pair) { return pair.second == raw; }))
                entry.tags.emplace(raw, "收藏目录：" + raw);
        }
    }
    // A generic Dynamics or Delay reverb category alone does not establish
    // compression or reverb. These exact Image-Line products are documented.
    if (entry.vendor == "Image-Line" && kind == "Effects") {
        static const Fields products = {{"Fruity Compressor","压缩"}, {"Fruity Multiband Compressor","压缩"},
            {"Fruity Reeverb 2","混响"}, {"Fruity Reeverb","混响"}, {"Fruity Delay 2","延迟"},
            {"Fruity Delay 3","延迟"}, {"Fruity Parametric EQ 2","均衡"}, {"Fruity Parametric EQ","均衡"}};
        const auto match = products.find(fields["ps_name"]);
        if (match != products.end()) entry.tags.emplace(match->second, "Image-Line 产品：" + match->first);
    }
    std::string reference = fields["ps_presetfilename"];
    std::replace(reference.begin(), reference.end(), '\\', '/');
    const std::string prefix = "%flplugindbpath%/";
    reference = lower(reference);
    reference = reference.compare(0, prefix.size(), prefix) == 0 ? reference.substr(prefix.size()) : "";
    return {std::move(entry), installed, conflicted, reference, fs::file_size(file)};
}

bool identical(const fs::path& a, const fs::path& b, const Cancel& cancelled) {
    auto first = open_file(a), second = open_file(b);
    std::array<char, 16384> left{}, right{};
    do {
        checkpoint(cancelled);
        first.read(left.data(), left.size());
        second.read(right.data(), right.size());
        if (first.gcount() != second.gcount() ||
            !std::equal(left.begin(), left.begin() + first.gcount(), right.begin())) return false;
    } while (first && second);
    return first.eof() && second.eof();
}

void link_presets(const fs::path& root, std::vector<Record>& records, const Cancel& cancelled) {
    std::map<std::string, const Record*> paths;
    std::map<std::pair<std::string, std::uintmax_t>, std::vector<const Record*>> by_size;
    std::map<std::string, std::set<std::string>> vendors;
    for (const auto& record : records) {
        const auto& entry = record.entry;
        if (!entry.vendor.empty()) vendors[entry.plugin_key].insert(entry.vendor);
        if (!record.installed || entry.format.empty()) continue;
        paths.emplace(lower(entry.path), &record);
        by_size[{entry.kind, record.size}].push_back(&record);
    }
    // A reused class ID with conflicting manufacturers cannot safely identify
    // records, even if the descriptive metadata uses the same plugin name.
    for (auto& record : records) {
        if (vendors[record.entry.plugin_key].size() > 1) {
            record.conflicted = true;
            record.entry.plugin_key = "path:" + record.entry.path;
        }
    }
    for (auto& record : records) {
        checkpoint(cancelled);
        auto& entry = record.entry;
        if (record.conflicted || (record.installed && !entry.format.empty())) continue;
        const bool has_id = entry.plugin_key.compare(0, 5, "path:") != 0;
        const Record* match = nullptr;
        const auto reference = paths.find(record.reference);
        if (reference != paths.end()) {
            match = reference->second;
        } else if (!has_id && record.size != 0) {
            // A size match is only a shortlist. Compare complete FST bytes and
            // require that all matching originals identify the same plugin.
            for (auto source : by_size[{entry.kind, record.size}]) {
                if (!identical(root / fs::u8path(entry.path), root / fs::u8path(source->entry.path), cancelled))
                    continue;
                if (source->conflicted || (match && match->entry.plugin_key != source->entry.plugin_key)) {
                    match = nullptr;
                    break;
                }
                match = source;
            }
        }
        if (match && (match->conflicted || match->entry.kind != entry.kind ||
            (has_id && entry.plugin_key != match->entry.plugin_key) ||
            (!entry.vendor.empty() && !match->entry.vendor.empty() && entry.vendor != match->entry.vendor))) {
            record.conflicted = true;
            entry.plugin_key = "path:" + entry.path;
            continue;
        }
        if (match) {
            entry.plugin_key = match->entry.plugin_key;
            if (entry.vendor.empty()) entry.vendor = match->entry.vendor;
            if (entry.format.empty()) entry.format = match->entry.format;
            for (const auto& [tag, evidence] : match->entry.tags)
                entry.tags.emplace(tag, match->entry.path + " / " + evidence);
        } else if (has_id && entry.vendor.empty() && vendors[entry.plugin_key].size() == 1) {
            entry.vendor = *vendors[entry.plugin_key].begin();
        }
    }
}
} // namespace

std::vector<fs::path> thumbnail_files(const fs::path& root, const std::string& preset) {
    const auto rel=fs::u8path(preset);
    if (rel.empty() || rel.has_root_path()) return {};
    auto folder=root;
    for (const auto& part : rel.parent_path()) {
        if (part==".." || part==".") return {};
        folder/=part;
        if (!fs::is_directory(folder) || linked(folder)) return {};
    }
    const auto file=root/rel;
    std::vector<fs::path> candidates;
    auto append=[&](fs::path name) {
        if (name.empty() || name.has_root_path() || name.has_parent_path() ||
            name.u8string().find(':')!=std::string::npos) return;
        auto ext=lower(name.extension().u8string());
        auto candidate=folder/name;
        if ((ext==".png" || ext==".bmp" || ext==".jpg" || ext==".jpeg") &&
            fs::is_regular_file(candidate) && !linked(candidate) &&
            std::find(candidates.begin(),candidates.end(),candidate)==candidates.end())
            candidates.push_back(candidate);
    };
    auto fields=read_fields(file);
    if (fields.count("bitmap")) append(fs::u8path(fields.at("bitmap")));
    for (const char* ext : {".png",".bmp",".jpg",".jpeg"}) append(rel.filename().replace_extension(ext));
    return candidates;
}

std::vector<Entry> scan(const std::filesystem::path& root, const std::function<bool()>& cancelled) {
    checkpoint(cancelled);
    if (!fs::is_directory(root)) throw std::runtime_error("请选择有效的 Plugin database 目录");
    std::vector<Record> records;
    bool found = false;
    for (const bool installed : {true, false}) {
        const fs::path parent = installed ? root / "Installed" : root;
        if (installed && fs::exists(parent) && linked(parent))
            throw std::runtime_error("Installed 是文件系统链接");
        for (const std::string kind : {"Effects", "Generators"}) {
            const auto branch = parent / kind;
            if (!fs::exists(branch)) continue;
            if (!fs::is_directory(branch) || linked(branch))
                throw std::runtime_error("无效的插件目录：" + branch.u8string());
            found = true;
            for (fs::recursive_directory_iterator it(branch), end; it != end; ++it) {
                checkpoint(cancelled);
                const auto& path = it->path();
                if (linked(path) || (!installed && path == branch / fs::u8path("插件分类"))) {
                    it.disable_recursion_pending();
                    continue;
                }
                if (lower(path.extension().u8string()) == ".fst" && it->is_regular_file())
                    records.push_back(read_record(path, root, branch, kind, installed));
            }
        }
    }
    if (!found) throw std::runtime_error("该目录缺少 FL Studio 插件分类目录");
    link_presets(root, records, cancelled);
    std::vector<Entry> result;
    result.reserve(records.size());
    for (auto& record : records) {
        record.entry.identity_conflict = record.conflicted;
        result.push_back(std::move(record.entry));
    }
    std::sort(result.begin(), result.end(), [](const Entry& a, const Entry& b) { return a.path < b.path; });
    checkpoint(cancelled);
    return result;
}
} // namespace flcat
