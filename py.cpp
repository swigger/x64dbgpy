#include "py.h"
#include "stringutils.h"
#include "resource.h"
#include <windows.h>
#include <stdio.h>
#include <psapi.h>
#include <python.h>
#include <shlwapi.h>
#include "pluginsdk/_scriptapi_debug.h"

#pragma comment(lib, "shlwapi.lib")

#define module_name "x64dbgpy"
#define token_paste(a, b) token_paste_(a, b)
#define token_paste_(a, b) a ## b
#define event_object_name "Event"
#define autorun_directory L"plugins\\x64dbgpy\\x64dbgpy\\autorun"
// lParam: ScanCode=0x41(ALT), cRepeat=1, fExtended=False, fAltDown=True, fRepeat=1, fUp=True
#define ALT_F7_SYSKEYUP 0xe0410001

PyObject* pModule, *pEventObject;
HINSTANCE hInst;
static UINT MSG_ASYNC_DOSTH = ::RegisterWindowMessage(L"x64dbgpy.async.do.sth");

enum
{
    MENU_RUNSCRIPTASYNC,
    MENU_RUNGUISCRIPT,
    MENU_ABOUT
};

extern "C" __declspec(dllexport) void CBMENUENTRY(CBTYPE cbType, PLUG_CB_MENUENTRY* info)
{
    switch(info->hEntry)
    {
    case MENU_RUNSCRIPTASYNC:
        DbgCmdExec("PyRunScriptAsync");
        break;

    case MENU_RUNGUISCRIPT:
        DbgCmdExec("PyRunGuiScript");
        break;

    case MENU_ABOUT:
        MessageBoxA(hwndDlg, "Made By RealGame (Tomer Zait)", plugin_name " Plugin", MB_ICONINFORMATION);
        break;
    }
}

static long pyCallback(const char* eventName, PyObject* pKwargs)
{
    PyObject* pFunc, *pValue;
    long ret = 0;
    // Check if event object exist.
    if(pEventObject == NULL)
        return ret;

    pFunc = PyObject_GetAttrString(pEventObject, eventName);
    if(pFunc && PyCallable_Check(pFunc))
    {
        pValue = PyObject_Call(pFunc, PyTuple_New(0), pKwargs);
        if (pValue && PyLong_Check(pValue))
            ret = PyLong_AsLong(pValue);
        Py_DECREF(pKwargs);
        Py_DECREF(pFunc);
        if(pValue == NULL)
        {
            _plugin_logprintf("[PYTHON] Could not use %s function.\n", eventName);
            PyErr_PrintEx(0);
        }
        else
            Py_DECREF(pValue);
    }
    return ret;
}

static bool OpenFileDialog(wchar_t Buffer[MAX_PATH])
{
    OPENFILENAMEW sOpenFileName = { 0 };
    const wchar_t szFilterString[] = L"Python files\0*.py\0\0";
    const wchar_t szDialogTitle[] = L"Select script file...";
    sOpenFileName.lStructSize = sizeof(sOpenFileName);
    sOpenFileName.lpstrFilter = szFilterString;
    sOpenFileName.nFilterIndex = 1;
    sOpenFileName.lpstrFile = Buffer;
    sOpenFileName.nMaxFile = MAX_PATH;
    sOpenFileName.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;
    sOpenFileName.lpstrTitle = szDialogTitle;
    sOpenFileName.hwndOwner = GuiGetWindowHandle();
    return (FALSE != GetOpenFileNameW(&sOpenFileName));
}

static void AddSysPath(const std::wstring & dir)
{
     PyObject* sysobj = PyImport_ImportModule("sys");
    PyObject* strpath = PyUnicode_FromString("path");

    PyObject* oldlst = PyObject_GetAttr(sysobj, strpath);
    int oldsz = (int)PyList_Size(oldlst);

    bool already = false;
    {
        //check dir already in sys.path.
        for (int i = 0; i < oldsz; ++i)
        {
            PyObject * obb = PyList_GetItem(oldlst, i); //borrowed.
            if (PyUnicode_Check(obb))
            {
                Py_ssize_t len1;
                Py_UNICODE* v = PyUnicode_AsUnicodeAndSize(obb, &len1);
                if (len1 == dir.length() && memcmp(v, dir.data(), sizeof(wchar_t)*dir.length() )==0)
                {
                    already = true;
                    break;
                }
            }
        }
    }
    if (!already)
    {
        PyObject* newlst = PyList_New(oldsz + 1);
        for (int i = 0; i < oldsz; ++i)
        {
            PyList_SET_ITEM(newlst, i, PyList_GET_ITEM(oldlst, i));
            PyList_SET_ITEM(oldlst, i, NULL);
        }
        PyObject* str = PyUnicode_FromUnicode(dir.c_str(), dir.length());
        PyList_SET_ITEM(newlst, oldsz, str);
        PyObject_SetAttr(sysobj, strpath, newlst);
        Py_XDECREF(newlst);
        //Py_XDECREF(str); //NO, it's used by newlst.
    }

    Py_XDECREF(strpath);
    Py_XDECREF(sysobj);
    Py_XDECREF(oldlst);
}

