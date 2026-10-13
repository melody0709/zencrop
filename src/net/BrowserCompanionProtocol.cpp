#include "BrowserCompanionProtocol.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>

namespace browser_companion {
namespace {
bool Transfer(HANDLE pipe, std::span<char> bytes, bool write, DWORD timeoutMs, std::stop_token stop, PipeIo io) {
    std::optional<ULONGLONG> deadline;
    if (timeoutMs != INFINITE)
        deadline = GetTickCount64() + timeoutMs;
    while (!bytes.empty()) {
        if (stop.stop_requested())
            return false;
        DWORD count = 0;
        OVERLAPPED ov = {};
        OVERLAPPED *operation = io == PipeIo::Overlapped ? &ov : nullptr;
        if (operation) {
            ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
            if (!ov.hEvent)
                return false;
        }
        BOOL ok = write ? WriteFile(pipe, bytes.data(), static_cast<DWORD>(bytes.size()), &count, operation)
                        : ReadFile(pipe, bytes.data(), static_cast<DWORD>(bytes.size()), &count, operation);
        if (!ok && operation && GetLastError() == ERROR_IO_PENDING) {
            std::stop_callback cancel(stop, [&] { CancelIoEx(pipe, &ov); });
            const auto now = GetTickCount64();
            const DWORD remaining = !deadline ? INFINITE : now < *deadline ? static_cast<DWORD>(*deadline - now) : 0;
            if (WaitForSingleObject(ov.hEvent, remaining) == WAIT_OBJECT_0) {
                ok = GetOverlappedResult(pipe, &ov, &count, FALSE);
            } else {
                CancelIoEx(pipe, &ov);
                GetOverlappedResult(pipe, &ov, &count, TRUE);
                ok = FALSE;
            }
        }
        if (ov.hEvent)
            CloseHandle(ov.hEvent);
        if (!ok || count == 0)
            return false;
        bytes = bytes.subspan(count);
        if (!write && io == PipeIo::Overlapped && timeoutMs == INFINITE && !bytes.empty())
            deadline = GetTickCount64() + 1000;
    }
    return true;
}
}

std::expected<Json, std::string> ParseMessage(std::string_view bytes) {
    if (bytes.empty() || bytes.size() > kMaxMessageBytes ||
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(), static_cast<int>(bytes.size()), nullptr, 0) ==
            0) {
        return std::unexpected("invalid_utf8_or_size");
    }
    int depth = 0;
    bool quoted = false, escaped = false;
    for (const char ch : bytes) {
        if (quoted) {
            if (escaped)
                escaped = false;
            else if (ch == '\\')
                escaped = true;
            else if (ch == '"')
                quoted = false;
        } else if (ch == '"')
            quoted = true;
        else if (ch == '{' || ch == '[') {
            if (++depth > 32)
                return std::unexpected("json_too_deep");
        } else if (ch == '}' || ch == ']')
            --depth;
    }
    auto json = Json::parse(bytes, nullptr, false);
    if (json.is_discarded() || !json.is_object())
        return std::unexpected("invalid_json");
    if (!json.contains("version") || !json["version"].is_number_integer() || json["version"] != kProtocolVersion) {
        return std::unexpected("version_incompatible");
    }
    if (!json.contains("type") || !json["type"].is_string())
        return std::unexpected("invalid_type");
    const auto type = json["type"].get<std::string>();
    constexpr std::array types = {"hello", "begin",  "inspect",     "navigate",
                                  "end",   "result", "unavailable", "invalidated"};
    if (std::ranges::find(types, type) == types.end())
        return std::unexpected("invalid_type");
    if (type != "hello") {
        for (const auto *key : {"sessionId", "requestId"}) {
            if (!json.contains(key) || !json[key].is_string() || json[key].get_ref<const std::string &>().size() > 64 ||
                json[key].get_ref<const std::string &>().empty())
                return std::unexpected("invalid_identity");
        }
    }
    const auto stringField = [&](const char *key, size_t limit) {
        return json.contains(key) && json[key].is_string() && !json[key].get_ref<const std::string &>().empty() &&
               json[key].get_ref<const std::string &>().size() <= limit;
    };
    const auto idField = [&](const char *key) {
        return json.contains(key) && json[key].is_number_integer() && json[key].get<double>() >= 0 &&
               json[key].get<double>() <= 2147483647.0;
    };
    if (type == "hello" || type == "result" || type == "begin" || type == "inspect" || type == "navigate") {
        if (!stringField("connectionId", 64))
            return std::unexpected("invalid_identity");
    }
    if (type == "result") {
        if (!idField("windowId") || !idField("tabId") || !idField("frameId") || !stringField("documentId", 128) ||
            !json.contains("scale") || !json["scale"].is_number() || !std::isfinite(json["scale"].get<double>()) ||
            !json.contains("geometryVersion") || !json["geometryVersion"].is_number_integer() ||
            json["geometryVersion"] != 1 || !json.contains("candidates") || !json["candidates"].is_array() ||
            json["candidates"].empty() || json["candidates"].size() > kMaxCandidates) {
            return std::unexpected("invalid_result");
        }
        for (const auto &candidate : json["candidates"]) {
            if (!candidate.is_array() || candidate.size() != 4)
                return std::unexpected("invalid_geometry");
            for (const auto &value : candidate)
                if (!value.is_number() || !std::isfinite(value.get<double>()) ||
                    std::abs(value.get<double>()) > 1000000)
                    return std::unexpected("invalid_geometry");
            if (candidate[2].get<double>() <= candidate[0].get<double>() ||
                candidate[3].get<double>() <= candidate[1].get<double>())
                return std::unexpected("invalid_geometry");
        }
    }
    if (type == "unavailable" || type == "invalidated") {
        if (!stringField("status", 64))
            return std::unexpected("invalid_status");
    }
    return json;
}

