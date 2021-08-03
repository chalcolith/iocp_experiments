#include "pch.h"
#include <stdio.h>

extern int tsprintf(const char* format, ...);
extern void printwindowserror(int err);

extern bool g_running;
extern bool g_client_can_connect;

extern LPFN_CONNECTEX g_ConnectEx;

extern char* g_serverHost;
extern char* g_serverPort;
extern addrinfo* g_serverAddress;

static bool connecting = false;
static SOCKET client_socket = INVALID_SOCKET;
static bool can_send = true;

enum class iocp_info_kind_t
{
  IOCP_KIND_CONNECT = 0,
  IOCP_KIND_SEND = 1
};

constexpr size_t IOCP_ACCEPT_ADDR_LEN = sizeof(struct sockaddr_storage) + 16;

typedef struct iocp_info_t
{
  OVERLAPPED ov;
  iocp_info_kind_t kind;
  SOCKET socket;
  char buf[IOCP_ACCEPT_ADDR_LEN * 2];
} iocp_info_t;

static iocp_info_t* alloc_iocp(iocp_info_kind_t kind, SOCKET socket)
{
  iocp_info_t* info = (iocp_info_t*)malloc(sizeof(iocp_info_t));
  if (info != NULL)
  {
    memset(info, 0, sizeof(iocp_info_t));
    info->kind = kind;
    info->socket = socket;
  }
  return info;
}

static bool complete_connect(SOCKET s);
static bool start_send();

static void print_wsa_error(SOCKET socket, LPOVERLAPPED overlapped)
{
  DWORD numBytes;
  DWORD flags;
  WSAGetOverlappedResult(socket, overlapped, &numBytes, false, &flags);
  DWORD error = GetLastError();
  tsprintf("%x ", error);
  printwindowserror(error);
}

static void __stdcall client_completion_routine(DWORD errorCode, DWORD numBytes, LPOVERLAPPED overlapped)
{
  iocp_info_t* info = (iocp_info_t*)overlapped;
  switch (info->kind)
  {
  case iocp_info_kind_t::IOCP_KIND_CONNECT:
    if (errorCode == ERROR_SUCCESS)
    {
      tsprintf("Client: socket %d connected\n", info->socket);
      complete_connect(info->socket);
      client_socket = info->socket;
    }
    else
    {
      tsprintf("Client: error connecting socket %d:\n", info->socket);
      print_wsa_error(info->socket, overlapped);
    }
    break;
  case iocp_info_kind_t::IOCP_KIND_SEND:
    if (errorCode == ERROR_SUCCESS)
    {
      tsprintf("Client: ccr sent %d bytes\n", numBytes);
      can_send = true;
    }
    else
    {
      tsprintf("Client: error sending:\n");
      print_wsa_error(info->socket, overlapped);
    }
    break;
  }

  free(info);
}

static bool start_connect()
{
  struct addrinfo hints = { 0 };
  hints.ai_flags = 0;
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = IPPROTO_TCP;

  struct addrinfo* server_address = NULL;

  if (getaddrinfo("127.0.0.1", g_serverPort, &hints, &server_address) != 0)
  {
    tsprintf("Client: unable to get address info for %s:%s:\n", g_serverHost, g_serverPort);
    printwindowserror(WSAGetLastError());
    return false;
  }

  SOCKET s = WSASocket(server_address->ai_family, server_address->ai_socktype, server_address->ai_protocol, NULL, 0, WSA_FLAG_OVERLAPPED);
  if (s == INVALID_SOCKET)
  {
    tsprintf("Client: unable to create client socket for %s:%s:\n", g_serverHost, g_serverPort);
    printwindowserror(WSAGetLastError());

    freeaddrinfo(server_address);
    return false;
  }

  if (!BindIoCompletionCallback((HANDLE)s, client_completion_routine, 0))
  {
    tsprintf("Client: unable to bind io completion callback:\n");
    printwindowserror(GetLastError());

    freeaddrinfo(server_address);
    closesocket(s);
    return false;
  }

  struct sockaddr_storage addr = { 0 };
  addr.ss_family = server_address->ai_family;
  if (bind(s, (struct sockaddr*)&addr, (int)server_address->ai_addrlen) != 0)
  {
    tsprintf("Client: unable to bind socket:\n");
    printwindowserror(WSAGetLastError());

    freeaddrinfo(server_address);
    closesocket(s);
    return false;
  }

  iocp_info_t* info = alloc_iocp(iocp_info_kind_t::IOCP_KIND_CONNECT, s);
  if (info == NULL)
  {
    tsprintf("Client: out of memory\n");
    freeaddrinfo(server_address);
    closesocket(s);
    return false;
  }

  connecting = true;
  if (g_ConnectEx(s, server_address->ai_addr, (int)server_address->ai_addrlen, NULL, 0, NULL, (LPOVERLAPPED)info))
  {
    tsprintf("Client: connected socket %d\n", s);
    complete_connect(s);
    client_socket = s;
  }
  else
  {
    DWORD error = GetLastError();
    if (error == ERROR_IO_PENDING)
    {
      tsprintf("Client: started connect for socket %d...\n", s);
    }
    else
    {
      tsprintf("Client: unable to start connect:\n");
      printwindowserror(error);

      connecting = false;
      freeaddrinfo(server_address);
      closesocket(s);
      return false;
    }
  }

  freeaddrinfo(server_address);
  return true;
}

static bool complete_connect(SOCKET s)
{
  return setsockopt(s, SOL_SOCKET, SO_UPDATE_CONNECT_CONTEXT, NULL, 0) == 0;
}

static char send_buffer[128];
static int num_sent = 0;

static bool start_send()
{
  int len = snprintf(send_buffer, 128, "MSG %d", num_sent++);

  iocp_info_t* info = alloc_iocp(iocp_info_kind_t::IOCP_KIND_SEND, client_socket);

  WSABUF buf;
  buf.buf = send_buffer;
  buf.len = len;

  DWORD bytesSent;

  if (WSASend(client_socket, &buf, 1, &bytesSent, 0, (LPWSAOVERLAPPED)info, NULL) == 0)
  {
    tsprintf("Client: wsa sent %d bytes\n", bytesSent);
  }
  else
  {
    DWORD error = WSAGetLastError();
    if (error == WSA_IO_PENDING)
    {
      can_send = false;
    }
    else
    {
      tsprintf("Client: error starting send:\n");
      printwindowserror(error);
      return false;
    }
  }

  return true;
}

DWORD WINAPI ClientThread(LPVOID data)
{
  tsprintf("Client running...\n");

  while (g_running)
  {
    SleepEx(1000, true);

    if (client_socket == INVALID_SOCKET)
    {
      if (g_client_can_connect && !connecting && !start_connect())
      {
        tsprintf("Client: start connect failed; exiting\n");
        return EXIT_FAILURE;
      }
    }
    else
    {
      DWORD error;
      int len = sizeof(error);
      getsockopt(client_socket, SOL_SOCKET, SO_ERROR, (char*)&error, &len);

      DWORD secs;
      len = sizeof(secs);
      getsockopt(client_socket, SOL_SOCKET, SO_CONNECT_TIME, (char*)&secs, &len);

      if (can_send && !start_send())
      {
        tsprintf("Client: start send failed for socket %d; exiting\n", client_socket);
        return EXIT_FAILURE;
      }
    }
  }

  tsprintf("Client: exiting with success\n");
  return EXIT_SUCCESS;
}
