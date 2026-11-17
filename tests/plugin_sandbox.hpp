#pragma once
#include <windows.h>
#include <shlobj.h>
#include <filesystem>
#include <stdexcept>
#include <cstring>

// Redirect known folders only inside the disposable test process. Production
// retains the Windows folder API and tests cannot open the user's real catalog.
namespace plugin_sandbox {
inline std::filesystem::path root;
inline bool reduce_motion=false;
inline auto original_folders=&SHGetKnownFolderPath;
inline auto original_settings=&SystemParametersInfoW;
inline HRESULT WINAPI folder(REFKNOWNFOLDERID id,DWORD flags,HANDLE token,PWSTR* out) {
    std::filesystem::path path;
    if(IsEqualGUID(id,FOLDERID_Documents)) path=root/L"Documents";
    else if(IsEqualGUID(id,FOLDERID_LocalAppData)) path=root/L"LocalAppData";
    else return original_folders(id,flags,token,out);
    auto text=path.wstring();
    *out=static_cast<PWSTR>(CoTaskMemAlloc((text.size()+1)*sizeof(wchar_t)));
    if(!*out) return E_OUTOFMEMORY;
    std::memcpy(*out,text.c_str(),(text.size()+1)*sizeof(wchar_t));return S_OK;
}
inline BOOL WINAPI settings(UINT action,UINT param,PVOID out,UINT flags) {
    if(action==SPI_GETCLIENTAREAANIMATION) {*static_cast<BOOL*>(out)=!reduce_motion;return TRUE;}
    return original_settings(action,param,out,flags);
}
inline void isolate(HMODULE module,const std::filesystem::path& sandbox) {
    root=std::filesystem::absolute(sandbox);
    std::filesystem::create_directories(root/L"Documents");
    std::filesystem::create_directories(root/L"LocalAppData");
    auto base=reinterpret_cast<BYTE*>(module);
    auto dos=reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    auto nt=reinterpret_cast<IMAGE_NT_HEADERS*>(base+dos->e_lfanew);
    auto table=reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base+nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress);
    bool found_folders=false,found_settings=false;
    for(;table->Name;++table) {
        auto names=reinterpret_cast<IMAGE_THUNK_DATA*>(base+table->OriginalFirstThunk);
        auto addresses=reinterpret_cast<IMAGE_THUNK_DATA*>(base+table->FirstThunk);
        for(;names->u1.AddressOfData;++names,++addresses) {
            if(IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) continue;
            auto item=reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base+names->u1.AddressOfData);
            auto name=reinterpret_cast<const char*>(item->Name);
            ULONG_PTR replacement=0;
            if(!std::strcmp(name,"SHGetKnownFolderPath")) {replacement=reinterpret_cast<ULONG_PTR>(&folder);found_folders=true;}
            if(!std::strcmp(name,"SystemParametersInfoW")) {replacement=reinterpret_cast<ULONG_PTR>(&settings);found_settings=true;}
            if(!replacement) continue;
            DWORD old=0;
            if(!VirtualProtect(&addresses->u1.Function,sizeof(ULONG_PTR),PAGE_READWRITE,&old))
                throw std::runtime_error("cannot isolate plugin imports");
            addresses->u1.Function=replacement;
            DWORD unused=0;VirtualProtect(&addresses->u1.Function,sizeof(ULONG_PTR),old,&unused);
        }
    }
    if(!found_folders || !found_settings) throw std::runtime_error("required isolation import absent");
}
struct Temporary {
    std::filesystem::path path=std::filesystem::temp_directory_path()/
        (L"atlas-native-ui-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
    Temporary() {if(!std::filesystem::create_directory(path)) throw std::runtime_error("cannot create UI sandbox");}
    ~Temporary() {std::error_code ignored;std::filesystem::remove_all(path,ignored);}
};
}