static bool ExecutePythonScript(const wchar_t* szFileName, int argc, char* argv[])
{
    wchar_t szCurrentDir[MAX_PATH] = L"";
    GetCurrentDirectoryW(_countof(szCurrentDir), szCurrentDir);

    //add path of szfilename to sys.path.
    {
        // FIXME: should get full path name?
        std::wstring wsfn(szFileName);
        for (size_t i = wsfn.size(); i-- > 0; )
        {
            if (wsfn[i] == '/' || wsfn[i] == '\\')
            {
                wsfn.resize(i);
                AddSysPath(wsfn);
                break;
            }
        }
    }

    FILE* fp = _wfopen(szFileName, L"r");
    if (!fp)
    {
        _plugin_logputs("[PYTHON] File does not exist...");
        return false;
    }
    if (argc > 0)
    {
        std::vector<wchar_t*> argv2;
        for (int i = 0; i < argc; ++i)
        {
            size_t la = strlen(argv[i]) + 3;
            wchar_t* a = (wchar_t*)malloc(la * sizeof(wchar_t));
            MultiByteToWideChar(CP_UTF8, 0, argv[i], -1, a, la);
            argv2.push_back(a);
        }
        argv2.push_back(nullptr);
        PySys_SetArgv(argc, &argv2[0]);
        for (auto p : argv2) free(p);
        argv2.clear();
    }


    size_t fnalen = wcslen(szFileName) * 3 + 10;
    char* fna = (char*)malloc(fnalen);
    WideCharToMultiByte(CP_UTF8, 0, szFileName, -1, fna, fnalen, 0, 0);
    auto result = PyRun_SimpleFileExFlags(fp, fna, 0, NULL);
    free(fna);
    SetCurrentDirectoryW(szCurrentDir);
    fclose(fp);

    if (result < 0)
    {
        PyObject* exception, * v, * tb;
        PyErr_Fetch(&exception, &v, &tb);
        bool has_exception = exception != NULL;
        Py_XDECREF(exception);
        Py_XDECREF(v);
        Py_XDECREF(tb);
        if (has_exception)
        {
            if (PyErr_ExceptionMatches(PyExc_SystemExit))
                _plugin_logprintf("[PYTHON] SystemExit...\n");
            else
                _plugin_logprintf("[PYTHON] Exception...\n");
            PyErr_PrintEx(1);
            return false;
        }
    }
    _plugin_logputs("[PYTHON] Execution is done!");
    GuiUpdateAllViews();
    return true;
}

// Exports for other plugins
extern "C" __declspec(dllexport) bool ExecutePythonScriptExA(const char* szFileName, int argc, char* argv[])
{
    _plugin_logprintf("[PYTHON] Executing script: \"%s\"\n", szFileName);
    return ExecutePythonScript(Utf8ToUtf16(szFileName).c_str(), argc, argv);
}

extern "C" __declspec(dllexport) bool ExecutePythonScriptA(const char* szFileName)
{
    return ExecutePythonScriptExA(szFileName, 0, nullptr);
}

extern "C" __declspec(dllexport) bool ExecutePythonScriptExW(const wchar_t* szFileName, int argc, wchar_t* argv[])
{
    std::vector<char*> argvPtr(argc);
    std::vector<std::vector<char>> argvData(argc);
    for(int i = 0; i < argc; i++)
    {
        auto conv = Utf16ToUtf8(argv[i]);
        argvData[i] = std::vector<char>(conv.begin(), conv.end());
        argvData[i].push_back('\0');
        argvPtr[i] = argvData[i].data();
    }
    _plugin_logprintf("[PYTHON] Executing script: \"%s\"\n", Utf16ToUtf8(szFileName).c_str());
    return ExecutePythonScript(szFileName, argc, argvPtr.data());
}

extern "C" __declspec(dllexport) bool ExecutePythonScriptW(const wchar_t* szFileName)
{
    return ExecutePythonScriptExW(szFileName, 0, nullptr);
}

// Command callbacks
static std::wstring scriptName;
static std::vector<std::vector<char>> scriptArgvData;
static std::vector<char*> scriptArgvPtr;

