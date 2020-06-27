#include <Windows.h>
#include <stdio.h>

int WINAPI DllMain(HMODULE mod, DWORD reason, void*r__)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        wchar_t path[4096];
        GetEnvironmentVariable(L"Path", path, _countof(path));
        size_t plen = wcslen(path);
        if (path[plen - 1] != ';')
            path[plen++] = ';';
#ifdef _M_IX86
        static const wchar_t py[300] = L"C:\\Program Files (x86)\\Microsoft Visual Studio\\Shared\\Python37_86";
#else
        static const wchar_t py[300] = L"C:\\Program Files (x86)\\Microsoft Visual Studio\\Shared\\Python37_64";
#endif
        wcscpy(path + plen, py);
        SetEnvironmentVariable(L"Path", path);
    }
    return FALSE;
}
