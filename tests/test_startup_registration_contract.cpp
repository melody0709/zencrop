#include "core/StartupRegistration.h"
#include "core/Settings.h"
#include "core/AppDataPaths.h"

#include <windows.h>
#include <iostream>
#include <string>

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

class ScopedIsolatedSettingsFile {
public:
    ScopedIsolatedSettingsFile() {
        m_path = GetSettingsFilePath();
        DeleteFileW(m_path.c_str());
    }

    ~ScopedIsolatedSettingsFile() {
        DeleteFileW(m_path.c_str());
    }

    const std::wstring& Path() const { return m_path; }

    void Write(const std::wstring& content) const {
        WriteTextFile(m_path, content);
    }

    std::wstring Read() const {
        return ReadTextFile(m_path);
    }

private:
    static void WriteTextFile(const std::wstring& path, const std::wstring& content) {
        std::string utf8;
        int len = WideCharToMultiByte(CP_UTF8, 0, content.c_str(), -1, nullptr, 0, nullptr, nullptr);
        if (len > 0) {
            utf8.resize(len - 1);
            WideCharToMultiByte(CP_UTF8, 0, content.c_str(), -1, &utf8[0], len, nullptr, nullptr);
        }
        HANDLE hFile = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hFile != INVALID_HANDLE_VALUE) {
            DWORD written = 0;
            ::WriteFile(hFile, utf8.data(), (DWORD)utf8.size(), &written, nullptr);
            CloseHandle(hFile);
        }
    }

    static std::wstring ReadTextFile(const std::wstring& path) {
        HANDLE hFile = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hFile == INVALID_HANDLE_VALUE) return L"";
        DWORD size = GetFileSize(hFile, nullptr);
        std::string utf8(size, '\0');
        DWORD readBytes = 0;
        ::ReadFile(hFile, utf8.data(), size, &readBytes, nullptr);
        CloseHandle(hFile);
        int len = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), (int)utf8.size(), nullptr, 0);
        if (len <= 0) return L"";
        std::wstring result(len, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), (int)utf8.size(), &result[0], len);
        return result;
    }

    std::wstring m_path;
};

// WriteStringToFile() writes "<settings>.tmp" first; occupying that path with a
// directory makes the atomic write fail deterministically.
class ScopedBlockedTempPath {
public:
    ScopedBlockedTempPath() {
        m_path = GetSettingsFilePath() + L".tmp";
        RemoveDirectoryW(m_path.c_str());
        m_created = CreateDirectoryW(m_path.c_str(), nullptr) != FALSE;
    }

    ~ScopedBlockedTempPath() {
        if (m_created) RemoveDirectoryW(m_path.c_str());
    }

    bool Created() const { return m_created; }

private:
    std::wstring m_path;
    bool m_created = false;
};

// The settings tests must never touch the real %LOCALAPPDATA%\ZenCrop\settings.json.
// Always use a fresh directory: an inherited ZENCROP_DATA_DIR may contain real settings.
class ScopedTestDataDir {
public:
    ScopedTestDataDir() {
        const DWORD previousSize = GetEnvironmentVariableW(L"ZENCROP_DATA_DIR", nullptr, 0);
        if (previousSize > 0) {
            m_previousValue.resize(previousSize);
            const DWORD copied = GetEnvironmentVariableW(
                L"ZENCROP_DATA_DIR", m_previousValue.data(), previousSize);
            if (copied == 0 || copied >= previousSize) return;
            m_previousValue.resize(copied);
            m_hadPrevious = true;
        }
        wchar_t root[MAX_PATH] = {};
        DWORD length = GetEnvironmentVariableW(L"ZENCROP_TEST_OUTPUT_ROOT", root, MAX_PATH);
        if (length == 0) length = GetTempPathW(MAX_PATH, root);
        if (length == 0 || length >= MAX_PATH) return;

        wchar_t uniquePath[MAX_PATH] = {};
        if (!GetTempFileNameW(root, L"ZCT", 0, uniquePath)) return;
        if (!DeleteFileW(uniquePath)) return;
        if (!CreateDirectoryW(uniquePath, nullptr)) return;
        m_directory = uniquePath;
        if (!SetEnvironmentVariableW(L"ZENCROP_DATA_DIR", m_directory.c_str())) return;
        m_ownsVariable = true;
        m_active = ZenCropAppDataDirectory() == m_directory;
    }

    ~ScopedTestDataDir() {
        if (m_ownsVariable) {
            SetEnvironmentVariableW(L"ZENCROP_DATA_DIR",
                m_hadPrevious ? m_previousValue.c_str() : nullptr);
        }
        if (!m_directory.empty()) RemoveDirectoryW(m_directory.c_str());
    }

    bool Active() const { return m_active; }
    const std::wstring& Directory() const { return m_directory; }

private:
    std::wstring m_directory;
    std::wstring m_previousValue;
    bool m_active = false;
    bool m_ownsVariable = false;
    bool m_hadPrevious = false;
};

void TestStartupRegistrationCommandLine() {
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
}

void TestStartupDraftRoundtrip() {
    ScopedIsolatedSettingsFile env;
    SettingsDraft draft;
    draft.baseline = GetSharedSettings();
    draft.pending = GetSharedSettings();
    draft.startupBaseline = false;
    draft.startupPending = true;

    SettingsDraft outDraft;
    SettingsCommitResult res = CommitSettingsPatch(draft, &outDraft);
    Expect(res.status == SettingsCommitStatus::Success, "CommitSettingsPatch empty patch succeeds");
    Expect(outDraft.startupBaseline == false, "outDraft preserves startupBaseline false");
    Expect(outDraft.startupPending == true, "outDraft preserves startupPending true");
}

void TestCommitPatchEmptyNoOp() {
    ScopedIsolatedSettingsFile env;
    env.Write(L"{\n  \"general\": {\n    \"language\": \"en\"\n  }\n}");

    SettingsDraft draft;
    draft.baseline = GetSharedSettings();
    draft.baseline.general = LoadGeneralSettings();
    draft.pending = draft.baseline;
    draft.ocrBaseline = LoadOcrSettings();
    draft.ocrPending = draft.ocrBaseline;

    const std::wstring before = env.Read();

    SettingsDraft outDraft;
    SettingsCommitResult res = CommitSettingsPatch(draft, &outDraft);
    Expect(res.status == SettingsCommitStatus::Success, "Empty patch commit reports success");
    Expect(env.Read() == before, "Empty patch leaves the settings file untouched");
}

void TestCommitPatchConflictDetection() {
    ScopedIsolatedSettingsFile env;
    env.Write(L"{\n  \"screenshot\": {\n    \"format\": \"png\"\n  }\n}");

    SettingsDraft draft;
    draft.baseline = GetSharedSettings();
    // Baseline diverged from disk (simulating external modification)
    draft.baseline.screenshot.format = ScreenshotFormat::Bmp;
    draft.pending = draft.baseline;
    draft.pending.screenshot.format = ScreenshotFormat::Jpeg;
    draft.ocrBaseline = LoadOcrSettings();
    draft.ocrPending = draft.ocrBaseline;

    SettingsDraft outDraft;
    SettingsCommitResult res = CommitSettingsPatch(draft, &outDraft);
    Expect(res.status == SettingsCommitStatus::Conflict, "External divergence triggers SettingsCommitStatus::Conflict");
    Expect(res.conflictingField == L"screenshot.format", "Conflicting field name identified accurately");
}

void TestCommitPatchPreserveUnrecognizedTopLevel() {
    ScopedIsolatedSettingsFile env;
    env.Write(
        L"{\n"
        L"  \"general\": {\n    \"language\": \"en\"\n  },\n"
        L"  \"customExtension\": {\n    \"pluginEnabled\": true,\n    \"pluginName\": \"Alpha\"\n  },\n"
        L"  \"customFlag\": 42\n"
        L"}"
    );

    SettingsDraft draft;
    draft.baseline = GetSharedSettings();
    draft.baseline.general = LoadGeneralSettings();
    draft.pending = draft.baseline;
    draft.pending.general.language.value = AppLanguage::Chinese;
    draft.ocrBaseline = LoadOcrSettings();
    draft.ocrPending = draft.ocrBaseline;

    SettingsDraft outDraft;
    SettingsCommitResult res = CommitSettingsPatch(draft, &outDraft);
    Expect(res.status == SettingsCommitStatus::Success, "Commit succeeds when custom top-level keys exist");

    std::wstring updatedJson = env.Read();
    Expect(updatedJson.find(L"\"customExtension\": {") != std::wstring::npos, "Preserves customExtension object");
    Expect(updatedJson.find(L"\"pluginName\": \"Alpha\"") != std::wstring::npos, "Preserves customExtension content");
    Expect(updatedJson.find(L"\"customFlag\": 42") != std::wstring::npos, "Preserves customFlag scalar");
    Expect(updatedJson.find(L"\"language\": \"zh\"") != std::wstring::npos, "Updated standard field to Chinese");
}