static bool openScriptName(int argc, char* argv[])
{
    // Get script name
    if(argc < 2)
    {
        wchar_t szFileName[MAX_PATH] = L"";
        if(!OpenFileDialog(szFileName))
            return false;
        scriptName = szFileName;
    }
    else
        scriptName = Utf8ToUtf16(argv[1]);

    // Get (optional) script arguments
    scriptArgvData.clear();
    scriptArgvPtr.clear();
    if(argc > 2)
    {
        auto pyArgc = argc - 1;
        scriptArgvData.resize(pyArgc);
        scriptArgvPtr.resize(pyArgc);
        for(int i = 0; i < pyArgc; i++)
        {
            auto arg = argv[i + 1];
            scriptArgvData[i] = std::vector<char>(arg, arg + strlen(arg) + 1);
            scriptArgvPtr[i] = scriptArgvData[i].data();
        }
    }
    return true;
}

static bool cbPythonCommand(int argc, char* argv[])
{
    if(argc < 2)
    {
        _plugin_logputs("[PYTHON] Command Example: Python \"print('Hello World')\".");
        return false;
    }
    PyRun_SimpleString(argv[0] + 7);
    GuiFlushLog();
    GuiUpdateAllViews();
    return true;
}

static bool cbPipCommand(int argc, char* argv[])
{
    PyObject* pUtilsModule, *pFunc;
    PyObject* pKwargs, /* *pArgs, */ *pValue;

    if(argc < 2)
    {
        _plugin_logputs("[PYTHON] Command Example: Pip freeze");
        return false;
    }

    // Import utils
    pUtilsModule = PyObject_GetAttrString(pModule, "utils");
    if(pEventObject == NULL)
    {
        _plugin_logputs("[PYTHON] Could not find utils package.");
        PyErr_PrintEx(0);
        return false;
    }

    pFunc = PyObject_GetAttrString(pUtilsModule, "x64dbg_pip");
    if(pFunc && PyCallable_Check(pFunc))
    {
        pKwargs = Py_BuildValue("{s:s}", "args", argv[0]);
        pValue = PyObject_Call(pFunc, PyTuple_New(0), pKwargs);
        Py_DECREF(pKwargs);
        Py_DECREF(pFunc);
        if(pValue == NULL)
        {
            _plugin_logputs("[PYTHON] Could not use x64dbg_pip function.");
            PyErr_PrintEx(0);
            return false;
        }
        Py_DECREF(pValue);
    }
    return true;
}

static bool cbPyRunScriptCommand(int argc, char* argv[])
{
    if(!openScriptName(argc, argv))
        return false;
    _plugin_logprintf("[PYTHON] Executing script: \"%s\"\n", Utf16ToUtf8(scriptName).c_str());
    return ExecutePythonScript(scriptName.c_str(), int(scriptArgvPtr.size()), scriptArgvPtr.data());
}

static bool cbPyRunScriptAsyncCommand(int argc, char* argv[])
{
    if(!openScriptName(argc, argv))
        return false;
    CloseHandle(CreateThread(nullptr, 0, [](void*) -> DWORD
    {
        _plugin_logprintf("[PYTHON] Executing script in seperate thread: \"%s\"\n", Utf16ToUtf8(scriptName).c_str());
        ExecutePythonScript(scriptName.c_str(), int(scriptArgvPtr.size()), scriptArgvPtr.data());
        return 0;
    }, nullptr, 0, nullptr));
    return true;
}

static bool cbPyRunGuiScriptCommand(int argc, char* argv[])
{
    if(!openScriptName(argc, argv))
        return false;
    GuiExecuteOnGuiThread([]()
    {
        _plugin_logprintf("[PYTHON] Executing script: \"%s\"\n", Utf16ToUtf8(scriptName).c_str());
        ExecutePythonScript(scriptName.c_str(), int(scriptArgvPtr.size()), scriptArgvPtr.data());
    });
    return true;
}

static bool cbPythonCommandExecute(const char* cmd)
{
    if(cmd)
    {
        PyRun_SimpleString(cmd);
        GuiFlushLog();
        GuiUpdateAllViews();
        return true;
    }
    return false;
}

static void cbWinEventCallback(CBTYPE cbType, void* info)
{
    auto pinfo = (PLUG_CB_WINEVENT*)info;
    if (pinfo->retval || pinfo->result) return;

    MSG* msg = ((PLUG_CB_WINEVENT*)info)->message;
    if (msg->message == WM_SYSKEYUP)
    {
        if (msg->lParam == ALT_F7_SYSKEYUP)
            DbgCmdExec("PyRunGuiScript");
    }
    else if (msg->message == MSG_ASYNC_DOSTH)
    {
        /* note: should sync with __event.py
        bp_run = 10001
        bp_stepin = 10002
        bp_stepout = 10003
        bp_stepover = 10004
        */
        switch (msg->lParam)
        {
        case 10001: DbgCmdExec("run"); break;
        case 10002: DbgCmdExec("StepInto"); break;
        case 10003: DbgCmdExec("StepOut"); break;
        case 10004: DbgCmdExec("StepOver"); break;
        default: break;
        }
    }
}

