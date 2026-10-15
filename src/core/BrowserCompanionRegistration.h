#pragma once

#include <string>
#include <expected>

namespace zencrop {

// Per-browser native messaging host state. Chrome and Edge are classified independently:
// one browser being ready never stands in for the other.
enum class BrowserCompanionHostState {
    NotRegistered,   // Neither HKCU nor HKLM has the native messaging host key
    Ready,           // Key resolves to this instance's manifest, which exists on disk
    PathMismatch,    // Key resolves to a different, existing manifest (another instance)
    TargetMissing    // Key exists, but the manifest it names is not on disk
};

struct BrowserCompanionBridgeInfo {
    BrowserCompanionHostState chrome = BrowserCompanionHostState::NotRegistered;
    BrowserCompanionHostState edge = BrowserCompanionHostState::NotRegistered;
};

// Gets the expected manifest path for the running instance:
// <exe_dir>\native-messaging\com.zencrop.browser_companion.json
std::wstring GetBrowserCompanionManifestPath();

// Classifies one browser's host registration without touching the registry.
// registeredManifestPath is that host key's default value; empty means the key is absent.
// Paths are canonicalized before comparison, so separator, case, ".." and 8.3 short-name
// spelling differences do not read as a mismatch.
BrowserCompanionHostState ClassifyBrowserCompanionHost(
    const std::wstring& registeredManifestPath,
    const std::wstring& localManifestPath);

// Queries HKCU before HKLM, 32-bit before 64-bit within each root.
// Edge also tries Chromium and Chrome keys, in browser search order.
// Strictly read-only: never writes or modifies registry keys.
BrowserCompanionBridgeInfo QueryBrowserCompanionBridge();

// Registers an installed runtime's manifest for the current user, without elevation.
// Shares the ownership record with register_browser_companion.ps1; refuses unowned keys.
std::expected<void, std::wstring> RegisterBrowserCompanionBridge(const std::wstring& manifestPath);

} // namespace zencrop