void TestCommitPatchUnsupportedSchema() {
    ScopedIsolatedSettingsFile env;
    env.Write(
        L"{\n"
        L"  \"translation\": {\n    \"schemaVersion\": 9999\n  }\n"
        L"}"
    );

    SettingsDraft draft;
    draft.baseline = GetSharedSettings();
    draft.baseline.general = LoadGeneralSettings();
    draft.pending = draft.baseline;
    draft.ocrBaseline = LoadOcrSettings();
    draft.ocrPending = draft.ocrBaseline;

    SettingsDraft outDraft;
    SettingsCommitResult res = CommitSettingsPatch(draft, &outDraft);
    Expect(res.status == SettingsCommitStatus::SchemaUnsupported, "Newer schema rejects patch with SchemaUnsupported");
}

void TestCommitPatchKeepsExternalSameDomainField() {
    ScopedIsolatedSettingsFile env;
    env.Write(L"{\n  \"screenshot\": {\n    \"format\": \"png\",\n    \"jpegQuality\": 90\n  }\n}");

    SettingsDraft draft;
    draft.baseline = GetSharedSettings();
    draft.baseline.screenshot = LoadScreenshotSettings();
    draft.pending = draft.baseline;
    // The user only changes the save format on the Screenshot page.
    draft.pending.screenshot.format = ScreenshotFormat::Jpeg;
    draft.ocrBaseline = LoadOcrSettings();
    draft.ocrPending = draft.ocrBaseline;

    // Meanwhile the screenshot editor changes the JPEG quality and saves.
    env.Write(L"{\n  \"screenshot\": {\n    \"format\": \"png\",\n    \"jpegQuality\": 60\n  }\n}");

    SettingsDraft outDraft;
    SettingsCommitResult res = CommitSettingsPatch(draft, &outDraft);
    Expect(res.status == SettingsCommitStatus::Success, "Different field in the same domain does not conflict");

    const std::wstring updated = env.Read();
    Expect(updated.find(L"\"jpegQuality\": 60") != std::wstring::npos,
        "Externally changed jpegQuality is preserved");
    Expect(updated.find(L"\"format\": \"jpeg\"") != std::wstring::npos,
        "User's format change is applied");
}

void TestCommitPatchTranslationKeepsExternalProvider() {
    ScopedIsolatedSettingsFile env;
    // The translation section is kept verbatim except for the fields the window owns,
    // so a minimal hand-written section is enough for this contract.
    env.Write(
        L"{\n"
        L"  \"translation\": {\n"
        L"    \"schemaVersion\": 8,\n"
        L"    \"enabled\": true,\n"
        L"    \"sourceLanguage\": \"en\",\n"
        L"    \"targetLanguage\": \"zh-Hans\",\n"
        L"    \"activeProviderId\": \"provider.a\"\n"
        L"  }\n"
        L"}"
    );

    SettingsDraft draft;
    draft.baseline = GetSharedSettings();
    draft.baseline.translation.schemaVersion = 8;
    draft.baseline.translation.enabled = true;
    draft.baseline.translation.sourceLanguage = L"en";
    draft.baseline.translation.targetLanguage = L"zh-Hans";
    draft.baseline.translation.activeProviderId = L"provider.a";
    draft.pending = draft.baseline;
    // The user only changes the source language on the Translate page.
    draft.pending.translation.sourceLanguage = L"ja";
    draft.ocrBaseline = LoadOcrSettings();
    draft.ocrPending = draft.ocrBaseline;

    // Meanwhile the provider manager switches the active provider.
    env.Write(
        L"{\n"
        L"  \"translation\": {\n"
        L"    \"schemaVersion\": 8,\n"
        L"    \"enabled\": true,\n"
        L"    \"sourceLanguage\": \"en\",\n"
        L"    \"targetLanguage\": \"zh-Hans\",\n"
        L"    \"activeProviderId\": \"provider.b\"\n"
        L"  }\n"
        L"}"
    );

    SettingsDraft outDraft;
    SettingsCommitResult res = CommitSettingsPatch(draft, &outDraft);
    Expect(res.status == SettingsCommitStatus::Success,
        "Translation language change does not conflict with provider switch");

    const std::wstring updated = env.Read();
    Expect(updated.find(L"\"activeProviderId\": \"provider.b\"") != std::wstring::npos,
        "Externally switched provider is preserved");
    Expect(updated.find(L"\"sourceLanguage\": \"ja\"") != std::wstring::npos,
        "User's source language change is applied");
}

void TestCommitPatchForceOverwriteResolvesConflict() {
    ScopedIsolatedSettingsFile env;
    env.Write(L"{\n  \"screenshot\": {\n    \"format\": \"png\"\n  }\n}");

    SettingsDraft draft;
    draft.baseline = GetSharedSettings();
    draft.baseline.screenshot = LoadScreenshotSettings();
    draft.pending = draft.baseline;
    draft.pending.screenshot.format = ScreenshotFormat::Jpeg;
    draft.ocrBaseline = LoadOcrSettings();
    draft.ocrPending = draft.ocrBaseline;

    // External writer touched the very same field.
    env.Write(L"{\n  \"screenshot\": {\n    \"format\": \"bmp\"\n  }\n}");

    SettingsDraft outDraft;
    SettingsCommitResult res = CommitSettingsPatch(draft, &outDraft);
    Expect(res.status == SettingsCommitStatus::Conflict, "Same-field external change still conflicts by default");
    Expect(env.Read().find(L"\"format\": \"bmp\"") != std::wstring::npos, "Conflict leaves the file untouched");

    // The user explicitly chose to keep the draft and overwrite.
    res = CommitSettingsPatch(draft, &outDraft, /*forceOverwrite=*/true);
    Expect(res.status == SettingsCommitStatus::Success, "Forced commit overrides the conflict");
    Expect(env.Read().find(L"\"format\": \"jpeg\"") != std::wstring::npos, "Forced commit applied the draft value");
}