static void cbInitDebugCallback(CBTYPE cbType, void* info)
{
    WIN32_FIND_DATAW FindFileData;
    HANDLE hFind = INVALID_HANDLE_VALUE;
    wchar_t autorunDirectory[MAX_PATH], currentDirectory[MAX_PATH];

    // Get Autorun Folder Path
    GetModuleFileNameW(NULL, autorunDirectory, MAX_PATH);
    PathRemoveFileSpecW(autorunDirectory);
    PathAppendW(autorunDirectory, autorun_directory);

    // Get Current Directory
    GetCurrentDirectoryW(MAX_PATH, currentDirectory);

    // Find And Execute *.py Files
    SetCurrentDirectoryW(autorunDirectory);
    hFind = FindFirstFileW(L"*.py", &FindFileData);
    if(hFind != INVALID_HANDLE_VALUE)
    {
        do
        {
            _plugin_logprintf("[PYTHON] Executing autorun file: \"%s\".\n", Utf16ToUtf8(FindFileData.cFileName).c_str());
            ExecutePythonScript(FindFileData.cFileName, int(scriptArgvPtr.size()), scriptArgvPtr.data());
        }
        while(FindNextFileW(hFind, &FindFileData) != 0);
        FindClose(hFind);
    }

    // Reset Current Directory
    SetCurrentDirectoryW(currentDirectory);
}

static void cbUnloadDllCallback(CBTYPE cbType, void* info)
{
    LPUNLOAD_DLL_DEBUG_INFO UnloadDll = ((PLUG_CB_UNLOADDLL*)info)->UnloadDll;

    pyCallback("unload_dll", Py_BuildValue("{s:N}",
        "lpBaseOfDll", PyLong_FromUnsignedLongLong((size_t)UnloadDll->lpBaseOfDll)
    ));
}

static void cbLoadDllCallback(CBTYPE cbType, void* info)
{
    PyObject* pLoadDll, *pPdbSig70, *pModInfo;

    PLUG_CB_LOADDLL* callbackInfo = (PLUG_CB_LOADDLL*)info;
    LOAD_DLL_DEBUG_INFO* LoadDll = callbackInfo->LoadDll;
    IMAGEHLP_MODULE64* modInfo = callbackInfo->modInfo;
    GUID PdbSig70 = modInfo->PdbSig70;

    pLoadDll = Py_BuildValue(
                   "{s:N, s:N, s:k, s:k, s:N, s:H}",
                   "hFile", PyLong_FromUnsignedLongLong((size_t)LoadDll->hFile),
                   "lpBaseOfDll", PyLong_FromUnsignedLongLong((size_t)LoadDll->lpBaseOfDll),
                   "dwDebugInfoFileOffset", LoadDll->dwDebugInfoFileOffset,
                   "nDebugInfoSize", LoadDll->nDebugInfoSize,
                   "lpImageName", PyLong_FromUnsignedLongLong((size_t)LoadDll->lpImageName),
                   "fUnicode", LoadDll->fUnicode
               );
    pPdbSig70 = Py_BuildValue(
                    "{s:k, s:H, s:H, s:N}",
                    "Data1", PdbSig70.Data1,
                    "Data2", PdbSig70.Data2,
                    "Data3", PdbSig70.Data3,
                    "Data4", PyByteArray_FromStringAndSize(
                        (char*)PdbSig70.Data4, ARRAYSIZE(PdbSig70.Data4)
                    )
                );
    pModInfo = Py_BuildValue(
                   "{s:k, s:K, s:k, s:k, s:k, s:k, s:i, s:s, s:s, s:s, s:s, "
                   " s:k, s:s, s:k, s:N, s:k, s:N, s:N, s:N, s:N, s:N, s:N, s:N}",
                   "SizeOfStruct", modInfo->SizeOfStruct,
                   "BaseOfImage", modInfo->BaseOfImage,
                   "ImageSize", modInfo->TimeDateStamp,
                   "TimeDateStamp", modInfo->TimeDateStamp,
                   "CheckSum", modInfo->CheckSum,
                   "NumSyms", modInfo->NumSyms,
                   "SymType", modInfo->SymType,
                   "ModuleName", modInfo->ModuleName,
                   "ImageName", modInfo->ImageName,
                   "LoadedImageName", modInfo->LoadedImageName,
                   "LoadedPdbName", modInfo->LoadedPdbName,
                   "CVSig", modInfo->CVSig,
                   "CVData", modInfo->CVData,
                   "PdbSig", modInfo->PdbSig,
                   "PdbSig70", pPdbSig70,
                   "PdbAge", modInfo->PdbAge,
                   "PdbUnmatched", PyBool_FromLong(modInfo->PdbUnmatched),
                   "DbgUnmatched", PyBool_FromLong(modInfo->DbgUnmatched),
                   "LineNumbers", PyBool_FromLong(modInfo->LineNumbers),
                   "GlobalSymbols", PyBool_FromLong(modInfo->GlobalSymbols),
                   "TypeInfo", PyBool_FromLong(modInfo->TypeInfo),
                   "SourceIndexed", PyBool_FromLong(modInfo->SourceIndexed),
                   "Publics", PyBool_FromLong(modInfo->Publics)
               );
    pyCallback("load_dll", Py_BuildValue(
                   "{s:N, s:N, s:s}",
                   "LoadDll", pLoadDll,
                   "modInfo", pModInfo,
                   "modname", callbackInfo->modname
               ));
    Py_DECREF(pLoadDll);
    Py_DECREF(pPdbSig70);
    Py_DECREF(pModInfo);
}

