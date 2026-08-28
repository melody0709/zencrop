#pragma once

#include <string>

// Persistent mutable application data never falls back to a generated runtime
// directory. ZENCROP_DATA_DIR overrides the location; placing portable.flag
// beside ZenCrop.exe is the only opt-in portable behavior.
const std::wstring& ZenCropAppDataDirectory();
std::wstring ZenCropAppDataFilePath(const wchar_t* fileName);
bool ZenCropIsPortableMode();
