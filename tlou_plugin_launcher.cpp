#include <windows.h>
#include <tlhelp32.h>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>

static char global_directory[MAX_PATH];
static char global_logPath[MAX_PATH];

/*
    Everything goes to the log. Once loud() is called, each line is also kept
    in global_said for the failure message box.
*/
static int global_loud = 0;

static void loud(void) { global_loud = 1; }

/* What the message box shows when something has gone wrong. */
static char global_said[2048];

static void writeLog(const char* fmt, ...) {
    va_list ap;
    if (global_loud) {
        char line[512];
        va_start(ap, fmt);
        _vsnprintf_s(line, sizeof(line), _TRUNCATE, fmt, ap);
        va_end(ap);
        /* The count must not exceed the room left in global_said. */
        const size_t said = strlen(global_said);
        if (said + 1 < sizeof(global_said))
            strncat_s(global_said, sizeof(global_said), line,
                      sizeof(global_said) - said - 1);
    }

    if (!global_logPath[0]) return;
    FILE* f = nullptr;
    if (fopen_s(&f, global_logPath, "a") != 0 || !f) return;
    SYSTEMTIME st; GetLocalTime(&st);
    fprintf(f, "[%04d-%02d-%02d %02d:%02d:%02d.%03d] ",
            st.wYear, st.wMonth, st.wDay,
            st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fclose(f);
}

/*
    Shows global_said in a message box once loud() has been called. The
    program has no console; this is the only thing it puts on screen.
*/
static void showFailure() {
    if (!global_loud || !global_said[0]) return;
    MessageBoxA(nullptr, global_said, "TLOU Plugin Launcher",
                MB_OK | MB_ICONERROR);
}

/* Formats into out. Answers 0 when the result does not fit in room, and out is
   then empty. */
static int formatInto(char* out, size_t room, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    const int written = _vsnprintf_s(out, room, _TRUNCATE, fmt, ap);
    va_end(ap);
    if (written >= 0) return 1;
    out[0] = 0;
    return 0;
}

/* The log of the previous run, moved into the Archive folder in logs and
   named for the time it was last written. */
static void archiveLog(const char* logs) {
    WIN32_FILE_ATTRIBUTE_DATA about;
    if (!GetFileAttributesExA(global_logPath, GetFileExInfoStandard, &about))
        return;
    if (!about.nFileSizeLow && !about.nFileSizeHigh) return;

    /* The local time the log was written at, with that date's daylight
       saving offset. */
    SYSTEMTIME written, when;
    FileTimeToSystemTime(&about.ftLastWriteTime, &written);
    if (!SystemTimeToTzSpecificLocalTime(nullptr, &written, &when)) {
        FILETIME local;
        FileTimeToLocalFileTime(&about.ftLastWriteTime, &local);
        FileTimeToSystemTime(&local, &when);
    }

    char folder[MAX_PATH], kept[MAX_PATH];
    if (!formatInto(folder, sizeof(folder), "%s\\Archive", logs)) return;
    CreateDirectoryA(folder, nullptr);
    if (!formatInto(kept, sizeof(kept),
                    "%s\\tlou_plugin_launcher_%04u%02u%02u_%02u%02u%02u.log",
                    folder, when.wYear, when.wMonth, when.wDay, when.wHour,
                    when.wMinute, when.wSecond))
        return;
    MoveFileA(global_logPath, kept);
}

static int pathTooLong(const char* what) {
    loud();
    writeLog("The path to %s is longer than %u characters, so the game was not "
             "started.\n", what, (unsigned)(MAX_PATH - 1));
    showFailure();
    return 1;
}

static bool isProcessAlive(DWORD pid) {
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) return false;
    DWORD code = 0;
    bool alive = GetExitCodeProcess(h, &code) && code == STILL_ACTIVE;
    CloseHandle(h);
    return alive;
}

/* The names are searched in the order given. */
static DWORD findProcess(const wchar_t* const* names, int nameCount) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;

    DWORD pid = 0;
    for (int i = 0; i < nameCount && !pid; ++i) {
        PROCESSENTRY32W pe; pe.dwSize = sizeof(pe);
        if (!Process32FirstW(snap, &pe)) break;
        do {
            if (_wcsicmp(pe.szExeFile, names[i]) == 0
                && isProcessAlive(pe.th32ProcessID)) {
                pid = pe.th32ProcessID;
                break;
            }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return pid;
}

/* Whether kernel32 is in the process's module list. LoadLibrary cannot be
   injected before it is. Returns false while the process is not ready, and
   also if it has exited. */
static bool hasKernel32(DWORD pid, DWORD* lastError) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, pid);
    if (snap == INVALID_HANDLE_VALUE) {
        if (lastError) *lastError = GetLastError();
        return false;
    }
    MODULEENTRY32W me; me.dwSize = sizeof(me);
    bool found = false;
    if (Module32FirstW(snap, &me)) {
        do {
            if (_wcsicmp(me.szModule, L"kernel32.dll") == 0) { found = true; break; }
        } while (Module32NextW(snap, &me));
    }
    CloseHandle(snap);
    return found;
}

