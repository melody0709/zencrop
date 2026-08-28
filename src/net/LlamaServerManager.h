#pragma once
#include "core/PaddleVlServerService.h"

using LlamaServerLaunchDiagnostics = PaddleVlServerLaunchDiagnostics;

class LlamaServerManager final : public PaddleVlServerService {
public:
    static LlamaServerManager& Instance();

    bool EnsureServerStarted() override;
    void BeginRequest() override;
    void EndRequest() override;
    void RefreshIdleShutdown();
    void StopServer();
    void GlobalShutdown();
    int GetPort() const override;

    bool IsServerRunning();
    bool FindServerExe(std::wstring& exePath) override;
    bool FindModelFiles(std::wstring& modelPath, std::wstring& mmprojPath) override;
    const std::string& GetModelName() const override { return m_modelName; }
    const LlamaServerLaunchDiagnostics& GetLaunchDiagnostics() const override {
        return m_launchDiagnostics;
    }

    // Stage3 3-A: break net→ocr_engine cycle.
    // Engine registers layout cleanup; Manager never includes engine headers.
    void SetShutdownHook(ShutdownHook hook) override;

private:
    LlamaServerManager();
    ~LlamaServerManager();
    LlamaServerManager(const LlamaServerManager&) = delete;
    LlamaServerManager& operator=(const LlamaServerManager&) = delete;

    bool StartServer();
    bool WaitForServerReady(int timeoutMs);
    int FindFreePort();
    void ExtractModelName(const std::wstring& modelPath);
    static DWORD WINAPI IdleShutdownThread(LPVOID param);
    void ScheduleIdleShutdownLocked(int timeoutMs);
    void StopServerLocked();
    bool StopServerIfIdle(unsigned int generation, int timeoutMs);
    int GetIdleTimeoutMs() const;
    void EnsureJobObject();
    void RunShutdownHook();

    PROCESS_INFORMATION m_procInfo = {};
    int m_port = 0;
    bool m_serverStarted = false;
    bool m_serverFailed = false;
    bool m_starting = false;
    int m_activeRequests = 0;
    unsigned int m_idleGeneration = 0;
    ULONGLONG m_lastActivityTick = 0;
    CRITICAL_SECTION m_cs;
    bool m_csInitialized = false;
    std::wstring m_lastModelDir;
    std::wstring m_lastPortStr;
    std::string m_modelName;
    LlamaServerLaunchDiagnostics m_launchDiagnostics;
    HANDLE m_hJob = nullptr;  // Job Object 绑定子进程生命期，ZenCrop 异常退出时一并 kill
    ShutdownHook m_shutdownHook = nullptr;
};
