#include "BrowserCompanionProtocol.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <set>
#include <thread>
#include <vector>

namespace browser_companion {
namespace {
// A synchronous handle has no deadline of its own: ReadFile/WriteFile block until data
// arrives or the handle closes, so a timeout parameter alone is a lie. The call is run
// on a helper thread and cancelled there, which is the only mechanism Windows offers
// for bounding synchronous I/O.
bool TransferSyncBounded(HANDLE pipe, std::span<char> bytes, bool write, DWORD timeoutMs, std::stop_token stop,
                         DWORD &count) {
    struct Call {
        HANDLE pipe;
        std::span<char> bytes;
        bool write;
        DWORD count = 0;
        bool ok = false;
    };
    Call call{pipe, bytes, write};
    std::mutex mutex;
    std::condition_variable cv;
    bool finished = false;
    std::jthread worker([&](std::stop_token workerStop) {
        const BOOL ok =
            !workerStop.stop_requested() &&
            (call.write
                 ? WriteFile(call.pipe, call.bytes.data(), static_cast<DWORD>(call.bytes.size()), &call.count, nullptr)
                 : ReadFile(call.pipe, call.bytes.data(), static_cast<DWORD>(call.bytes.size()), &call.count, nullptr));
        {
            std::lock_guard lock(mutex);
            call.ok = ok != FALSE;
            finished = true;
        }
        cv.notify_all();
    });
    const auto deadline = GetTickCount64() + timeoutMs;
    bool completed = false;
    {
        std::unique_lock lock(mutex);
        std::stop_callback cancel(stop, [&] {
            worker.request_stop();
            CancelSynchronousIo(worker.native_handle());
            cv.notify_all();
        });
        while (!finished && !stop.stop_requested()) {
            const auto now = GetTickCount64();
            if (now >= deadline)
                break;
            cv.wait_for(lock, std::chrono::milliseconds(deadline - now));
        }
        completed = finished;
        if (!finished) {
            worker.request_stop();
            CancelSynchronousIo(worker.native_handle());
        }
    }
    // Cancellation can precede ReadFile submission; retry until the worker has drained.
    while (WaitForSingleObject(worker.native_handle(), 5) == WAIT_TIMEOUT)
        CancelSynchronousIo(worker.native_handle());
    worker.join();
    if (!completed || !call.ok || call.count == 0)
        return false;
    count = call.count;
    return true;
}

bool Transfer(HANDLE pipe, std::span<char> bytes, bool write, DWORD timeoutMs, std::stop_token stop, PipeIo io) {
    if (io == PipeIo::Synchronous && timeoutMs != INFINITE) {
        // One absolute deadline for the whole transfer: a peer that dribbles bytes must
        // not be able to refresh its budget.
        const auto deadline = GetTickCount64() + timeoutMs;
        while (!bytes.empty()) {
            if (stop.stop_requested())
                return false;
            const auto now = GetTickCount64();
            if (now >= deadline)
                return false;
            DWORD count = 0;
            if (!TransferSyncBounded(pipe, bytes, write, static_cast<DWORD>(deadline - now), stop, count))
                return false;
            bytes = bytes.subspan(count);
        }
        return true;
    }
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

bool ValidUtf8(std::string_view bytes) {
    return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(), static_cast<int>(bytes.size()), nullptr,
                               0) != 0;
}

// Quote-aware nesting scan. Rejects a frame before any parser sees it, so the depth
// limit cannot be escaped by a hostile encoder.
int NestingDepth(std::string_view bytes) {
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
            if (++depth > static_cast<int>(kMaxNestingDepth))
                return depth;
        } else if (ch == '}' || ch == ']')
            --depth;
    }
    return depth;
}

// Duplicate keys give a control frame two meanings, so they are rejected instead of
// resolved. Detection runs through the library's own SAX interface: the lexer stays
// the library's, this only records which keys each open object has seen.
class DuplicateKeySax : public nlohmann::json_sax<Json> {
public:
    bool duplicate() const { return duplicate_; }
    bool null() override { return true; }
    bool boolean(bool) override { return true; }
    bool number_integer(Json::number_integer_t) override { return true; }
    bool number_unsigned(Json::number_unsigned_t) override { return true; }
    bool number_float(Json::number_float_t, const std::string &) override { return true; }
    bool string(std::string &) override { return true; }
    bool binary(Json::binary_t &) override { return true; }
    bool start_object(std::size_t) override {
        keys_.emplace_back();
        return !duplicate_;
    }
    bool key(std::string &value) override {
        if (!keys_.empty() && !keys_.back().insert(value).second)
            duplicate_ = true;
        return !duplicate_;
    }
    bool end_object() override {
        if (!keys_.empty())
            keys_.pop_back();
        return true;
    }
    bool start_array(std::size_t) override { return !duplicate_; }
    bool end_array() override { return true; }
    bool parse_error(std::size_t, const std::string &, const nlohmann::detail::exception &) override { return false; }

private:
    std::vector<std::set<std::string>> keys_;
    bool duplicate_ = false;
};

