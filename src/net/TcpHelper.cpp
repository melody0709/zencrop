#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

bool TcpPortIsOpen(const wchar_t* host, int port, int timeoutMs) {
    SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INVALID_SOCKET)
        return false;

    u_long mode = 1;
    ioctlsocket(sock, FIONBIO, &mode);

    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons((u_short)port);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

    connect(sock, (sockaddr*)&addr, sizeof(addr));

    fd_set fd;
    FD_ZERO(&fd);
    FD_SET(sock, &fd);
    timeval tv = { timeoutMs / 1000, (timeoutMs % 1000) * 1000 };
    bool result = (select(0, nullptr, &fd, nullptr, &tv) == 1);

    closesocket(sock);
    return result;
}