// The settings window writes through CommitSettingsPatch while the runtime writes
// through Save*Settings. Both must produce the same JSON for the fields they own;
// this guards against the two serialization paths drifting apart.
void TestCommitPatchMatchesSaveSerialization() {
    ScopedIsolatedSettingsFile env;

    const auto seedDefaults = []() {
        SaveGeneralSettings(GeneralSettings{});
        SaveAotSettings(AotSettings{});
        SaveOverlaySettings(OverlaySettings{});
        SaveScreenshotSettings(ScreenshotSettings{});
        SaveOcrSettings(OcrSettings{});
        SaveHotkeySettings(HotkeySettings{}, nullptr);
    };

    const auto editedScreenshot = []() {
        ScreenshotSettings s;
        s.format = ScreenshotFormat::WebP;
        s.jpegQuality = 37;
        s.includeCursor = true;
        s.quickSaveDir = L"C:\\shots\\dir with space";
        s.hoverMagnifierEnabled = true;
        s.longShotAfterInitAction = 2;
        s.longShotAutoCrop = true;
        return s;
    };
    const auto editedOcr = []() {
        OcrSettings s;
        s.language = L"ja";
        s.mode = L"paddle_cloud";
        s.altHotkeyRoute = L"paddle_local";
        s.altHotkeyIdleTimeoutMin = 23;
        s.paddleApiUrl = L"https://example.invalid/api/v2/ocr/jobs";
        s.paddleToken = L"tok\"en";
        s.paddleCloudUseChartRecognition = true;
        s.timeoutMs = 999;                 // out of range -> normalized
        s.paddleLocalModelDir = L"D:\\models\\vl";
        s.paddleLocalPort = 8123;
        s.paddleLocalIdleTimeoutMin = 17;
        s.paddleLocalPrompt = L"Table Recognition:";
        s.paddleVlMaxTokens = 8192;
        s.enableDocParsing = true;
        s.enableImageCrop = true;
        s.localRasterMaxPixelEdge = 12345;
        s.localRasterMaxMegapixels = 999;
        s.docLayoutModelPath = L"D:\\models\\layout.onnx";
        s.layoutModelFamily = L"pp_doclayout_v2";
        s.layoutThresholdProfile = L"official-like";
        s.paddleDocGroupingMode = L"none";
        s.docRecognizeCharts = true;
        s.docRecognizeImages = true;
        s.docRecognizeSeals = true;
        s.docIgnorePageDecorations = true;
        s.docKeepFootnotes = true;
        s.docUsePhysicalSorting = true;
        s.ocrFontSize = 21;
        s.resultOnTop = true;
        s.ppocrv6ModelDir = L"D:\\models\\v6";
        s.ppocrv6Variant = L"medium";
        s.ppocrv6CpuThreads = 6;
        s.ppocrv6RecBatchSize = 3;
        s.ppocrv6DetLimitSideLen = 960;
        s.ppocrv6DetLimitType = L"max";
        s.ppocrv6DetMaxSideLimit = 2048;
        s.ppocrv6DetThreshPct = 33;
        s.ppocrv6DetBoxThreshPct = 66;
        s.ppocrv6DetUnclipRatioPct = 155;
        s.ppocrv6RecScoreThreshPct = 44;
        s.ppocrv6Preset = L"balanced";
        return s;
    };
    const auto editedHotkeys = []() {
        HotkeySettings h;
        h.reparent = { true, true, false, false, 'R' };
        h.thumbnail = { false, true, true, false, 'T' };
        h.viewport = { false, false, false, true, 'V' };
        h.closeReparent = { false, true, false, false, 'Q' };
        h.alwaysOnTop = { false, false, true, true, 'A' };
        h.screenshot = { false, true, true, true, 'S' };
        h.ocr = { false, false, false, false, 'O' };
        h.ocrAlt = { false, false, false, true, 'P' };
        h.selectionTranslate = { true, false, false, false, 'L' };
        return h;
    };

    // Path A: the runtime writers.
    seedDefaults();
    SaveScreenshotSettings(editedScreenshot());
    SaveOcrSettings(editedOcr());
    SaveHotkeySettings(editedHotkeys(), nullptr);
    const std::wstring savedByRuntime = env.Read();

    // Path B: the settings window entry point.
    seedDefaults();
    SettingsDraft draft;
    draft.baseline = GetSharedSettings();
    draft.baseline.general = LoadGeneralSettings();
    draft.baseline.aot = LoadAotSettings();
    draft.baseline.overlay = LoadOverlaySettings();
    draft.baseline.screenshot = LoadScreenshotSettings();
    draft.baseline.hotkeys = LoadHotkeySettings();
    draft.pending = draft.baseline;
    draft.pending.screenshot = editedScreenshot();
    draft.pending.hotkeys = editedHotkeys();
    draft.ocrBaseline = LoadOcrSettings();
    draft.ocrPending = editedOcr();

    SettingsDraft outDraft;
    const SettingsCommitResult res = CommitSettingsPatch(draft, &outDraft);
    Expect(res.status == SettingsCommitStatus::Success, "Commit patch serialization test succeeds");
    const std::wstring savedByCommit = env.Read();

    if (savedByRuntime != savedByCommit) {
        size_t diffAt = 0;
        while (diffAt < savedByRuntime.size() && diffAt < savedByCommit.size() &&
               savedByRuntime[diffAt] == savedByCommit[diffAt]) {
            ++diffAt;
        }
        std::cerr << "serialization mismatch at offset " << diffAt
                  << " (runtime " << savedByRuntime.size()
                  << " chars, commit " << savedByCommit.size() << " chars)\n";
    }
    Expect(savedByRuntime == savedByCommit,
        "Save*Settings and CommitSettingsPatch produce identical JSON");
}

// The settings window can be the first writer of a translation section when the file
// has none (CommitSettingsPatch builds a minimal one, unlike SerializeTranslationSection
// which writes the full canonical shape). Parsing tolerates the missing keys, but the
// branch itself must stay covered.
void TestCommitPatchCreatesMissingTranslationSection() {
    ScopedIsolatedSettingsFile env;   // no settings file at all

    SettingsDraft draft;
    draft.baseline = GetSharedSettings();
    TranslationSettings translation;
    translation.enabled = true;       // matches LoadTranslationSettings()'s fallback
    draft.baseline.translation = translation;
    draft.pending = draft.baseline;
    draft.pending.translation.sourceLanguage = L"ja";
    draft.ocrBaseline = LoadOcrSettings();
    draft.ocrPending = draft.ocrBaseline;

    SettingsDraft outDraft;
    const SettingsCommitResult res = CommitSettingsPatch(draft, &outDraft);
    Expect(res.status == SettingsCommitStatus::Success, "Missing translation section can be created");
    Expect(!outDraft.pending.translation.sourceLanguage.empty(), "Commit reports the merged draft back");

    const std::wstring updated = env.Read();
    Expect(updated.find(L"\"translation\": {") != std::wstring::npos, "Translation section is written");
    Expect(updated.find(L"\"schemaVersion\": 8") != std::wstring::npos, "Minimal section carries the schema version");
    Expect(updated.find(L"\"enabled\": true") != std::wstring::npos, "Minimal section carries the enabled flag");
    Expect(updated.find(L"\"sourceLanguage\": \"ja\"") != std::wstring::npos, "User's language change is applied");

    // A second commit must now take the "section exists" path and stay stable.
    SettingsDraft second = outDraft;
    second.pending.translation.targetLanguage = L"en";
    SettingsCommitResult res2 = CommitSettingsPatch(second, &outDraft);
    Expect(res2.status == SettingsCommitStatus::Success, "Second commit reuses the created section");
    Expect(env.Read().find(L"\"targetLanguage\": \"en\"") != std::wstring::npos, "Second commit applies to the created section");
}

// Every write path shares AssembleSettingsJson, so the runtime Save*Settings writers
// must keep unknown top-level keys too (they used to rebuild only the known sections).
void TestSaveSettingsPreservesUnrecognizedTopLevel() {
    ScopedIsolatedSettingsFile env;
    env.Write(
        L"{\n"
        L"  \"general\": {\n    \"language\": \"en\"\n  },\n"
        L"  \"translation\": {\n    \"schemaVersion\": 8,\n    \"enabled\": true,\n    \"sourceLanguage\": \"en\"\n  },\n"
        L"  \"customFlag\": 42,\n"
        L"  \"customExtension\": {\n    \"pluginName\": \"Alpha\"\n  }\n"
        L"}"
    );

    GeneralSettings general = LoadGeneralSettings();
    general.language.value = AppLanguage::Chinese;
    SaveGeneralSettings(general);

    const std::wstring afterGeneral = env.Read();
    Expect(afterGeneral.find(L"\"customFlag\": 42") != std::wstring::npos,
        "SaveGeneralSettings keeps unknown scalar keys");
    Expect(afterGeneral.find(L"\"pluginName\": \"Alpha\"") != std::wstring::npos,
        "SaveGeneralSettings keeps unknown object keys");
    Expect(afterGeneral.find(L"\"language\": \"zh\"") != std::wstring::npos,
        "SaveGeneralSettings still writes its own section");
    Expect(afterGeneral.find(L"\"sourceLanguage\": \"en\"") != std::wstring::npos,
        "SaveGeneralSettings keeps the raw translation section");

    OcrSettings ocr = LoadOcrSettings();
    ocr.ocrFontSize = 23;
    SaveOcrSettings(ocr);

    const std::wstring afterOcr = env.Read();
    Expect(afterOcr.find(L"\"customFlag\": 42") != std::wstring::npos,
        "SaveOcrSettings keeps unknown top-level keys");
    Expect(afterOcr.find(L"\"pluginName\": \"Alpha\"") != std::wstring::npos,
        "SaveOcrSettings keeps unknown object keys");
    Expect(afterOcr.find(L"\"ocrFontSize\": 23") != std::wstring::npos,
        "SaveOcrSettings writes its own section");
    Expect(afterOcr.find(L"\"general\": {") != std::wstring::npos,
        "SaveOcrSettings keeps the sections it does not own");
    Expect(afterOcr.find(L"\"sourceLanguage\": \"en\"") != std::wstring::npos,
        "SaveOcrSettings keeps the raw translation section");
}

void TestCommitPatchWriteFailureReportsIoError() {
    ScopedIsolatedSettingsFile env;
    env.Write(L"{\n  \"general\": {\n    \"language\": \"en\"\n  }\n}");

    ScopedBlockedTempPath blocked;

    SettingsDraft draft;
    draft.baseline = GetSharedSettings();
    draft.baseline.general = LoadGeneralSettings();
    draft.pending = draft.baseline;
    draft.pending.general.language.value = AppLanguage::Chinese;
    draft.ocrBaseline = LoadOcrSettings();
    draft.ocrPending = draft.ocrBaseline;

    SettingsDraft outDraft;
    SettingsCommitResult res = CommitSettingsPatch(draft, &outDraft);
    Expect(blocked.Created(), "Blocked temporary path created");
    Expect(res.status == SettingsCommitStatus::IoError, "Unwritable settings file reports IoError");
    Expect(res.errorMessage.find(L"failed") != std::wstring::npos, "IoError carries an error message");
}

