#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cmath>
#include <iostream>
#include <string>
#include <vector>

#include "ocr/engine/PPOcrV6CtcDecode.h"

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

void ExpectEqW(const std::wstring& got, const std::wstring& want, const wchar_t* name) {
    if (got == want) {
        std::wcout << L"  PASS  " << name << L"\n";
        return;
    }
    std::wcerr << L"  FAIL  " << name << L" got=[" << got << L"] want=[" << want << L"]\n";
    ++g_failures;
}

void ExpectNear(float got, float want, float eps, const wchar_t* name) {
    if (std::fabs(got - want) <= eps) {
        std::wcout << L"  PASS  " << name << L"\n";
        return;
    }
    std::wcerr << L"  FAIL  " << name << L" got=" << got << L" want=" << want << L"\n";
    ++g_failures;
}

// Build a tiny synthetic alphabet for hermetic tests:
// blank=0, dict[0]='Z', [1]='E', [2]='N', [3]=' ', [4]='C', [5]='R', [6]='O', [7]='P',
// [8]='1', [9]='2', [10]='3', [11]=U+3000
// So classes = 13 (blank + 12 chars). Space is last-but-one ASCII space class in real
// models; here we place ASCII space at index 3 and U+3000 at the end for coverage.
std::vector<std::wstring> MakeTinyDict() {
    return {
        L"Z", L"E", L"N", L" ", L"C", L"R", L"O", L"P",
        L"1", L"2", L"3", L"　",
    };
}

// Fill one timestep's class vector with a one-hot-ish score at bestClass.
void FillTimestepBtc(
    std::vector<float>& values,
    int batchIndex,
    int t,
    int timesteps,
    int classes,
    int bestClass,
    float bestScore = 0.9f,
    float otherScore = 0.01f)
{
    for (int c = 0; c < classes; c++) {
        const size_t idx = ((static_cast<size_t>(batchIndex) * timesteps + t) * classes) + c;
        values[idx] = (c == bestClass) ? bestScore : otherScore;
    }
}

void FillTimestepBct(
    std::vector<float>& values,
    int batchIndex,
    int t,
    int timesteps,
    int classes,
    int bestClass,
    float bestScore = 0.9f,
    float otherScore = 0.01f)
{
    for (int c = 0; c < classes; c++) {
        const size_t idx = ((static_cast<size_t>(batchIndex) * classes + c) * timesteps) + t;
        values[idx] = (c == bestClass) ? bestScore : otherScore;
    }
}

void TestAppendSpace() {
    std::wcout << L"\n[append space]\n";
    std::vector<std::wstring> base = { L"a", L"b", L"　" };
    PPOcrV6Ctc::AppendOfficialSpaceChar(base);
    Expect(base.size() == 4, L"append grows by 1");
    ExpectEqW(base[2], L"　", L"U+3000 preserved before append");
    ExpectEqW(base[3], L" ", L"trailing ASCII space");
    Expect(PPOcrV6Ctc::ExpectedOutputClasses(base) == 5, L"classes = effective+1");
}

void TestLayoutMismatch() {
    std::wcout << L"\n[layout / class count]\n";
    auto dict = MakeTinyDict(); // effective 12, expected classes 13
    int batch = 0, timesteps = 0, classes = 0;
    bool btc = true;
    std::wstring error;

    // Correct B,T,C
    Expect(PPOcrV6Ctc::ResolveLayout(
        {1, 4, 13}, static_cast<int>(dict.size()), batch, timesteps, classes, btc, error),
        L"resolve B,T,C match");
    Expect(btc && batch == 1 && timesteps == 4 && classes == 13, L"B,T,C fields");

    // Correct B,C,T
    Expect(PPOcrV6Ctc::ResolveLayout(
        {1, 13, 4}, static_cast<int>(dict.size()), batch, timesteps, classes, btc, error),
        L"resolve B,C,T match");
    Expect(!btc && batch == 1 && timesteps == 4 && classes == 13, L"B,C,T fields");

    // Wrong class count must fail (do not silently drop tail class).
    Expect(!PPOcrV6Ctc::ResolveLayout(
        {1, 4, 12}, static_cast<int>(dict.size()), batch, timesteps, classes, btc, error),
        L"reject classes=12 vs expected 13");
    Expect(!error.empty(), L"mismatch error message non-empty");

    Expect(!PPOcrV6Ctc::ResolveLayout(
        {1, 4, 18710}, static_cast<int>(dict.size()), batch, timesteps, classes, btc, error),
        L"reject official 18710 against tiny dict");
}