bool HasDuplicateKeys(std::string_view bytes) {
    DuplicateKeySax sax;
    Json::sax_parse(bytes, &sax);
    return sax.duplicate();
}

std::expected<Json, std::string> ParseVersionEnvelopeImpl(std::string_view bytes) {
    if (bytes.empty() || bytes.size() > kMaxMessageBytes)
        return std::unexpected("control_too_large");
    if (!ValidUtf8(bytes))
        return std::unexpected("invalid_utf8_or_size");
    if (NestingDepth(bytes) > static_cast<int>(kMaxNestingDepth))
        return std::unexpected("json_too_deep");
    if (HasDuplicateKeys(bytes))
        return std::unexpected("duplicate_control_key");
    auto parsed = Json::parse(bytes, nullptr, false);
    if (parsed.is_discarded() || !parsed.is_object())
        return std::unexpected("invalid_control_json");
    return parsed;
}

std::expected<Json, std::string> ControlEnvelope(std::string_view bytes, int version, std::string_view type,
                                                std::span<const char *const> fields) {
    auto parsed = ParseVersionEnvelope(bytes);
    if (!parsed)
        return parsed;
    if (!(*parsed).contains("version") || !(*parsed)["version"].is_number_integer() || (*parsed)["version"] != version)
        return std::unexpected("version_incompatible");
    if (!(*parsed).contains("type") || !(*parsed)["type"].is_string() ||
        (*parsed)["type"].get_ref<const std::string &>() != type)
        return std::unexpected("invalid_control_type");
    // Closed field set: an extra field has no defined meaning on the control plane,
    // and duplicates were already rejected while parsing.
    if (parsed->size() != fields.size())
        return std::unexpected("unexpected_control_field");
    for (const auto *field : fields)
        if (!parsed->contains(field))
            return std::unexpected("missing_control_field");
    return parsed;
}

std::expected<std::string, std::string> HexField(const Json &object, std::string_view key, size_t bytes) {
    const std::string name(key);
    if (!object.contains(name) || !object.at(name).is_string())
        return std::unexpected("invalid_control_field");
    auto value = object.at(name).get<std::string>();
    if (!IsLowerHex(value, bytes))
        return std::unexpected("invalid_control_field");
    return value;
}

std::expected<std::string, std::string> BoundedTokenField(const Json &object, std::string_view key,
                                                          size_t maxLength) {
    const std::string name(key);
    if (!object.contains(name) || !object.at(name).is_string())
        return std::unexpected("invalid_control_field");
    const auto value = object.at(name).get<std::string>();
    if (value.empty() || value.size() > maxLength ||
        !std::ranges::all_of(value, [](unsigned char ch) {
            return (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '_';
        }))
        return std::unexpected("invalid_control_field");
    return value;
}

std::string DumpControl(const Json &message) {
    return message.dump(-1, ' ', false, Json::error_handler_t::strict);
}
}