// ---------------------------------------------------------------------------
// Read-semantics contract.
//
// TestSectionJsonShape pins the byte format of the write tables; this block pins
// what each loader does with the JSON it finds, from hand-written samples with
// independently stated expectations. Covered: missing file and missing section,
// missing key versus explicit empty string versus unreadable token, the documented
// value boundaries, the three hotkeys.ocrAlt shapes, OCR legacy aliases and preset
// normalization, and the screenshot migrations plus its read/write clamp split.
// ---------------------------------------------------------------------------

// New-install hotkeys, kept by a missing file or a missing hotkeys section.
HotkeySettings InstallDefaultHotkeys() {
    HotkeySettings h;
    h.reparent = { false, true, false, true, 'X' };
    h.thumbnail = { false, true, false, true, 'C' };
    h.viewport = { false, true, false, true, 'V' };
    h.closeReparent = { false, true, false, true, 'Z' };
    h.alwaysOnTop = { false, false, false, true, 'T' };
    h.screenshot = { false, false, true, true, 'S' };
    h.ocr = { false, false, true, false, 'X' };
    h.ocrAlt = { false, false, true, true, 'X' };
    h.selectionTranslate = { false, false, true, false, 'A' };
    return h;
}

void TestReadDefaultsWithoutFileOrSection() {
    ScopedIsolatedSettingsFile env;

    // No settings file at all.
    Expect(LoadGeneralSettings() == GeneralSettings(), "general keeps its defaults without a settings file");
    Expect(LoadAotSettings() == AotSettings(), "alwaysOnTop keeps its defaults without a settings file");
    Expect(LoadOverlaySettings() == OverlaySettings(), "overlay keeps its defaults without a settings file");
    Expect(LoadScreenshotSettings() == ScreenshotSettings(), "screenshot keeps its defaults without a settings file");
    Expect(LoadOcrSettings() == OcrSettings(), "ocr keeps its defaults without a settings file");
    Expect(LoadHotkeySettings() == InstallDefaultHotkeys(), "hotkeys keep the install defaults without a settings file");

    // A file carrying none of the known sections.
    env.Write(L"{\n  \"customExtension\": {\n    \"pluginName\": \"Alpha\"\n  }\n}");
    Expect(LoadGeneralSettings() == GeneralSettings(), "general keeps its defaults when its section is absent");
    Expect(LoadAotSettings() == AotSettings(), "alwaysOnTop keeps its defaults when its section is absent");
    Expect(LoadOverlaySettings() == OverlaySettings(), "overlay keeps its defaults when its section is absent");
    Expect(LoadScreenshotSettings() == ScreenshotSettings(), "screenshot keeps its defaults when its section is absent");
    Expect(LoadOcrSettings() == OcrSettings(), "ocr keeps its defaults when its section is absent");
    Expect(LoadHotkeySettings() == InstallDefaultHotkeys(), "hotkeys keep the install defaults when their section is absent");

    // Present but empty sections. Every field keeps its default except the one
    // documented key that a present section must explicitly carry: ocrAlt.
    env.Write(
        L"{\n"
        L"  \"general\": {},\n  \"alwaysOnTop\": {},\n  \"overlay\": {},\n"
        L"  \"screenshot\": {},\n  \"ocr\": {},\n  \"hotkeys\": {}\n"
        L"}"
    );
    Expect(LoadGeneralSettings() == GeneralSettings(), "an empty general section keeps its defaults");
    Expect(LoadAotSettings() == AotSettings(), "an empty alwaysOnTop section keeps its defaults");
    Expect(LoadOverlaySettings() == OverlaySettings(), "an empty overlay section keeps its defaults");
    Expect(LoadScreenshotSettings() == ScreenshotSettings(), "an empty screenshot section keeps its defaults");
    Expect(LoadOcrSettings() == OcrSettings(), "an empty ocr section keeps its defaults");
    HotkeySettings emptySectionHotkeys = InstallDefaultHotkeys();
    emptySectionHotkeys.ocrAlt = {};
    Expect(LoadHotkeySettings() == emptySectionHotkeys,
        "an empty hotkeys section keeps the install defaults except ocrAlt");
}

void TestReadMissingKeyEmptyStringAndInvalidTokens() {
    ScopedIsolatedSettingsFile env;
    const auto loadGeneral = [&env](const std::wstring& fields) {
        env.Write(L"{\n  \"general\": {\n    " + fields + L"\n  }\n}");
        return LoadGeneralSettings();
    };
    const auto loadAot = [&env](const std::wstring& fields) {
        env.Write(L"{\n  \"alwaysOnTop\": {\n    " + fields + L"\n  }\n}");
        return LoadAotSettings();
    };
    const auto loadOverlay = [&env](const std::wstring& fields) {
        env.Write(L"{\n  \"overlay\": {\n    " + fields + L"\n  }\n}");
        return LoadOverlaySettings();
    };

    Expect(loadGeneral(L"\"language\": \"en\",\n    \"showTitlebar\": true").language.value == AppLanguage::English,
        "language reads a known id");
    Expect(loadGeneral(L"\"language\": \"en\",\n    \"showTitlebar\": true").showTitlebar == true,
        "showTitlebar reads true");
    Expect(loadGeneral(L"\"language\": \"fr\"").language.value == AppLanguage::Auto,
        "an unknown language id reads as auto");

    // An unreadable bool token reads false, even for a field defaulting to true.
    Expect(loadAot(L"\"showBorder\": \"on\"").showBorder == false,
        "an unreadable showBorder token reads false");
    Expect(loadAot(L"\"customColor\": \"maybe\"").customColor == false,
        "an unreadable customColor token reads false");
    Expect(loadOverlay(L"\"cropOnTop\": \"yes\"").cropOnTop == false,
        "an unreadable cropOnTop token reads false");

    // Integers clamp on read; a missing key leaves the default alone.
    const AotSettings clamped = loadAot(L"\"opacity\": 0,\n    \"thickness\": 99,\n    \"inset\": -3");
    Expect(clamped.opacity == 1, "an opacity below its floor reads as that floor");
    Expect(clamped.thickness == 20, "a thickness above its ceiling reads as that ceiling");
    Expect(clamped.inset == 0, "a negative inset reads as 0");

    const AotSettings unreadable = loadAot(L"\"opacity\": \"abc\"");
    Expect(unreadable.opacity == 1, "an unreadable opacity reads as 1");
    Expect(unreadable.thickness == 4, "a missing thickness key keeps its default");
    Expect(unreadable.inset == 1, "a missing inset key keeps its default");

    Expect(loadAot(L"\"opacity\": \"\"").opacity == 100,
        "an explicitly empty int reads like a missing key");

    const AotSettings bounded = loadAot(L"\"opacity\": 100,\n    \"thickness\": 1");
    Expect(bounded.opacity == 100, "opacity keeps its documented ceiling");
    Expect(bounded.thickness == 1, "thickness keeps its documented floor");

    // Colors reuse the shared hex parser; an unreadable value is the red fallback.
    Expect(loadAot(L"\"color\": \"#010203\"").color == RGB(1, 2, 3), "a color reads from its hex text");
    Expect(loadAot(L"\"color\": \"not-a-color\"").color == RGB(255, 0, 0),
        "an unreadable color reads as the red fallback");

    Expect(loadOverlay(L"\"thickness\": 0").thickness == 1, "an overlay thickness below its floor reads as 1");
    Expect(loadOverlay(L"\"thickness\": 11").thickness == 10, "an overlay thickness above its ceiling reads as 10");
}

