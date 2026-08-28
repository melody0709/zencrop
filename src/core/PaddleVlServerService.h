#pragma once

#include <windows.h>

#include <cstdint>
#include <string>

// Process-server capability consumed by Paddle OCR engines. The net layer
// owns its implementation; application bootstrap injects it into engines.
struct PaddleVlServerLaunchDiagnostics {
    std::wstring serverVersion;
    std::wstring backend;
    std::wstring serverExePath;
    std::wstring modelPath;
    std::wstring mmprojPath;
    std::wstring chatTemplatePath;
    std::wstring modelSha256;
    std::wstring mmprojSha256;
    bool modelSha256CacheHit = false;
    bool mmprojSha256CacheHit = false;
    DWORD sha256Ms = 0;
    uint64_t modelBytes = 0;
    uint64_t mmprojBytes = 0;
    uint64_t imageMinPixels = 0;
    uint64_t imageMaxPixels = 0;
};

class PaddleVlServerService {
public:
    using ShutdownHook = void (*)();
    virtual ~PaddleVlServerService() = default;
    virtual bool EnsureServerStarted() = 0;
    virtual void BeginRequest() = 0;
    virtual void EndRequest() = 0;
    virtual int GetPort() const = 0;
    virtual bool FindServerExe(std::wstring& exePath) = 0;
    virtual bool FindModelFiles(std::wstring& modelPath, std::wstring& mmprojPath) = 0;
    virtual const std::string& GetModelName() const = 0;
    virtual const PaddleVlServerLaunchDiagnostics& GetLaunchDiagnostics() const = 0;
    virtual void SetShutdownHook(ShutdownHook hook) = 0;
};
