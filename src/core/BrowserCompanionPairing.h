#pragma once

// Transport credentials for the authenticated direct browser connection.
//
// WHAT THIS IS
// ------------
// Per-browser-profile secrets the desktop hands out exactly once, during an
// explicit pairing bootstrap, and never transmits over the direct connection
// itself. Storage is DPAPI-encrypted under the current user with an owner-only
// ACL; the file name carries a hash of this installation's normalized executable
// path, so a moved directory is a different instance and must pair again.
//
// WHY IT IS NOT IN Settings
// -------------------------
// These are transport credentials, not user preferences: no Settings section, no
// JSON settings merge, no UI round-trip. A damaged store therefore fails closed
// and is only recoverable through an explicit revocation - it never silently
// rewrites itself, because a silent reset would turn "the desktop forgot me" into
// "the desktop accepts anything".

#include <expected>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace zencrop::browser_pairing {

inline constexpr size_t kMaxPairings = 32;
inline constexpr size_t kProfileBytes = 16;   // per browser profile id
inline constexpr size_t kSecretBytes = 32;    // HMAC-SHA256 key
inline constexpr size_t kNonceBytes = 32;     // challenge nonce / proof

bool IsLowerHex(std::string_view value, size_t bytes);
std::string RandomHex(size_t bytes);
std::string Hmac(std::string_view secretHex, std::string_view message);
bool EqualProof(std::string_view expected, std::string_view supplied);

struct Credential {
    std::string profile;
    std::string secret;
    std::wstring browserImage;
};

// %LOCALAPPDATA%\ZenCrop\browser-websocket-pairing-<install hash>.dat, honouring
// ZENCROP_DATA_DIR / portable decisions made by AppDataPaths. Empty when the data
// directory cannot be resolved: the caller must fail instead of falling back.
std::wstring DefaultStorePath();

class Store {
public:
    explicit Store(std::wstring path = DefaultStorePath());
    ~Store();
    Store(const Store&) = delete;
    Store& operator=(const Store&) = delete;

    // Reads (or initializes) the store. A damaged file keeps an error and refuses
    // pairing until RevokeAll() is called.
    std::expected<void, std::string> Load();
    // Issues a fresh secret for one profile, replacing only that profile's entry.
    // The secret is returned to the caller exactly once, for the bootstrap reply.
    std::expected<Credential, std::string> Pair(std::string profile, std::wstring browserImage);
    std::optional<Credential> Find(std::string_view profile, std::wstring_view browserImage) const;
    // True while `secret` is still the current secret for that profile and image. This
    // is the live-connection test: re-pairing one profile rotates only its own secret,
    // so only that profile's connections fail it, while RevokeAll() fails all of them.
    bool Matches(std::string_view profile, std::wstring_view browserImage, std::string_view secret) const;
    // Rotates the instance identity and drops every credential. This is the only
    // way out of a damaged store.
    std::expected<void, std::string> RevokeAll();

    std::string InstanceId() const;
    size_t Count() const;
    size_t CountForBrowser(std::wstring_view basename) const;
    std::string Error() const;
    const std::wstring &Path() const { return path_; }

private:
    std::expected<void, std::string> Save(const std::vector<Credential> &credentials, const std::string &instance);

    std::wstring path_;
    // Writers serialize disk commits; readers only lock the committed in-memory state.
    std::mutex writeMutex_;
    mutable std::mutex mutex_;
    std::string instance_;
    std::string error_;
    std::vector<Credential> credentials_;
};

} // namespace zencrop::browser_pairing
