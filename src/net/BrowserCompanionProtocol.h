#pragma once

#include <windows.h>
#include <nlohmann/json.hpp>
#include <expected>
#include <span>
#include <string_view>
#include <stop_token>

namespace browser_companion {
using Json = nlohmann::json;
inline constexpr size_t kMaxMessageBytes = 65536;
inline constexpr size_t kMaxCandidates = 64;
inline constexpr int kProtocolVersion = 1;
inline constexpr std::wstring_view kExtensionOrigin = L"chrome-extension://halonieocgkgbdogndcipmlpklljmoja/";

std::expected<Json, std::string> ParseMessage(std::string_view bytes);
enum class PipeIo { Synchronous, Overlapped };
std::expected<Json, std::string> ReadMessage(HANDLE pipe, DWORD timeoutMs = INFINITE, std::stop_token stop = {},
                                             PipeIo io = PipeIo::Synchronous);
bool WriteMessage(HANDLE pipe, const Json &message, DWORD timeoutMs = INFINITE, std::stop_token stop = {},
                  PipeIo io = PipeIo::Synchronous);
bool MatchWindowBounds(const Json &window, const RECT &logical, const RECT &physical, const RECT &monitorWork);
std::expected<RECT, std::string> MapCandidate(const Json &rect, const RECT &viewport, double scale, POINT point);
}
