#include "preset_drag.hpp"
#include <shlobj.h>
#include <stdexcept>
#include <new>

namespace fs = std::filesystem;

IDataObject* preset_drag_data(const fs::path& library, const fs::path& relative) {
    if (relative.empty() || relative.has_root_path() || _wcsicmp(relative.extension().c_str(), L".fst"))
        throw std::runtime_error("该条目没有可加载的 FL 预设。");
    std::error_code ec;
    auto root = fs::canonical(library, ec);
    if (ec) throw std::runtime_error("插件库无法访问，请重新选择目录。");
    auto file = fs::canonical(root / relative, ec);
    auto within = file.lexically_relative(root);
    if (ec || within.empty() || *within.begin() == L".." || _wcsicmp(file.extension().c_str(), L".fst") ||
        !fs::is_regular_file(file, ec))
        throw std::runtime_error("源预设已失效，请重新扫描插件库。");
    IShellItem* item = nullptr;
    if (FAILED(SHCreateItemFromParsingName(file.c_str(), nullptr, IID_PPV_ARGS(&item))))
        throw std::runtime_error("Windows 无法读取源预设。");
    IDataObject* data = nullptr;
    HRESULT hr = item->BindToHandler(nullptr, BHID_DataObject, IID_PPV_ARGS(&data));
    item->Release();
    if (FAILED(hr)) throw std::runtime_error("无法创建预设拖放数据。");
    return data;
}

namespace {
// OLE handles target discovery and feedback. A second click commits placement.
class ClickPlacement final : public IDropSource {
    ULONG refs=1;
    HWND panel;
    DWORD host_pid=0;
public:
    explicit ClickPlacement(HWND w):panel(w) {
        GetWindowThreadProcessId(GetAncestor(w,GA_ROOTOWNER),&host_pid);
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** result) override {
        if(!result) return E_POINTER;
        *result=nullptr;
        if(id!=IID_IUnknown && id!=IID_IDropSource) return E_NOINTERFACE;
        *result=static_cast<IDropSource*>(this);
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override {return ++refs;}
    ULONG STDMETHODCALLTYPE Release() override {
        auto count=--refs;
        if(!count) delete this;
        return count;
    }
    HRESULT STDMETHODCALLTYPE QueryContinueDrag(BOOL escape,DWORD keys) override {
        if(escape || !IsWindowVisible(panel) || (keys&MK_RBUTTON)) return DRAGDROP_S_CANCEL;
        if(!(keys&MK_LBUTTON)) return S_OK;
        POINT pt{};
        GetCursorPos(&pt);
        HWND target=WindowFromPoint(pt);
        DWORD pid=0;
        GetWindowThreadProcessId(target,&pid);
        if(!pid || pid!=host_pid || target==panel || IsChild(panel,target)) return DRAGDROP_S_CANCEL;
        return DRAGDROP_S_DROP;
    }
    HRESULT STDMETHODCALLTYPE GiveFeedback(DWORD) override {return DRAGDROP_S_USEDEFAULTCURSORS;}
};
}

bool place_preset(HWND panel, const fs::path& library, const fs::path& relative) {
    if (FAILED(OleInitialize(nullptr))) throw std::runtime_error("宿主界面线程无法启动 Windows 拖放。");
    struct OleScope { ~OleScope() { OleUninitialize(); } } scope;
    auto* data = preset_drag_data(library, relative);
    DWORD effect = DROPEFFECT_NONE;
    auto source = new(std::nothrow) ClickPlacement(panel);
    if(!source){data->Release(); throw std::bad_alloc();}
    auto result = DoDragDrop(data, source, DROPEFFECT_COPY, &effect);
    source->Release();
    data->Release();
    if (FAILED(result)) throw std::runtime_error("宿主未能接收预设。");
    return result==DRAGDROP_S_DROP && effect!=DROPEFFECT_NONE;
}
