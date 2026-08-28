#pragma once

#include <windows.h>
#include <string>

// Stage3 3-A-3: platform clipboard sole in core (screenshot↔ocr_ui reverse break).
// Full contract: text CF_UNICODETEXT; bitmap CF_HDROP + PREFERRED_DROPEFFECT +
// CF_DIBV5 + CF_DIB + PNG.  Callers may supply an already-encoded file for
// CF_HDROP; core deliberately has no dependency on screenshot Settings.
// OCR UI and Screenshot:: wrappers both call these; dual ScreenshotUtils body deleted.

bool CopyTextToClipboard(HWND owner, const std::wstring& text);

// Returns a unique, not-yet-created path in ZenCrop's clipboard temp directory.
// The extension must include its leading dot.  The caller owns encoding the file;
// stale ZenCrop_clip_* files are removed by the normal one-day cleanup.
std::wstring BuildClipboardTempFilePath(const std::wstring& extension);

// An empty fileDropPath preserves the generic PNG temp-file fallback.  A supplied
// path must already exist; it is published as CF_HDROP so file-drop targets such
// as Explorer receive the caller-selected encoded file.
bool CopyBitmapToClipboard(HWND owner, HBITMAP hBitmap, const std::wstring& fileDropPath = L"");