void TestDecodeZenCrop123() {
    std::wcout << L"\n[decode ZEN CROP 123]\n";
    auto dict = MakeTinyDict();
    // class ids: blank=0, Z=1,E=2,N=3,sp=4,C=5,R=6,O=7,P=8,1=9,2=10,3=11,ideographic=12
    // Token sequence with blanks and a duplicate:
    // Z E N blank blank space C R O O P blank space 1 2 3
    const std::vector<int> tokens = {
        1, 2, 3, 0, 0, 4, 5, 6, 7, 7, 8, 0, 4, 9, 10, 11
    };
    const int T = static_cast<int>(tokens.size());
    const int C = static_cast<int>(dict.size()) + 1;
    std::vector<float> values(static_cast<size_t>(1 * T * C), 0.0f);
    for (int t = 0; t < T; t++) {
        FillTimestepBtc(values, 0, t, T, C, tokens[t], 0.8f + 0.01f * (t % 3));
    }

    // Score per timestep: 0.8 + 0.01*(t%3). Kept after blank/dup drop:
    // Z E N sp C R O P sp 1 2 3 = 12 (dup O at t=9 dropped; two spaces kept).
    double scoreSum = 0.0;
    int kept = 0;
    int prevTok = -1;
    for (int t = 0; t < T; t++) {
        const int tok = tokens[static_cast<size_t>(t)];
        if (tok != 0 && tok != prevTok) {
            scoreSum += 0.8 + 0.01 * (t % 3);
            kept++;
        }
        prevTok = tok;
    }
    const float expectedMean = kept > 0 ? static_cast<float>(scoreSum / kept) : 0.0f;

    auto decoded = PPOcrV6Ctc::DecodeFromShape(
        values.data(), values.size(), {1, T, C}, 0, dict);
    Expect(decoded.ok, L"decode ok");
    ExpectEqW(decoded.text, L"ZEN CROP 123", L"exact text with ASCII spaces");
    Expect(decoded.score > 0.0f && decoded.score <= 1.0f, L"score in (0,1]");
    // 12 kept tokens including two ASCII spaces; mean must match synthetic scores.
    Expect(kept == 12, L"kept token count 12 (spaces included)");
    ExpectNear(decoded.score, expectedMean, 1e-5f, L"score mean matches kept tokens");
}

void TestDecodeIdeographicSpace() {
    std::wcout << L"\n[decode U+3000]\n";
    auto dict = MakeTinyDict();
    // Z + U+3000 + E
    const std::vector<int> tokens = {1, 12, 2};
    const int T = static_cast<int>(tokens.size());
    const int C = static_cast<int>(dict.size()) + 1;
    std::vector<float> values(static_cast<size_t>(1 * T * C), 0.0f);
    for (int t = 0; t < T; t++) {
        FillTimestepBtc(values, 0, t, T, C, tokens[t], 0.95f);
    }
    auto decoded = PPOcrV6Ctc::DecodeFromShape(
        values.data(), values.size(), {1, T, C}, 0, dict);
    Expect(decoded.ok, L"ideographic decode ok");
    ExpectEqW(decoded.text, L"Z　E", L"U+3000 kept as real char");
    // 3 kept tokens, all 0.95
    ExpectNear(decoded.score, 0.95f, 1e-5f, L"score mean includes U+3000");
}