static void cbSystemBreakpointCallback(CBTYPE cbType, void* info)
{
    pyCallback("system_breakpoint", PyDict_New());
}

static void cbExitThreadCallback(CBTYPE cbType, void* info)
{
    PLUG_CB_EXITTHREAD* callbackInfo = ((PLUG_CB_EXITTHREAD*)info);

    pyCallback("exit_thread", Py_BuildValue(
                   "{s:k, s:k}",
                   "dwThreadId", callbackInfo->dwThreadId,
                   "dwExitCode", callbackInfo->ExitThread->dwExitCode
               ));
}

static void cbCreateThreadCallback(CBTYPE cbType, void* info)
{
    PyObject* pCreateThread;

    PLUG_CB_CREATETHREAD* callbackInfo = (PLUG_CB_CREATETHREAD*)info;
    CREATE_THREAD_DEBUG_INFO* CreateThread = callbackInfo->CreateThread;

    pCreateThread = Py_BuildValue(
                        "{s:k, s:N, s:N}",
                        "hThread", CreateThread->hThread,
                        "lpThreadLocalBase", PyLong_FromUnsignedLongLong((size_t)CreateThread->lpThreadLocalBase),
                        "lpStartAddress", PyLong_FromUnsignedLongLong((size_t)CreateThread->lpThreadLocalBase)
                    );
    pyCallback("create_thread", Py_BuildValue(
                   "{s:k, s:N}",
                   "dwThreadId", callbackInfo->dwThreadId,
                   "CreateThread", pCreateThread
               ));
    Py_DECREF(pCreateThread);

}

static void cbExitProcessCallback(CBTYPE cbType, void* info)
{
    EXIT_PROCESS_DEBUG_INFO* ExitProcess = ((PLUG_CB_EXITPROCESS*)info)->ExitProcess;

    pyCallback("exit_process", Py_BuildValue(
                   "{s:k}",
                   "dwExitCode", ExitProcess->dwExitCode
               ));
}

