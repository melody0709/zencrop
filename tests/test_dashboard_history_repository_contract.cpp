#include "ocr/ui/dashboard/DashboardHistoryRepository.h"
#include "ocr/ui/dashboard/DashboardHistoryStore.h"

#include <windows.h>
#include <process.h>
#include <iostream>
#include <set>
#include <string>
#include <vector>

static int g_fail = 0;
static void Expect(bool c, const char* n) {
    if (!c) { std::cerr << "FAIL " << n << "\n"; ++g_fail; }
    else std::cout << "PASS " << n << "\n";
}

static std::wstring TempPath(const wchar_t* name) {
    wchar_t dir[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, dir);
    return std::wstring(dir) + name;
}

static bool RunInvalidDataRootContractInChild() {
    wchar_t executablePath[32768] = {};
    const DWORD length = GetModuleFileNameW(
        nullptr, executablePath, static_cast<DWORD>(_countof(executablePath)));
    if (length == 0 || length >= _countof(executablePath)) return false;
    return _wspawnl(
        _P_WAIT,
        executablePath,
        executablePath,
        L"--invalid-data-root",
        nullptr) == 0;
}

int main(int argc, char** argv) {
    wchar_t previousDataDirectory[32768] = {};
    const DWORD previousDataDirectoryLength = GetEnvironmentVariableW(
        L"ZENCROP_DATA_DIR",
        previousDataDirectory,
        static_cast<DWORD>(_countof(previousDataDirectory)));
    if (argc == 2 && std::string(argv[1]) == "--invalid-data-root") {
        const std::wstring invalidRoot =
            TempPath(L"zencrop_app_data_path_contract_file");
        DeleteFileW(invalidRoot.c_str());
        FILE* invalidFile = nullptr;
        _wfopen_s(&invalidFile, invalidRoot.c_str(), L"wb");
        if (invalidFile) fclose(invalidFile);
        SetEnvironmentVariableW(L"ZENCROP_DATA_DIR", invalidRoot.c_str());
        Expect(
            DashboardHistoryRepository::DefaultHistoryPath() ==
                invalidRoot + L"\\ocr_history.json",
            "invalid override never falls back to executable directory");
        DeleteFileW(invalidRoot.c_str());
        if (previousDataDirectoryLength > 0 &&
            previousDataDirectoryLength < _countof(previousDataDirectory)) {
            SetEnvironmentVariableW(L"ZENCROP_DATA_DIR", previousDataDirectory);
        } else {
            SetEnvironmentVariableW(L"ZENCROP_DATA_DIR", nullptr);
        }
        return g_fail ? 1 : 0;
    }

    Expect(
        RunInvalidDataRootContractInChild(),
        "invalid data root contract subprocess");

    const std::wstring dataRoot = TempPath(L"zencrop_app_data_path_contract");
    RemoveDirectoryW(dataRoot.c_str());
    SetEnvironmentVariableW(L"ZENCROP_DATA_DIR", dataRoot.c_str());
    Expect(
        DashboardHistoryRepository::DefaultHistoryPath() ==
            dataRoot + L"\\ocr_history.json",
        "default history uses data root");
    Expect(
        DashboardHistoryRepository::DefaultDismissedPath() ==
            dataRoot + L"\\ocr_dashboard_dismissed.json",
        "default dismissed uses data root");

    const std::wstring hist = TempPath(L"zencrop_hist_repo_test.json");
    const std::wstring disc = TempPath(L"zencrop_disc_repo_test.json");
    DeleteFileW(hist.c_str());
    DeleteFileW(disc.c_str());

    DashboardHistoryRepository repo(hist, disc);

    std::vector<OcrDashboardHistoryItem> items;
    Expect(repo.LoadItems(items), "load missing ok");
    Expect(items.empty(), "empty");

    OcrDashboardHistoryItem item;
    item.timestamp = L"2026-01-01T00:00:00";
    item.imagePath = L"C:\\tmp\\x.png";
    item.text = L"repo-test";
    item.elapsedMs = 7;
    items.push_back(item);
    Expect(repo.SaveItems(items), "save");

    std::vector<OcrDashboardHistoryItem> loaded;
    Expect(repo.LoadItems(loaded), "reload");
    Expect(loaded.size() == 1, "count");
    Expect(!loaded.empty() && loaded[0].text == L"repo-test", "text");
    Expect(!loaded.empty() && loaded[0].elapsedMs == 7, "elapsed");

    // R0-FIX-1: UTF-8 round-trip boundary cases (WStringToUtf8/Utf8ToWString size-based).
    // Exercises conversion used on SaveItems/LoadItems JSON path; catches NUL-buffer overrun
    // class bugs that only show under multi-byte / multi-unit payload.
    {
        DeleteFileW(hist.c_str());
        DashboardHistoryRepository utfRepo(hist, disc);
        std::vector<OcrDashboardHistoryItem> utfItems;

        OcrDashboardHistoryItem ascii;
        ascii.timestamp = L"2026-01-02T00:00:00";
        ascii.imagePath = L"C:\\tmp\\ascii.png";
        ascii.text = L"plain-ascii";
        ascii.elapsedMs = 1;
        utfItems.push_back(ascii);

        OcrDashboardHistoryItem cjk;
        cjk.timestamp = L"2026-01-02T00:00:01";
        cjk.imagePath = L"C:\\tmp\\中文路径.png";
        cjk.text = L"汉字与全角：测试";
        cjk.elapsedMs = 2;
        utfItems.push_back(cjk);

        OcrDashboardHistoryItem emoji;
        emoji.timestamp = L"2026-01-02T00:00:02";
        // U+1F4CC pushpin (surrogate pair in UTF-16) + mixed CJK/ASCII
        emoji.imagePath = L"C:\\tmp\\\U0001F4CC-pin.png";
        emoji.text = L"pin\U0001F4CC混合emoji";
        emoji.elapsedMs = 3;
        utfItems.push_back(emoji);

        OcrDashboardHistoryItem emptyText;
        emptyText.timestamp = L"2026-01-02T00:00:03";
        emptyText.imagePath = L"C:\\tmp\\empty.txt";
        emptyText.text = L"";
        emptyText.elapsedMs = 0;
        utfItems.push_back(emptyText);

        // Longer multipage-ish payload: many multi-byte chars (stress buffer sizing).
        OcrDashboardHistoryItem longCjk;
        longCjk.timestamp = L"2026-01-02T00:00:04";
        longCjk.imagePath = L"C:\\tmp\\long.png";
        longCjk.text = std::wstring(256, L'测') + L"\U0001F4CC" + std::wstring(128, L'试');
        longCjk.elapsedMs = 99;
        utfItems.push_back(longCjk);

        Expect(utfRepo.SaveItems(utfItems), "utf save");
        std::vector<OcrDashboardHistoryItem> utfLoaded;
        Expect(utfRepo.LoadItems(utfLoaded), "utf reload");
        Expect(utfLoaded.size() == utfItems.size(), "utf count");
        if (utfLoaded.size() == utfItems.size()) {
            for (size_t i = 0; i < utfItems.size(); ++i) {
                Expect(utfLoaded[i].text == utfItems[i].text, "utf text roundtrip");
                Expect(utfLoaded[i].imagePath == utfItems[i].imagePath, "utf path roundtrip");
                Expect(utfLoaded[i].elapsedMs == utfItems[i].elapsedMs, "utf elapsed roundtrip");
            }
        }

        // Dismissed keys also convert via WStringToUtf8.
        std::set<std::wstring> utfKeys = {
            L"manifest:c:\\中文\\a.json",
            L"manifest:c:\\\U0001F4CC\\b.json",
        };
        Expect(utfRepo.SaveDismissedKeys(utfKeys), "utf save dismissed");
        std::set<std::wstring> utfKeys2;
        Expect(utfRepo.LoadDismissedKeys(utfKeys2), "utf load dismissed");
        Expect(utfKeys2.count(L"manifest:c:\\中文\\a.json") == 1, "utf dismissed cjk");
        Expect(utfKeys2.count(L"manifest:c:\\\U0001F4CC\\b.json") == 1, "utf dismissed emoji");
    }

    // Corrupt file -> load fails and backs up
    {
        FILE* f = nullptr;
        _wfopen_s(&f, hist.c_str(), L"wb");
        if (f) { fputs("[{", f); fclose(f); }
    }
    std::vector<OcrDashboardHistoryItem> bad;
    Expect(!repo.LoadItems(bad), "corrupt fails");
    Expect(bad.empty(), "corrupt empty");

    std::set<std::wstring> keys = {L"manifest:c:\\a.json"};
    Expect(repo.SaveDismissedKeys(keys), "save dismissed");
    std::set<std::wstring> keys2;
    Expect(repo.LoadDismissedKeys(keys2), "load dismissed");
    Expect(keys2.count(L"manifest:c:\\a.json") == 1, "dismissed key");

    // cleanup best-effort
    DeleteFileW(hist.c_str());
    DeleteFileW(disc.c_str());
    RemoveDirectoryW(dataRoot.c_str());
    if (previousDataDirectoryLength > 0 &&
        previousDataDirectoryLength < _countof(previousDataDirectory)) {
        SetEnvironmentVariableW(L"ZENCROP_DATA_DIR", previousDataDirectory);
    } else {
        SetEnvironmentVariableW(L"ZENCROP_DATA_DIR", nullptr);
    }
    // may leave .bad.* backups

    if (g_fail) return 1;
    std::cout << "ALL PASSED\n";
    return 0;
}