void TestDecodeBctLayout() {
    std::wcout << L"\n[B,C,T layout]\n";
    auto dict = MakeTinyDict();
    const std::vector<int> tokens = {1, 4, 5}; // Z space C
    const int T = static_cast<int>(tokens.size());
    const int C = static_cast<int>(dict.size()) + 1;
    std::vector<float> values(static_cast<size_t>(1 * C * T), 0.0f);
    for (int t = 0; t < T; t++) {
        FillTimestepBct(values, 0, t, T, C, tokens[t], 0.7f);
    }
    auto decoded = PPOcrV6Ctc::DecodeFromShape(
        values.data(), values.size(), {1, C, T}, 0, dict);
    Expect(decoded.ok, L"B,C,T decode ok");
    ExpectEqW(decoded.text, L"Z C", L"B,C,T text with space");
    ExpectNear(decoded.score, 0.7f, 1e-5f, L"B,C,T score includes space");
}

void TestAllBlank() {
    std::wcout << L"\n[all blank]\n";
    auto dict = MakeTinyDict();
    const int T = 5;
    const int C = static_cast<int>(dict.size()) + 1;
    std::vector<float> values(static_cast<size_t>(1 * T * C), 0.0f);
    for (int t = 0; t < T; t++) {
        FillTimestepBtc(values, 0, t, T, C, 0, 0.99f);
    }
    auto decoded = PPOcrV6Ctc::DecodeFromShape(
        values.data(), values.size(), {1, T, C}, 0, dict);
    Expect(decoded.ok, L"all-blank ok");
    ExpectEqW(decoded.text, L"", L"all-blank empty text");
    ExpectNear(decoded.score, 0.0f, 1e-6f, L"all-blank score 0");
}

void TestBatchIndexOob() {
    std::wcout << L"\n[batch oob]\n";
    auto dict = MakeTinyDict();
    const int T = 2;
    const int C = static_cast<int>(dict.size()) + 1;
    std::vector<float> values(static_cast<size_t>(1 * T * C), 0.01f);
    auto decoded = PPOcrV6Ctc::DecodeFromShape(
        values.data(), values.size(), {1, T, C}, 3, dict);
    Expect(!decoded.ok, L"batch oob fails");
    Expect(!decoded.error.empty(), L"batch oob error set");
}

void TestSpaceInConfidence() {
    std::wcout << L"\n[space in confidence]\n";
    auto dict = MakeTinyDict();
    // Only a space token: class 4, score 0.55
    const int T = 1;
    const int C = static_cast<int>(dict.size()) + 1;
    std::vector<float> values(static_cast<size_t>(1 * T * C), 0.01f);
    FillTimestepBtc(values, 0, 0, T, C, 4, 0.55f);
    auto decoded = PPOcrV6Ctc::DecodeFromShape(
        values.data(), values.size(), {1, T, C}, 0, dict);
    Expect(decoded.ok, L"space-only ok");
    ExpectEqW(decoded.text, L" ", L"space-only text");
    ExpectNear(decoded.score, 0.55f, 1e-5f, L"space contributes to score");
}

void TestOfficialContractConstants() {
    std::wcout << L"\n[official size constants]\n";
    Expect(PPOcrV6Ctc::kExpectedBaseDictSize == 18708, L"base 18708");
    Expect(PPOcrV6Ctc::kExpectedEffectiveDictSize == 18709, L"effective 18709");
    Expect(PPOcrV6Ctc::kExpectedOutputClasses == 18710, L"classes 18710");
    Expect(PPOcrV6Ctc::kBlankIndex == 0, L"blank index 0");
}

} // namespace

int wmain() {
    std::wcout << L"PP-OCRv6 hermetic CTC decode contract tests\n";
    TestOfficialContractConstants();
    TestAppendSpace();
    TestLayoutMismatch();
    TestDecodeZenCrop123();
    TestDecodeIdeographicSpace();
    TestDecodeBctLayout();
    TestAllBlank();
    TestBatchIndexOob();
    TestSpaceInConfidence();

    if (g_failures == 0) {
        std::wcout << L"\nALL PASSED\n";
        return 0;
    }
    std::wcerr << L"\nFAILURES: " << g_failures << L"\n";
    return 1;
}
