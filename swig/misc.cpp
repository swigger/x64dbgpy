#include <windows.h>
#include "../pluginsdk/bridgemain.h"

extern "C"
void DoAsync(long id)
{
	static UINT MSG_ASYNC_DOSTH = ::RegisterWindowMessageW(L"x64dbgpy.async.do.sth");
	::PostMessage(::GuiGetWindowHandle(), MSG_ASYNC_DOSTH, 0, (LPARAM)id);
}
