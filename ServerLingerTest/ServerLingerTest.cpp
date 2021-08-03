
#include "pch.h"

extern int tsprintf(const char* format, ...);
extern void printwindowserror(int err);

extern BOOL WINAPI CtrlHandler(DWORD dwEvent);
extern DWORD WINAPI ServerThread(LPVOID data);
extern DWORD WINAPI ClientThread(LPVOID data);

constexpr auto NUM_THREADS = 2;

bool g_test_closed_connection = true;

bool g_running = true;
bool g_client_can_connect = true;

LPFN_ACCEPTEX g_AcceptEx = NULL;
LPFN_CONNECTEX g_ConnectEx = NULL;
LPFN_DISCONNECTEX g_DisconnectEx = NULL;

char *g_serverHost = NULL;
char *g_serverPort = NULL;

LPVOID g_pThreadData[NUM_THREADS];
HANDLE g_hThreads[NUM_THREADS];
DWORD g_dwThreadIds[NUM_THREADS];

static char serverHost[NI_MAXHOST];
static char serverPort[NI_MAXSERV];

//
static int init_wsa()
{
  WORD wsaVersion = MAKEWORD(2, 2);
  WSADATA wsaData;

  if (WSAStartup(wsaVersion, &wsaData) != 0)
  {
    printwindowserror(WSAGetLastError());
    return 5;
  }

  GUID guid;
  DWORD dwSize;
  SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

  guid = WSAID_CONNECTEX;
  if (WSAIoctl(s, SIO_GET_EXTENSION_FUNCTION_POINTER, &guid, sizeof(guid), &g_ConnectEx, sizeof(g_ConnectEx), &dwSize, NULL, NULL) == SOCKET_ERROR)
  {
    printwindowserror(WSAGetLastError());
    closesocket(s);
    WSACleanup();
    return 6;
  }

  guid = WSAID_ACCEPTEX;
  if (WSAIoctl(s, SIO_GET_EXTENSION_FUNCTION_POINTER, &guid, sizeof(guid), &g_AcceptEx, sizeof(g_AcceptEx), &dwSize, NULL, NULL) == SOCKET_ERROR)
  {
    printwindowserror(WSAGetLastError());
    closesocket(s);
    WSACleanup();
    return 7;
  }

  guid = WSAID_DISCONNECTEX;
  if (WSAIoctl(s, SIO_GET_EXTENSION_FUNCTION_POINTER, &guid, sizeof(guid), &g_DisconnectEx, sizeof(g_DisconnectEx), &dwSize, NULL, NULL) == SOCKET_ERROR)
  {
    printwindowserror(WSAGetLastError());
    closesocket(s);
    WSACleanup();
    return 8;
  }

  closesocket(s);
  return 0;
}

//
static int init_threads()
{
  g_pThreadData[0] = &g_running;
  g_hThreads[0] = CreateThread(
    NULL,
    0,
    ServerThread,
    g_pThreadData[0],
    0,
    &g_dwThreadIds[0]
  );

  if (g_hThreads[0] == NULL)
  {
    printwindowserror(GetLastError());
    return 1;
  }

  g_pThreadData[1] = &g_running;
  g_hThreads[1] = CreateThread(
    NULL,
    0,
    ClientThread,
    g_pThreadData[1],
    0,
    &g_dwThreadIds[1]
  );

  if (g_hThreads[1] == NULL)
  {
    printwindowserror(GetLastError());
    return 2;
  }

  return 0;
}

//
static int cleanup_threads()
{
  for (int i = 0; i < NUM_THREADS; i++)
  {
    DWORD exitCode;
    if (GetExitCodeThread(g_hThreads[i], &exitCode))
    {
      tsprintf("%s thread exited with code %d\n", i == 0 ? "Server" : "Client", exitCode);
    }
    else
    {
      printwindowserror(GetLastError());
    }

    CloseHandle(g_hThreads[i]);
  }

  return 0;
}

//
int main()
{
  int result = 0;

  // break handler
  if (!SetConsoleCtrlHandler(CtrlHandler, TRUE)) {
    printwindowserror(GetLastError());
    return 4;
  }

  // wsa setup
  (g_serverHost = serverHost)[0] = 0;
  (g_serverPort = serverPort)[0] = 0;

  if ((result = init_wsa()) != 0)
  {
    return result;
  }

  // thread setup
  if ((result = init_threads()) != 0)
  {
    return result;
  }

  // wait for threads
  if (WaitForMultipleObjects(NUM_THREADS, g_hThreads, TRUE, INFINITE) == WAIT_FAILED)
  {
    printwindowserror(GetLastError());
    return 3;
  }

  // clean up threads
  if ((result = cleanup_threads()) != 0)
  {
    return result;
  }

  // clean up wsa
  WSACleanup();

  //
  printwindowserror(GetLastError());
  return 0;
}
