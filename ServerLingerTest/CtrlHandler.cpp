#include "pch.h"

extern bool g_running;

BOOL WINAPI CtrlHandler(DWORD dwEvent)
{
  switch (dwEvent)
  {
  case CTRL_BREAK_EVENT:
  case CTRL_C_EVENT:
  case CTRL_LOGOFF_EVENT:
  case CTRL_SHUTDOWN_EVENT:
  case CTRL_CLOSE_EVENT:
    g_running = false;
    return TRUE;
  default:
    return FALSE;
  }
}
