#pragma once

#include <windows.h>
#include <cstddef>
#include <string>

// Windows Run-key values are command lines and are limited to 260 characters.
// See: https://learn.microsoft.com/windows/win32/setupapi/run-and-runonce-registry-keys
constexpr size_t kStartupRegistrationCommandLineMaxChars = 260;

struct StartupRegistrationState {
    bool registered = false;
    DWORD error = ERROR_SUCCESS;

    bool Succeeded() const { return error == ERROR_SUCCESS; }
};

// Reads the current user's ZenCrop entry from the Windows Run key. The registry
// is the source of truth; this feature intentionally has no duplicate JSON flag.
StartupRegistrationState QueryZenCropStartupRegistration();

// Adds or removes the current executable's entry in the current user's Run key.
// The operation is idempotent and does not rewrite an entry that is already in
// the requested state.
DWORD SetZenCropStartupRegistration(bool enable);

// Pure helpers kept public for contract tests.
std::wstring BuildStartupRegistrationCommandLine(const std::wstring& executablePath);
bool IsStartupRegistrationCommandLineValid(const std::wstring& commandLine);
