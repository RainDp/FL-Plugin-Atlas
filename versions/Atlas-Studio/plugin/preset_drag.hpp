#pragma once
#include <filesystem>
#include <shobjidl.h>

// Called on an OLE-initialized UI thread. The caller owns the returned reference.
IDataObject* preset_drag_data(const std::filesystem::path& library, const std::filesystem::path& relative);
bool place_preset(HWND panel, const std::filesystem::path& library, const std::filesystem::path& relative);
