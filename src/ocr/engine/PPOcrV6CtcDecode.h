#pragma once

// Hermetic PP-OCRv6 CTC label decode helpers.
// Official contract (small/medium rec ONNX):
//   base dict from inference.yml: 18708
//   append ASCII space (use_space_char=true): +1
//   blank index: 0
//   effective dict size: 18709
//   expected output classes: 18710
//   class 0: blank
//   class 1..18708: base dictionary[0..18707]
//   class 18709: ASCII space U+0020
//
// Base dictionary also contains U+3000 IDEOGRAPHIC SPACE as a normal character;
// that is distinct from the trailing ASCII space class.

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include "core/WideStringUtils.h"

namespace PPOcrV6Ctc {

inline constexpr int kBlankIndex = 0;
inline constexpr int kExpectedBaseDictSize = 18708;
inline constexpr int kExpectedEffectiveDictSize = 18709; // base + ASCII space
inline constexpr int kExpectedOutputClasses = 18710;     // blank + effective

struct DecodeResult {
    std::wstring text;
    float score = 0.0f;
    bool ok = false;
    std::wstring error;
};

// Official CTCLabelDecode: base characters + trailing ASCII space.
// Call after loading the base dictionary TXT (one character per line, no trailing space line).
inline void AppendOfficialSpaceChar(std::vector<std::wstring>& baseDict) {
    baseDict.push_back(L" ");
}

inline int ExpectedOutputClasses(const std::vector<std::wstring>& effectiveDict) {
    return static_cast<int>(effectiveDict.size()) + 1; // blank + chars
}

// Resolve output layout as B,T,C (preferred when d2 matches classes) or B,C,T.
// Returns false when shapes cannot describe a valid CTC tensor for the dict.
inline bool ResolveLayout(
    const std::vector<int64_t>& shape,
    int effectiveDictSize,
    int& batch,
    int& timesteps,
    int& classes,
    bool& btc,
    std::wstring& error)
{
    batch = 0;
    timesteps = 0;
    classes = 0;
    btc = true;
    error.clear();

    if (shape.size() != 3) {
        error = L"PP-OCRv6 CTC output rank must be 3 (B,T,C or B,C,T).";
        return false;
    }
    if (effectiveDictSize <= 0) {
        error = L"PP-OCRv6 effective recognition dictionary is empty.";
        return false;
    }

    const int64_t d0 = shape[0];
    const int64_t d1 = shape[1];
    const int64_t d2 = shape[2];
    if (d0 <= 0 || d1 <= 0 || d2 <= 0) {
        error = L"PP-OCRv6 CTC output shape has non-positive dimensions.";
        return false;
    }

    const int expectedClasses = effectiveDictSize + 1;
    // Prefer layout whose class axis equals the contract size.
    // No fuzzy "d2 >= expected" path: wrong class count must fail closed.
    if (d2 == expectedClasses) {
        btc = true;
        batch = static_cast<int>(d0);
        timesteps = static_cast<int>(d1);
        classes = static_cast<int>(d2);
    } else if (d1 == expectedClasses) {
        btc = false;
        batch = static_cast<int>(d0);
        timesteps = static_cast<int>(d2);
        classes = static_cast<int>(d1);
    } else {
        // Report the closer axis so the error is actionable.
        const int64_t reported = (d2 >= d1) ? d2 : d1;
        // OWN-126: pure int labels for CTC class count mismatch (WideStringUtils).
        error = L"PP-OCRv6 CTC class count mismatch: got "
            + WideFormatIntLabel(static_cast<int>(reported))
            + L", expected "
            + WideFormatIntLabel(expectedClasses)
            + L" (effective_dict_size="
            + WideFormatIntLabel(effectiveDictSize)
            + L", blank+dict).";
        return false;
    }
    if (classes <= 1) {
        error = L"PP-OCRv6 CTC class count too small.";
        return false;
    }
    return true;
}

// Pure CTC greedy decode over one batch row.
// values: flattened float tensor in B,T,C or B,C,T order according to btc.
// blank=0; remove consecutive duplicates; keep non-blank tokens including ASCII space.
// Confidence = mean probability of kept non-blank tokens (space included).
inline DecodeResult DecodeGreedy(
    const float* values,
    size_t valueCount,
    int batchIndex,
    int batch,
    int timesteps,
    int classes,
    bool btc,
    const std::vector<std::wstring>& effectiveDict)
{
    DecodeResult result;
    if (!values || valueCount == 0 || effectiveDict.empty()) {
        result.error = L"PP-OCRv6 CTC decode input is empty.";
        return result;
    }
    if (batchIndex < 0 || batchIndex >= batch) {
        result.error = L"PP-OCRv6 CTC batch index out of range.";
        return result;
    }
    if (timesteps <= 0 || classes <= 1) {
        result.error = L"PP-OCRv6 CTC timesteps/classes invalid.";
        return result;
    }

    const int expectedClasses = static_cast<int>(effectiveDict.size()) + 1;
    if (classes != expectedClasses) {
        // OWN-126: pure int labels for CTC class count mismatch (WideStringUtils).
        result.error = L"PP-OCRv6 CTC class count mismatch: got "
            + WideFormatIntLabel(classes)
            + L", expected "
            + WideFormatIntLabel(expectedClasses)
            + L".";
        return result;
    }

    const size_t need = static_cast<size_t>(batch) * static_cast<size_t>(timesteps) * static_cast<size_t>(classes);
    if (valueCount < need) {
        result.error = L"PP-OCRv6 CTC tensor buffer shorter than shape.";
        return result;
    }

    int prev = -1;
    int used = 0;
    double scoreSum = 0.0;
    for (int t = 0; t < timesteps; t++) {
        int best = 0;
        float bestScore = -(std::numeric_limits<float>::infinity)();
        for (int c = 0; c < classes; c++) {
            const size_t idx = btc
                ? (((static_cast<size_t>(batchIndex) * timesteps + t) * classes) + c)
                : (((static_cast<size_t>(batchIndex) * classes + c) * timesteps) + t);
            if (idx >= valueCount) {
                result.error = L"PP-OCRv6 CTC index out of range while decoding.";
                return result;
            }
            const float v = values[idx];
            if (v > bestScore) {
                bestScore = v;
                best = c;
            }
        }

        // Official: drop blank and consecutive duplicates; keep first of a run.
        if (best != kBlankIndex && best != prev) {
            const int dictIndex = best - 1;
            if (dictIndex < 0 || dictIndex >= static_cast<int>(effectiveDict.size())) {
                // Should be impossible after class-count check; fail closed.
                // OWN-126: pure int label for out-of-range class id (WideStringUtils).
                result.error = L"PP-OCRv6 CTC class id out of dictionary range: "
                    + WideFormatIntLabel(best);
                return result;
            }
            result.text += effectiveDict[static_cast<size_t>(dictIndex)];
            scoreSum += bestScore;
            used++;
        }
        prev = best;
    }

    result.score = used > 0 ? static_cast<float>(scoreSum / used) : 0.0f;
    result.ok = true;
    return result;
}

// Convenience: resolve layout then decode one batch row.
inline DecodeResult DecodeFromShape(
    const float* values,
    size_t valueCount,
    const std::vector<int64_t>& shape,
    int batchIndex,
    const std::vector<std::wstring>& effectiveDict)
{
    int batch = 0;
    int timesteps = 0;
    int classes = 0;
    bool btc = true;
    std::wstring error;
    if (!ResolveLayout(shape, static_cast<int>(effectiveDict.size()), batch, timesteps, classes, btc, error)) {
        DecodeResult bad;
        bad.error = error;
        return bad;
    }
    return DecodeGreedy(values, valueCount, batchIndex, batch, timesteps, classes, btc, effectiveDict);
}

} // namespace PPOcrV6Ctc