void TestReadHotkeyShapes() {
    ScopedIsolatedSettingsFile env;

    // A file without a hotkeys section keeps the new-install defaults, the
    // alternate OCR key included.
    env.Write(L"{\n  \"general\": {\n    \"language\": \"en\"\n  }\n}");
    Expect(LoadHotkeySettings() == InstallDefaultHotkeys(),
        "a file without a hotkeys section keeps the install defaults");

    // The section exists without an ocrAlt key: that key is cleared, and the keys
    // the section omits keep their defaults.
    env.Write(
        L"{\n  \"hotkeys\": {\n"
        L"    \"reparent\": {\"win\": true, \"ctrl\": true, \"shift\": false, \"alt\": false, \"key\": 82}\n"
        L"  }\n}"
    );
    HotkeySettings hotkeys = LoadHotkeySettings();
    Expect(hotkeys.reparent == HotkeyConfig{ true, true, false, false, 'R' },
        "a listed hotkey replaces its default");
    Expect(hotkeys.thumbnail == InstallDefaultHotkeys().thumbnail, "an unlisted hotkey keeps its default");
    Expect(hotkeys.ocrAlt.IsEmpty(), "a missing ocrAlt key clears the alternate OCR hotkey");

    // A hotkey object parses from an empty config, so modifiers it does not list
    // stay off and an empty object clears the key.
    env.Write(
        L"{\n  \"hotkeys\": {\n"
        L"    \"ocrAlt\": {\"key\": 80},\n"
        L"    \"ocr\": {}\n"
        L"  }\n}"
    );
    hotkeys = LoadHotkeySettings();
    Expect(hotkeys.ocrAlt == HotkeyConfig{ false, false, false, false, 'P' },
        "a partially specified ocrAlt object parses from an empty config");
    Expect(hotkeys.ocr.IsEmpty(), "an empty hotkey object clears the key");
}

void TestReadOcrAliasesAndPresetNormalization() {
    ScopedIsolatedSettingsFile env;
    const auto loadOcr = [&env](const std::wstring& fields) {
        env.Write(L"{\n  \"ocr\": {\n    " + fields + L"\n  }\n}");
        return LoadOcrSettings();
    };

    // Legacy route aliases and unknown routes all land on the document route.
    Expect(loadOcr(L"\"altHotkeyRoute\": \"paddle_doc\"").altHotkeyRoute == L"paddle_local_doc",
        "the paddle_doc route alias reads as paddle_local_doc");
    Expect(loadOcr(L"\"altHotkeyRoute\": \"doc_parsing\"").altHotkeyRoute == L"paddle_local_doc",
        "the doc_parsing route alias reads as paddle_local_doc");
    Expect(loadOcr(L"\"altHotkeyRoute\": \"current\"").altHotkeyRoute == L"paddle_local_doc",
        "the legacy current route reads as paddle_local_doc");
    Expect(loadOcr(L"\"altHotkeyRoute\": \"gibberish\"").altHotkeyRoute == L"paddle_local_doc",
        "an unknown route reads as paddle_local_doc");
    Expect(loadOcr(L"\"altHotkeyRoute\": \"paddle_cloud\"").altHotkeyRoute == L"paddle_cloud",
        "a known route is kept");

    // The cloud URL is normalized; a bare host falls back to the jobs endpoint.
    Expect(loadOcr(L"\"paddleApiUrl\": \"https://example.invalid/custom/\"").paddleApiUrl ==
        L"https://example.invalid/custom", "trailing slashes are stripped from the cloud URL");
    Expect(loadOcr(L"\"paddleApiUrl\": \"https://paddleocr.aistudio-app.com\"").paddleApiUrl ==
        L"https://paddleocr.aistudio-app.com/api/v2/ocr/jobs", "the bare cloud host reads as the jobs endpoint");

    // Layout family, threshold profile and grouping aliases.
    Expect(loadOcr(L"\"layoutModelFamily\": \"pp-doclayoutv3\"").layoutModelFamily == L"pp_doclayout_v3",
        "the pp-doclayoutv3 alias reads as pp_doclayout_v3");
    Expect(loadOcr(L"\"layoutModelFamily\": \"v2\"").layoutModelFamily == L"pp_doclayout_v2",
        "the v2 alias reads as pp_doclayout_v2");
    Expect(loadOcr(L"\"layoutModelFamily\": \"mystery\"").layoutModelFamily == L"auto",
        "an unknown layout family reads as auto");
    Expect(loadOcr(L"\"layoutThresholdProfile\": \"official_like\"").layoutThresholdProfile == L"official",
        "the official_like alias reads as official");
    Expect(loadOcr(L"\"layoutThresholdProfile\": \"mystery\"").layoutThresholdProfile == L"official",
        "an unknown threshold profile reads as official");
    Expect(loadOcr(L"\"layoutThresholdProfile\": \"recall\"").layoutThresholdProfile == L"recall",
        "the recall profile is kept");
    Expect(loadOcr(L"\"paddleDocGroupingMode\": \"legacy-union-ab\"").paddleDocGroupingMode == L"legacy_union_ab",
        "the legacy-union-ab alias reads as legacy_union_ab");
    Expect(loadOcr(L"\"paddleDocGroupingMode\": \"mystery\"").paddleDocGroupingMode == L"official_group",
        "an unknown grouping mode reads as official_group");
    Expect(loadOcr(L"\"ppocrv6Variant\": \"mystery\"").ppocrv6Variant == L"small",
        "an unknown PP-OCRv6 variant reads as small");
    Expect(loadOcr(L"\"ppocrv6DetLimitType\": \"mystery\"").ppocrv6DetLimitType == L"min",
        "an unknown det limit type reads as min");

    // Integers clamp on read, except the port which is read as entered.
    const OcrSettings clamped = loadOcr(
        L"\"ocrFontSize\": 99,\n    \"ppocrv6DetLimitSideLen\": 10,\n"
        L"    \"ppocrv6DetUnclipRatioPct\": 50,\n    \"ppocrv6RecBatchSize\": 99,\n"
        L"    \"timeoutMs\": 5,\n    \"paddleLocalPort\": 70000");
    Expect(clamped.ocrFontSize == 32, "an ocrFontSize above its ceiling reads as 32");
    Expect(clamped.ppocrv6DetLimitSideLen == 64, "a det limit side length below its floor reads as 64");
    Expect(clamped.ppocrv6DetUnclipRatioPct == 100, "an unclip ratio below its floor reads as 100");
    Expect(clamped.ppocrv6RecBatchSize == 8, "a rec batch size above its ceiling reads as 8");
    Expect(clamped.timeoutMs == 120000, "a timeout below its floor reads as the 2 minute minimum");
    Expect(clamped.paddleLocalPort == 70000, "the local port reads without a clamp");

    // The token budget is normalized to 4096/8192, a missing key included.
    Expect(loadOcr(L"\"language\": \"ja\"").paddleVlMaxTokens == 4096,
        "paddleVlMaxTokens normalizes to 4096 when its key is absent");
    Expect(loadOcr(L"\"paddleVlMaxTokens\": 8192").paddleVlMaxTokens == 8192,
        "paddleVlMaxTokens keeps 8192");
    Expect(loadOcr(L"\"paddleVlMaxTokens\": 1234").paddleVlMaxTokens == 4096,
        "an unsupported paddleVlMaxTokens reads as 4096");

    // docIncludeIgnoredRegions is derived, and the legacy inverted key is honoured.
    const OcrSettings derived = loadOcr(L"\"docIgnorePageDecorations\": false");
    Expect(derived.docIgnorePageDecorations == false, "docIgnorePageDecorations reads as stored");
    Expect(derived.docIncludeIgnoredRegions == true, "docIncludeIgnoredRegions mirrors docIgnorePageDecorations");
    const OcrSettings derivedDefault = loadOcr(L"\"language\": \"ja\"");
    Expect(derivedDefault.docIgnorePageDecorations == true, "the ignore-page-decorations default is on");
    Expect(derivedDefault.docIncludeIgnoredRegions == false, "the derived flag also holds when neither key is present");
    const OcrSettings legacyKey = loadOcr(L"\"docIncludeIgnoredRegions\": false");
    Expect(legacyKey.docIgnorePageDecorations == true, "the legacy include key inverts into docIgnorePageDecorations");
    Expect(legacyKey.docIncludeIgnoredRegions == false, "the legacy include key reads back as stored");

    // The named preset is normalized only against the knobs read alongside it.
    Expect(loadOcr(L"\"ppocrv6Preset\": \"fast_cpu\"").ppocrv6Preset == L"custom",
        "a legacy preset id reads as custom");
    Expect(loadOcr(
        L"\"ppocrv6Preset\": \"balanced\",\n    \"ppocrv6DetLimitType\": \"min\",\n"
        L"    \"ppocrv6DetLimitSideLen\": 64,\n    \"ppocrv6DetMaxSideLimit\": 4000,\n"
        L"    \"ppocrv6DetThreshPct\": 20,\n    \"ppocrv6DetBoxThreshPct\": 45,\n"
        L"    \"ppocrv6DetUnclipRatioPct\": 140,\n    \"ppocrv6RecScoreThreshPct\": 0,\n"
        L"    \"ppocrv6RecBatchSize\": 1").ppocrv6Preset == L"balanced",
        "a preset id whose knobs match is kept");
    Expect(loadOcr(L"\"ppocrv6Preset\": \"balanced\",\n    \"ppocrv6DetLimitSideLen\": 320").ppocrv6Preset == L"custom",
        "a preset id whose knobs diverged reads as custom");
    Expect(loadOcr(L"\"ppocrv6DetLimitSideLen\": 320").ppocrv6Preset == L"custom",
        "divergent knobs without a preset key read as custom");
}

