

#include "stdafx.h"

#ifdef _WIN32
#pragma comment(lib, "Ws2_32.lib")
#define SOCKET_ERRNO WSAGetLastError()
#define ERRNO GetLastError()
#else
#define SOCKET_ERRNO errno
#define ERRNO errno
#define closesocket close
#endif
#include <io.h>
#include <fcntl.h>
#include <conio.h>
#include <errno.h>


extern float viewAngle;

int listeningForMsg();

