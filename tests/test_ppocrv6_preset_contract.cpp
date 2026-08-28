#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <iostream>
#include <string>

#include "core/Settings.h"

namespace {

int g_failures = 0;

void Expect(bool cond, const wchar_t* name) {
    if (cond) {
        std::wcout << L"  PASS  " << name << L"\n";
        return;
    }
    std::wcerr << L"  FAIL  " << name << L"\n";
    ++g_failures;
}

void ExpectEqI(int got, int want, const wchar_t* name) {
    if (got == want) {
        std::wcout << L"  PASS  " << name << L"\n";
        return;
    }
    std::wcerr << L"  FAIL  " << name << L" got=" << got << L" want=" << want << L"\n";
    ++g_failures;
}

void ExpectEqW(const std::wstring& got, const std::wstring& want, const wchar_t* name) {
    if (got == want) {
        std::wcout << L"  PASS  " << name << L"\n";
        return;
    }
    std::wcerr << L"  FAIL  " << name << L" got=[" << got << L"] want=[" << want << L"]\n";
    ++g_failures;
}

void TestParseRoundTrip() {
    std::wcout << L"\n[parse / name round-trip]\n";
    ExpectEqW(PPOcrV6PresetIdName(ParsePPOcrV6PresetId(L"balanced")),
        L"balanced", L"balanced id");
    ExpectEqW(PPOcrV6PresetIdName(ParsePPOcrV6PresetId(L"quality")),
        L"quality", L"quality id");
    ExpectEqW(PPOcrV6PresetIdName(ParsePPOcrV6PresetId(L"fast")),
        L"fast", L"fast id");
    ExpectEqW(PPOcrV6PresetIdName(ParsePPOcrV6PresetId(L"official_37")),
        L"official_37", L"official id");
    // Legacy ids still parse to new packs.
    ExpectEqW(PPOcrV6PresetIdName(ParsePPOcrV6PresetId(L"screenshot_balanced")),
        L"balanced", L"legacy balanced");
    ExpectEqW(PPOcrV6PresetIdName(ParsePPOcrV6PresetId(L"screenshot_small_text")),
        L"quality", L"legacy quality");
    ExpectEqW(PPOcrV6PresetIdName(ParsePPOcrV6PresetId(L"fast_cpu")),
        L"fast", L"legacy fast");
    ExpectEqW(PPOcrV6PresetIdName(ParsePPOcrV6PresetId(L"document_official_37")),
        L"official_37", L"legacy official");
    ExpectEqW(PPOcrV6PresetIdName(ParsePPOcrV6PresetId(L"nope")),
        L"custom", L"unknown to custom");
}

void TestBalanced() {
    std::wcout << L"\n[Balanced pack - native res]\n";
    OcrSettings s;
    s.ppocrv6Variant = L"small"; // must survive Apply
    s.ppocrv6ModelDir = L"keep-me";
    s.ppocrv6CpuThreads = 7;
    ApplyPPOcrV6Preset(s, PPOcrV6PresetId::Balanced);
    ExpectEqW(s.ppocrv6Preset, L"balanced", L"preset name");
    ExpectEqW(s.ppocrv6Variant, L"small", L"variant untouched");
    ExpectEqW(s.ppocrv6ModelDir, L"keep-me", L"modelDir untouched");
    ExpectEqI(s.ppocrv6CpuThreads, 7, L"threads untouched");
    ExpectEqW(s.ppocrv6DetLimitType, L"min", L"min type");
    ExpectEqI(s.ppocrv6DetLimitSideLen, 64, L"Side 64 floor");
    ExpectEqI(s.ppocrv6DetThreshPct, 20, L"Det 20");
    ExpectEqI(s.ppocrv6DetBoxThreshPct, 45, L"Box 45");
    ExpectEqI(s.ppocrv6DetUnclipRatioPct, 140, L"Unclip 140");
    ExpectEqI(s.ppocrv6RecBatchSize, 1, L"Batch 1");
}

void TestQuality() {
    std::wcout << L"\n[Quality pack - mild upscale small]\n";
    OcrSettings s;
    s.ppocrv6Variant = L"medium";
    ApplyPPOcrV6Preset(s, PPOcrV6PresetId::Quality);
    ExpectEqW(s.ppocrv6Preset, L"quality", L"preset name");
    ExpectEqW(s.ppocrv6Variant, L"medium", L"variant untouched");
    ExpectEqW(s.ppocrv6DetLimitType, L"min", L"min");
    ExpectEqI(s.ppocrv6DetLimitSideLen, 320, L"Side 320");
    ExpectEqI(s.ppocrv6DetThreshPct, 20, L"Det 20");
    ExpectEqI(s.ppocrv6DetBoxThreshPct, 45, L"Box 45");
    ExpectEqI(s.ppocrv6DetUnclipRatioPct, 140, L"Unclip 140");
    ExpectEqI(s.ppocrv6RecBatchSize, 1, L"Batch 1");
}

void TestFast() {
    std::wcout << L"\n[Fast pack - mild downscale large, same model]\n";
    OcrSettings s;
    s.ppocrv6Variant = L"medium";
    ApplyPPOcrV6Preset(s, PPOcrV6PresetId::Fast);
    ExpectEqW(s.ppocrv6Preset, L"fast", L"preset name");
    ExpectEqW(s.ppocrv6Variant, L"medium", L"variant NOT forced to small");
    ExpectEqW(s.ppocrv6DetLimitType, L"max", L"max type");
    ExpectEqI(s.ppocrv6DetLimitSideLen, 1280, L"Side 1280");
    ExpectEqI(s.ppocrv6DetThreshPct, 20, L"Det 20");
    ExpectEqI(s.ppocrv6DetBoxThreshPct, 45, L"Box 45");
    ExpectEqI(s.ppocrv6DetUnclipRatioPct, 140, L"Unclip 140");
    ExpectEqI(s.ppocrv6RecBatchSize, 1, L"Batch 1");
}

void TestOfficial37() {
    std::wcout << L"\n[Official 3.7 pack]\n";
    OcrSettings s;
    s.ppocrv6Variant = L"small";
    ApplyPPOcrV6Preset(s, PPOcrV6PresetId::Official37);
    ExpectEqW(s.ppocrv6Preset, L"official_37", L"preset name");
    ExpectEqW(s.ppocrv6Variant, L"small", L"variant untouched");
    ExpectEqW(s.ppocrv6DetLimitType, L"min", L"min");
    ExpectEqI(s.ppocrv6DetLimitSideLen, 64, L"Side 64");
    ExpectEqI(s.ppocrv6DetThreshPct, 30, L"Det 30");
    ExpectEqI(s.ppocrv6DetBoxThreshPct, 60, L"Box 60");
    ExpectEqI(s.ppocrv6DetUnclipRatioPct, 150, L"Unclip 150");
    ExpectEqI(s.ppocrv6RecBatchSize, 6, L"Batch 6");
}

void TestCustomNoop() {
    std::wcout << L"\n[Custom leaves knobs]\n";
    OcrSettings s;
    s.ppocrv6DetLimitSideLen = 777;
    s.ppocrv6Variant = L"medium";
    ApplyPPOcrV6Preset(s, PPOcrV6PresetId::Custom);
    ExpectEqI(s.ppocrv6DetLimitSideLen, 777, L"custom does not rewrite Side");
    ExpectEqW(s.ppocrv6Preset, L"custom", L"custom name");
}

void TestMatchIgnoresVariant() {
    std::wcout << L"\n[match ignores variant]\n";
    OcrSettings s;
    ApplyPPOcrV6Preset(s, PPOcrV6PresetId::Balanced);
    s.ppocrv6Variant = L"small";
    Expect(PPOcrV6KnobsMatchPreset(s, PPOcrV6PresetId::Balanced),
        L"small still matches balanced knobs");
    s.ppocrv6Variant = L"medium";
    Expect(PPOcrV6KnobsMatchPreset(s, PPOcrV6PresetId::Balanced),
        L"medium still matches balanced knobs");
    DowngradePPOcrV6PresetIfDiverged(s);
    ExpectEqW(s.ppocrv6Preset, L"balanced", L"variant change does not downgrade");
}

void TestDowngradeWhenSideDiverges() {
    std::wcout << L"\n[downgrade when side diverges]\n";
    OcrSettings s;
    ApplyPPOcrV6Preset(s, PPOcrV6PresetId::Balanced);
    s.ppocrv6DetLimitSideLen = 960;
    Expect(!PPOcrV6KnobsMatchPreset(s, PPOcrV6PresetId::Balanced),
        L"side 960 diverges");
    DowngradePPOcrV6PresetIfDiverged(s);
    ExpectEqW(s.ppocrv6Preset, L"custom", L"diverged to custom");
}

void TestFastNotSmallModel() {
    std::wcout << L"\n[fast never forces small model]\n";
    OcrSettings s;
    s.ppocrv6Variant = L"medium";
    ApplyPPOcrV6Preset(s, PPOcrV6PresetId::Fast);
    ExpectEqW(s.ppocrv6Variant, L"medium", L"medium stays on Fast");
    // Legacy fast_cpu id maps to new fast pack (max/1280, not small).
    OcrSettings s2;
    s2.ppocrv6Variant = L"medium";
    ApplyPPOcrV6Preset(s2, ParsePPOcrV6PresetId(L"fast_cpu"));
    ExpectEqW(s2.ppocrv6Preset, L"fast", L"legacy id to fast");
    ExpectEqI(s2.ppocrv6DetLimitSideLen, 1280, L"legacy apply gets 1280");
    ExpectEqW(s2.ppocrv6Variant, L"medium", L"legacy apply keeps medium");
}

void TestLegacyLoadPreservesValuesAsCustom() {
    std::wcout << L"\n[legacy load preserves values as Custom]\n";
    OcrSettings s;
    s.ppocrv6Variant = L"medium";
    s.ppocrv6DetLimitType = L"min";
    s.ppocrv6DetLimitSideLen = 960;
    s.ppocrv6DetThreshPct = 20;
    s.ppocrv6DetBoxThreshPct = 45;
    s.ppocrv6DetUnclipRatioPct = 140;
    s.ppocrv6RecBatchSize = 4;

    NormalizeLoadedPPOcrV6Preset(s, L"screenshot_small_text");
    ExpectEqW(s.ppocrv6Preset, L"custom", L"legacy id becomes custom");
    ExpectEqW(s.ppocrv6Variant, L"medium", L"legacy variant preserved");
    ExpectEqW(s.ppocrv6DetLimitType, L"min", L"legacy type preserved");
    ExpectEqI(s.ppocrv6DetLimitSideLen, 960, L"legacy Side preserved");
    ExpectEqI(s.ppocrv6RecBatchSize, 4, L"legacy Batch preserved");
}

void TestCanonicalLoadNormalization() {
    std::wcout << L"\n[canonical load normalization]\n";
    OcrSettings matching;
    ApplyPPOcrV6Preset(matching, PPOcrV6PresetId::Quality);
    NormalizeLoadedPPOcrV6Preset(matching, L"quality");
    ExpectEqW(matching.ppocrv6Preset, L"quality", L"matching canonical stays named");

    OcrSettings diverged;
    ApplyPPOcrV6Preset(diverged, PPOcrV6PresetId::Quality);
    diverged.ppocrv6DetLimitSideLen = 777;
    NormalizeLoadedPPOcrV6Preset(diverged, L"quality");
    ExpectEqW(diverged.ppocrv6Preset, L"custom", L"diverged canonical becomes custom");
    ExpectEqI(diverged.ppocrv6DetLimitSideLen, 777, L"diverged knob preserved");
}

} // namespace

int wmain() {
    std::wcout << L"PP-OCRv6 preset contract tests (scheme 1)\n";
    TestParseRoundTrip();
    TestBalanced();
    TestQuality();
    TestFast();
    TestOfficial37();
    TestCustomNoop();
    TestMatchIgnoresVariant();
    TestDowngradeWhenSideDiverges();
    TestFastNotSmallModel();
    TestLegacyLoadPreservesValuesAsCustom();
    TestCanonicalLoadNormalization();
    if (g_failures == 0) {
        std::wcout << L"\nALL PASSED\n";
        return 0;
    }
    std::wcerr << L"\nFAILURES: " << g_failures << L"\n";
    return 1;
}