void TestReadScreenshotMigrationsAndClampSplit() {
    ScopedIsolatedSettingsFile env;
    const auto loadScreenshot = [&env](const std::wstring& fields) {
        env.Write(L"{\n  \"screenshot\": {\n    " + fields + L"\n  }\n}");
        return LoadScreenshotSettings();
    };

    // The mosaic strength reads 0..100 but is written clamped to 0..28, so the
    // read range must not be taken from the write table.
    Expect(loadScreenshot(L"\"annotationMosaicStrength\": 100").annotationMosaicStrength == 100,
        "the mosaic strength reads up to its own ceiling of 100");
    Expect(loadScreenshot(L"\"annotationMosaicStrength\": 500").annotationMosaicStrength == 100,
        "the mosaic strength reads clamped to 100");
    Expect(loadScreenshot(L"\"annotationMosaicStrength\": -4").annotationMosaicStrength == 0,
        "the mosaic strength reads clamped to 0");

    ScreenshotSettings maxStrength;
    maxStrength.annotationMosaicStrength = 100;
    SaveScreenshotSettings(maxStrength);
    Expect(env.Read().find(L"\"annotationMosaicStrength\": 28") != std::wstring::npos,
        "the mosaic strength is written clamped to its own ceiling of 28");

    // Unreadable integer tokens differ per field: jpegQuality parses through 0,
    // a pen width keeps the value already loaded.
    const ScreenshotSettings unreadable = loadScreenshot(
        L"\"jpegQuality\": \"abc\",\n    \"annotationGeometryPenWidth\": \"abc\"");
    Expect(unreadable.jpegQuality == 1, "an unreadable jpegQuality reads as 1");
    Expect(unreadable.annotationGeometryPenWidth == 4,
        "an unreadable pen width keeps the value already loaded");
    Expect(loadScreenshot(L"\"annotationGeometryPenWidth\": 0").annotationGeometryPenWidth == 1,
        "a pen width below its floor reads as that floor");
    Expect(loadScreenshot(L"\"annotationGeometryPenWidth\": 99").annotationGeometryPenWidth == 32,
        "a pen width above its ceiling reads as that ceiling");

    const ScreenshotSettings bounded = loadScreenshot(L"\"jpegQuality\": 0,\n    \"longShotAfterInitAction\": 9");
    Expect(bounded.jpegQuality == 1, "a jpegQuality below its floor reads as 1");
    Expect(bounded.longShotAfterInitAction == 3, "a long-shot action above its ceiling reads as 3");

    // The pre-versioned long-shot default is migrated once, and only once.
    Expect(loadScreenshot(L"\"longShotAfterInitAction\": 0").longShotAfterInitAction == 1,
        "a versionless long-shot action 0 migrates to 1");
    Expect(loadScreenshot(L"\"longShotAfterInitAction\": 0,\n    \"longShotBehaviorVersion\": 1")
        .longShotAfterInitAction == 0, "a versioned long-shot action 0 is preserved");

    // Hover magnifier legacy values migrate only when their key is present.
    Expect(loadScreenshot(L"\"hoverMagnifierPower\": 100").hoverMagnifierPower == 11,
        "the legacy magnifier power 100 migrates to 11");
    Expect(loadScreenshot(L"\"includeCursor\": false").hoverMagnifierPower == 11,
        "a missing magnifier power keeps the calibrated default of 11");
    Expect(loadScreenshot(L"\"hoverMagnifierColorFormat\": 0").hoverMagnifierColorFormat == 3,
        "magnifier color format 0 migrates to 3");

    // Bool tokens fall back per field: most to false, the alpha-loss warning to true.
    const ScreenshotSettings bools = loadScreenshot(
        L"\"warnAlphaLossForJpegBmp\": \"nope\",\n    \"hoverMagnifierShowCoord\": \"nope\"");
    Expect(bools.warnAlphaLossForJpegBmp == true, "an unreadable alpha-loss warning token reads true");
    Expect(bools.hoverMagnifierShowCoord == false, "an unreadable show-coordinate token reads false");

    // Only the string keys that opt in treat an explicit empty string as a value.
    const ScreenshotSettings cleared = loadScreenshot(
        L"\"annotationWatermarkText\": \"\",\n    \"functionAreaAlwaysHide\": \"\",\n"
        L"    \"annotationTextFontFamily\": \"\"");
    Expect(cleared.annotationWatermarkText.empty(), "an empty watermark text clears the field");
    Expect(cleared.functionAreaAlwaysHide.empty(), "an empty toolbar list clears the field");
    Expect(cleared.annotationTextFontFamily == L"Microsoft YaHei", "an empty font family keeps its default");
}

// ---------------------------------------------------------------------------
// Every-field persistence contract.
//
// Keep each Maximal* fixture in sync when adding a persisted field. These
// manually enumerated non-default values check Save/Load and patch merging;
// a newly added field is not covered until its fixture is updated.
// ---------------------------------------------------------------------------

GeneralSettings MaximalGeneral() {
    GeneralSettings s;
    s.language.value = AppLanguage::Chinese;
    s.showTitlebar = true;
    return s;
}

AotSettings MaximalAot() {
    AotSettings s;
    s.showBorder = false;
    s.customColor = false;
    s.color = RGB(1, 2, 3);
    s.opacity = 41;
    s.thickness = 7;
    s.roundedCorners = false;
    s.inset = 13;
    return s;
}

OverlaySettings MaximalOverlay() {
    OverlaySettings s;
    s.color = RGB(4, 5, 6);
    s.thickness = 9;
    s.cropOnTop = false;
    return s;
}

HotkeySettings MaximalHotkeys() {
    HotkeySettings h;
    h.reparent = { true, true, false, false, 'R' };
    h.thumbnail = { false, true, true, false, 'T' };
    h.viewport = { false, false, false, true, 'V' };
    h.closeReparent = { false, true, false, false, 'Q' };
    h.alwaysOnTop = { false, false, true, true, 'A' };
    h.screenshot = { false, true, true, true, 'S' };
    h.ocr = { false, false, false, false, 'O' };
    h.ocrAlt = { false, false, false, true, 'P' };
    h.selectionTranslate = { true, false, false, false, 'L' };
    return h;
}

