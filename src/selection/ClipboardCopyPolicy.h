#pragma once

#include <windows.h>

#include <array>

namespace selection {

inline constexpr UINT kSyntheticCopyInputCount = 4;

struct SyntheticCopyCleanupInputs {
    std::array<INPUT, 2> inputs{};
    UINT count = 0;
};

std::array<INPUT, kSyntheticCopyInputCount> BuildSyntheticCopyInputs(
    ULONG_PTR marker);
SyntheticCopyCleanupInputs BuildSyntheticCopyCleanupInputs(
    UINT inserted, ULONG_PTR marker);
bool IsSyntheticCopySuppressedWindowClass(const wchar_t* className);
bool ShouldSuppressSyntheticCopyForTarget(HWND topLevelWindow);

} // namespace selection
