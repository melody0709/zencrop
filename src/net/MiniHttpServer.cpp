#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include "MiniHttpServer.h"
#include "OcrUtils.h"
#include "core/WideStringUtils.h"
#include "core/NarrowStringUtils.h"
#include <shlwapi.h>
#include <fstream>
#include <sstream>
#include <algorithm>

#pragma comment(lib, "ws2_32.lib")

static bool SendAll(SOCKET sock, const char* data, int len) {
    int sent = 0;
    while (sent < len) {
        int n = send(sock, data + sent, len - sent, 0);
        if (n == SOCKET_ERROR) return false;
        sent += n;
    }
    return true;
}

struct CsWrapper {
    CRITICAL_SECTION cs;
};

static bool CanonicalizeHttpPath(std::wstring path, std::wstring& out) {
    for (wchar_t& ch : path) {
        if (ch == L'/') ch = L'\\';
    }
    wchar_t canonical[MAX_PATH] = {};
    if (!PathCanonicalizeW(canonical, path.c_str())) return false;
    out = canonical;
    return !out.empty();
}

// OWN-79: thin wrapper over pure WideStringUtils.
static std::wstring LowerHttpPath(std::wstring value) {
    return WideToLower(std::move(value));
}

static bool IsPathUnderHttpRoot(const std::wstring& path, const std::wstring& root) {
    std::wstring fullPath = LowerHttpPath(path);
    std::wstring fullRoot = LowerHttpPath(root);
    while (!fullRoot.empty() && (fullRoot.back() == L'\\' || fullRoot.back() == L'/')) {
        fullRoot.pop_back();
    }
    return fullPath.size() >= fullRoot.size() &&
        fullPath.rfind(fullRoot, 0) == 0 &&
        (fullPath.size() == fullRoot.size() ||
         fullPath[fullRoot.size()] == L'\\' ||
         fullPath[fullRoot.size()] == L'/');
}

MiniHttpServer::MiniHttpServer() {
    auto* csw = new CsWrapper();
    InitializeCriticalSection(&csw->cs);
    m_cs = csw;
    m_csInit = true;
}

MiniHttpServer::~MiniHttpServer() {
    Stop();
    if (m_csInit && m_cs) {
        auto* csw = static_cast<CsWrapper*>(m_cs);
        DeleteCriticalSection(&csw->cs);
        delete csw;
        m_cs = nullptr;
        m_csInit = false;
    }
}

MiniHttpServer& MiniHttpServer::Instance() {
    static MiniHttpServer instance;
    return instance;
}

bool MiniHttpServer::Start(unsigned short port) {
    auto* csw = static_cast<CsWrapper*>(m_cs);
    EnterCriticalSection(&csw->cs);
    if (m_running) {
        LeaveCriticalSection(&csw->cs);
        return true;
    }
    LeaveCriticalSection(&csw->cs);

    m_port = port;

    for (int attempt = 0; attempt < 10; attempt++) {
        unsigned short tryPort = (attempt == 0) ? m_port : m_port + attempt;

        SOCKET listenSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (listenSock == INVALID_SOCKET) {
            continue;
        }

        int reuse = 1;
        setsockopt(listenSock, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse));

        sockaddr_in addr = {};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = htons(tryPort);

        if (bind(listenSock, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
            closesocket(listenSock);
            continue;
        }

        if (listen(listenSock, 5) == SOCKET_ERROR) {
            closesocket(listenSock);
            continue;
        }

        m_listenSocket = (void*)(long long)listenSock;
        m_port = tryPort;
        break;
    }

    if (m_listenSocket == (void*)(long long)-1) {
        SOCKET listenSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (listenSock != INVALID_SOCKET) {
            int reuse = 1;
            setsockopt(listenSock, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse));

            sockaddr_in addr = {};
            addr.sin_family = AF_INET;
            addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            addr.sin_port = htons(0);

            if (bind(listenSock, (sockaddr*)&addr, sizeof(addr)) != SOCKET_ERROR &&
                listen(listenSock, 5) != SOCKET_ERROR) {
                sockaddr_in boundAddr = {};
                int addrLen = sizeof(boundAddr);
                getsockname(listenSock, (sockaddr*)&boundAddr, &addrLen);
                m_port = ntohs(boundAddr.sin_port);
                m_listenSocket = (void*)(long long)listenSock;
            } else {
                closesocket(listenSock);
            }
        }
    }

    if (m_listenSocket == (void*)(long long)-1) {
        OutputDebugStringA("[MiniHttp] Failed to bind any port\n");
        return false;
    }

    EnterCriticalSection(&csw->cs);
    m_running = true;
    LeaveCriticalSection(&csw->cs);

    m_hThread = CreateThread(nullptr, 0, ServerThread, this, 0, nullptr);
    if (!m_hThread) {
        EnterCriticalSection(&csw->cs);
        m_running = false;
        LeaveCriticalSection(&csw->cs);
        SOCKET listenSock = (SOCKET)(long long)m_listenSocket;
        closesocket(listenSock);
        m_listenSocket = (void*)(long long)-1;
        return false;
    }

    // OWN-116: pure narrow debug (NarrowStringUtils).
    OutputDebugStringA(NarrowFormatMiniHttpStarted(m_port).c_str());
    return true;
}

