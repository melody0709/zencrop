#pragma once

#include <windows.h>

#include <string>
#include <vector>

inline constexpr int kPaddleDocLayoutInputSize = 800;

// Exact logical input contract used by PP-DocLayoutV3. The original image is
// retained outside this structure; layout boxes map back through these scales.
struct PaddleDocLayoutInput {
    int originalWidth = 0;
    int originalHeight = 0;
    float scaleHeight = 1.0f;
    float scaleWidth = 1.0f;
    std::vector<float> chw;
};

// Builds the official PP-DocLayout input: opaque white background, RGB,
// direct 800x800 cubic resize, float32 CHW values in [0, 1].
bool BuildPaddleDocLayoutInput(
    HBITMAP hBitmap,
    PaddleDocLayoutInput& output,
    std::string* error = nullptr);
