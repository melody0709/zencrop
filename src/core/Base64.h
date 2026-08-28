#pragma once
#include <string>
#include <vector>

static const char base64_chars[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "abcdefghijklmnopqrstuvwxyz"
    "0123456789+/";

inline std::string Base64Encode(const unsigned char* data, size_t len) {
    std::string result;
    result.reserve(((len + 2) / 3) * 4);

    for (size_t i = 0; i < len; i += 3) {
        const size_t remaining = len - i;
        unsigned int a = data[i];
        unsigned int b = remaining > 1 ? data[i + 1] : 0;
        unsigned int c = remaining > 2 ? data[i + 2] : 0;
        unsigned int triple = (a << 16) | (b << 8) | c;

        result += base64_chars[(triple >> 18) & 0x3F];
        result += base64_chars[(triple >> 12) & 0x3F];
        result += remaining > 1 ? base64_chars[(triple >> 6) & 0x3F] : '=';
        result += remaining > 2 ? base64_chars[triple & 0x3F] : '=';
    }

    return result;
}

inline std::string Base64Encode(const std::vector<unsigned char>& data) {
    return Base64Encode(data.data(), data.size());
}
