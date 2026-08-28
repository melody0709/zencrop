#pragma once
#include <string>

class MiniHttpServer {
public:
    static MiniHttpServer& Instance();

    bool Start(unsigned short port = 28080);
    void Stop();
    bool IsRunning() const;
    unsigned short GetPort() const;

private:
    MiniHttpServer();
    ~MiniHttpServer();

    static unsigned long WINAPI ServerThread(void* param);

    void* m_listenSocket = (void*)(long long)-1;
    void* m_hThread = nullptr;
    volatile bool m_running = false;
    unsigned short m_port = 28080;
    void* m_cs = nullptr;
    bool m_csInit = false;
};