ScreenshotSettings MaximalScreenshot() {
    ScreenshotSettings s;
    s.format = ScreenshotFormat::WebP;
    s.jpegQuality = 37;
    s.includeCursor = true;
    s.quickSaveDir = L"C:\\shots\\dir with space";
    s.fileNameTemplate = L"probe_{HHmmss}";
    s.warnAlphaLossForJpegBmp = false;
    s.annotationActiveTool = 3;
    s.annotationGeometryTool = 6;
    s.annotationMarkerTool = 2;
    s.annotationArrowTool = 7;
    s.annotationTextTool = 9;
    s.annotationMosaicTool = 12;
    s.annotationColorIndex = 5;
    s.annotationGeometryColorIndex = 4;
    s.annotationMarkerColorIndex = 6;
    s.annotationUsesCustomColor = true;
    s.annotationCustomColor = RGB(11, 22, 33);
    s.annotationColorAlpha = 61;
    s.annotationColorPickerMode = 2;
    s.annotationLineStyle = 3;
    s.annotationGeometryPenWidth = 9;
    s.annotationGeometryRoundedRadius = 17;
    s.annotationPencilPenWidth = 11;
    s.annotationMarkerPenWidth = 13;
    s.annotationArrowPenWidth = 15;
    s.annotationArrowShape = 6;
    s.annotationBrokenLineMode = 1;
    s.annotationBrokenLineArrow = false;
    s.annotationBrokenLineStartArrowType = 7;
    s.annotationBrokenLineEndArrowType = 9;
    s.annotationMagnifierPenWidth = 5;
    s.annotationMagnifierRoundedRadius = 23;
    s.annotationMagnifierEllipse = true;
    s.annotationMagnifierEraseMark = true;
    s.annotationMagnifierAntiAlias = false;
    s.annotationMagnifierShadow = true;
    s.annotationMagnifierLinkType = 2;
    s.annotationMagnifierMagnification = 275;
    s.annotationMosaicPenWidth = 8;
    s.annotationEraserPenWidth = 10;
    s.annotationSerialPenWidth = 14;
    s.annotationMosaicStrength = 21;
    s.annotationMarkerBlendMode = 1;
    s.annotationMosaicMode = 1;
    s.annotationSerialType = 3;
    s.annotationHighLightStroke = true;
    s.annotationHighLightOpacity = 33;
    s.annotationHighLightStrokeColor = RGB(44, 55, 66);
    s.annotationAutoMosaicSync = false;
    s.annotationTextOutline = true;
    s.annotationTextOutlineSize = 19;
    s.annotationTextOutlineColor = RGB(77, 88, 99);
    s.annotationTextBackground = true;
    s.annotationTextBackgroundColor = RGB(101, 111, 121);
    s.annotationTextBackgroundOpacity = 44;
    s.annotationTextBackgroundRounded = 12;
    s.annotationTextBackgroundPadding = 27;
    s.annotationTextBold = true;
    s.annotationTextItalics = true;
    s.annotationTextFontFamily = L"Consolas";
    s.annotationTextFontSize = 41;
    s.annotationWatermarkText = L"Probe \\ \"quoted\"";
    s.annotationWatermarkColor = RGB(131, 141, 151);
    s.annotationWatermarkBold = true;
    s.annotationWatermarkItalics = true;
    s.annotationWatermarkOpacity = 55;
    s.annotationWatermarkFontSize = 39;
    s.annotationWatermarkGap = 123;
    s.annotationWatermarkAngle = -45;
    s.annotationWatermarkFontFamily = L"Segoe UI";
    s.annotationWatermarkPosition = 6;
    s.postProcessEnabledEveryScreenshot = true;
    s.postProcessMode = 2;
    s.roundedCornerRadius = 29;
    s.postProcessShadowSize = 31;
    s.postProcessShadowColor = RGB(161, 171, 181);
    s.postProcessBorderSize = 35;
    s.postProcessBorderColor = RGB(191, 201, 211);
    s.functionAreaAlwaysShow = L"A,B";
    s.functionAreaMorePanel = L"C,D";
    s.functionAreaAlwaysHide = L"E";
    s.hoverMagnifierEnabled = true;
    s.hoverMagnifierPower = 63;
    s.hoverMagnifierColorFormat = 4;
    s.hoverMagnifierShowCoord = false;
    s.longShotSuperLongWarningNoAsk = true;
    s.longShotMaxLengthWarningNoAsk = true;
    s.longShotMatchFailWarningNoAsk = true;
    s.longShotStopClearConfirmNoAsk = true;
    s.longShotAfterInitAction = 3;
    s.longShotAutoCrop = true;
    return s;
}

OcrSettings MaximalOcr() {
    OcrSettings s;
    s.language = L"ja";
    s.mode = L"paddle_cloud";
    // "current" is rewritten to paddle_local_doc on save, so probe a stable route.
    s.altHotkeyRoute = L"paddle_cloud";
    s.altHotkeyIdleTimeoutMin = 23;
    s.paddleApiUrl = L"https://example.invalid/api/v2/ocr/jobs";
    s.paddleToken = L"tok\"en";
    s.paddleCloudUseChartRecognition = true;
    s.timeoutMs = 180000;
    s.paddleLocalModelDir = L"D:\\models\\vl";
    s.paddleLocalPort = 8123;
    s.paddleLocalIdleTimeoutMin = 17;
    s.paddleLocalPrompt = L"Table Recognition:";
    s.paddleVlMaxTokens = 8192;
    s.enableDocParsing = true;
    s.enableImageCrop = false;
    s.localRasterMaxPixelEdge = 3000;
    s.localRasterMaxMegapixels = 40;
    s.docLayoutModelPath = L"D:\\models\\layout.onnx";
    s.layoutModelFamily = L"pp_doclayout_v3";
    s.layoutThresholdProfile = L"balanced";
    s.paddleDocGroupingMode = L"none";
    s.docRecognizeCharts = true;
    s.docRecognizeImages = true;
    s.docRecognizeSeals = true;
    s.docIgnorePageDecorations = false;
    s.docKeepFootnotes = true;
    s.docUsePhysicalSorting = true;
    s.ocrFontSize = 21;
    s.resultOnTop = true;
    s.ppocrv6ModelDir = L"D:\\models\\v6";
    s.ppocrv6Variant = L"medium";
    // Provider is written as the fixed "cpu" literal, and the preset id is
    // re-derived from the knobs on load, so both stay at their stable values.
    s.ppocrv6Provider = L"cpu";
    s.ppocrv6CpuThreads = 6;
    s.ppocrv6RecBatchSize = 3;
    s.ppocrv6DetLimitSideLen = 960;
    s.ppocrv6DetLimitType = L"max";
    s.ppocrv6DetMaxSideLimit = 2048;
    s.ppocrv6DetThreshPct = 33;
    s.ppocrv6DetBoxThreshPct = 66;
    s.ppocrv6DetUnclipRatioPct = 155;
    s.ppocrv6RecScoreThreshPct = 44;
    s.ppocrv6Preset = L"custom";
    // Derived on load: not an independent field.
    s.docIncludeIgnoredRegions = !s.docIgnorePageDecorations;
    return s;
}

void SeedDefaultSettingsFile() {
    SaveGeneralSettings(GeneralSettings{});
    SaveAotSettings(AotSettings{});
    SaveOverlaySettings(OverlaySettings{});
    SaveScreenshotSettings(ScreenshotSettings{});
    SaveOcrSettings(OcrSettings{});
    SaveHotkeySettings(HotkeySettings{}, nullptr);
}

void TestEveryFieldSurvivesSaveAndLoad() {
    ScopedIsolatedSettingsFile env;
    SeedDefaultSettingsFile();

    SaveGeneralSettings(MaximalGeneral());
    SaveAotSettings(MaximalAot());
    SaveOverlaySettings(MaximalOverlay());
    SaveScreenshotSettings(MaximalScreenshot());
    SaveOcrSettings(MaximalOcr());
    SaveHotkeySettings(MaximalHotkeys(), nullptr);

    Expect(LoadGeneralSettings() == MaximalGeneral(), "general round trips every field");
    Expect(LoadAotSettings() == MaximalAot(), "alwaysOnTop round trips every field");
    Expect(LoadOverlaySettings() == MaximalOverlay(), "overlay round trips every field");
    Expect(LoadScreenshotSettings() == MaximalScreenshot(), "screenshot round trips every field");
    Expect(LoadOcrSettings() == MaximalOcr(), "ocr round trips every field");
    Expect(LoadHotkeySettings() == MaximalHotkeys(), "hotkeys round trip every field");
}

// The subset of ScreenshotSettings the settings window owns. Everything else
// (annotation tools, watermark, post-processing, function-area layout) belongs
// to the screenshot editor, which writes it through SaveScreenshotSettings.
// Adding a field here means the window may now edit it; removing one means it
// must survive a commit untouched. Both directions are asserted below.
ScreenshotSettings WindowOwnedScreenshot() {
    ScreenshotSettings s;
    s.format = ScreenshotFormat::WebP;
    s.jpegQuality = 37;
    s.includeCursor = true;
    s.quickSaveDir = L"C:\\shots\\dir with space";
    s.hoverMagnifierEnabled = true;
    s.longShotAfterInitAction = 3;
    s.longShotAutoCrop = true;
    return s;
}

void TestEveryFieldIsMergedByCommit() {
    ScopedIsolatedSettingsFile env;
    SeedDefaultSettingsFile();

    SettingsDraft draft;
    draft.baseline.general = LoadGeneralSettings();
    draft.baseline.aot = LoadAotSettings();
    draft.baseline.overlay = LoadOverlaySettings();
    draft.baseline.screenshot = LoadScreenshotSettings();
    draft.baseline.hotkeys = LoadHotkeySettings();
    draft.pending = draft.baseline;
    draft.pending.general = MaximalGeneral();
    draft.pending.aot = MaximalAot();
    draft.pending.overlay = MaximalOverlay();
    draft.pending.screenshot = MaximalScreenshot();
    draft.pending.hotkeys = MaximalHotkeys();
    draft.ocrBaseline = LoadOcrSettings();
    draft.ocrPending = MaximalOcr();

    SettingsDraft outDraft;
    const SettingsCommitResult res = CommitSettingsPatch(draft, &outDraft);
    Expect(res.status == SettingsCommitStatus::Success, "all-field patch commits");

    // These sections are wholly owned by the settings window.
    Expect(LoadGeneralSettings() == MaximalGeneral(), "general merges every field");
    Expect(LoadAotSettings() == MaximalAot(), "alwaysOnTop merges every field");
    Expect(LoadOverlaySettings() == MaximalOverlay(), "overlay merges every field");
    Expect(LoadOcrSettings() == MaximalOcr(), "ocr merges every field");
    Expect(LoadHotkeySettings() == MaximalHotkeys(), "hotkeys merge every field");
    // The screenshot section is shared with the annotation editor.
    Expect(LoadScreenshotSettings() == WindowOwnedScreenshot(),
        "screenshot merges the window-owned fields only");
}