std::expected<Json, std::string> ReadMessage(HANDLE pipe, DWORD timeoutMs, std::stop_token stop, PipeIo io) {
    const auto start = GetTickCount64();
    std::array<char, 4> header = {};
    if (!Transfer(pipe, header, false, timeoutMs, stop, io))
        return std::unexpected("disconnected");
    uint32_t size = 0;
    for (unsigned i = 0; i < 4; ++i)
        size |= static_cast<uint32_t>(static_cast<unsigned char>(header[i])) << (8 * i);
    if (size == 0 || size > kMaxMessageBytes)
        return std::unexpected("message_too_large");
    std::string bytes(size, '\0');
    const DWORD remaining = timeoutMs == INFINITE && io == PipeIo::Overlapped ? 1000
                            : timeoutMs == INFINITE                           ? INFINITE
                            : GetTickCount64() - start < timeoutMs
                                ? timeoutMs - static_cast<DWORD>(GetTickCount64() - start)
                                : 0;
    if (!Transfer(pipe, bytes, false, remaining, stop, io))
        return std::unexpected("disconnected");
    return ParseMessage(bytes);
}

bool WriteMessage(HANDLE pipe, const Json &message, DWORD timeoutMs, std::stop_token stop, PipeIo io) {
    const auto start = GetTickCount64();
    std::string bytes;
    try {
        bytes = message.dump(-1, ' ', false, Json::error_handler_t::strict);
    } catch (const Json::exception &) {
        return false;
    }
    if (!ParseMessage(bytes))
        return false;
    const uint32_t size = static_cast<uint32_t>(bytes.size());
    std::array<char, 4> header = {};
    for (unsigned i = 0; i < 4; ++i)
        header[i] = static_cast<char>((size >> (8 * i)) & 255);
    if (!Transfer(pipe, header, true, timeoutMs, stop, io))
        return false;
    const DWORD remaining = timeoutMs == INFINITE ? INFINITE
                            : GetTickCount64() - start < timeoutMs
                                ? timeoutMs - static_cast<DWORD>(GetTickCount64() - start)
                                : 0;
    return Transfer(pipe, bytes, true, remaining, stop, io);
}

