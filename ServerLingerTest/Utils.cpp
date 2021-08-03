#include "pch.h"
#include <stdarg.h>
#include <stdio.h>

constexpr auto TSP_BUF_SIZE = 1024;

int tsprintf(const char* format, ...)
{
  va_list args;
  char buffer[TSP_BUF_SIZE];
  memset(buffer, 0, TSP_BUF_SIZE);

  va_start(args, format);
  int len = vsprintf_s(buffer, TSP_BUF_SIZE - 1, format, args);
  HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
  DWORD written = 0;

  if (hOut != INVALID_HANDLE_VALUE)
  {
    WriteConsoleA(hOut, buffer, len, &written, NULL);
  }

  va_end(args);
  return (int)written;
}

void printwindowserror(int err)
{
  LPTSTR lpMsgBuffer = NULL;
  DWORD dwErr = err;

  DWORD dwLen = FormatMessage(
    FORMAT_MESSAGE_ALLOCATE_BUFFER |
    FORMAT_MESSAGE_FROM_SYSTEM |
    FORMAT_MESSAGE_IGNORE_INSERTS,
    NULL,
    dwErr,
    MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
    (LPTSTR)&lpMsgBuffer,
    0, NULL);

  HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
  if (hOut != INVALID_HANDLE_VALUE)
  {
    DWORD dwWritten;
    WriteConsole(hOut, lpMsgBuffer, dwLen, &dwWritten, NULL);
  }
}
