#pragma once
#include <filesystem>
#include <cstdlib>
#include <string>
#include <stdexcept>
#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#endif
namespace flcat {
namespace fs = std::filesystem;
#ifdef _WIN32
inline fs::path known_folder(REFKNOWNFOLDERID folder) {
    PWSTR text = nullptr;
    if (FAILED(SHGetKnownFolderPath(folder, KF_FLAG_DONT_VERIFY, nullptr, &text)))
        throw std::runtime_error("无法定位用户目录。请指定 --library 和 --db。");
    fs::path result(text);
    CoTaskMemFree(text);
    return result;
}
inline fs::path documents() { return known_folder(FOLDERID_Documents); }
inline fs::path data_home() { return known_folder(FOLDERID_LocalAppData); }
#else
inline fs::path home() {
    const auto* value = std::getenv("HOME");
    if (!value || !*value) throw std::runtime_error("请指定 --library 和 --db。");
    return fs::u8path(value);
}
inline fs::path documents() { return home() / "Documents"; }
inline fs::path data_home() {
    const auto* data = std::getenv("XDG_DATA_HOME");
    return data && *data ? fs::u8path(data) : home() / ".local/share";
}
#endif

inline std::string path_id(const fs::path& path) {
#ifdef _WIN32
    const auto wide = path.generic_wstring();
    const int count = LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE, wide.data(),
                                    static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr, 0);
    std::wstring lower(count, L'\0');
    if (!count || !LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE, wide.data(),
                                 static_cast<int>(wide.size()), lower.data(), count, nullptr, nullptr, 0))
        throw std::runtime_error("无法规范化路径。");
    return fs::path(lower).generic_u8string();
#else
    return path.generic_u8string();
#endif
}

} // namespace flcat