bool MatchWindowBounds(const Json &window, const RECT &logical, const RECT &physical, const RECT &monitorWork) {
    if (!window.is_object())
        return false;
    for (const char *key : {"id", "left", "top", "width", "height"}) {
        if (!window.contains(key) || !window[key].is_number_integer() ||
            std::abs(window[key].get<double>()) > (std::string_view(key) == "id" ? 2147483647.0 : 10000000.0))
            return false;
    }
    if (window["width"].get<int>() <= 0 || window["height"].get<int>() <= 0)
        return false;
    const std::array<double, 4> browser{window["left"].get<double>(), window["top"].get<double>(),
                                        window["width"].get<double>(), window["height"].get<double>()};
    const auto components = [](const RECT &r) {
        return std::array<double, 4>{static_cast<double>(r.left), static_cast<double>(r.top),
                                     static_cast<double>(r.right - r.left), static_cast<double>(r.bottom - r.top)};
    };
    const auto system = components(logical), pixels = components(physical);
    if (window.contains("screen")) {
        const auto &screen = window["screen"];
        if (!screen.is_array() || screen.size() != 4)
            return false;
        for (const auto &value : screen)
            if (!value.is_number_integer() || std::abs(value.get<double>()) > 10000000)
                return false;
        const double width = screen[2].get<double>() - screen[0].get<double>();
        const double height = screen[3].get<double>() - screen[1].get<double>();
        if (width <= 0 || height <= 0 || IsRectEmpty(&monitorWork))
            return false;
        const double scale = (monitorWork.right - monitorWork.left) / width;
        if (!std::isfinite(scale) || scale < 0.25 || scale > 16 ||
            std::abs(height * scale - (monitorWork.bottom - monitorWork.top)) > 2 * scale + 1)
            return false;
        // Chromium maps origins relative to each monitor; screen positions do not share a global scale.
        const std::array<double, 4> mapped{monitorWork.left + (browser[0] - screen[0].get<double>()) * scale,
                                           monitorWork.top + (browser[1] - screen[1].get<double>()) * scale,
                                           browser[2] * scale, browser[3] * scale};
        return std::ranges::equal(mapped, pixels,
                                  [scale](double a, double b) { return std::abs(a - b) <= 2 * scale + 1; });
    }
    if (std::ranges::equal(browser, system, [](double a, double b) { return std::abs(a - b) <= 2; }))
        return true;
    // Compatibility with older companions and Chrome's forced UI scale on a single display.
    const double scale = pixels[2] / browser[2];
    return std::isfinite(scale) && scale >= 0.25 && scale <= 16 &&
           std::ranges::equal(browser, pixels, [scale](double dip, double pixel) {
               return std::abs(dip * scale - pixel) <= 2 * scale + 1;
           });
}

std::expected<RECT, std::string> MapCandidate(const Json &rect, const RECT &viewport, double scale, POINT point) {
    if (!rect.is_array() || rect.size() != 4 || !std::isfinite(scale) || scale < 0.25 || scale > 16) {
        return std::unexpected("invalid_geometry");
    }
    std::array<double, 4> values = {};
    for (size_t i = 0; i < 4; ++i) {
        if (!rect[i].is_number())
            return std::unexpected("invalid_geometry");
        values[i] = rect[i].get<double>();
        if (!std::isfinite(values[i]) || std::abs(values[i]) > 1000000)
            return std::unexpected("invalid_geometry");
        values[i] = values[i] * scale + (i % 2 == 0 ? viewport.left : viewport.top);
        if (std::abs(values[i]) > 10000000)
            return std::unexpected("invalid_geometry");
    }
    RECT result = {static_cast<LONG>(std::floor(values[0])), static_cast<LONG>(std::floor(values[1])),
                   static_cast<LONG>(std::ceil(values[2])), static_cast<LONG>(std::ceil(values[3]))};
    RECT clipped = {};
    if (!IntersectRect(&clipped, &result, &viewport) ||
        static_cast<int64_t>(clipped.right - clipped.left) * (clipped.bottom - clipped.top) <= 25 ||
        !PtInRect(&clipped, point))
        return std::unexpected("invalid_geometry");
    return clipped;
}
}