static void cbCreateProcessCallback(CBTYPE cbType, void* info)
{
    PyObject* pCreateProcessInfo, *pPdbSig70, *pModInfo, *pFdProcessInfo;

    PLUG_CB_CREATEPROCESS* callbackInfo = (PLUG_CB_CREATEPROCESS*)info;
    CREATE_PROCESS_DEBUG_INFO* CreateProcessInfo = callbackInfo->CreateProcessInfo;
    IMAGEHLP_MODULE64* modInfo = callbackInfo->modInfo;
    PROCESS_INFORMATION* fdProcessInfo = callbackInfo->fdProcessInfo;
    GUID PdbSig70 = modInfo->PdbSig70;

    pCreateProcessInfo = Py_BuildValue(
                             "{s:N, s:N, s:N, s:N, s:k, s:k, s:N, s:N, s:N, s:H}",
                             "hFile", PyLong_FromUnsignedLongLong((size_t)CreateProcessInfo->hFile),
                             "hProcess", PyLong_FromUnsignedLongLong((size_t)CreateProcessInfo->hProcess),
                             "hThread", PyLong_FromUnsignedLongLong((size_t)CreateProcessInfo->hThread),
                             "lpBaseOfImage", PyLong_FromUnsignedLongLong((size_t)CreateProcessInfo->lpBaseOfImage),
                             "dwDebugInfoFileOffset", CreateProcessInfo->dwDebugInfoFileOffset,
                             "nDebugInfoSize", CreateProcessInfo->nDebugInfoSize,
                             "lpThreadLocalBase", PyLong_FromUnsignedLongLong((size_t)CreateProcessInfo->lpThreadLocalBase),
                             "lpStartAddress", PyLong_FromUnsignedLongLong((size_t)CreateProcessInfo->lpStartAddress),
                             "lpImageName", PyLong_FromUnsignedLongLong((size_t)CreateProcessInfo->lpImageName),
                             "fUnicode", CreateProcessInfo->fUnicode
                         );
    pPdbSig70 = Py_BuildValue(
                    "{s:k, s:H, s:H, s:N}",
                    "Data1", PdbSig70.Data1,
                    "Data2", PdbSig70.Data2,
                    "Data3", PdbSig70.Data3,
                    "Data4", PyByteArray_FromStringAndSize(
                        (char*)PdbSig70.Data4, ARRAYSIZE(PdbSig70.Data4)
                    )
                );
    pModInfo = Py_BuildValue(
                   "{s:k, s:K, s:k, s:k, s:k, s:k, s:i, s:s, s:s, s:s, s:s, "
                   " s:k, s:s, s:k, s:N, s:k, s:N, s:N, s:N, s:N, s:N, s:N, s:N}",
                   "SizeOfStruct", modInfo->SizeOfStruct,
                   "BaseOfImage", modInfo->BaseOfImage,
                   "ImageSize", modInfo->TimeDateStamp,
                   "TimeDateStamp", modInfo->TimeDateStamp,
                   "CheckSum", modInfo->CheckSum,
                   "NumSyms", modInfo->NumSyms,
                   "SymType", modInfo->SymType,
                   "ModuleName", modInfo->ModuleName,
                   "ImageName", modInfo->ImageName,
                   "LoadedImageName", modInfo->LoadedImageName,
                   "LoadedPdbName", modInfo->LoadedPdbName,
                   "CVSig", modInfo->CVSig,
                   "CVData", modInfo->CVData,
                   "PdbSig", modInfo->PdbSig,
                   "PdbSig70", pPdbSig70,
                   "PdbAge", modInfo->PdbAge,
                   "PdbUnmatched", PyBool_FromLong(modInfo->PdbUnmatched),
                   "DbgUnmatched", PyBool_FromLong(modInfo->DbgUnmatched),
                   "LineNumbers", PyBool_FromLong(modInfo->LineNumbers),
                   "GlobalSymbols", PyBool_FromLong(modInfo->GlobalSymbols),
                   "TypeInfo", PyBool_FromLong(modInfo->TypeInfo),
                   "SourceIndexed", PyBool_FromLong(modInfo->SourceIndexed),
                   "Publics", PyBool_FromLong(modInfo->Publics)
               );
    pFdProcessInfo = Py_BuildValue(
                         "{s:N, s:N, s:k, s:k}",
                         "hProcess", PyLong_FromUnsignedLongLong((size_t)fdProcessInfo->hProcess),
                         "hThread", PyLong_FromUnsignedLongLong((size_t)fdProcessInfo->hThread),
                         "dwProcessId", fdProcessInfo->dwProcessId,
                         "dwThreadId", fdProcessInfo->dwThreadId
                     );
    pyCallback("create_process", Py_BuildValue(
                   "{s:N, s:N, s:s, s:N}",
                   "CreateProcessInfo", pCreateProcessInfo,
                   "modInfo", pModInfo,
                   "DebugFileName", callbackInfo->DebugFileName,
                   "fdProcessInfo", pFdProcessInfo
               ));
    Py_DECREF(pCreateProcessInfo);
    Py_DECREF(pPdbSig70);
    Py_DECREF(pModInfo);
    Py_DECREF(pFdProcessInfo);
}

static void cbBreakPointCallback(CBTYPE cbType, void* info)
{
    BRIDGEBP* breakpoint = ((PLUG_CB_BREAKPOINT*)info)->breakpoint;

    pyCallback("breakpoint", Py_BuildValue(
                   "{s:i, s:N, s:N, s:N, s:N, s:s, s:s, s:i}",
                   "type", breakpoint->type,
                   "addr", PyLong_FromUnsignedLongLong(breakpoint->addr),
                   "enabled", PyBool_FromLong(breakpoint->enabled),
                   "singleshoot", PyBool_FromLong(breakpoint->singleshoot),
                   "active", PyBool_FromLong(breakpoint->active),
                   "mod", breakpoint->mod,
                   "name", breakpoint->name,
                   "slot", breakpoint->slot
               ));
}

static void cbStopDebugCallback(CBTYPE cbType, void* info)
{
    pyCallback("stop_debug", PyDict_New());
}

