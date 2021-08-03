#include "pch.h"

extern int tsprintf(const char* format, ...);
extern void printwindowserror(int err);

extern bool g_test_closed_connection;

extern bool g_running;
extern bool g_client_can_connect;

extern char* g_serverHost;
extern char* g_serverPort;

extern LPFN_ACCEPTEX g_AcceptEx;
extern LPFN_DISCONNECTEX g_DisconnectEx;

static SOCKET listen_socket = INVALID_SOCKET;
static bool accepting = false;
static SOCKET new_socket = INVALID_SOCKET;
static SOCKET accepted_socket = INVALID_SOCKET;

constexpr size_t READ_BUFFER_SIZE = 1024;
static char read_buffer[READ_BUFFER_SIZE];

enum class iocp_info_kind_t
{
  IOCP_KIND_ACCEPT = 0,
  IOCP_KIND_RECV = 1,
  IOCP_KIND_DISCONNECT = 3
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

bool get_socket_name(sockaddr* addr, char* hostName, char* servName)
{
  socklen_t actual_address_length = sizeof(struct sockaddr_storage);
  return getnameinfo(addr, actual_address_length, hostName, NI_MAXHOST, servName, NI_MAXSERV, NI_NUMERICHOST | NI_NUMERICSERV) == 0;
}

static bool complete_accept(SOCKET s);
static bool start_recv(SOCKET s);

static char print_buffer[READ_BUFFER_SIZE - 1];

static void print_wsa_error(SOCKET socket, LPOVERLAPPED overlapped)
{
  DWORD numBytes;
  DWORD flags;
  WSAGetOverlappedResult(socket, overlapped, &numBytes, false, &flags);
  DWORD error = GetLastError();
  tsprintf("%x ", error);
  printwindowserror(error);
}

static void __stdcall server_completion_routine(DWORD errorCode, DWORD numBytes, LPOVERLAPPED overlapped)
{
  iocp_info_t* info = (iocp_info_t*)overlapped;
  switch (info->kind)
  {
  case iocp_info_kind_t::IOCP_KIND_ACCEPT:
    if (errorCode == ERROR_SUCCESS && complete_accept(info->socket))
    {
      if (!g_test_closed_connection)
      {
        g_client_can_connect = true;
      }

      start_recv(info->socket);
    }
    else
    {
      tsprintf("Server: accept failed for %d with error %x:\n", info->socket, errorCode);
      print_wsa_error(info->socket, overlapped);
    }
    break;

  case iocp_info_kind_t::IOCP_KIND_RECV:
    if (errorCode == ERROR_SUCCESS)
    {
      memset(print_buffer, 0, READ_BUFFER_SIZE - 1);
      memcpy(print_buffer, read_buffer, numBytes);

      tsprintf("Server: read %d bytes: '%s'\n", numBytes, print_buffer);

      if (numBytes > 0)
      {
        start_recv(info->socket);
      }
    }
    else
    {
      tsprintf("Server: recv failed for %d with error %x:\n", info->socket, errorCode);
      print_wsa_error(info->socket, overlapped);
    }
    break;

  case iocp_info_kind_t::IOCP_KIND_DISCONNECT:
    if (errorCode == ERROR_SUCCESS)
    {
      tsprintf("Server: disconnect succeeded for %d", info->socket);
    }
    else
    {
      tsprintf("Server: disconnect for %d failed with error %x:\n", info->socket, errorCode);
      print_wsa_error(info->socket, overlapped);
    }
    break;

  default:
    break;
  }

  free(info);
}

bool create_listen_socket()
{
  struct addrinfo hints = { 0 };
  struct addrinfo* requested_address = NULL;

  hints.ai_flags = AI_PASSIVE;
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = IPPROTO_IP;

  if (getaddrinfo(NULL, "0", &hints, &requested_address) != 0)
  {
    printwindowserror(WSAGetLastError());
    return false;
  }

  if (requested_address == NULL)
  {
    tsprintf("Server: getaddrinfo() failed!");
    return false;
  }

  listen_socket = WSASocket(requested_address->ai_family, requested_address->ai_socktype, requested_address->ai_protocol, NULL, 0, WSA_FLAG_OVERLAPPED);
  if (listen_socket == INVALID_SOCKET)
  {
    tsprintf("Server: unable to create listen_socket:\n");
    printwindowserror(WSAGetLastError());
    freeaddrinfo(requested_address);
    return false;
  }

  if (!BindIoCompletionCallback((HANDLE)listen_socket, server_completion_routine, 0))
  {
    tsprintf("Server: unable to bind io completion for listen_socket:\n");
    printwindowserror(GetLastError());
    freeaddrinfo(requested_address);
    return false;
  }

  int result = bind(listen_socket, requested_address->ai_addr, (int)requested_address->ai_addrlen);
  if (result == SOCKET_ERROR)
  {
    tsprintf("Server: unable to bind listen_socket:\n");
    printwindowserror(WSAGetLastError());
    freeaddrinfo(requested_address);
    return false;
  }

  result = listen(listen_socket, SOMAXCONN);
  if (result == SOCKET_ERROR)
  {
    tsprintf("Server: listen failed:\n");
    printwindowserror(WSAGetLastError());
    freeaddrinfo(requested_address);
    return false;
  }

  socklen_t actual_address_length = sizeof(struct sockaddr_storage);
  if (getsockname(listen_socket, requested_address->ai_addr, &actual_address_length) != 0)
  {
    tsprintf("Server: unable to get socket name:\n");
    printwindowserror(WSAGetLastError());
    freeaddrinfo(requested_address);
    return false;
  }

  if (get_socket_name(requested_address->ai_addr, g_serverHost, g_serverPort))
  {
    tsprintf("Server: socket %d listening on %s:%s\n", listen_socket, g_serverHost, g_serverPort);
  }
  else
  {
    tsprintf("Server: unable to get socket name:\n");
    printwindowserror(WSAGetLastError());
    freeaddrinfo(requested_address);
    return false;
  }

  freeaddrinfo(requested_address);
  return true;
}

static bool start_accept()
{
  WSAPROTOCOL_INFO protocolInfo;

  if (WSADuplicateSocket(listen_socket, GetCurrentProcessId(), &protocolInfo) != 0)
  {
    tsprintf("Server: ERROR duplicating socket for accept:\n");
    printwindowserror(WSAGetLastError());
    return false;
  }

  new_socket = WSASocket(protocolInfo.iAddressFamily, protocolInfo.iSocketType, protocolInfo.iProtocol, NULL, 0, WSA_FLAG_OVERLAPPED);
  if (new_socket != INVALID_SOCKET)
  {
    tsprintf("Server: new accept socket %d\n", new_socket);
  }
  else
  {
    tsprintf("Server: ERROR creating new accept socket:\n");
    printwindowserror(WSAGetLastError());
    return false;
  }

  if (BindIoCompletionCallback((HANDLE)new_socket, server_completion_routine, 0) == 0)
  {
    tsprintf("Server: ERROR binding IO completion callback\n");
    printwindowserror(GetLastError());
    return false;
  }

  iocp_info_t* info = alloc_iocp(iocp_info_kind_t::IOCP_KIND_ACCEPT, new_socket);
  if (info == NULL)
  {
    tsprintf("Server: out of memory\n");
    return false;
  }

  DWORD bytes;
  if (g_AcceptEx(listen_socket, new_socket, info->buf, 0, IOCP_ACCEPT_ADDR_LEN, IOCP_ACCEPT_ADDR_LEN, &bytes, &info->ov))
  {
    if (!complete_accept(new_socket))
    {
      tsprintf("Server: unable to update accept context:\n");
      printwindowserror(WSAGetLastError());
      return false;
    }
  }
  else
  {
    DWORD err = GetLastError();
    if (err == ERROR_IO_PENDING)
    {
      tsprintf("Server: accept pending...\n");
    }
    else
    {
      tsprintf("Server: ERROR accepting:\n");
      printwindowserror(err);
      free(info);
      return false;
    }
  }

  return true;
}

static bool complete_accept(SOCKET s)
{
  new_socket = INVALID_SOCKET;

  if (s != INVALID_SOCKET)
  {
    tsprintf("Server: successfully accepted on socket %d\n", s);

    if (setsockopt(s, SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT, (char*)&listen_socket, sizeof(SOCKET)) == 0)
    {
      accepted_socket = s;
      accepting = false;

      return true;
    }
    else
    {
      tsprintf("Server: unable to update accept context:\n");
      printwindowserror(WSAGetLastError());
    }
  }

  return false;
}

static bool start_recv(SOCKET s)
{
  iocp_info_t* info = alloc_iocp(iocp_info_kind_t::IOCP_KIND_RECV, s);

  WSABUF buf;
  buf.buf = read_buffer;
  buf.len = READ_BUFFER_SIZE;
  DWORD bytesReceived;
  DWORD flags = 0;

  if (WSARecv(s, &buf, 1, &bytesReceived, &flags, (LPWSAOVERLAPPED)info, NULL) != 0)
  {
    DWORD error = GetLastError();
    if (error != WSA_IO_PENDING)
    {
      tsprintf("Server: unable to start Recv:\n");
      printwindowserror(error);
      return false;
    }
  }
  return true;
}

static void set_no_linger(SOCKET s)
{
  LINGER linger_opt;
  linger_opt.l_onoff = 1;
  linger_opt.l_linger = 0;
  setsockopt(s, SOL_SOCKET, SO_LINGER, (const char*)&linger_opt, sizeof(linger_opt));
}

static void close_socket(SOCKET s)
{
  //set_no_linger(s);
  CancelIoEx((HANDLE)s, NULL);
}

static DWORD close_sockets()
{
  DWORD return_value = EXIT_SUCCESS;

  if (listen_socket != INVALID_SOCKET)
  {
    iocp_info_t* info = alloc_iocp(iocp_info_kind_t::IOCP_KIND_DISCONNECT, listen_socket);

    if (CancelIoEx((HANDLE)listen_socket, NULL))
    {
      tsprintf("Server: listen_socket %d IO canceled\n", listen_socket);
    }
    else
    {
      DWORD error = GetLastError();
      if (error != ERROR_IO_PENDING)
      {
        tsprintf("Server: cancel for listen_socket %d failed:\n", listen_socket);
        printwindowserror(error);
      }
    }

    set_no_linger(listen_socket);
    if (closesocket(listen_socket) == 0)
    {
      tsprintf("Server: listen_socket %d closed\n", listen_socket);
    }
    else
    {
      tsprintf("Server: ERROR closing listen_socket %d:\n", listen_socket);
      printwindowserror(GetLastError());
    }
    listen_socket = INVALID_SOCKET;
  }

  if (new_socket != INVALID_SOCKET)
  {
    if (CancelIoEx((HANDLE)new_socket, NULL))
    {
      tsprintf("Server: new_socket %d canceled\n");
    }
    else
    {
      DWORD error = GetLastError();
      if (error != ERROR_IO_PENDING)
      {
        tsprintf("Server: cancel for new_socket %d failed:\n", new_socket);
        printwindowserror(error);
      }
    }
    closesocket(new_socket);
    new_socket = INVALID_SOCKET;
  }

  if (accepted_socket != INVALID_SOCKET)
  {
    iocp_info_t* info = alloc_iocp(iocp_info_kind_t::IOCP_KIND_DISCONNECT, accepted_socket);
    if (g_DisconnectEx(accepted_socket, (LPOVERLAPPED)info, TF_REUSE_SOCKET, 0))
    {
      tsprintf("Server: accepted_socket %d disconnected\n");
    }
    else
    {
      DWORD error = WSAGetLastError();
      if (error != ERROR_IO_PENDING)
      {
        tsprintf("Server: disconnect for accepted_socket %d failed:\n", accepted_socket);
        printwindowserror(error);
      }
    }
    accepted_socket = INVALID_SOCKET;
  }

  return return_value;
}

DWORD WINAPI ServerThread(LPVOID data)
{
  // create listen socket
  if (!create_listen_socket())
  {
    g_running = false;
    tsprintf("Server: ERROR; exiting");
    return EXIT_FAILURE;
  }

  //
  tsprintf("Server: running...\n");
  DWORD return_value = EXIT_SUCCESS;

  while (g_running)
  {
    if (listen_socket != INVALID_SOCKET)
    {
      if (!accepting && accepted_socket == INVALID_SOCKET)
      {
        if (start_accept())
        {
          accepting = true;
        }
        else
        {
          g_running = false;
          break;
        }
      }
      else if (accepting && accepted_socket == INVALID_SOCKET)
      {
        if (g_test_closed_connection)
          close_sockets();
      }
    }

    SleepEx(1, true);
  }

  // clean up
  return_value = close_sockets();

  if (return_value == EXIT_SUCCESS)
  {
    tsprintf("Server: exiting successfully\n");
  }
  return return_value;
}
