#pragma once

#include <windows.h>
#include <nlohmann/json.hpp>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <stop_token>

namespace browser_companion {
using Json = nlohmann::json;
inline constexpr size_t kMaxMessageBytes = 65536;
inline constexpr size_t kMaxCandidates = 64;
inline constexpr int kProtocolVersion = 1;

// Transport control plane. Pairing bootstrap and the authenticated direct
// connection carry their own version so the v1 selection parser keeps rejecting
// exactly what it always rejected: splitting framing from validation must not
// widen the accepted message set.
inline constexpr int kControlVersion = 2;
inline constexpr int kPairingWsVersion = 2;
inline constexpr size_t kMaxControlBytes = 1024;
inline constexpr size_t kMaxPairings = 32;
inline constexpr size_t kMaxNestingDepth = 32;
// Control type names are a closed, tiny set; anything longer is not one of them.
inline constexpr size_t kMaxTypeBytes = 64;

// Native messaging identity. The argv/manifest URI form keeps its trailing slash.
inline constexpr std::wstring_view kExtensionOrigin = L"chrome-extension://halonieocgkgbdogndcipmlpklljmoja/";
// HTTP Origin header form (RFC 6454): no trailing slash. Compared exactly, never
// as a prefix, so no other extension or web page can satisfy it.
inline constexpr std::string_view kExtensionHandshakeOrigin = "chrome-extension://halonieocgkgbdogndcipmlpklljmoja";
inline constexpr std::string_view kExtensionIdUtf8 = "halonieocgkgbdogndcipmlpklljmoja";
inline constexpr std::string_view kNativeHostName = "com.zencrop.browser_companion";

// Wire format is fixed (see the plan, 10.2). Every field below is required, typed
// and closed: an unknown key, a duplicated key or a wrong type is a rejection, not
// a value to be guessed.
inline constexpr std::string_view kPairType = "pair";
inline constexpr std::string_view kPairedType = "paired";
inline constexpr std::string_view kPairingErrorType = "pairing_error";
inline constexpr std::string_view kChallengeType = "challenge";
inline constexpr std::string_view kAuthStartType = "auth_start";
inline constexpr std::string_view kAuthServerType = "auth_server";
inline constexpr std::string_view kAuthClientType = "auth_client";
inline constexpr std::string_view kAuthOkType = "auth_ok";
inline constexpr std::string_view kPingType = "ping";
inline constexpr std::string_view kPongType = "pong";

struct PairRequest {
    std::string profile;
};
struct PairingGrant {
    unsigned short port = 0;
    std::string instance;
    std::string profile;
    std::string secret;
};
struct PairingChallenge {
    std::string instance;
    std::string nonce;
};
struct AuthStartMessage {
    std::string profile;
    std::string nonce;
};

// Lower-case hex of exactly `bytes` bytes. Shared shape with the credential store,
// duplicated per layer on purpose: core must not include net.
bool IsLowerHex(std::string_view value, size_t bytes);

// v1 selection protocol. Unchanged behaviour, including which messages it refuses.
std::expected<Json, std::string> ParseMessage(std::string_view bytes);

// Structural envelope for both planes: bounded size, bounded nesting, unique keys,
// object with an integer `version` and a string `type`. Use it to decide which plane
// a frame belongs to; it does not by itself accept any message.
std::expected<Json, std::string> ParseVersionEnvelope(std::string_view bytes);

// Control-plane schemas. Bootstrap messages are version 1 (they travel the native
// channel), direct-connection messages are version 2.
std::expected<PairRequest, std::string> ParsePairRequest(std::string_view bytes);
std::expected<PairingGrant, std::string> ParsePairedReply(std::string_view bytes);
std::expected<std::string, std::string> ParsePairingError(std::string_view bytes);
std::expected<PairingChallenge, std::string> ParseChallenge(std::string_view bytes);
std::expected<AuthStartMessage, std::string> ParseAuthStart(std::string_view bytes);
std::expected<std::string, std::string> ParseAuthProof(std::string_view bytes, std::string_view type);
bool ParseControlAck(std::string_view bytes, std::string_view type);

std::string BuildPairRequest(std::string_view extensionId, std::string_view profile);
std::string BuildPairedReply(unsigned short port, std::string_view instance, std::string_view profile,
                             std::string_view secret);
std::string BuildPairingError(std::string_view reason);
std::string BuildChallenge(std::string_view instance, std::string_view nonce);
std::string BuildAuthStart(std::string_view profile, std::string_view nonce);
std::string BuildAuthProof(std::string_view type, std::string_view proof);
std::string BuildControlAck(std::string_view type);

enum class PipeIo { Synchronous, Overlapped };
// Framing only: a 4-byte little-endian length plus that many UTF-8 JSON bytes,
// bounded by kMaxMessageBytes and one shared deadline for header and body. Nothing
// here decides what a message means, so the bootstrap and direct-connection planes
// can use it without touching the v1 message set.
std::expected<std::string, std::string> ReadFrameBytes(HANDLE pipe, DWORD timeoutMs = INFINITE,
                                                       std::stop_token stop = {},
                                                       PipeIo io = PipeIo::Synchronous);
bool WriteFrameBytes(HANDLE pipe, std::string_view bytes, DWORD timeoutMs = INFINITE, std::stop_token stop = {},
                     PipeIo io = PipeIo::Synchronous);
// v1 wrappers over the two above, byte-for-byte equivalent to the original
// combined implementation.
std::expected<Json, std::string> ReadMessage(HANDLE pipe, DWORD timeoutMs = INFINITE, std::stop_token stop = {},
                                             PipeIo io = PipeIo::Synchronous);
bool WriteMessage(HANDLE pipe, const Json &message, DWORD timeoutMs = INFINITE, std::stop_token stop = {},
                  PipeIo io = PipeIo::Synchronous);
bool MatchWindowBounds(const Json &window, const RECT &logical, const RECT &physical, const RECT &monitorWork);
std::expected<RECT, std::string> MapCandidate(const Json &rect, const RECT &viewport, double scale, POINT point);
}
