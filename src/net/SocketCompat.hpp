#pragma once
// SocketCompat : les sockets de Windows (Winsock) et ceux d'Android (POSIX)
// se ressemblent beaucoup. Le code réseau est écrit avec les noms de
// Windows ; ailleurs, ce fichier les traduit.

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <cerrno>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

// Sous POSIX, un socket est un simple descripteur de fichier (un int).
using SOCKET = int;
using BOOL = int;
using u_long = unsigned long;
constexpr SOCKET INVALID_SOCKET = -1;
constexpr BOOL TRUE = 1;
constexpr BOOL FALSE = 0;

inline int closesocket(SOCKET s)
{
    return ::close(s);
}

inline int ioctlsocket(SOCKET s, unsigned long command, u_long* argument)
{
    int value = static_cast<int>(*argument);
    return ::ioctl(s, static_cast<int>(command), &value);
}
#endif