void MiniHttpServer::Stop() {
    auto* csw = static_cast<CsWrapper*>(m_cs);
    EnterCriticalSection(&csw->cs);
    m_running = false;
    LeaveCriticalSection(&csw->cs);

    if (m_listenSocket != (void*)(long long)-1) {
        SOCKET listenSock = (SOCKET)(long long)m_listenSocket;
        closesocket(listenSock);
        m_listenSocket = (void*)(long long)-1;
    }

    if (m_hThread) {
        WaitForSingleObject(m_hThread, 3000);
        CloseHandle(m_hThread);
        m_hThread = nullptr;
    }

    OutputDebugStringA("[MiniHttp] Stopped\n");
}

bool MiniHttpServer::IsRunning() const {
    return m_running;
}

unsigned short MiniHttpServer::GetPort() const {
    return m_port;
}

unsigned long WINAPI MiniHttpServer::ServerThread(void* param) {
    auto* self = (MiniHttpServer*)param;
    auto* csw = static_cast<CsWrapper*>(self->m_cs);
    SOCKET listenSock = (SOCKET)(long long)self->m_listenSocket;

    while (self->m_running) {
        fd_set readFds;
        FD_ZERO(&readFds);
        FD_SET(listenSock, &readFds);

        timeval timeout = {};
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;

        int selectResult = select(0, &readFds, nullptr, nullptr, &timeout);
        if (selectResult == SOCKET_ERROR || !self->m_running) break;
        if (selectResult == 0) continue;

        sockaddr_in clientAddr = {};
        int addrLen = sizeof(clientAddr);
        SOCKET clientSocket = accept(listenSock, (sockaddr*)&clientAddr, &addrLen);
        if (clientSocket == INVALID_SOCKET) continue;

        if (clientAddr.sin_addr.s_addr != htonl(INADDR_LOOPBACK)) {
            closesocket(clientSocket);
            continue;
        }

        char buf[4096] = {};
        int received = recv(clientSocket, buf, sizeof(buf) - 1, 0);
        if (received <= 0) {
            closesocket(clientSocket);
            continue;
        }

        std::string request(buf, received);

        if (request.find("GET ") != 0) {
            const char* resp = "HTTP/1.1 405 Method Not Allowed\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
            SendAll(clientSocket, resp, (int)strlen(resp));
            closesocket(clientSocket);
            continue;
        }

        size_t pathStart = 4;
        size_t pathEnd = request.find(' ', pathStart);
        if (pathEnd == std::string::npos) {
            closesocket(clientSocket);
            continue;
        }

        std::string fullPath = request.substr(pathStart, pathEnd - pathStart);

        std::wstring filePath;

        size_t queryPos = fullPath.find("?path=");
        if (queryPos != std::string::npos) {
            std::string encodedPath = fullPath.substr(queryPos + 6);

            std::string decodedPath;
            for (size_t i = 0; i < encodedPath.length(); i++) {
                if (encodedPath[i] == '%' && i + 2 < encodedPath.length()) {
                    char hex[3] = { encodedPath[i + 1], encodedPath[i + 2], 0 };
                    decodedPath += (char)strtol(hex, nullptr, 16);
                    i += 2;
                } else if (encodedPath[i] == '+') {
                    decodedPath += ' ';
                } else {
                    decodedPath += encodedPath[i];
                }
            }

            int wlen = MultiByteToWideChar(CP_UTF8, 0, decodedPath.c_str(), (int)decodedPath.length(), nullptr, 0);
            if (wlen > 0) {
                filePath.resize(wlen);
                MultiByteToWideChar(CP_UTF8, 0, decodedPath.c_str(), (int)decodedPath.length(), &filePath[0], wlen);
            }
        }

        if (filePath.empty()) {
            const char* resp = "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
            SendAll(clientSocket, resp, (int)strlen(resp));
            closesocket(clientSocket);
            continue;
        }

        std::wstring canonicalPathString;
        if (!CanonicalizeHttpPath(filePath, canonicalPathString)) {
            const char* resp = "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
            SendAll(clientSocket, resp, (int)strlen(resp));
            closesocket(clientSocket);
            continue;
        }
        const wchar_t* canonicalPath = canonicalPathString.c_str();

        std::wstring ocrDir = GetOcrImageDir();
        std::wstring canonicalOcrDir;
        bool allowed = CanonicalizeHttpPath(ocrDir, canonicalOcrDir) &&
            IsPathUnderHttpRoot(canonicalPathString, canonicalOcrDir);

        if (!allowed) {
            const char* resp = "HTTP/1.1 403 Forbidden\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
            SendAll(clientSocket, resp, (int)strlen(resp));
            closesocket(clientSocket);
            continue;
        }

        // OWN-94: pure allowed image-ext check (WideStringUtils).
        if (!WideIsAllowedImageExtension(filePath)) {
            const char* resp = "HTTP/1.1 403 Forbidden\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
            SendAll(clientSocket, resp, (int)strlen(resp));
            closesocket(clientSocket);
            continue;
        }

        if (GetFileAttributesW(canonicalPath) == INVALID_FILE_ATTRIBUTES) {
            const char* resp = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
            SendAll(clientSocket, resp, (int)strlen(resp));
            closesocket(clientSocket);
            continue;
        }

        std::ifstream file(canonicalPath, std::ios::binary | std::ios::ate);
        if (!file.is_open()) {
            const char* resp = "HTTP/1.1 500 Internal Server Error\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
            SendAll(clientSocket, resp, (int)strlen(resp));
            closesocket(clientSocket);
            continue;
        }

        auto fileSize = file.tellg();
        file.seekg(0, std::ios::beg);

        if (fileSize > 10 * 1024 * 1024) {
            const char* resp = "HTTP/1.1 413 Payload Too Large\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
            SendAll(clientSocket, resp, (int)strlen(resp));
            closesocket(clientSocket);
            continue;
        }

        std::vector<char> fileData((size_t)fileSize);
        file.read(fileData.data(), fileSize);
        file.close();

        // OWN-94: pure ext compare for MIME (WideStringUtils).
        const std::wstring ext = WideExtensionFromPath(canonicalPath);
        std::string mimeType = "application/octet-stream";
        if (WideEqualsNoCase(ext, L".jpg") || WideEqualsNoCase(ext, L".jpeg"))
            mimeType = "image/jpeg";
        else if (WideEqualsNoCase(ext, L".png"))
            mimeType = "image/png";
        else if (WideEqualsNoCase(ext, L".gif"))
            mimeType = "image/gif";
        else if (WideEqualsNoCase(ext, L".bmp"))
            mimeType = "image/bmp";
        else if (WideEqualsNoCase(ext, L".webp"))
            mimeType = "image/webp";

        std::string header = "HTTP/1.1 200 OK\r\n";
        header += "Content-Type: " + mimeType + "\r\n";
        header += "Content-Length: " + std::to_string(fileData.size()) + "\r\n";
        header += "Connection: close\r\n";
        header += "Cache-Control: max-age=3600\r\n";
        header += "\r\n";

        SendAll(clientSocket, header.c_str(), (int)header.length());
        SendAll(clientSocket, fileData.data(), (int)fileData.size());
        closesocket(clientSocket);
    }

    return 0;
}
