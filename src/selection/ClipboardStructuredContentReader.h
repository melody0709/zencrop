#pragma once

#include "SelectionTypes.h"

#include <string>

namespace selection {

// The caller must keep the Win32 clipboard open for the duration of the call.
bool TryReadStructuredClipboardContentOpen(
    const std::wstring& requestToken,
    SelectionContent& content);
bool ApplyVsCodeClipboardMetadata(
    const std::string& metadataBytes,
    SelectionContent& content);
bool ApplyPreformattedSourceClipboardHtml(SelectionContent& content);

} // namespace selection