/* Whether a module is already mapped in the process. */
static bool hasModule(DWORD pid, const wchar_t* name) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, pid);
    if (snap == INVALID_HANDLE_VALUE) return false;
    MODULEENTRY32W me; me.dwSize = sizeof(me);
    bool found = false;
    if (Module32FirstW(snap, &me)) {
        do {
            if (_wcsicmp(me.szModule, name) == 0) { found = true; break; }
        } while (Module32NextW(snap, &me));
    }
    CloseHandle(snap);
    return found;
}

static bool processAlive(DWORD pid) {
    HANDLE proc = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION,
                              FALSE, pid);
    if (!proc) return false;
    const DWORD state = WaitForSingleObject(proc, 0);
    CloseHandle(proc);
    return state == WAIT_TIMEOUT;
}

static bool injectInto(DWORD pid, const char* dllPath) {
    const DWORD access = PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION
                       | PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ;
    HANDLE proc = OpenProcess(access, FALSE, pid);
    if (!proc) {
        writeLog("  OpenProcess failed (%lu)%s\n", GetLastError(),
                 GetLastError() == 5 ? " - the game is running at a higher "
                                       "integrity level, which usually means "
                                       "Steam was started as Administrator" : "");
        return false;
    }
    bool ok = false;
    size_t len = strlen(dllPath) + 1;
    void* remote = VirtualAllocEx(proc, nullptr, len, MEM_COMMIT | MEM_RESERVE,
                                  PAGE_READWRITE);
    if (!remote) {
        writeLog("  VirtualAllocEx failed (%lu)\n", GetLastError());
    } else if (!WriteProcessMemory(proc, remote, dllPath, len, nullptr)) {
        writeLog("  WriteProcessMemory failed (%lu)\n", GetLastError());
    } else {
        auto loadLib = (LPTHREAD_START_ROUTINE)GetProcAddress(
            GetModuleHandleA("kernel32.dll"), "LoadLibraryA");
        HANDLE th = CreateRemoteThread(proc, nullptr, 0, loadLib, remote, 0,
                                       nullptr);
        if (!th) {
            writeLog("  CreateRemoteThread failed (%lu)%s\n", GetLastError(),
                     GetLastError() == 5 ? " - the process is most likely "
                                           "shutting down; wait for it to close "
                                           "fully and launch again" : "");
        } else {
            DWORD w = WaitForSingleObject(th, 20000);
            DWORD code = 0;
            if (w == WAIT_OBJECT_0) GetExitCodeThread(th, &code);
            CloseHandle(th);
            if (w != WAIT_OBJECT_0) {
                writeLog("  injected thread did not finish in 20s - is the game "
                         "suspended by a debugger?\n");
            } else if (code == 0) {
                writeLog("  LoadLibrary returned 0 - the DLL did not load\n");
            } else {
                ok = true;
            }
        }
    }
    if (remote) VirtualFreeEx(proc, remote, 0, MEM_RELEASE);
    CloseHandle(proc);
    return ok;
}

