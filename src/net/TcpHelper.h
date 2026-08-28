#pragma once
#include <windows.h>

bool TcpPortIsOpen(const wchar_t* host, int port, int timeoutMs);
