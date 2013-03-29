
#ifdef WIN32
#include <windows.h>
BOOL __stdcall DllMain( HINSTANCE instance, DWORD reason, void * reserve )
{
  return TRUE;
}
#endif