static void cbTraceExecuteCallback(CBTYPE cbType, void* info)
{
    PLUG_CB_TRACEEXECUTE* traceInfo = (PLUG_CB_TRACEEXECUTE*)info;

    PyObject* pTraceExecute;

    pTraceExecute = Py_BuildValue(
                        "{s:N, s:N}",
                        "cip", PyLong_FromUnsignedLongLong(traceInfo->cip),
                        "stop", PyBool_FromLong(traceInfo->stop)
                    );

    // Packed in another dict because then it is passed by reference, so "stop" can be changed by the script.
    pyCallback("trace_execute", Py_BuildValue("{s:N}", "trace", pTraceExecute));

    traceInfo->stop = !!PyObject_IsTrue(PyDict_GetItemString(pTraceExecute, "stop"));

    Py_DECREF(pTraceExecute);
}

static std::wstring makeX64dbgPackageDir(const std::wstring & directory)
{
    auto dir = directory;
    if(dir[dir.length() - 1] != L'\\')
        dir.push_back(L'\\');
    dir.append(L"Lib\\site-packages");
    return dir;
}

static bool isValidPythonHome(const wchar_t* directory)
{
    if(!directory || !*directory)
        return false;
    auto attr = GetFileAttributesW(makeX64dbgPackageDir(directory).c_str());
    if(attr == INVALID_FILE_ATTRIBUTES)
        return false;
    return (attr & FILE_ATTRIBUTE_DIRECTORY) == FILE_ATTRIBUTE_DIRECTORY;
}

static bool findX64dbgPythonHome(std::wstring & home)
{
    //Get from configuration
    char setting[MAX_SETTING_SIZE] = "";
    if(BridgeSettingGet("x64dbgpy", "PythonHome", setting))
    {
        home = Utf8ToUtf16(setting);
        if(isValidPythonHome(home.c_str()))
        {
            _plugin_logputs("[PYTHON] Found valid PythonHome in the plugin settings!");
            return true;
        }
        _plugin_logprintf("[PYTHON] Found invalid PythonHome setting \"%s\"...\n", setting);
    }
    //Get from the developer environment variable
#ifdef _WIN64
    auto regpath = L"SOFTWARE\\Python\\PythonCore\\3.7\\InstallPath";
    auto python27x = _wgetenv(L"PYTHON27X64");
#else
    auto regpath = L"SOFTWARE\\Python\\PythonCore\\3.7-32\\InstallPath";
    auto python27x = _wgetenv(L"PYTHON27X86");
#endif //_WIN64

    if(isValidPythonHome(python27x))
    {
#ifdef _WIN64
        _plugin_logputs("[PYTHON] Found valid PythonHome in the PYTHON27X64 environment variable!");
#else
        _plugin_logputs("[PYTHON] Found valid PythonHome in the PYTHON27X86 environment variable!");
#endif //_WIN64
        home = python27x;
        return true;
    }
    //Get from registry
    HKEY hKey;
    wchar_t szRegHome[MAX_SETTING_SIZE] = L"";

    if(RegOpenKeyExW(HKEY_LOCAL_MACHINE, regpath, 0, KEY_READ, &hKey) == ERROR_SUCCESS)
    {
        DWORD dwSize = sizeof(szRegHome);
        RegQueryValueExW(hKey, nullptr, nullptr, nullptr, LPBYTE(szRegHome), &dwSize);
        RegCloseKey(hKey);
    }
    if(isValidPythonHome(szRegHome))
    {
        _plugin_logputs("[PYTHON] Found valid PythonHome in the registry!");
        home = szRegHome;
        return true;
    }
    //Get from PYTHONHOME environment variable
    auto pythonHome = _wgetenv(L"PYTHONHOME");
    if(isValidPythonHome(pythonHome))
    {
        _plugin_logputs("[PYTHON] Found valid PythonHome in the PYTHONHOME environment variable!");
        home = pythonHome;
        return true;
    }
    return false;
}