bool IsLowerHex(std::string_view value, size_t bytes) {
    return value.size() == bytes * 2 &&
           std::ranges::all_of(value, [](char ch) { return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f'); });
}

std::expected<Json, std::string> ParseVersionEnvelope(std::string_view bytes) {
    auto parsed = ParseVersionEnvelopeImpl(bytes);
    if (!parsed)
        return parsed;

    // Every frame shares this envelope, and callers read these two fields without
    // checking types again: validating them here is what keeps a type mismatch from
    // reaching a throwing accessor on a live peer thread.
    if (!parsed->contains("version") || !(*parsed)["version"].is_number_integer())
        return std::unexpected("invalid_control_version");
    const auto &declared = (*parsed)["version"];
    // Read through the accessor that matches the stored type: a huge unsigned value or
    // a signed negative must fail here rather than throw out of a peer thread.
    const bool known = declared.is_number_unsigned()
                           ? declared.get<uint64_t>() == static_cast<uint64_t>(kProtocolVersion) ||
                                 declared.get<uint64_t>() == static_cast<uint64_t>(kControlVersion)
                           : declared.get<int64_t>() == kProtocolVersion || declared.get<int64_t>() == kControlVersion;
    if (!known)
        return std::unexpected("version_incompatible");
    if (!parsed->contains("type") || !(*parsed)["type"].is_string())
        return std::unexpected("invalid_control_type");
    if ((*parsed)["type"].get_ref<const std::string &>().size() > kMaxTypeBytes)
        return std::unexpected("invalid_control_type");
    return parsed;
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

std::expected<PairRequest, std::string> ParsePairRequest(std::string_view bytes) {
    // A bootstrap frame is tiny by construction. The 64 KiB first-frame transport
    // limit exists for legacy hello and must not become a bootstrap allowance.
    if (bytes.size() > kMaxControlBytes)
        return std::unexpected("pair_frame_too_large");
    constexpr std::array<const char *, 4> fields{"version", "type", "ext", "profile"};
    const auto parsed = ControlEnvelope(bytes, kProtocolVersion, kPairType, fields);
    if (!parsed)
        return std::unexpected(parsed.error());
    if (!(*parsed)["ext"].is_string() || (*parsed)["ext"].get_ref<const std::string &>() != kExtensionIdUtf8)
        return std::unexpected("extension_mismatch");
    const auto profile = HexField(*parsed, "profile", 16);
    if (!profile)
        return std::unexpected(profile.error());
    return PairRequest{*profile};
}

std::expected<PairingGrant, std::string> ParsePairedReply(std::string_view bytes) {
    if (bytes.size() > kMaxControlBytes)
        return std::unexpected("paired_frame_too_large");
    constexpr std::array<const char *, 7> fields{"version", "type", "wsVersion", "port",
                                                 "instance", "profile", "secret"};
    const auto parsed = ControlEnvelope(bytes, kProtocolVersion, kPairedType, fields);
    if (!parsed)
        return std::unexpected(parsed.error());
    if (!(*parsed)["wsVersion"].is_number_integer() || (*parsed)["wsVersion"] != kPairingWsVersion)
        return std::unexpected("incompatible_desktop");
    if (!(*parsed)["port"].is_number_integer() || (*parsed)["port"] < 1 || (*parsed)["port"] > 65535)
        return std::unexpected("invalid_control_field");
    const auto port = (*parsed)["port"].get<unsigned short>();
    PairingGrant grant;
    const auto instance = HexField(*parsed, "instance", 16);
    const auto profile = HexField(*parsed, "profile", 16);
    const auto secret = HexField(*parsed, "secret", 32);
    if (!instance || !profile || !secret)
        return std::unexpected("invalid_control_field");
    grant.port = static_cast<unsigned short>(port);
    grant.instance = *instance;
    grant.profile = *profile;
    grant.secret = *secret;
    return grant;
}

std::expected<std::string, std::string> ParsePairingError(std::string_view bytes) {
    if (bytes.size() > kMaxControlBytes)
        return std::unexpected("pairing_error_frame_too_large");
    constexpr std::array<const char *, 3> fields{"version", "type", "reason"};
    const auto parsed = ControlEnvelope(bytes, kProtocolVersion, kPairingErrorType, fields);
    if (!parsed)
        return std::unexpected(parsed.error());
    return BoundedTokenField(*parsed, "reason", 64);
}

std::expected<PairingChallenge, std::string> ParseChallenge(std::string_view bytes) {
    if (bytes.size() > kMaxControlBytes)
        return std::unexpected("control_frame_too_large");
    constexpr std::array<const char *, 4> fields{"version", "type", "instance", "nonce"};
    const auto parsed = ControlEnvelope(bytes, kControlVersion, kChallengeType, fields);
    if (!parsed)
        return std::unexpected(parsed.error());
    const auto instance = HexField(*parsed, "instance", 16);
    const auto nonce = HexField(*parsed, "nonce", 32);
    if (!instance || !nonce)
        return std::unexpected("invalid_control_field");
    return PairingChallenge{*instance, *nonce};
}

std::expected<AuthStartMessage, std::string> ParseAuthStart(std::string_view bytes) {
    if (bytes.size() > kMaxControlBytes)
        return std::unexpected("control_frame_too_large");
    constexpr std::array<const char *, 4> fields{"version", "type", "profile", "nonce"};
    const auto parsed = ControlEnvelope(bytes, kControlVersion, kAuthStartType, fields);
    if (!parsed)
        return std::unexpected(parsed.error());
    const auto profile = HexField(*parsed, "profile", 16);
    const auto nonce = HexField(*parsed, "nonce", 32);
    if (!profile || !nonce)
        return std::unexpected("invalid_control_field");
    return AuthStartMessage{*profile, *nonce};
}

std::expected<std::string, std::string> ParseAuthProof(std::string_view bytes, std::string_view type) {
    if (bytes.size() > kMaxControlBytes)
        return std::unexpected("control_frame_too_large");
    if (type != kAuthServerType && type != kAuthClientType)
        return std::unexpected("invalid_control_type");
    constexpr std::array<const char *, 3> fields{"version", "type", "proof"};
    const auto parsed = ControlEnvelope(bytes, kControlVersion, type, fields);
    if (!parsed)
        return std::unexpected(parsed.error());
    return HexField(*parsed, "proof", 32);
}

bool ParseControlAck(std::string_view bytes, std::string_view type) {
    if (bytes.size() > kMaxControlBytes)
        return false;
    if (type != kAuthOkType && type != kPingType && type != kPongType)
        return false;
    constexpr std::array<const char *, 2> fields{"version", "type"};
    return ControlEnvelope(bytes, kControlVersion, type, fields).has_value();
}

std::string BuildPairRequest(std::string_view extensionId, std::string_view profile) {
    return DumpControl({{"version", kProtocolVersion},
                        {"type", std::string(kPairType)},
                        {"ext", std::string(extensionId)},
                        {"profile", std::string(profile)}});
}

std::string BuildPairedReply(unsigned short port, std::string_view instance, std::string_view profile,
                             std::string_view secret) {
    return DumpControl({{"version", kProtocolVersion},
                        {"type", std::string(kPairedType)},
                        {"wsVersion", kPairingWsVersion},
                        {"port", static_cast<int>(port)},
                        {"instance", std::string(instance)},
                        {"profile", std::string(profile)},
                        {"secret", std::string(secret)}});
}

std::string BuildPairingError(std::string_view reason) {
    return DumpControl({{"version", kProtocolVersion},
                        {"type", std::string(kPairingErrorType)},
                        {"reason", std::string(reason)}});
}

std::string BuildChallenge(std::string_view instance, std::string_view nonce) {
    return DumpControl({{"version", kControlVersion},
                        {"type", std::string(kChallengeType)},
                        {"instance", std::string(instance)},
                        {"nonce", std::string(nonce)}});
}

std::string BuildAuthStart(std::string_view profile, std::string_view nonce) {
    return DumpControl({{"version", kControlVersion},
                        {"type", std::string(kAuthStartType)},
                        {"profile", std::string(profile)},
                        {"nonce", std::string(nonce)}});
}

std::string BuildAuthProof(std::string_view type, std::string_view proof) {
    return DumpControl(
        {{"version", kControlVersion}, {"type", std::string(type)}, {"proof", std::string(proof)}});
}

std::string BuildControlAck(std::string_view type) {
    return DumpControl({{"version", kControlVersion}, {"type", std::string(type)}});
}

std::expected<std::string, std::string> ReadFrameBytes(HANDLE pipe, DWORD timeoutMs, std::stop_token stop,
                                                PipeIo io) {
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
    return bytes;
}

bool WriteFrameBytes(HANDLE pipe, std::string_view bytes, DWORD timeoutMs, std::stop_token stop, PipeIo io) {
    if (bytes.empty() || bytes.size() > kMaxMessageBytes)
        return false;
    const auto start = GetTickCount64();
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
    // Transfer writes through a mutable span, exactly like the v1 writer did. The
    // payload is a named local because a span over a temporary would dangle.
    std::string payload(bytes);
    return Transfer(pipe, payload, true, remaining, stop, io);
}
std::expected<Json, std::string> ReadMessage(HANDLE pipe, DWORD timeoutMs, std::stop_token stop, PipeIo io) {
    const auto bytes = ReadFrameBytes(pipe, timeoutMs, stop, io);
    if (!bytes)
        return std::unexpected(bytes.error());
    return ParseMessage(*bytes);
}

bool WriteMessage(HANDLE pipe, const Json &message, DWORD timeoutMs, std::stop_token stop, PipeIo io) {
    std::string bytes;
    try {
        bytes = message.dump(-1, ' ', false, Json::error_handler_t::strict);
    } catch (const Json::exception &) {
        return false;
    }
    if (!ParseMessage(bytes))
        return false;
    return WriteFrameBytes(pipe, bytes, timeoutMs, stop, io);
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