void TestCommitPreservesUneditedScreenshotFields() {
    ScopedIsolatedSettingsFile env;
    SeedDefaultSettingsFile();
    SaveScreenshotSettings(MaximalScreenshot());

    SettingsDraft draft;
    draft.baseline.general = LoadGeneralSettings();
    draft.baseline.aot = LoadAotSettings();
    draft.baseline.overlay = LoadOverlaySettings();
    draft.baseline.screenshot = LoadScreenshotSettings();
    draft.baseline.hotkeys = LoadHotkeySettings();
    draft.pending = draft.baseline;
    draft.ocrBaseline = LoadOcrSettings();
    draft.ocrPending = draft.ocrBaseline;
    Expect(draft.baseline.screenshot == MaximalScreenshot(), "seeded screenshot settings load back");

    draft.pending.screenshot.jpegQuality = 12;

    SettingsDraft outDraft;
    const SettingsCommitResult res = CommitSettingsPatch(draft, &outDraft);
    Expect(res.status == SettingsCommitStatus::Success, "single-field screenshot patch commits");

    ScreenshotSettings expected = MaximalScreenshot();
    expected.jpegQuality = 12;
    Expect(LoadScreenshotSettings() == expected,
        "commit preserves every screenshot field it does not own");
}

void TestEveryFieldConflictIsDetected() {
    // One representative field per section: an external change to the same field
    // the user edited must be reported as a conflict naming that field.
    struct Case {
        const wchar_t* expectedField;
        void (*seedDisk)();
        void (*editDraft)(SettingsDraft&);
        void (*editDisk)();
    };

    const Case cases[] = {
        {
            L"general.language",
            []() { SaveGeneralSettings(GeneralSettings{}); },
            [](SettingsDraft& d) { d.pending.general.language.value = AppLanguage::Chinese; },
            []() { GeneralSettings g; g.language.value = AppLanguage::English; SaveGeneralSettings(g); },
        },
        {
            L"alwaysOnTop.opacity",
            []() { SaveAotSettings(AotSettings{}); },
            [](SettingsDraft& d) { d.pending.aot.opacity = 55; },
            []() { AotSettings a; a.opacity = 77; SaveAotSettings(a); },
        },
        {
            L"overlay.thickness",
            []() { SaveOverlaySettings(OverlaySettings{}); },
            [](SettingsDraft& d) { d.pending.overlay.thickness = 6; },
            []() { OverlaySettings o; o.thickness = 8; SaveOverlaySettings(o); },
        },
        {
            L"screenshot.quickSaveDir",
            []() { SaveScreenshotSettings(ScreenshotSettings{}); },
            [](SettingsDraft& d) { d.pending.screenshot.quickSaveDir = L"C:\\mine"; },
            []() { ScreenshotSettings s; s.quickSaveDir = L"C:\\theirs"; SaveScreenshotSettings(s); },
        },
        {
            L"hotkeys.ocrAlt",
            []() { SaveHotkeySettings(HotkeySettings{}, nullptr); },
            [](SettingsDraft& d) { d.pending.hotkeys.ocrAlt = { false, false, false, false, 'M' }; },
            []() {
                HotkeySettings h;
                h.ocrAlt = { false, false, false, false, 'N' };
                SaveHotkeySettings(h, nullptr);
            },
        },
    };

    for (const Case& item : cases) {
        ScopedIsolatedSettingsFile env;
        item.seedDisk();

        SettingsDraft draft;
        draft.baseline.general = LoadGeneralSettings();
        draft.baseline.aot = LoadAotSettings();
        draft.baseline.overlay = LoadOverlaySettings();
        draft.baseline.screenshot = LoadScreenshotSettings();
        draft.baseline.hotkeys = LoadHotkeySettings();
        draft.pending = draft.baseline;
        draft.ocrBaseline = LoadOcrSettings();
        draft.ocrPending = draft.ocrBaseline;

        item.editDraft(draft);
        item.editDisk();

        SettingsDraft outDraft;
        const SettingsCommitResult res = CommitSettingsPatch(draft, &outDraft);
        if (res.status != SettingsCommitStatus::Conflict) {
            std::cerr << "FAIL missing conflict for an externally changed field\n";
            ++g_failures;
            continue;
        }
        Expect(res.conflictingField == item.expectedField,
            "conflict names the externally changed field");
    }
}

// The field tables own the byte format of every section, so pin the exact text
// of the small sections end to end. Keys inside a section are grouped by kind
// (bools, ints, strings, colors, hotkeys, transforms, constants) and every
// reader is key-based, so this is a format contract, not an ordering one.
void TestSectionJsonShape() {
    ScopedIsolatedSettingsFile env;
    SeedDefaultSettingsFile();

    GeneralSettings general;
    general.language.value = AppLanguage::Chinese;
    general.showTitlebar = true;
    SaveGeneralSettings(general);
    std::wstring json = env.Read();
    Expect(json.find(
        L"  \"general\": {\n    \"showTitlebar\": true,\n    \"language\": \"zh\"\n  }")
        != std::wstring::npos, "general section keeps its exact JSON shape");

    AotSettings aot;
    aot.showBorder = true;
    aot.customColor = false;
    aot.color = RGB(0, 255, 0);
    aot.opacity = 80;
    aot.thickness = 3;
    aot.roundedCorners = true;
    aot.inset = 2;
    SaveAotSettings(aot);
    json = env.Read();
    Expect(json.find(
        L"  \"alwaysOnTop\": {\n"
        L"    \"showBorder\": true,\n    \"customColor\": false,\n"
        L"    \"roundedCorners\": true,\n    \"opacity\": 80,\n"
        L"    \"thickness\": 3,\n    \"inset\": 2,\n"
        L"    \"color\": \"#00FF00\"\n  }") != std::wstring::npos,
        "alwaysOnTop section keeps its exact JSON shape");

    OverlaySettings overlay;
    overlay.color = RGB(255, 0, 0);
    overlay.thickness = 2;
    overlay.cropOnTop = false;
    SaveOverlaySettings(overlay);
    json = env.Read();
    Expect(json.find(
        L"  \"overlay\": {\n    \"cropOnTop\": false,\n    \"thickness\": 2,\n"
        L"    \"color\": \"#FF0000\"\n  }") != std::wstring::npos,
        "overlay section keeps its exact JSON shape");
}

} // namespace

int main() {
    ScopedTestDataDir isolation;
    TestStartupRegistrationCommandLine();

    if (!isolation.Active()) {
        std::cerr << "WARNING: could not isolate ZENCROP_DATA_DIR; skipping the settings-file "
                     "contract tests so the real user configuration is not modified.\n";
        return g_failures != 0 ? 1 : 0;
    }
    Expect(ZenCropAppDataDirectory() == isolation.Directory(),
        "Settings contracts resolve to a private scratch directory");

    TestStartupDraftRoundtrip();
    TestCommitPatchEmptyNoOp();
    TestCommitPatchConflictDetection();
    TestCommitPatchPreserveUnrecognizedTopLevel();
    TestCommitPatchUnsupportedSchema();
    TestCommitPatchKeepsExternalSameDomainField();
    TestCommitPatchTranslationKeepsExternalProvider();
    TestCommitPatchForceOverwriteResolvesConflict();
    TestCommitPatchMatchesSaveSerialization();
    TestCommitPatchCreatesMissingTranslationSection();
    TestSaveSettingsPreservesUnrecognizedTopLevel();
    TestCommitPatchWriteFailureReportsIoError();
    TestReadDefaultsWithoutFileOrSection();
    TestReadMissingKeyEmptyStringAndInvalidTokens();
    TestReadHotkeyShapes();
    TestReadOcrAliasesAndPresetNormalization();
    TestReadScreenshotMigrationsAndClampSplit();
    TestEveryFieldSurvivesSaveAndLoad();
    TestEveryFieldIsMergedByCommit();
    TestCommitPreservesUneditedScreenshotFields();
    TestEveryFieldConflictIsDetected();
    TestSectionJsonShape();

    if (g_failures != 0) return 1;
    std::cout << "ALL PASSED\n";
    return 0;
}
