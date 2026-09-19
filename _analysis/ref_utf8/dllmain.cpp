// dllmain.cpp
#include "Init.hpp"
#include "gui.h"

static LONG WINAPI CrashHandler(_EXCEPTION_POINTERS* ep) {
    char buf[512];
    void* addr = ep->ExceptionRecord->ExceptionAddress;
    HMODULE mod; GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, (LPCSTR)addr, &mod);
    wchar_t modName[MAX_PATH] = L"??";
    if (mod) GetModuleFileNameW(mod, modName, MAX_PATH);
    sprintf_s(buf, "[Watermark] CRASH code=0x%08X addr=%p mod=%ws\n",
        ep->ExceptionRecord->ExceptionCode, addr, modName);
    OutputDebugStringA(buf);
    return EXCEPTION_CONTINUE_SEARCH;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
    g_hModule = hModule;
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(hModule);
        AddVectoredExceptionHandler(1, CrashHandler);
        CreateThread(NULL, 0, MainApp, NULL, 0, NULL);
        break;

    case DLL_PROCESS_DETACH:
        opengl_hook::g_isDetaching.store(true, std::memory_order_release);
        break;
    }
    return TRUE;
}