bool pyInit(PLUG_INITSTRUCT* initStruct)
{
#if 0
    if (AllocConsole())
    {
        freopen("CONIN$", "r", stdin);
        freopen("CONOUT$", "w", stdout);
        freopen("CONOUT$", "w", stderr);
        printf("hello from C\n");
    }
#endif

    // Register python command handler
    SCRIPTTYPEINFO info;
    strcpy_s(info.name, "Python");
    info.id = 0;
    info.execute = cbPythonCommandExecute;
    info.completeCommand = nullptr;
    GuiRegisterScriptLanguage(&info);

    // Register commands
    auto regCmd = [](const char* command, CBPLUGINCOMMAND cbCommand)
    {
        if(!_plugin_registercommand(pluginHandle, command, cbCommand, false))
            _plugin_logputs((std::string("[PYTHON] error registering the \"") + command + std::string("\" command!")).c_str());
    };

    regCmd("Python", cbPythonCommand);
    regCmd("Pip", cbPipCommand);
    regCmd("PyRunScript", cbPyRunScriptCommand);
    regCmd("PyRunScriptAsync", cbPyRunScriptAsyncCommand);
    regCmd("PyRunGuiScript", cbPyRunGuiScriptCommand);
    regCmd("PyDebug", [](int argc, char* argv[])
    {
        Py_DebugFlag = 1;
        Py_VerboseFlag = 1;
        return true;
    });

    // Find and set the PythonHome
    std::wstring home;
    if(!findX64dbgPythonHome(home))
    {
        _plugin_logputs("[PYTHON] Failed to find PythonHome (do you have \\Lib\\site-packages?)...");
        BridgeSettingSet("x64dbgpy", "PythonHome", "Install Python!");
        return false;
    }
    BridgeSettingSet("x64dbgpy", "PythonHome", Utf16ToUtf8(home).c_str());
    _plugin_logprintf("[PYTHON] PythonHome: \"%s\"\n", Utf16ToUtf8(home).c_str());
    Py_SetPythonHome(home.c_str());

    // Initialize threads & python interpreter
    PyEval_InitThreads();
    Py_InspectFlag = 1;
    Py_InitializeEx(0);

    // Add 'plugins' (current directory) to sys.path
    wchar_t dir[300];
    GetCurrentDirectoryW(_countof(dir), dir);
    if(dir[wcslen(dir) - 1] != L'\\')
        wcsncat_s(dir, L"\\", _TRUNCATE);
    wcsncat_s(dir, token_paste(L, module_name), _TRUNCATE);
    GetShortPathNameW(dir, dir, _countof(dir));
    _plugin_logprintf("set python load path: %s\n", Utf16ToUtf8(dir).c_str());
    PyList_Insert(PySys_GetObject("path"), 0, PyUnicode_FromUnicode(dir, wcslen(dir)));

    // Import x64dbgpy
    pModule = PyImport_Import(PyUnicode_FromString(module_name));
    if(pModule != NULL)
    {
        // Get Event Object
        pEventObject = PyObject_GetAttrString(pModule, event_object_name);
        if(pEventObject == NULL)
        {
            _plugin_logputs("[PYTHON] Could not find Event object.");
            PyErr_PrintEx(0);
        }
    }
    else
    {
        _plugin_logputs("[PYTHON] Could not import " module_name ".");
        PyErr_PrintEx(0);
    }

    PyRun_SimpleString("from " module_name " import *\n");
    return true;
}

void pyStop()
{
    // Properly ends the python environment
    Py_Finalize();
}

void pySetup()
{
    // Set Menu Icon
    ICONDATA pyIcon;
    HRSRC hRes = FindResourceW(hInst, MAKEINTRESOURCEW(IDB_PNG1), L"PNG");
    DWORD size = SizeofResource(hInst, hRes);
    HGLOBAL hMem = LoadResource(hInst, hRes);

    pyIcon.data = LockResource(hMem);
    pyIcon.size = size;
    _plugin_menuseticon(hMenu, &pyIcon);

    FreeResource(hMem);
    _plugin_menuaddentry(hMenu, MENU_RUNGUISCRIPT, "&Open GUI Script...\tAlt+F7");
    _plugin_menuaddentry(hMenu, MENU_RUNSCRIPTASYNC, "Open Async Script...");
    _plugin_menuaddentry(hMenu, MENU_ABOUT, "&About");

    // Set Callbacks
    _plugin_registercallback(pluginHandle, CB_WINEVENT, cbWinEventCallback);
    _plugin_registercallback(pluginHandle, CB_INITDEBUG, cbInitDebugCallback);
    _plugin_registercallback(pluginHandle, CB_BREAKPOINT, cbBreakPointCallback);
    _plugin_registercallback(pluginHandle, CB_STOPDEBUG, cbStopDebugCallback);
    _plugin_registercallback(pluginHandle, CB_CREATEPROCESS, cbCreateProcessCallback);
    _plugin_registercallback(pluginHandle, CB_EXITPROCESS, cbExitProcessCallback);
    _plugin_registercallback(pluginHandle, CB_CREATETHREAD, cbCreateThreadCallback);
    _plugin_registercallback(pluginHandle, CB_EXITTHREAD, cbExitThreadCallback);
    _plugin_registercallback(pluginHandle, CB_SYSTEMBREAKPOINT, cbSystemBreakpointCallback);
    _plugin_registercallback(pluginHandle, CB_LOADDLL, cbLoadDllCallback);
    _plugin_registercallback(pluginHandle, CB_UNLOADDLL, cbUnloadDllCallback);
    _plugin_registercallback(pluginHandle, CB_TRACEEXECUTE, cbTraceExecuteCallback);
}
