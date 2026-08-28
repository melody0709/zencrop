#include "core/StartupRegistration.h"

#include <iostream>

namespace {

int g_failures = 0;

void Expect(bool condition, const char* name) {
    if (condition) {
        std::cout << "PASS " << name << "\n";
    } else {
        std::cerr << "FAIL " << name << "\n";
        ++g_failures;
    }
}

} // namespace

int main() {
    const std::wstring path = L"C:\\Program Files\\ZenCrop\\ZenCrop.exe";
    const std::wstring commandLine = BuildStartupRegistrationCommandLine(path);
    Expect(commandLine == L"\"C:\\Program Files\\ZenCrop\\ZenCrop.exe\"",
        "quotes executable path");
    Expect(IsStartupRegistrationCommandLineValid(commandLine),
        "normal command line is valid");
    Expect(BuildStartupRegistrationCommandLine(L"").empty(),
        "empty executable path is rejected");

    const std::wstring maxLengthCommand(kStartupRegistrationCommandLineMaxChars, L'x');
    const std::wstring oversizedCommand(kStartupRegistrationCommandLineMaxChars + 1, L'x');
    Expect(IsStartupRegistrationCommandLineValid(maxLengthCommand),
        "260 character command line is valid");
    Expect(!IsStartupRegistrationCommandLineValid(oversizedCommand),
        "261 character command line is rejected");

    if (g_failures != 0) return 1;
    std::cout << "ALL PASSED\n";
    return 0;
}