int main(int argc, char** argv) {
    const DWORD length = GetModuleFileNameA(nullptr, global_directory, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) return pathTooLong("the launcher");
    char* slash = strrchr(global_directory, '\\');
    if (slash) *slash = 0;

    /*
        The tree the loader and the plugins use. Each level must exist before
        the next can be created.

            Mods                          one folder per mod, and loadorder.txt
            Mods\Plugin Loader            the loader, its settings and its log
            Mods\Plugin Loader\Logs       this launcher's log as well
    */
    char folder[MAX_PATH], iniPath[MAX_PATH], dllPath[MAX_PATH], target[MAX_PATH];
    if (!formatInto(folder, sizeof(folder), "%s\\Mods", global_directory))
        return pathTooLong("the Mods folder");
    CreateDirectoryA(folder, nullptr);
    if (!formatInto(folder, sizeof(folder), "%s\\Mods\\Plugin Loader",
                    global_directory))
        return pathTooLong("the Plugin Loader folder");
    CreateDirectoryA(folder, nullptr);
    if (!formatInto(folder, sizeof(folder), "%s\\Mods\\Plugin Loader\\Logs",
                    global_directory))
        return pathTooLong("the Plugin Loader's Logs folder");
    CreateDirectoryA(folder, nullptr);
    if (!formatInto(global_logPath, sizeof(global_logPath),
                    "%s\\tlou_plugin_launcher.log", folder))
        return pathTooLong("tlou_plugin_launcher.log");
    archiveLog(folder);

    if (!formatInto(iniPath, sizeof(iniPath),
                    "%s\\Mods\\Plugin Loader\\tlou_plugin_loader.ini",
                    global_directory))
        return pathTooLong("tlou_plugin_loader.ini");
    if (!formatInto(dllPath, sizeof(dllPath),
                    "%s\\Mods\\Plugin Loader\\tlou_plugin_loader.dll",
                    global_directory))
        return pathTooLong("tlou_plugin_loader.dll");
    GetPrivateProfileStringA("launcher", "target", "launcher.exe", target,
                             sizeof(target), iniPath);
    int waitMs = GetPrivateProfileIntA("launcher", "timeout_ms", 120000, iniPath);

    if (GetFileAttributesA(dllPath) == INVALID_FILE_ATTRIBUTES) {
        loud();
        writeLog("tlou_plugin_loader.dll was not found.\n");
        writeLog("Expected: %s\n", dllPath);
        showFailure();
        return 1;
    }

    /* Real game executables first, then the variants the game's own launcher
       may start and discard. */
    static const wchar_t* gameNames[] = { L"tlou-i.exe", L"tlou-ii.exe",
                                          L"tlou-i-l.exe", L"tlou-ii-l.exe" };
    const int gameNameCount = 4;

    DWORD pid = findProcess(gameNames, gameNameCount);
    if (!pid) {
        /* The longest command line CreateProcessA accepts. */
        static char cmd[32768];
        int fits = formatInto(cmd, sizeof(cmd), "\"%s\\%s\"",
                              global_directory, target);
        for (int i = 1; i < argc && fits; ++i) {
            const size_t used = strlen(cmd);
            fits = formatInto(cmd + used, sizeof(cmd) - used, " %s", argv[i]);
        }
        if (!fits) {
            loud();
            writeLog("the command line for %s is longer than %u characters, so "
                     "it was not started.\n", target,
                     (unsigned)(sizeof(cmd) - 1));
            showFailure();
            return 1;
        }
        STARTUPINFOA si = { sizeof(si) };
        PROCESS_INFORMATION pi = {};
        if (!CreateProcessA(nullptr, cmd, nullptr, nullptr, FALSE, 0, nullptr,
                            global_directory, &si, &pi)) {
            loud();
            writeLog("could not start %s (%lu)\n", target, GetLastError());
            showFailure();
            return 1;
        }
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }

    /*
        A pid must hold still for settleMs before it is injected into, and must
        still be alive afterwards before the run counts as a success.
    */
    const int settleMs = 1500;
    const int attempts = 5;

    for (int attempt = 1; attempt <= attempts; ++attempt) {
        bool ready = false;
        DWORD lastError = 0;
        int stable = 0;
        for (int waited = 0; waited <= waitMs; waited += 250) {
            DWORD now = findProcess(gameNames, gameNameCount);
            if (now && now != pid) {
                pid = now;
                stable = 0;
            } else if (!now && pid) {
                pid = 0;
                stable = 0;
            } else if (now) {
                stable += 250;
            }
            if (pid && stable >= settleMs && hasKernel32(pid, &lastError)) {
                ready = true;
                break;
            }
            Sleep(250);
        }

        if (!ready) {
            loud();
            if (!pid) {
                writeLog("no game process appeared within %d ms.\n", waitMs);
                writeLog("launcher.exe may still be running a prerequisite step.\n");
            } else {
                writeLog("pid %lu never settled within %d ms "
                         "(last module snapshot error %lu).\n", pid, waitMs,
                         lastError);
            }
            showFailure();
            return 1;
        }

        if (hasModule(pid, L"tlou_plugin_loader.dll")) {
            loud();
            writeLog("pid %lu already has tlou_plugin_loader.dll mapped, so "
                     "this game was started earlier and is still running.\n", pid);
            writeLog("Loading it again would run no plugin at all: Windows only "
                     "raises the reference count and DllMain does not run "
                     "twice.\n");
            writeLog("Quit the game, make sure no tlou-i.exe is left in Task "
                     "Manager, then start the launcher again.\n");
            showFailure();
            return 1;
        }

        if (!injectInto(pid, dllPath)) {
            loud();
            writeLog("injection failed; the reason is in %s\n", global_logPath);
            showFailure();
            return 1;
        }

        Sleep(2000);
        if (!processAlive(pid)) {
            pid = 0;
            continue;
        }
        return 0;
    }

    loud();
    writeLog("gave up after %d attempt(s); every process injected into died "
             "immediately.\n", attempts);
    showFailure();
    return 1;
}
