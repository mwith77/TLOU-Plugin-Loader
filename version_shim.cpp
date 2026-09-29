/*
    version_shim.cpp - the stand-in version.dll.

    The game imports version.dll, so Windows loads this one from the game folder
    in the real one's place. It does two things and nothing else:

      - it republishes every version.dll export and forwards each to the real
        version.dll in the system folder, so the game and everything in it that
        calls version.dll still reaches the real code;
      - it loads the real loader, Mods\Plugin Loader\tlou_plugin_loader.dll,
        which does all the work.

    It carries no hook engine and no plugin logic, so it never has to change.
    Ship it once; update tlou_plugin_loader.dll as often as you like. The
    forwarding thunks are in version_shim.asm.
*/

#include <windows.h>

/* The real version.dll's exports, filled by resolveRealVersion at load. The
   thunks in version_shim.asm tail-jump through these. */
extern "C" {
    void* real_GetFileVersionInfoA        = nullptr;
    void* real_GetFileVersionInfoByHandle = nullptr;
    void* real_GetFileVersionInfoExA      = nullptr;
    void* real_GetFileVersionInfoExW      = nullptr;
    void* real_GetFileVersionInfoSizeA    = nullptr;
    void* real_GetFileVersionInfoSizeExA  = nullptr;
    void* real_GetFileVersionInfoSizeExW  = nullptr;
    void* real_GetFileVersionInfoSizeW    = nullptr;
    void* real_GetFileVersionInfoW        = nullptr;
    void* real_VerFindFileA               = nullptr;
    void* real_VerFindFileW               = nullptr;
    void* real_VerInstallFileA            = nullptr;
    void* real_VerInstallFileW            = nullptr;
    void* real_VerQueryValueA             = nullptr;
    void* real_VerQueryValueW             = nullptr;
}

#pragma comment(linker, "/export:GetFileVersionInfoA")
#pragma comment(linker, "/export:GetFileVersionInfoByHandle")
#pragma comment(linker, "/export:GetFileVersionInfoExA")
#pragma comment(linker, "/export:GetFileVersionInfoExW")
#pragma comment(linker, "/export:GetFileVersionInfoSizeA")
#pragma comment(linker, "/export:GetFileVersionInfoSizeExA")
#pragma comment(linker, "/export:GetFileVersionInfoSizeExW")
#pragma comment(linker, "/export:GetFileVersionInfoSizeW")
#pragma comment(linker, "/export:GetFileVersionInfoW")
#pragma comment(linker, "/export:VerFindFileA")
#pragma comment(linker, "/export:VerFindFileW")
#pragma comment(linker, "/export:VerInstallFileA")
#pragma comment(linker, "/export:VerInstallFileW")
#pragma comment(linker, "/export:VerQueryValueA")
#pragma comment(linker, "/export:VerQueryValueW")

/* Points every real_ pointer at the matching export of the system version.dll.
   A name that does not resolve is left null, and its thunk returns zero. */
static void resolveRealVersion() {
    WCHAR path[MAX_PATH];
    const UINT n = GetSystemDirectoryW(path, MAX_PATH);
    if (n == 0 || n > MAX_PATH - 16) return;
    lstrcatW(path, L"\\version.dll");
    const HMODULE real = LoadLibraryW(path);
    if (!real) return;

    struct { const char* name; void** slot; } exports[] = {
        { "GetFileVersionInfoA",        &real_GetFileVersionInfoA },
        { "GetFileVersionInfoByHandle", &real_GetFileVersionInfoByHandle },
        { "GetFileVersionInfoExA",      &real_GetFileVersionInfoExA },
        { "GetFileVersionInfoExW",      &real_GetFileVersionInfoExW },
        { "GetFileVersionInfoSizeA",    &real_GetFileVersionInfoSizeA },
        { "GetFileVersionInfoSizeExA",  &real_GetFileVersionInfoSizeExA },
        { "GetFileVersionInfoSizeExW",  &real_GetFileVersionInfoSizeExW },
        { "GetFileVersionInfoSizeW",    &real_GetFileVersionInfoSizeW },
        { "GetFileVersionInfoW",        &real_GetFileVersionInfoW },
        { "VerFindFileA",               &real_VerFindFileA },
        { "VerFindFileW",               &real_VerFindFileW },
        { "VerInstallFileA",            &real_VerInstallFileA },
        { "VerInstallFileW",            &real_VerInstallFileW },
        { "VerQueryValueA",             &real_VerQueryValueA },
        { "VerQueryValueW",             &real_VerQueryValueW },
    };
    for (const auto& e : exports)
        *e.slot = (void*)GetProcAddress(real, e.name);
}

/* The real loader is at <game>\Mods\Plugin Loader\tlou_plugin_loader.dll.
   Loading it runs its DllMain, which starts the loader on its own thread. This
   runs on a thread of its own so the load is off the loader lock. */
static DWORD WINAPI loadRealLoader(LPVOID) {
    WCHAR path[MAX_PATH];
    const DWORD n = GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return 0;
    WCHAR* slash = wcsrchr(path, L'\\');
    if (!slash) return 0;
    *slash = 0;

    static const WCHAR tail[] = L"\\Mods\\Plugin Loader\\tlou_plugin_loader.dll";
    if (lstrlenW(path) + (int)(sizeof(tail) / sizeof(WCHAR)) > MAX_PATH) return 0;
    lstrcatW(path, tail);
    LoadLibraryW(path);
    return 0;
}

BOOL APIENTRY DllMain(HMODULE self, DWORD reason, LPVOID) {
    if (reason != DLL_PROCESS_ATTACH) return TRUE;
    DisableThreadLibraryCalls(self);

    /* Resolve the real exports before the game can call them, then load the
       loader off the loader lock. */
    resolveRealVersion();
    const HANDLE thread = CreateThread(nullptr, 0, loadRealLoader, nullptr, 0,
                                       nullptr);
    if (thread) CloseHandle(thread);
    return TRUE;
}
