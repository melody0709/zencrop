#pragma once

#include "BrowserCompanionProtocol.h"
#include <memory>
#include <stop_token>
#include <string>

namespace browser_companion {
class Bridge {
public:
    static Bridge &Instance();
    bool Start();
    void Stop();
    std::expected<Json, std::string> Query(HWND window, const RECT &viewport, POINT point, const std::string &session,
                                           const std::string &request, bool frozen, std::stop_token stop,
                                           std::span<const RECT> alternatives = {}, bool foregroundAtCapture = false);
    void End(const std::string &session);
    bool IsValid(const std::string &session, const std::string &document) const;
    bool IsConnectedBrowser(DWORD pid) const;

private:
    Bridge();
    ~Bridge();
    struct State;
    std::unique_ptr<State> m_state;
};

bool IsNativeInvocation(std::span<const std::wstring> args);
int RunNativeHost(std::span<const std::wstring> args);
}
