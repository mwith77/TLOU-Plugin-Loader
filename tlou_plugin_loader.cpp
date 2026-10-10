#include <windows.h>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <tlhelp32.h>
#include <psapi.h>

#undef near
#undef far

#include "tlou_plugin_sdk.h"
#include "hook_finder.h"

#pragma comment(lib, "version.lib")

/* The loader's version, for its own log lines. Keep it the same as
   tlou_plugin_loader.rc. */
#define LOADER_VERSION "v1.1.0"

/* Lines of normal function, used by the beta test (v1.0.1_beta_1): the
   program the loader finds itself in when that is not the game, and how long
   the game's code took to be in place. 0 in a release, which logs only
   failures. */
#define LOADER_BETA_LINES 0

#if defined(_MSC_VER)
#define GUARD_TRY    __try
#define GUARD_EXCEPT __except (EXCEPTION_EXECUTE_HANDLER)
#else
#define GUARD_TRY    if (1)
#define GUARD_EXCEPT else
#endif

static char global_gameDir[MAX_PATH];
static char global_modsDir[MAX_PATH];
/* The loader's folder for its own files, Mods\Plugin Loader in the game's
   folder, wherever the loader's own file lies: tlou_plugin_loader.ini,
   loadorder.txt, its log and the hook finder's hook_cache.ini. */
static char global_ownFolder[MAX_PATH];
static char global_logPath[MAX_PATH];
static char global_iniPath[MAX_PATH];
static char global_loadorderPath[MAX_PATH];
static char global_exeName[64];

static HMODULE  global_selfModule = nullptr;
static uint64_t global_gameModuleBase = 0;
static uint64_t global_gameModuleSize = 0;
static uint64_t global_gameTextBase = 0;
static uint64_t global_gameTextSize = 0;
static uint32_t global_gameTimeDateStamp = 0;
static uint32_t global_gameCheckSum = 0;
static uint32_t global_gameFileVersion[4] = { 0, 0, 0, 0 };

static CRITICAL_SECTION global_logLock;
static bool global_logLockReady = false;

/*
    The log is one file, Mods\Plugin Loader\tlou_plugin_loader.log, newest
    line first: each line is written above the lines already there, the
    earlier runs' included. It is at most LOG_MOST_BYTES; the oldest lines,
    at its end, are left out to keep it there. Each line is written through
    <log>.new, renamed over the log. global_logBuffer is used only under
    global_logLock, or before the lock exists, while one thread runs.
*/
#define LOG_MOST_BYTES (1024u * 1024u)
#define LOG_LINE_MOST  4096

static char global_logBuffer[LOG_MOST_BYTES];

static void logRaw(const char* plugin, const char* fmt, va_list ap) {
    if (!global_logPath[0]) return;
    char line[LOG_LINE_MOST];
    SYSTEMTIME st; GetLocalTime(&st);
    int used = sprintf_s(line, "[%04d-%02d-%02d %02d:%02d:%02d.%03d] ",
                         st.wYear, st.wMonth, st.wDay,
                         st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    if (used < 0) return;
    if (plugin && *plugin) {
        const int named = sprintf_s(line + used, sizeof(line) - used, "%-20s ",
                                    plugin);
        if (named > 0) used += named;
    }
    const int text = _vsnprintf_s(line + used, sizeof(line) - used - 1,
                                  _TRUNCATE, fmt, ap);
    used = text < 0 ? (int)strlen(line) : used + text;
    line[used++] = '\n';

    if (global_logLockReady) EnterCriticalSection(&global_logLock);
    char* out = global_logBuffer;
    memcpy(out, line, (size_t)used);
    size_t total = (size_t)used;
    const HANDLE old = CreateFileA(global_logPath, GENERIC_READ,
                                   FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                   FILE_ATTRIBUTE_NORMAL, nullptr);
    if (old != INVALID_HANDLE_VALUE) {
        DWORD got = 0;
        const DWORD room = (DWORD)(LOG_MOST_BYTES - total);
        if (ReadFile(old, out + total, room, &got, nullptr)) {
            LARGE_INTEGER size;
            if (GetFileSizeEx(old, &size) && size.QuadPart > (LONGLONG)got)
                while (got > 0 && out[total + got - 1] != '\n') --got;
            total += got;
        }
        CloseHandle(old);
    }
    char fresh[MAX_PATH];
    if (sprintf_s(fresh, "%s.new", global_logPath) > 0) {
        const HANDLE file = CreateFileA(fresh, GENERIC_WRITE, 0, nullptr,
                                        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL,
                                        nullptr);
        if (file != INVALID_HANDLE_VALUE) {
            DWORD wrote = 0;
            const BOOL ok = WriteFile(file, out, (DWORD)total, &wrote, nullptr)
                         && wrote == (DWORD)total;
            CloseHandle(file);
            if (ok) MoveFileExA(fresh, global_logPath, MOVEFILE_REPLACE_EXISTING);
            else DeleteFileA(fresh);
        }
    }
    if (global_logLockReady) LeaveCriticalSection(&global_logLock);
}

static void writeLog(const char* fmt, ...) {
    va_list ap; va_start(ap, fmt); logRaw(nullptr, fmt, ap); va_end(ap);
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

/* The folder holding a module's file. Answers 0 when the path does not fit in
   room, and out is then empty. */
static int moduleFolder(HMODULE module, char* out, size_t room) {
    const DWORD length = GetModuleFileNameA(module, out, (DWORD)room);
    if (length == 0 || length >= room) { out[0] = 0; return 0; }
    char* slash = strrchr(out, '\\');
    if (slash) *slash = 0;
    return 1;
}

/* --------------------------------------------------- the default settings */

/*
    The default settings are the RCDATA resource DEFAULT_SETTINGS: the
    plugin's ini as it stands in the project. Read as an ini, a line [name]
    opens a section, a line key = value below it is a setting of that
    section, and a line opening with ; is a comment. The blanks around a name,
    a key or a value are not part of it. Lines end in CR, LF or CR LF.
*/

/* The image this code is linked into; its address is its module handle. */
extern "C" IMAGE_DOS_HEADER __ImageBase;

/* The three bytes of a UTF-8 byte-order mark. */
static const char utf8ByteOrderMark[] = "\xEF\xBB\xBF";

/* The default settingIsPresent gives GetPrivateProfileStringA. No value in a
   settings file is this text. */
static const char missingSettingMark[] = "\x01missing\x01";

/*
    The RCDATA resource DEFAULT_SETTINGS of module, less a UTF-8 byte-order
    mark at its start: *length bytes, not ended by a zero. nullptr, with 0 in
    *length, where module holds no such resource or it holds nothing more.
*/
static const char* defaultSettingsText(HMODULE module, DWORD* length) {
    *length = 0;
    const HRSRC found = FindResourceA(module, "DEFAULT_SETTINGS",
                                      (LPCSTR)RT_RCDATA);
    const HGLOBAL loaded = found ? LoadResource(module, found) : nullptr;
    const char* text = loaded ? (const char*)LockResource(loaded) : nullptr;
    if (!text) return nullptr;
    DWORD size = SizeofResource(module, found);
    if (size >= 3 && memcmp(text, utf8ByteOrderMark, 3) == 0) {
        text += 3;
        size -= 3;
    }
    if (!size) return nullptr;
    *length = size;
    return text;
}

/*
    1 where the settings file at path holds key in section, whatever its
    value, an empty one included; 0 where Windows reports the key absent
    (ERROR_FILE_NOT_FOUND, which also stands for no such section and no such
    file); -1 where the file could not be read, for example while another
    program holds it open without read sharing (ERROR_SHARING_VIOLATION),
    with the error in GetLastError.
*/
static int settingIsPresent(const char* path, const char* section,
                            const char* key) {
    char held[sizeof(missingSettingMark) + 1];
    SetLastError(0);
    GetPrivateProfileStringA(section, key, missingSettingMark, held,
                             (DWORD)sizeof(held), path);
    const DWORD error = GetLastError();
    if (strcmp(held, missingSettingMark) != 0) return 1;
    if (error == ERROR_FILE_NOT_FOUND) return 0;
    SetLastError(error);
    return -1;
}

/* One setting of the default settings: its section, key and value, each
   without the blanks at its ends. */
struct DefaultSetting {
    char section[256];
    char key[256];
    char value[512];
};

/* The text from first up to last, without the blanks at its ends, copied
   into out with a zero after it. Answers 0, with out empty, where it does not
   fit in room. */
static int copyTrimmed(char* out, size_t room, const char* first,
                       const char* last) {
    while (first < last && (*first == ' ' || *first == '\t')) ++first;
    while (last > first && (last[-1] == ' ' || last[-1] == '\t')) --last;
    out[0] = 0;
    if ((size_t)(last - first) >= room) return 0;
    memcpy(out, first, (size_t)(last - first));
    out[last - first] = 0;
    return 1;
}

/*
    The next setting of the default settings text from *at up to end, read
    into setting, with *at moved past its line. Answers 0 once no setting is
    left. setting->section is empty before the first call and then holds the
    section last opened. A line with no =, a setting before the first section
    or below a [ line with no ], and a line too long for setting are passed
    over.
*/
static int nextDefaultSetting(const char** at, const char* end,
                              DefaultSetting* setting) {
    while (*at < end) {
        const char* first = *at;
        const char* last = first;
        while (last < end && *last != '\r' && *last != '\n') ++last;
        *at = last;
        while (*at < end && (**at == '\r' || **at == '\n')) ++*at;

        while (first < last && (*first == ' ' || *first == '\t')) ++first;
        if (first == last || *first == ';') continue;
        if (*first == '[') {
            const char* close = (const char*)memchr(first, ']',
                                                    (size_t)(last - first));
            if (!close
                || !copyTrimmed(setting->section, sizeof(setting->section),
                                first + 1, close))
                setting->section[0] = 0;
            continue;
        }
        const char* equals = (const char*)memchr(first, '=',
                                                 (size_t)(last - first));
        if (!setting->section[0] || !equals
            || !copyTrimmed(setting->key, sizeof(setting->key), first, equals)
            || !setting->key[0]
            || !copyTrimmed(setting->value, sizeof(setting->value), equals + 1,
                            last))
            continue;
        return 1;
    }
    return 0;
}

/*
    Adds to the settings file at path each setting of the default settings
    text (length bytes) that Windows reports absent from it, in its section
    with its default value, through WritePrivateProfileStringA. A setting the
    file holds, an empty value included, and every other line of the file
    are left as they are. Stops at the first setting that cannot be looked up
    or added, with one line in the log.
*/
static void addMissingSettings(const char* path, const char* text,
                               DWORD length) {
    const char* at = text;
    DefaultSetting setting = {};
    while (nextDefaultSetting(&at, text + length, &setting)) {
        const int present = settingIsPresent(path, setting.section,
                                             setting.key);
        if (present > 0) continue;
        if (present < 0) {
            const DWORD error = GetLastError();
            writeLog("SETTINGS %s could not be read (Windows error %lu), so "
                     "[%s] %s and the settings after it were not looked up, "
                     "and those it lacks take their default values", path,
                     error, setting.section, setting.key);
            return;
        }
        if (!WritePrivateProfileStringA(setting.section, setting.key,
                                        setting.value, path)) {
            const DWORD error = GetLastError();
            writeLog("SETTINGS [%s] %s could not be added to %s (Windows error "
                     "%lu), so it and the settings after it that the file "
                     "lacks take their default values", setting.section,
                     setting.key, path, error);
            return;
        }
    }
}

/*
    Gives the settings file at path the default settings built into module:
    - no file, or an empty one (0 bytes): the text whole, comments included,
      written to <path>.new, which is flushed, closed and moved to path. The
      move never replaces a file. An empty file is first deleted, through a
      handle no other may share, and only while that handle shows it empty;
      an empty file that cannot be deleted is left as it is. Where a file is
      at path at the time of the move, the .new file is deleted and the
      settings that file lacks are added instead.
    - a file that starts with a UTF-8 byte-order mark: nothing.
    - any other file: each setting it lacks, added by addMissingSettings.
    Each failure writes one line to the log; a .new file this wrote is
    deleted after one.
*/
static void writeDefaultSettings(HMODULE module, const char* path) {
    if (!path || !path[0]) return;
    DWORD length = 0;
    const char* const text = defaultSettingsText(module, &length);
    if (!text) {
        writeLog("SETTINGS the default settings are not built into this DLL, "
                 "so none were written to %s", path);
        return;
    }

    /* The file's size and first three bytes, read under a shared lock on the
       whole file, which waits while WritePrivateProfileStringA writes it. */
    int empty = 0;
    const HANDLE file = CreateFileA(path, GENERIC_READ,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE
                                        | FILE_SHARE_DELETE,
                                    nullptr, OPEN_EXISTING,
                                    FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        const DWORD error = GetLastError();
        if (error != ERROR_FILE_NOT_FOUND) {
            writeLog("SETTINGS %s could not be read (Windows error %lu), so "
                     "no settings were added to it, and those it lacks take "
                     "their default values", path, error);
            return;
        }
    } else {
        OVERLAPPED whole = {};
        LARGE_INTEGER size = {};
        char first[3] = {};
        DWORD got = 0;
        const BOOL locked = LockFileEx(file, 0, 0, MAXDWORD, MAXDWORD, &whole);
        const BOOL looked = locked && GetFileSizeEx(file, &size)
                            && ReadFile(file, first, (DWORD)sizeof(first),
                                        &got, nullptr);
        const DWORD error = looked ? ERROR_SUCCESS : GetLastError();
        if (locked) UnlockFileEx(file, 0, MAXDWORD, MAXDWORD, &whole);
        CloseHandle(file);
        if (!looked) {
            writeLog("SETTINGS %s could not be read (Windows error %lu), so "
                     "no settings were added to it, and those it lacks take "
                     "their default values", path, error);
            return;
        }
        if (size.QuadPart > 0) {
            if (got == sizeof(first)
                && memcmp(first, utf8ByteOrderMark, sizeof(first)) == 0)
                writeLog("SETTINGS %s starts with a UTF-8 byte-order mark, "
                         "which Windows reads as part of its first line, so no "
                         "settings were added to it", path);
            else
                addMissingSettings(path, text, length);
            return;
        }
        empty = 1;
    }

    char newPath[MAX_PATH];
    if (_snprintf_s(newPath, sizeof(newPath), _TRUNCATE, "%s.new", path) < 0) {
        writeLog("SETTINGS %s is too long a path to write the default "
                 "settings beside it", path);
        return;
    }
    const HANDLE out = CreateFileA(newPath, GENERIC_WRITE, 0, nullptr,
                                   CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL,
                                   nullptr);
    if (out == INVALID_HANDLE_VALUE) {
        const DWORD error = GetLastError();
        writeLog("SETTINGS %s could not be made (Windows error %lu), so the "
                 "default settings were not written to %s", newPath, error,
                 path);
        return;
    }
    DWORD written = 0;
    DWORD error = ERROR_SUCCESS;
    if (!WriteFile(out, text, length, &written, nullptr)) error = GetLastError();
    else if (written != length) error = ERROR_WRITE_FAULT;
    else if (!FlushFileBuffers(out)) error = GetLastError();
    if (!CloseHandle(out) && error == ERROR_SUCCESS) error = GetLastError();

    if (error == ERROR_SUCCESS) {
        /* Why an empty file at path stays: ERROR_SUCCESS where it was
           deleted, is gone, or holds something by now. */
        DWORD kept = ERROR_SUCCESS;
        if (empty) {
            const HANDLE old = CreateFileA(path, DELETE | FILE_READ_ATTRIBUTES,
                                           0, nullptr, OPEN_EXISTING,
                                           FILE_ATTRIBUTE_NORMAL, nullptr);
            if (old == INVALID_HANDLE_VALUE) {
                if (GetLastError() != ERROR_FILE_NOT_FOUND) kept = GetLastError();
            } else {
                LARGE_INTEGER size = {};
                FILE_DISPOSITION_INFO gone = { TRUE };
                if (GetFileSizeEx(old, &size) && size.QuadPart == 0
                    && !SetFileInformationByHandle(old, FileDispositionInfo,
                                                   &gone, (DWORD)sizeof(gone)))
                    kept = GetLastError();
                CloseHandle(old);
            }
        }
        if (kept != ERROR_SUCCESS) {
            DeleteFileA(newPath);
            writeLog("SETTINGS %s is empty and could not be replaced (Windows "
                     "error %lu), so the default settings were not written to "
                     "it, and the settings take their default values", path,
                     kept);
            return;
        }
        if (MoveFileExA(newPath, path, 0)) return;
        error = GetLastError();
    }
    DeleteFileA(newPath);
    if (error == ERROR_ALREADY_EXISTS || error == ERROR_FILE_EXISTS) {
        addMissingSettings(path, text, length);
        return;
    }
    writeLog("SETTINGS the default settings could not be written to %s "
             "(Windows error %lu), so the settings take their default values",
             path, error);
}

/*
    Reads the open file from its file pointer to its end. Answers 1 where it
    holds nothing but blanks, tabs and line ends (0 bytes included), 0 where it
    holds anything else, and -1 where it could not be read, with the Windows
    error in *error.
*/
static int holdsOnlyBlanks(HANDLE file, DWORD* error) {
    char buffer[4096];
    for (;;) {
        DWORD got = 0;
        if (!ReadFile(file, buffer, sizeof(buffer), &got, nullptr)) {
            *error = GetLastError();
            return -1;
        }
        if (!got) return 1;
        for (DWORD i = 0; i < got; ++i)
            if (buffer[i] != ' ' && buffer[i] != '\t' && buffer[i] != '\r'
                && buffer[i] != '\n')
                return 0;
    }
}

/*
    Whether the load order file at path is empty, which counts as missing.
    Answers 1 where it holds nothing but blanks, tabs and line ends (0 bytes
    included), 0 where it holds anything else, and -1 where it does not exist
    or could not be read, with the Windows error in *error
    (ERROR_FILE_NOT_FOUND where it does not exist).
*/
static int loadOrderIsEmpty(const char* path, DWORD* error) {
    const HANDLE file = CreateFileA(path, GENERIC_READ,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE
                                        | FILE_SHARE_DELETE,
                                    nullptr, OPEN_EXISTING,
                                    FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        *error = GetLastError();
        return -1;
    }
    const int empty = holdsOnlyBlanks(file, error);
    CloseHandle(file);
    return empty;
}

/*
    Writes the RCDATA resource DEFAULT_LOAD_ORDER of module, less a UTF-8
    byte-order mark at its start, whole as the file at path where there is no
    file there or it is empty (nothing but blanks, tabs and line ends, 0 bytes
    included). The text goes into <path>.new, which is flushed, closed and
    moved to path; the move never replaces a file. An empty file is first
    deleted, through a handle no other may share, and only while that handle
    shows it empty. A file with something in it, one that appeared meanwhile
    included, is left as it is. Each failure writes one line to the log; a .new
    file this wrote and did not move is deleted.
*/
static void writeDefaultLoadOrder(HMODULE module, const char* path) {
    const HRSRC found = FindResourceA(module, "DEFAULT_LOAD_ORDER",
                                      (LPCSTR)RT_RCDATA);
    const HGLOBAL loaded = found ? LoadResource(module, found) : nullptr;
    const char* text = loaded ? (const char*)LockResource(loaded) : nullptr;
    DWORD length = text ? SizeofResource(module, found) : 0;
    if (length >= 3 && memcmp(text, utf8ByteOrderMark, 3) == 0) {
        text += 3;
        length -= 3;
    }
    if (!length) {
        writeLog("the default load order is not built into this DLL, so none "
                 "was written to %s", path);
        return;
    }

    DWORD error = ERROR_SUCCESS;
    const int empty = loadOrderIsEmpty(path, &error);
    if (empty == 0) return;
    if (empty < 0 && error != ERROR_FILE_NOT_FOUND) {
        writeLog("%s could not be read (Windows error %lu), so the default "
                 "load order was not written to it", path, error);
        return;
    }

    char newPath[MAX_PATH];
    if (_snprintf_s(newPath, sizeof(newPath), _TRUNCATE, "%s.new", path) < 0) {
        writeLog("%s is too long a path to write the default load order "
                 "beside it", path);
        return;
    }
    const HANDLE out = CreateFileA(newPath, GENERIC_WRITE, 0, nullptr,
                                   CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL,
                                   nullptr);
    if (out == INVALID_HANDLE_VALUE) {
        error = GetLastError();
        writeLog("%s could not be made (Windows error %lu), so the default "
                 "load order was not written to %s", newPath, error, path);
        return;
    }
    DWORD written = 0;
    error = ERROR_SUCCESS;
    if (!WriteFile(out, text, length, &written, nullptr))
        error = GetLastError();
    else if (written != length) error = ERROR_WRITE_FAULT;
    else if (!FlushFileBuffers(out)) error = GetLastError();
    if (!CloseHandle(out) && error == ERROR_SUCCESS) error = GetLastError();

    if (error == ERROR_SUCCESS && empty == 1) {
        const HANDLE old = CreateFileA(path, GENERIC_READ | DELETE, 0, nullptr,
                                       OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
                                       nullptr);
        if (old == INVALID_HANDLE_VALUE) {
            if (GetLastError() != ERROR_FILE_NOT_FOUND) error = GetLastError();
        } else {
            FILE_DISPOSITION_INFO gone = { TRUE };
            const int stillEmpty = holdsOnlyBlanks(old, &error);
            if (stillEmpty == 0) error = ERROR_ALREADY_EXISTS;
            else if (stillEmpty == 1
                     && !SetFileInformationByHandle(old, FileDispositionInfo,
                                                    &gone, (DWORD)sizeof(gone)))
                error = GetLastError();
            CloseHandle(old);
        }
    }
    if (error == ERROR_SUCCESS) {
        if (MoveFileExA(newPath, path, 0)) return;
        error = GetLastError();
    }
    DeleteFileA(newPath);
    if (error == ERROR_ALREADY_EXISTS || error == ERROR_FILE_EXISTS) return;
    writeLog("the default load order could not be written to %s (Windows "
             "error %lu), so the plugins load in DLL file name order", path,
             error);
}

/*
    Moves Mods\loadorder.txt, where earlier versions of the loader kept the
    load order, to path, where no file is at path or the file there is empty
    (nothing but blanks, tabs and line ends, 0 bytes included), and
    Mods\loadorder.txt is not empty or cannot be read. Only an empty file at
    path is replaced. A move that fails for any reason but Mods\loadorder.txt
    not existing writes one line to the log.
*/
static void moveOldLoadOrder(const char* path) {
    char oldPath[MAX_PATH];
    if (!formatInto(oldPath, sizeof(oldPath), "%s\\loadorder.txt",
                    global_modsDir))
        return;
    DWORD error = ERROR_SUCCESS;
    const int empty = loadOrderIsEmpty(path, &error);
    if (empty == 0 || (empty < 0 && error != ERROR_FILE_NOT_FOUND)) return;
    const int oldEmpty = loadOrderIsEmpty(oldPath, &error);
    if (oldEmpty == 1 || (oldEmpty < 0 && error == ERROR_FILE_NOT_FOUND))
        return;
    if (MoveFileExA(oldPath, path, empty == 1 ? MOVEFILE_REPLACE_EXISTING : 0))
        return;
    error = GetLastError();
    if (error == ERROR_FILE_NOT_FOUND) return;
    writeLog("the load order %s could not be moved to %s (Windows error %lu), "
             "so the order it gives is not used", oldPath, path, error);
}

/* Parses "4D 8B 45 ?? 48" into bytes plus a wildcard mask. Returns the byte
   count, or -1 if the string is malformed. */
static int parsePattern(const char* p, uint8_t* bytes, bool* mask, int maxLen) {
    int n = 0;
    while (*p && n < maxLen) {
        while (*p == ' ' || *p == '\t') ++p;
        if (!*p) break;
        if (p[0] == '?') {
            bytes[n] = 0; mask[n] = false; ++n;
            p += (p[1] == '?') ? 2 : 1;
        } else {
            char* end = nullptr;
            long v = strtol(p, &end, 16);
            if (end == p || end - p > 2 || v < 0 || v > 255) return -1;
            bytes[n] = (uint8_t)v; mask[n] = true; ++n;
            p = end;
        }
    }
    return n ? n : -1;
}

/* --------------------------------------------------------------------------
   Exports. Plugins resolve these by name: renaming or removing one breaks the
   plugins that use it.
   The handle argument is the loader module and is not used.
   -------------------------------------------------------------------------- */

#define LOADER_EXPORT extern "C" __declspec(dllexport)

LOADER_EXPORT void LoaderLog(LoaderHandle, const char* plugin,
                             const char* fmt, ...) {
    va_list ap; va_start(ap, fmt); logRaw(plugin, fmt, ap); va_end(ap);
}

LOADER_EXPORT void* LoaderFindPattern(LoaderHandle, const char* pattern) {
    uint8_t bytes[256]; bool mask[256];
    int n = parsePattern(pattern, bytes, mask, 256);
    if (n <= 0) {
        writeLog("LoaderFindPattern: malformed pattern \"%s\"", pattern);
        return nullptr;
    }
    uint8_t* base = (uint8_t*)global_gameTextBase;
    size_t size = (size_t)global_gameTextSize;
    if (size < (size_t)n) return nullptr;
    for (size_t i = 0; i + n <= size; ++i) {
        bool ok = true;
        for (int j = 0; j < n; ++j)
            if (mask[j] && base[i + j] != bytes[j]) { ok = false; break; }
        if (ok) return base + i;
    }
    return nullptr;
}

LOADER_EXPORT int LoaderPatch(LoaderHandle, void* address, const void* bytes,
                              size_t size, const void* expect) {
    if (!address || !bytes || !size) return 0;
    if (expect && memcmp(address, expect, size) != 0) {
        writeLog("LoaderPatch refused at %p: existing bytes do not match expectation",
             address);
        return 0;
    }
    DWORD old;
    if (!VirtualProtect(address, size, PAGE_EXECUTE_READWRITE, &old)) {
        writeLog("LoaderPatch failed at %p: VirtualProtect error %lu", address,
             GetLastError());
        return 0;
    }
    memcpy(address, bytes, size);
    VirtualProtect(address, size, old, &old);
    FlushInstructionCache(GetCurrentProcess(), address, size);
    return 1;
}

LOADER_EXPORT void* LoaderAllocNearGame(LoaderHandle, size_t size) {
    SYSTEM_INFO si; GetSystemInfo(&si);
    const uintptr_t gran = si.dwAllocationGranularity;
    uintptr_t start = (uintptr_t)global_gameModuleBase;
    for (int dir = 0; dir < 2; ++dir)
        for (uintptr_t off = gran; off < 0x70000000ull; off += gran) {
            if (dir == 0 && off > start) break;
            uintptr_t addr = dir == 0 ? start - off : start + off;
            void* p = VirtualAlloc((void*)(addr & ~(gran - 1)), size,
                                   MEM_RESERVE | MEM_COMMIT,
                                   PAGE_EXECUTE_READWRITE);
            if (p) return p;
        }
    return nullptr;
}

LOADER_EXPORT uint64_t LoaderGameModuleBase(LoaderHandle) { return global_gameModuleBase; }
LOADER_EXPORT uint64_t LoaderGameModuleSize(LoaderHandle) { return global_gameModuleSize; }
LOADER_EXPORT uint64_t LoaderGameTextBase(LoaderHandle)   { return global_gameTextBase; }
LOADER_EXPORT uint64_t LoaderGameTextSize(LoaderHandle)   { return global_gameTextSize; }
LOADER_EXPORT uint32_t LoaderGameTimeDateStamp(LoaderHandle) { return global_gameTimeDateStamp; }
LOADER_EXPORT uint32_t LoaderGameCheckSum(LoaderHandle)   { return global_gameCheckSum; }
LOADER_EXPORT const char* LoaderGameExeName(LoaderHandle) { return global_exeName; }

LOADER_EXPORT uint32_t LoaderGameFileVersion(LoaderHandle, int index) {
    return (index >= 0 && index < 4) ? global_gameFileVersion[index] : 0;
}

/*
    A mod owns a folder and everything under it. These four answer for the mod
    whose Init is running, and outside Init for whichever mod initialised
    last. A plugin must read them during Init and keep a copy.

        LoaderModFolder      Mods\<mod>
        LoaderConfigFolder   the same folder, where the mod's ini lives
        LoaderLogsFolder     the same folder, where the mod keeps its log
        LoaderCacheFolder    the same folder, where the mod keeps any cache

    The loader makes no folder inside a mod's folder.
*/
static char global_modFolder[MAX_PATH];

LOADER_EXPORT const char* LoaderGameFolder(LoaderHandle)    { return global_gameDir; }
LOADER_EXPORT const char* LoaderPluginsFolder(LoaderHandle) { return global_modsDir; }
LOADER_EXPORT const char* LoaderModFolder(LoaderHandle)     { return global_modFolder; }
LOADER_EXPORT const char* LoaderLogsFolder(LoaderHandle)    { return global_modFolder; }
LOADER_EXPORT const char* LoaderConfigFolder(LoaderHandle)  { return global_modFolder; }
LOADER_EXPORT const char* LoaderCacheFolder(LoaderHandle)   { return global_modFolder; }

/* --------------------------------------------------------- hooking service */

/*
    A plugin names a function by the signature the compiler wrote into the game
    and supplies a callback; the loader finds the function once, writes one
    jump at its entry, and calls every plugin that asked, in the order they
    asked.

    A callback is handed an opaque pointer to the register block and reads it
    with LoaderHookRegister. The block's layout is not part of the plugin
    interface.

    hook_stub.asm publishes one stub per slot; HOOK_SLOT_COUNT and
    HOOK_STUB_LIST must match it. A slot holds one hooked function and up to
    MAX_SUBSCRIBERS callbacks.
*/

struct SavedRegisters {
    uint64_t rax, rcx, rdx, rbx, rbp, rsi, rdi;
    uint64_t r8, r9, r10, r11, r12, r13, r14, r15;
};
static_assert(sizeof(SavedRegisters) == 15 * 8, "must match the 15 pushes");

#define HOOK_SLOT_COUNT   256
#define MAX_DISPLACED     16
#define MAX_SUBSCRIBERS   8
#define SMALLEST_POINTER  0x10000ull

#define HOOK_STUB_LIST X(00,0) X(01,1) X(02,2) X(03,3) X(04,4) X(05,5) \
   X(06,6) X(07,7) X(08,8) X(09,9) X(10,10) X(11,11) X(12,12) X(13,13) \
   X(14,14) X(15,15) X(16,16) X(17,17) X(18,18) X(19,19) X(20,20) X(21,21) \
   X(22,22) X(23,23) X(24,24) X(25,25) X(26,26) X(27,27) X(28,28) X(29,29) \
   X(30,30) X(31,31) X(32,32) X(33,33) X(34,34) X(35,35) X(36,36) X(37,37) \
   X(38,38) X(39,39) X(40,40) X(41,41) X(42,42) X(43,43) X(44,44) X(45,45) \
   X(46,46) X(47,47) X(48,48) X(49,49) X(50,50) X(51,51) X(52,52) X(53,53) \
   X(54,54) X(55,55) X(56,56) X(57,57) X(58,58) X(59,59) X(60,60) X(61,61) \
   X(62,62) X(63,63) X(64,64) X(65,65) X(66,66) X(67,67) X(68,68) X(69,69) \
   X(70,70) X(71,71) X(72,72) X(73,73) X(74,74) X(75,75) X(76,76) X(77,77) \
   X(78,78) X(79,79) X(80,80) X(81,81) X(82,82) X(83,83) X(84,84) X(85,85) \
   X(86,86) X(87,87) X(88,88) X(89,89) X(90,90) X(91,91) X(92,92) X(93,93) \
   X(94,94) X(95,95) X(96,96) X(97,97) X(98,98) X(99,99) X(100,100) \
   X(101,101) X(102,102) X(103,103) X(104,104) X(105,105) X(106,106) \
   X(107,107) X(108,108) X(109,109) X(110,110) X(111,111) X(112,112) \
   X(113,113) X(114,114) X(115,115) X(116,116) X(117,117) X(118,118) \
   X(119,119) X(120,120) X(121,121) X(122,122) X(123,123) X(124,124) \
   X(125,125) X(126,126) X(127,127) X(128,128) X(129,129) X(130,130) \
   X(131,131) X(132,132) X(133,133) X(134,134) X(135,135) X(136,136) \
   X(137,137) X(138,138) X(139,139) X(140,140) X(141,141) X(142,142) \
   X(143,143) X(144,144) X(145,145) X(146,146) X(147,147) X(148,148) \
   X(149,149) X(150,150) X(151,151) X(152,152) X(153,153) X(154,154) \
   X(155,155) X(156,156) X(157,157) X(158,158) X(159,159) X(160,160) \
   X(161,161) X(162,162) X(163,163) X(164,164) X(165,165) X(166,166) \
   X(167,167) X(168,168) X(169,169) X(170,170) X(171,171) X(172,172) \
   X(173,173) X(174,174) X(175,175) X(176,176) X(177,177) X(178,178) \
   X(179,179) X(180,180) X(181,181) X(182,182) X(183,183) X(184,184) \
   X(185,185) X(186,186) X(187,187) X(188,188) X(189,189) X(190,190) \
   X(191,191) X(192,192) X(193,193) X(194,194) X(195,195) X(196,196) \
   X(197,197) X(198,198) X(199,199) X(200,200) X(201,201) X(202,202) \
   X(203,203) X(204,204) X(205,205) X(206,206) X(207,207) X(208,208) \
   X(209,209) X(210,210) X(211,211) X(212,212) X(213,213) X(214,214) \
   X(215,215) X(216,216) X(217,217) X(218,218) X(219,219) X(220,220) \
   X(221,221) X(222,222) X(223,223) X(224,224) X(225,225) X(226,226) \
   X(227,227) X(228,228) X(229,229) X(230,230) X(231,231) X(232,232) \
   X(233,233) X(234,234) X(235,235) X(236,236) X(237,237) X(238,238) \
   X(239,239) X(240,240) X(241,241) X(242,242) X(243,243) X(244,244) \
   X(245,245) X(246,246) X(247,247) X(248,248) X(249,249) X(250,250) \
   X(251,251) X(252,252) X(253,253) X(254,254) X(255,255)

extern "C" {
#define X(tag, n)                               \
    void hookStub##tag();                       \
    extern void*    global_hookCallback##tag;   \
    extern uint64_t global_returnAddress##tag;
HOOK_STUB_LIST
#undef X
}

struct HookSubscriber {
    LoaderHookCallback callback;
    void*              user;
};

/*
    stub            entry point of the assembly stub belonging to this slot
    callback        the stub reads this to find the function to call
    returnAddress   the stub jumps here when the callback returns
    name            the name the hook was installed under, or null when free
    patchBytes      the originalLength bytes written at patchedAt: E9, its
                    rel32, then 90 up to originalLength
*/
struct HookSlot {
    uint64_t       stub;
    void**         callback;
    uint64_t*      returnAddress;
    const char*    name;
    uint8_t*       patchedAt;
    uint8_t        originalBytes[MAX_DISPLACED];
    uint8_t        patchBytes[MAX_DISPLACED];
    int            originalLength;
    HookSubscriber subscriber[MAX_SUBSCRIBERS];
    volatile LONG  subscribers;
};

static HookSlot global_hookSlots[HOOK_SLOT_COUNT];
static int      global_hookSlotsReady = 0;
static CRITICAL_SECTION global_hookLock;

static void dispatchSlot(int slot, SavedRegisters* regs) {
    HookSlot* s = &global_hookSlots[slot];
    const LONG count = s->subscribers;
    for (LONG i = 0; i < count; ++i)
        if (s->subscriber[i].callback)
            s->subscriber[i].callback(regs, s->subscriber[i].user);
}

#define X(tag, n) \
    static void dispatch##tag(SavedRegisters* r) { dispatchSlot(n, r); }
HOOK_STUB_LIST
#undef X

static void prepareHookSlots() {
    if (global_hookSlotsReady) return;
    memset(global_hookSlots, 0, sizeof(global_hookSlots));
#define X(tag, n)                                                    \
    global_hookSlots[n].stub = (uint64_t)&hookStub##tag;             \
    global_hookSlots[n].callback = &global_hookCallback##tag;        \
    global_hookSlots[n].returnAddress = &global_returnAddress##tag;  \
    *global_hookSlots[n].callback = (void*)&dispatch##tag;
HOOK_STUB_LIST
#undef X
    InitializeCriticalSection(&global_hookLock);
    global_hookSlotsReady = 1;
}

/* The slot already hooked on this function, or -1. */
static int slotForFunction(const char* name) {
    for (int i = 0; i < HOOK_SLOT_COUNT; ++i)
        if (global_hookSlots[i].patchedAt && global_hookSlots[i].name
            && strcmp(global_hookSlots[i].name, name) == 0)
            return i;
    return -1;
}

/* The slot already hooked at this address, or -1. */
static int slotForAddress(const uint8_t* address) {
    for (int i = 0; i < HOOK_SLOT_COUNT; ++i)
        if (global_hookSlots[i].patchedAt == address) return i;
    return -1;
}

/* The hooked slot whose patch shares a byte with the length bytes at
   address, or -1. */
static int slotOverlapping(const uint8_t* address, int length) {
    for (int i = 0; i < HOOK_SLOT_COUNT; ++i) {
        const HookSlot* s = &global_hookSlots[i];
        if (s->patchedAt && s->patchedAt < address + length
            && address < s->patchedAt + s->originalLength)
            return i;
    }
    return -1;
}

/* Writes the refusal line and answers 1 when the length bytes at address
   share a byte with one of the loader's own jumps; answers 0 otherwise. */
static int refuseOverlap(const char* name, const uint8_t* address,
                         int length) {
    const int overlapped = slotOverlapping(address, length);
    if (overlapped < 0) return 0;
    writeLog("hook refused for %s: the %d byte(s) its jump would replace "
             "overlap the loader's own jump for %s", name, length,
             global_hookSlots[overlapped].name);
    return 1;
}

static int freeSlot() {
    for (int i = 0; i < HOOK_SLOT_COUNT; ++i)
        if (!global_hookSlots[i].patchedAt) return i;
    return -1;
}
/* --------------------------------------- writing a hook in place */

static uint8_t* allocNear(uint8_t* anchor, size_t size) {
    SYSTEM_INFO si; GetSystemInfo(&si);
    const uintptr_t gran = si.dwAllocationGranularity;
    uintptr_t start = (uintptr_t)anchor;
    for (int dir = 0; dir < 2; ++dir)
        for (uintptr_t off = gran; off < 0x70000000ull; off += gran) {
            if (dir == 0 && off > start) break;
            uintptr_t addr = dir == 0 ? start - off : start + off;
            void* p = VirtualAlloc((void*)(addr & ~(gran - 1)), size,
                                   MEM_RESERVE | MEM_COMMIT,
                                   PAGE_EXECUTE_READWRITE);
            if (p) return (uint8_t*)p;
        }
    return nullptr;
}

static size_t writeAbsoluteJump(uint8_t* at, uint64_t target) {
    size_t n = 0;
    at[n++] = 0x50;
    at[n++] = 0x48; at[n++] = 0xB8;
    memcpy(at + n, &target, 8); n += 8;
    at[n++] = 0x48; at[n++] = 0x87; at[n++] = 0x04; at[n++] = 0x24;
    at[n++] = 0xC3;
    return n;
}

struct MovedInstruction {
    int length;
    int ripDisplacement;
};

/*
    Measures the ModRM byte at p[i] and what follows it: a SIB byte, a
    displacement, then immediate bytes. Records where a rip relative
    displacement lies. Answers 1.
*/
static int measureOperands(const uint8_t* p, int i, int immediate,
                           MovedInstruction* out) {
    const uint8_t modrm = p[i++];
    const int mod = (modrm >> 6) & 3;
    const int rm  = modrm & 7;

    if (mod != 3) {
        if (rm == 4) {
            const uint8_t sib = p[i++];
            if (mod == 0 && (sib & 7) == 5) i += 4;
        } else if (mod == 0 && rm == 5) {
            out->ripDisplacement = i;
            i += 4;
        }
        if (mod == 1) i += 1;
        else if (mod == 2) i += 4;
    }
    i += immediate;
    out->length = i;
    return 1;
}

/*
    The opcodes known after a VEX prefix (AVX, AVX2, FMA, F16C, BMI1, BMI2,
    AES, PCLMULQDQ, GFNI and AVX-VNNI), as ranges of the opcode byte from first
    to last. map is the opcode map the prefix names: 1 for 0F, 2 for 0F 38,
    3 for 0F 3A. prefixes holds a bit for each value of the prefix's pp field
    the opcodes are defined with. Every opcode is followed by a ModRM byte,
    except 0F 77 (vzeroupper, vzeroall), and its operands end with immediate
    bytes.
*/
struct VexOpcodeRange {
    uint8_t map;
    uint8_t first;
    uint8_t last;
    uint8_t prefixes;
    uint8_t immediate;
};

/* The pp field of a VEX prefix, as its bit in prefixes, named by the legacy
   prefix it stands for. */
static const uint8_t vexPrefixNone = 1;     /* pp 0 */
static const uint8_t vexPrefix66   = 2;     /* pp 1 */
static const uint8_t vexPrefixF3   = 4;     /* pp 2 */
static const uint8_t vexPrefixF2   = 8;     /* pp 3 */
static const uint8_t vexPrefixAny  = 15;

static const VexOpcodeRange vexOpcodes[] = {
    /* vmovups to vmovddup */
    { 1, 0x10, 0x12, vexPrefixAny, 0 },
    /* vmovlps, vunpcklps, vunpckhps */
    { 1, 0x13, 0x15, vexPrefixNone | vexPrefix66, 0 },
    /* vmovhps, vmovshdup */
    { 1, 0x16, 0x16, vexPrefixNone | vexPrefix66 | vexPrefixF3, 0 },
    /* vmovhps store */
    { 1, 0x17, 0x17, vexPrefixNone | vexPrefix66, 0 },
    /* vmovaps */
    { 1, 0x28, 0x29, vexPrefixNone | vexPrefix66, 0 },
    /* vcvtsi2ss, vcvtsi2sd */
    { 1, 0x2A, 0x2A, vexPrefixF3 | vexPrefixF2, 0 },
    /* vmovntps */
    { 1, 0x2B, 0x2B, vexPrefixNone | vexPrefix66, 0 },
    /* vcvttss2si, vcvtss2si */
    { 1, 0x2C, 0x2D, vexPrefixF3 | vexPrefixF2, 0 },
    /* vucomiss, vcomiss */
    { 1, 0x2E, 0x2F, vexPrefixNone | vexPrefix66, 0 },
    /* vmovmskps */
    { 1, 0x50, 0x50, vexPrefixNone | vexPrefix66, 0 },
    /* vsqrtps */
    { 1, 0x51, 0x51, vexPrefixAny, 0 },
    /* vrsqrtps, vrcpps */
    { 1, 0x52, 0x53, vexPrefixNone | vexPrefixF3, 0 },
    /* vandps to vxorps */
    { 1, 0x54, 0x57, vexPrefixNone | vexPrefix66, 0 },
    /* vaddps, vmulps, vcvtps2pd */
    { 1, 0x58, 0x5A, vexPrefixAny, 0 },
    /* vcvtdq2ps, vcvtps2dq, vcvttps2dq */
    { 1, 0x5B, 0x5B, vexPrefixNone | vexPrefix66 | vexPrefixF3, 0 },
    /* vsubps to vmaxps */
    { 1, 0x5C, 0x5F, vexPrefixAny, 0 },
    /* vpunpcklbw to vmovd */
    { 1, 0x60, 0x6E, vexPrefix66, 0 },
    /* vmovdqa, vmovdqu */
    { 1, 0x6F, 0x6F, vexPrefix66 | vexPrefixF3, 0 },
    /* vpshufd, vpshufhw, vpshuflw */
    { 1, 0x70, 0x70, vexPrefix66 | vexPrefixF3 | vexPrefixF2, 1 },
    /* shifts by an immediate */
    { 1, 0x71, 0x73, vexPrefix66, 1 },
    /* vpcmpeqb to vpcmpeqd */
    { 1, 0x74, 0x76, vexPrefix66, 0 },
    /* vzeroupper, vzeroall */
    { 1, 0x77, 0x77, vexPrefixNone, 0 },
    /* vhaddpd, vhsubpd */
    { 1, 0x7C, 0x7D, vexPrefix66 | vexPrefixF2, 0 },
    /* vmovd, vmovq, vmovdqa store */
    { 1, 0x7E, 0x7F, vexPrefix66 | vexPrefixF3, 0 },
    /* vldmxcsr, vstmxcsr */
    { 1, 0xAE, 0xAE, vexPrefixNone, 0 },
    /* vcmpps */
    { 1, 0xC2, 0xC2, vexPrefixAny, 1 },
    /* vpinsrw, vpextrw */
    { 1, 0xC4, 0xC5, vexPrefix66, 1 },
    /* vshufps */
    { 1, 0xC6, 0xC6, vexPrefixNone | vexPrefix66, 1 },
    /* vaddsubpd */
    { 1, 0xD0, 0xD0, vexPrefix66 | vexPrefixF2, 0 },
    /* vpsrlw to vpmulhw */
    { 1, 0xD1, 0xE5, vexPrefix66, 0 },
    /* vcvttpd2dq, vcvtdq2pd, vcvtpd2dq */
    { 1, 0xE6, 0xE6, vexPrefix66 | vexPrefixF3 | vexPrefixF2, 0 },
    /* vmovntdq to vpxor */
    { 1, 0xE7, 0xEF, vexPrefix66, 0 },
    /* vlddqu */
    { 1, 0xF0, 0xF0, vexPrefixF2, 0 },
    /* vpsllw to vpaddd */
    { 1, 0xF1, 0xFE, vexPrefix66, 0 },
    /* vpshufb to vtestpd */
    { 2, 0x00, 0x0F, vexPrefix66, 0 },
    /* vcvtph2ps */
    { 2, 0x13, 0x13, vexPrefix66, 0 },
    /* vpermps, vptest, broadcasts */
    { 2, 0x16, 0x1A, vexPrefix66, 0 },
    /* vpabsb to vpabsd */
    { 2, 0x1C, 0x1E, vexPrefix66, 0 },
    /* vpmovsx */
    { 2, 0x20, 0x25, vexPrefix66, 0 },
    /* vpmuldq to vmaskmovpd */
    { 2, 0x28, 0x2F, vexPrefix66, 0 },
    /* vpmovzx to vphminposuw */
    { 2, 0x30, 0x41, vexPrefix66, 0 },
    /* vpsrlvd, vpsravd, vpsllvd */
    { 2, 0x45, 0x47, vexPrefix66, 0 },
    /* vpdpbusd to vpdpwssds */
    { 2, 0x50, 0x53, vexPrefix66, 0 },
    /* vpbroadcastd to vbroadcasti128 */
    { 2, 0x58, 0x5A, vexPrefix66, 0 },
    /* vpbroadcastb, vpbroadcastw */
    { 2, 0x78, 0x79, vexPrefix66, 0 },
    /* vpmaskmovd load */
    { 2, 0x8C, 0x8C, vexPrefix66, 0 },
    /* vpmaskmovd store */
    { 2, 0x8E, 0x8E, vexPrefix66, 0 },
    /* gathers */
    { 2, 0x90, 0x93, vexPrefix66, 0 },
    /* fused multiply-add, 132 */
    { 2, 0x96, 0x9F, vexPrefix66, 0 },
    /* fused multiply-add, 213 */
    { 2, 0xA6, 0xAF, vexPrefix66, 0 },
    /* fused multiply-add, 231 */
    { 2, 0xB6, 0xBF, vexPrefix66, 0 },
    /* vgf2p8mulb */
    { 2, 0xCF, 0xCF, vexPrefix66, 0 },
    /* vaesimc to vaesdeclast */
    { 2, 0xDB, 0xDF, vexPrefix66, 0 },
    /* andn, blsr, blsmsk, blsi */
    { 2, 0xF2, 0xF3, vexPrefixNone, 0 },
    /* bzhi, pext, pdep */
    { 2, 0xF5, 0xF5, vexPrefixNone | vexPrefixF3 | vexPrefixF2, 0 },
    /* mulx */
    { 2, 0xF6, 0xF6, vexPrefixF2, 0 },
    /* bextr, shlx, sarx, shrx */
    { 2, 0xF7, 0xF7, vexPrefixAny, 0 },
    /* vpermq, vpermpd, vpblendd */
    { 3, 0x00, 0x02, vexPrefix66, 1 },
    /* vpermilps, vpermilpd, vperm2f128 */
    { 3, 0x04, 0x06, vexPrefix66, 1 },
    /* vroundps to vpalignr */
    { 3, 0x08, 0x0F, vexPrefix66, 1 },
    /* vpextrb to vextractf128 */
    { 3, 0x14, 0x19, vexPrefix66, 1 },
    /* vcvtps2ph */
    { 3, 0x1D, 0x1D, vexPrefix66, 1 },
    /* vpinsrb, vinsertps, vpinsrd */
    { 3, 0x20, 0x22, vexPrefix66, 1 },
    /* vinserti128, vextracti128 */
    { 3, 0x38, 0x39, vexPrefix66, 1 },
    /* vdpps, vdppd, vmpsadbw */
    { 3, 0x40, 0x42, vexPrefix66, 1 },
    /* vpclmulqdq */
    { 3, 0x44, 0x44, vexPrefix66, 1 },
    /* vperm2i128 */
    { 3, 0x46, 0x46, vexPrefix66, 1 },
    /* vblendvps, vblendvpd, vpblendvb */
    { 3, 0x4A, 0x4C, vexPrefix66, 1 },
    /* vpcmpestrm to vpcmpistri */
    { 3, 0x60, 0x63, vexPrefix66, 1 },
    /* vgf2p8affineqb, vgf2p8affineinvqb */
    { 3, 0xCE, 0xCF, vexPrefix66, 1 },
    /* vaeskeygenassist */
    { 3, 0xDF, 0xDF, vexPrefix66, 1 },
    /* rorx */
    { 3, 0xF0, 0xF0, vexPrefixF2, 1 },
};

/*
    Measures the VEX encoded instruction whose prefix byte is p[i]. C5 is
    followed by one byte (R, vvvv, L, pp) and stands for the map 0F; C4 by
    two, the first holding R, X, B and the map in its low five bits, the
    second W, vvvv, L and pp. Answers 0 for a map, opcode or pp not in
    vexOpcodes.
*/
static int decodeVexInstruction(const uint8_t* p, int i,
                                MovedInstruction* out) {
    int map = 1;
    int pp = 0;
    if (p[i] == 0xC4) {
        map = p[i + 1] & 0x1F;
        pp = p[i + 2] & 3;
        i += 3;
    } else {
        pp = p[i + 1] & 3;
        i += 2;
    }
    const uint8_t op = p[i++];

    for (size_t k = 0; k < sizeof(vexOpcodes) / sizeof(vexOpcodes[0]); ++k) {
        const VexOpcodeRange* range = &vexOpcodes[k];
        if (range->map != map || op < range->first || op > range->last)
            continue;
        if (!(range->prefixes & (1 << pp))) return 0;
        if (map == 1 && op == 0x77) {
            out->length = i;
            return 1;
        }
        return measureOperands(p, i, range->immediate, out);
    }
    return 0;
}

/*
    Enough of the x86-64 encoding to measure an instruction's length and find a
    rip relative displacement in it.

    Accepts only an instruction that can be moved to another address
    unchanged, or unchanged but for a rip relative displacement the caller
    rewrites: the general purpose instructions listed below and the VEX
    encoded ones of vexOpcodes. Relative branches, calls through a register or
    memory (FF /2 and FF /3) and unknown encodings are refused.
*/
static int decodeInstruction(const uint8_t* p, MovedInstruction* out) {
    int i = 0;
    out->length = 0;
    out->ripDisplacement = -1;

    /* Legacy prefixes. 0x66 also narrows an immediate from four bytes to two.
       A VEX prefix after 66, F2, F3 or REX is invalid. */
    int operandSize32 = 1;
    int vexAllowed = 1;
    for (;;) {
        const uint8_t prefix = p[i];
        if (prefix == 0x66) { operandSize32 = 0; vexAllowed = 0; ++i; continue; }
        if (prefix == 0xF2 || prefix == 0xF3) { vexAllowed = 0; ++i; continue; }
        if (prefix == 0x67
            || prefix == 0x2E || prefix == 0x36 || prefix == 0x3E
            || prefix == 0x26 || prefix == 0x64 || prefix == 0x65) {
            ++i; continue;
        }
        break;
    }
    if (p[i] == 0xC4 || p[i] == 0xC5)
        return vexAllowed ? decodeVexInstruction(p, i, out) : 0;
    if ((p[i] & 0xF0) == 0x40) {
        if (p[i] & 0x08) operandSize32 = 1;   /* REX.W beats the 0x66 prefix */
        ++i;
    }

    uint8_t op = p[i++];

    /* No ModRM byte and no operand. */
    if ((op >= 0x50 && op <= 0x5F) || op == 0x90 || op == 0x99 || op == 0x98) {
        out->length = i;
        return 1;
    }
    if (op == 0x6A) { out->length = i + 1; return 1; }      /* push imm8 */
    if (op == 0x68) { out->length = i + (operandSize32 ? 4 : 2); return 1; }

    int immediate = 0;

    if (op == 0x0F) {
        const uint8_t second = p[i++];
        switch (second) {
            case 0xAF:                                   /* imul r, r/m */
            case 0xB6: case 0xB7:                        /* movzx */
            case 0xBE: case 0xBF:                        /* movsx */
            case 0xA3: case 0xAB: case 0xB3: case 0xBB:  /* bt, bts, btr, btc */
                break;
            default:
                if (second >= 0x40 && second <= 0x4F) break;   /* cmovcc */
                if (second >= 0x90 && second <= 0x9F) break;   /* setcc */
                return 0;
        }
    } else {
        switch (op) {
            /* the arithmetic and logic group, both directions, both widths */
            case 0x00: case 0x01: case 0x02: case 0x03:
            case 0x08: case 0x09: case 0x0A: case 0x0B:
            case 0x10: case 0x11: case 0x12: case 0x13:
            case 0x18: case 0x19: case 0x1A: case 0x1B:
            case 0x20: case 0x21: case 0x22: case 0x23:
            case 0x28: case 0x29: case 0x2A: case 0x2B:
            case 0x30: case 0x31: case 0x32: case 0x33:
            case 0x38: case 0x39: case 0x3A: case 0x3B:
            case 0x63:                                   /* movsxd */
            case 0x84: case 0x85:                        /* test */
            case 0x86: case 0x87:                        /* xchg */
            case 0x88: case 0x89: case 0x8A: case 0x8B:  /* mov */
            case 0x8D:                                   /* lea */
            case 0xD0: case 0xD1: case 0xD2: case 0xD3:  /* shift by 1 or cl */
            case 0xFE:                                   /* inc, dec */
                break;
            case 0xFF:                                   /* inc, dec, jmp, push */
                if (((p[i] >> 3) & 7) == 2 || ((p[i] >> 3) & 7) == 3)
                    return 0;                            /* call, call far */
                break;
            case 0x80: case 0x83:                        /* group 1, imm8 */
            case 0xC0: case 0xC1:                        /* shift, imm8 */
            case 0xC6:                                   /* mov r/m8, imm8 */
                immediate = 1;
                break;
            case 0x81:                                   /* group 1, imm16/32 */
            case 0xC7:                                   /* mov r/m, imm16/32 */
                immediate = operandSize32 ? 4 : 2;
                break;
            case 0xF6:                                   /* group 3, r/m8 */
                immediate = ((p[i] >> 3) & 7) <= 1 ? 1 : 0;
                break;
            case 0xF7:                                   /* group 3 */
                immediate = ((p[i] >> 3) & 7) <= 1
                          ? (operandSize32 ? 4 : 2) : 0;
                break;
            default:
                return 0;
        }
    }

    return measureOperands(p, i, immediate, out);
}

#define MAX_MOVED 8

struct MovedPrologue {
    int              total;
    int              count;
    int              offset[MAX_MOVED];
    MovedInstruction part[MAX_MOVED];
};

static int movablePrologue(const uint8_t* p, MovedPrologue* out) {
    out->total = 0;
    out->count = 0;
    while (out->total < 5) {
        if (out->count >= MAX_MOVED) return 0;
        MovedInstruction one;
        if (!decodeInstruction(p + out->total, &one) || one.length <= 0) return 0;
        out->offset[out->count] = out->total;
        out->part[out->count] = one;
        out->total += one.length;
        ++out->count;
    }
    return out->total;
}

static int firstInstructionLength(const uint8_t* p) {
    MovedPrologue moved;
    return movablePrologue(p, &moved);
}

/* Every other thread in the process, held still while a hook is written. */
#define MAX_HELD_THREADS 256

struct HeldThreads {
    HANDLE handle[MAX_HELD_THREADS];
    int    count;
};

static void releaseThreads(HeldThreads* held) {
    for (int i = 0; i < held->count; ++i) {
        ResumeThread(held->handle[i]);
        CloseHandle(held->handle[i]);
    }
    held->count = 0;
}

/*
    Answers 1 with the other threads suspended, up to MAX_HELD_THREADS of them.
    Answers 0 where a thread is stopped inside the span, and nothing is then
    left suspended.
*/
static int holdThreads(HeldThreads* held, const uint8_t* span, int length) {
    held->count = 0;
    const DWORD mine = GetCurrentThreadId();
    const DWORD process = GetCurrentProcessId();

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return 0;

    THREADENTRY32 entry;
    entry.dwSize = sizeof(entry);
    int inTheWay = 0;
    if (Thread32First(snapshot, &entry)) {
        do {
            if (entry.th32OwnerProcessID != process) continue;
            if (entry.th32ThreadID == mine) continue;
            if (held->count >= MAX_HELD_THREADS) break;
            HANDLE one = OpenThread(
                THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION,
                FALSE, entry.th32ThreadID);
            if (!one) continue;
            if (SuspendThread(one) == (DWORD)-1) { CloseHandle(one); continue; }
            held->handle[held->count++] = one;

            CONTEXT where;
            memset(&where, 0, sizeof(where));
            where.ContextFlags = CONTEXT_CONTROL;
            if (GetThreadContext(one, &where)) {
                const uint8_t* at = (const uint8_t*)where.Rip;
                if (at >= span && at < span + length) inTheWay = 1;
            }
        } while (Thread32Next(snapshot, &entry));
    }
    CloseHandle(snapshot);

    if (inTheWay) { releaseThreads(held); return 0; }
    return 1;
}

/* Gives back what installEntryHook took before it failed, leaving the slot
   free. */
static void abandonHook(HookSlot* s, uint8_t* page) {
    VirtualFree(page, 0, MEM_RELEASE);
    *s->returnAddress = 0;
    memset(s->originalBytes, 0, sizeof(s->originalBytes));
    s->originalLength = 0;
}

/*
    Replaces the opening instructions of a function with a jump to the slot's
    stub. The displaced bytes are copied into a resume buffer followed by a
    jump back.
*/
static int installEntryHook(int slot, const char* name,
                            const FunctionSite* site, int displacedLength) {
    HookSlot* s = &global_hookSlots[slot];

    if (refuseOverlap(name, site->address, displacedLength)) return 0;
    if (site->address[0] == 0xE9) {
        writeLog("hook refused for %s: rva %08X already begins with a jump, so "
                 "something outside the loader is hooked there", name,
                 site->rva);
        return 0;
    }
    if (displacedLength < 5 || displacedLength > MAX_DISPLACED) {
        writeLog("hook refused for %s: %d displaced byte(s); a jump needs at "
                 "least 5 and the slot holds at most %d", name, displacedLength,
                 MAX_DISPLACED);
        return 0;
    }
    uint8_t* page = allocNear((uint8_t*)GetModuleHandleW(nullptr), 0x1000);
    if (!page) {
        writeLog("hook refused for %s: no executable memory within 2GB of the "
                 "game", name);
        return 0;
    }
    uint8_t* thunk  = page;
    uint8_t* resume = page + 0x20;

    writeAbsoluteJump(thunk, s->stub);
    memcpy(resume, site->address, (size_t)displacedLength);

    /*
        Each rip relative displacement is rewritten by the distance the
        instruction moved. A result that does not fit the signed 32 bit field
        refuses the hook.
    */
    MovedPrologue moved;
    if (movablePrologue(site->address, &moved)) {
        const int64_t shift = (int64_t)(site->address - resume);
        for (int i = 0; i < moved.count; ++i) {
            const int at = moved.part[i].ripDisplacement;
            if (at < 0) continue;
            if (moved.offset[i] + at + 4 > displacedLength) continue;
            int32_t displacement = 0;
            memcpy(&displacement, resume + moved.offset[i] + at, 4);
            const int64_t wanted = (int64_t)displacement + shift;
            if (wanted < INT32_MIN || wanted > INT32_MAX) {
                writeLog("hook refused for %s: a rip relative operand cannot "
                         "reach from the resume buffer", name);
                abandonHook(s, page);
                return 0;
            }
            displacement = (int32_t)wanted;
            memcpy(resume + moved.offset[i] + at, &displacement, 4);
        }
    }

    writeAbsoluteJump(resume + displacedLength,
                      (uint64_t)(site->address + displacedLength));
    *s->returnAddress = (uint64_t)resume;

    memcpy(s->originalBytes, site->address, (size_t)displacedLength);
    s->originalLength = displacedLength;

    uint8_t patch[MAX_DISPLACED];
    patch[0] = 0xE9;
    int32_t rel = (int32_t)(thunk - (site->address + 5));
    memcpy(patch + 1, &rel, 4);
    for (int i = 5; i < displacedLength; ++i) patch[i] = 0x90;

    const SIZE_T span = (SIZE_T)displacedLength;
    DWORD old;
    if (!VirtualProtect(site->address, span, PAGE_EXECUTE_READWRITE, &old)) {
        writeLog("hook refused for %s: VirtualProtect failed with error %lu",
                 name, GetLastError());
        abandonHook(s, page);
        return 0;
    }

    HeldThreads held;
    if (!holdThreads(&held, site->address, displacedLength)) {
        VirtualProtect(site->address, span, old, &old);
        writeLog("hook refused for %s: a thread is stopped inside the bytes "
                 "the jump would replace", name);
        abandonHook(s, page);
        return 0;
    }
    memcpy(site->address, patch, span);
    FlushInstructionCache(GetCurrentProcess(), site->address, span);
    releaseThreads(&held);

    VirtualProtect(site->address, span, old, &old);
    memcpy(s->patchBytes, patch, span);
    s->patchedAt = site->address;
    s->name = name;
    return 1;
}

static void hookFinderLogSink(const char* text) { writeLog("%s", text); }

/*
    The hook finder's source for the bytes before the loader's hooks patched
    them. Copies the length bytes at address into out as memory holds them,
    then lays each hooked slot's originalBytes over the part of its patch, the
    originalLength bytes from patchedAt, that falls inside that span. A slot is
    laid over only while memory still holds its whole patchBytes; a patch
    written over since, and anything written by LoaderPatch or from outside
    the loader, is read as memory holds it. installEntryHook refuses a patch
    that would overlap another, so each byte lies under at most one slot.
    Holds global_hookLock throughout, so prepareHookSlots must have run first.
*/
static void originalBytesAt(const unsigned char* address, size_t length,
                            unsigned char* out) {
    EnterCriticalSection(&global_hookLock);
    memcpy(out, address, length);
    const uint8_t* end = address + length;
    for (int i = 0; i < HOOK_SLOT_COUNT; ++i) {
        const HookSlot* s = &global_hookSlots[i];
        if (!s->patchedAt) continue;
        const uint8_t* from = s->patchedAt > address ? s->patchedAt : address;
        const uint8_t* to = s->patchedAt + s->originalLength;
        if (to > end) to = end;
        if (from >= to) continue;
        if (memcmp(s->patchedAt, s->patchBytes, (size_t)s->originalLength) != 0)
            continue;
        for (const uint8_t* p = from; p < to; ++p)
            out[p - address] = s->originalBytes[p - s->patchedAt];
    }
    LeaveCriticalSection(&global_hookLock);
}

/*
    Hooks the function that carries this signature and calls back on every
    entry to it. Several plugins may hook the same function; the loader writes
    one jump and calls each of them in the order they registered.

    name        the key the located address is cached under
    signature   the signature string the compiler wrote into the game
    callback    void (*)(void *registers, void *user)
    user        passed back to the callback untouched

    Returns 1 when the callback is registered.
*/
LOADER_EXPORT int LoaderHookFunction(LoaderHandle, const char* name,
                                     const char* signature,
                                     LoaderHookCallback callback, void* user) {
    if (!name || !signature || !callback) return 0;
    prepareHookSlots();
    EnterCriticalSection(&global_hookLock);
    int answer = 0;

    int slot = slotForFunction(name);
    if (slot < 0) {
        FunctionSite site;
        if (!FindFunctionByName(name, signature, &site)) {
            writeLog("hook refused for %s: the function was not found", name);
            LeaveCriticalSection(&global_hookLock);
            return 0;
        }
        MarkFunctionVerified(name, &site);
        if (refuseOverlap(name, site.address, 5)) {
            LeaveCriticalSection(&global_hookLock);
            return 0;
        }
        const int length = firstInstructionLength(site.address);
        if (!length) {
            writeLog("hook refused for %s: the opening instructions cannot be "
                     "moved", name);
            LeaveCriticalSection(&global_hookLock);
            return 0;
        }
        slot = freeSlot();
        if (slot < 0) {
            writeLog("hook refused for %s: all %d slot(s) are in use", name,
                     HOOK_SLOT_COUNT);
            LeaveCriticalSection(&global_hookLock);
            return 0;
        }
        if (!installEntryHook(slot, name, &site, length)) {
            LeaveCriticalSection(&global_hookLock);
            return 0;
        }
    }

    HookSlot* s = &global_hookSlots[slot];
    if (s->subscribers >= MAX_SUBSCRIBERS) {
        writeLog("hook refused for %s: it already has %d subscriber(s)", name,
                 MAX_SUBSCRIBERS);
    } else {
        const LONG at = s->subscribers;
        s->subscriber[at].callback = callback;
        s->subscriber[at].user = user;
        InterlockedExchange(&s->subscribers, at + 1);
        answer = 1;
    }
    LeaveCriticalSection(&global_hookLock);
    return answer;
}

/*
    Hooks an instruction rather than a function entry.

    name       the key the hook is logged under
    address    the first byte of an instruction inside the game
    callback   void (*)(void *registers, void *user)
    user       passed back to the callback untouched

    The instruction at address, and as many after it as a five byte jump
    needs, are moved into the slot's resume buffer. A second caller naming the
    same address is added as a subscriber and nothing is patched twice. An
    address whose moved bytes would overlap another of the loader's jumps is
    refused.

    Returns 1 when the callback is registered.
*/
LOADER_EXPORT int LoaderHookAddress(LoaderHandle, const char* name,
                                    uint64_t address,
                                    LoaderHookCallback callback, void* user) {
    if (!name || address < SMALLEST_POINTER || !callback) return 0;
    prepareHookSlots();
    EnterCriticalSection(&global_hookLock);
    int answer = 0;

    int slot = slotForAddress((const uint8_t*)address);
    if (slot < 0) {
        FunctionSite site;
        memset(&site, 0, sizeof(site));
        site.address = (unsigned char*)address;
        const uint64_t base = (uint64_t)GetModuleHandleW(nullptr);
        site.rva = (unsigned int)(address - base);

        if (refuseOverlap(name, site.address, 5)) {
            LeaveCriticalSection(&global_hookLock);
            return 0;
        }
        const int length = firstInstructionLength(site.address);
        if (!length) {
            writeLog("hook refused for %s: the instruction at rva %08X cannot "
                     "be moved", name, site.rva);
            LeaveCriticalSection(&global_hookLock);
            return 0;
        }
        slot = freeSlot();
        if (slot < 0) {
            writeLog("hook refused for %s: all %d slot(s) are in use", name,
                     HOOK_SLOT_COUNT);
            LeaveCriticalSection(&global_hookLock);
            return 0;
        }
        if (!installEntryHook(slot, name, &site, length)) {
            LeaveCriticalSection(&global_hookLock);
            return 0;
        }
    }

    HookSlot* s = &global_hookSlots[slot];
    if (s->subscribers >= MAX_SUBSCRIBERS) {
        writeLog("hook refused for %s: it already has %d subscriber(s)", name,
                 MAX_SUBSCRIBERS);
    } else {
        const LONG at = s->subscribers;
        s->subscriber[at].callback = callback;
        s->subscriber[at].user = user;
        InterlockedExchange(&s->subscribers, at + 1);
        answer = 1;
    }
    LeaveCriticalSection(&global_hookLock);
    return answer;
}

/* One register of a hooked call, by the LOADER_REG_ numbers. */
LOADER_EXPORT uint64_t LoaderHookRegister(LoaderHandle, void* registers,
                                          int which) {
    if (!registers || which < 0 || which >= LOADER_REG_COUNT) return 0;
    return ((const uint64_t*)registers)[which];
}

/*
    Writes one register of a hooked call, by the LOADER_REG_ numbers.

    registers   the block the callback was handed
    which       the register to write
    value       what that register is to hold

    The stub restores the block into the registers before it jumps to the
    original, so the value written here is what the hooked function receives.
    The subscribers of one hook are handed the same block: a register written
    by one of them is what the subscribers called after it read.

    Returns 1 when the register is written.
*/
LOADER_EXPORT int LoaderHookSetRegister(LoaderHandle, void* registers,
                                        int which, uint64_t value) {
    if (!registers || which < 0 || which >= LOADER_REG_COUNT) return 0;
    ((uint64_t*)registers)[which] = value;
    return 1;
}

/*
    The trampoline of a function the loader has hooked under this name: the
    bytes displaced from its opening, followed by a jump back past the patch.
    Calling it runs the whole original function and returns to the caller; no
    subscriber of the hook sees the call.

    name    the key the hook was installed under

    Cast it to a pointer with the game function's own signature and calling
    convention and call it with that function's arguments. Returns 0 when the
    loader has not hooked that name.
*/
LOADER_EXPORT uint64_t LoaderHookTrampoline(LoaderHandle, const char* name) {
    if (!name) return 0;
    prepareHookSlots();
    EnterCriticalSection(&global_hookLock);
    const int slot = slotForFunction(name);
    const uint64_t answer = slot < 0 ? 0 : *global_hookSlots[slot].returnAddress;
    LeaveCriticalSection(&global_hookLock);
    return answer;
}

/* The address of a function the caller means to call rather than hook, or 0. */
LOADER_EXPORT uint64_t LoaderFindFunction(LoaderHandle, const char* name,
                                          const char* signature) {
    if (!name || !signature) return 0;
    prepareHookSlots();
    EnterCriticalSection(&global_hookLock);
    FunctionSite site;
    if (!FindFunctionByName(name, signature, &site)) {
        writeLog("%s was not found", name);
        LeaveCriticalSection(&global_hookLock);
        return 0;
    }
    MarkFunctionVerified(name, &site);
    LeaveCriticalSection(&global_hookLock);
    return (uint64_t)site.address;
}

/* ------------------------------------------------------- crash reporting */

/*
    Faults are written to the loader's log: the game's own crashes, a plugin's,
    and a fault caught inside a plugin's guarded call to engine code.

    Three routes: an exception handler and filter for faults, an invalid
    parameter handler for a bounded call such as strcpy_s given a destination
    too small, and LoaderDescribeFault for a fault a plugin catches on
    purpose.
*/

static int readQwordGuarded(uint64_t address, uint64_t* out) {
    if (address < SMALLEST_POINTER) return 0;
    int ok = 0;
    GUARD_TRY { *out = *(const uint64_t*)address; ok = 1; }
    GUARD_EXCEPT { ok = 0; }
    return ok;
}

/*
    An address written as the module that holds it and the offset into it, or
    as a bare number when no loaded module holds it.
*/
static void describeAddress(uint64_t address, char* out, size_t room) {
    HMODULE holder = nullptr;
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
                           | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCSTR)address, &holder) && holder) {
        char name[MAX_PATH] = {0};
        GetModuleFileNameA(holder, name, MAX_PATH);
        const char* leaf = strrchr(name, '\\');
        _snprintf_s(out, room, _TRUNCATE, "%s +%08llX", leaf ? leaf + 1 : name,
                    (unsigned long long)(address - (uint64_t)holder));
        return;
    }
    _snprintf_s(out, room, _TRUNCATE, "%016llx, in no loaded module",
                (unsigned long long)address);
}

/* Whether an address lies in an executable section of the module holding it. */
static int addressIsCode(uint64_t address) {
    HMODULE holder = nullptr;
    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
                            | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            (LPCSTR)address, &holder) || !holder) return 0;

    auto* dos = (IMAGE_DOS_HEADER*)holder;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return 0;
    auto* nt = (IMAGE_NT_HEADERS64*)((uint8_t*)holder + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return 0;

    auto* sec = IMAGE_FIRST_SECTION(nt);
    for (int i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++sec) {
        if (!(sec->Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
        const uint64_t base = (uint64_t)holder + sec->VirtualAddress;
        if (address >= base && address < base + sec->Misc.VirtualSize) return 1;
    }
    return 0;
}

/* The file name of the module holding an address. */
static void moduleNameAt(uint64_t address, char* out, size_t room) {
    HMODULE holder = nullptr;
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
                           | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCSTR)address, &holder) && holder) {
        char name[MAX_PATH] = {0};
        GetModuleFileNameA(holder, name, MAX_PATH);
        const char* leaf = strrchr(name, '\\');
        _snprintf_s(out, room, _TRUNCATE, "%s", leaf ? leaf + 1 : name);
        return;
    }
    _snprintf_s(out, room, _TRUNCATE, "%s", "no loaded module");
}

static const char* faultCodeName(DWORD code) {
    switch (code) {
        case EXCEPTION_ACCESS_VIOLATION:      return "an access violation";
        case EXCEPTION_STACK_OVERFLOW:        return "a stack overflow";
        case EXCEPTION_ILLEGAL_INSTRUCTION:   return "an illegal instruction";
        case EXCEPTION_INT_DIVIDE_BY_ZERO:    return "a divide by zero";
        case EXCEPTION_PRIV_INSTRUCTION:      return "a privileged instruction";
        case EXCEPTION_IN_PAGE_ERROR:         return "a page that could not be read";
        case 0xC0000409:                      return "a stack buffer overrun";
        case 0xE06D7363:                      return "an uncaught C++ exception";
        default:                              return "a fault";
    }
}

/*
    who    what the fault is reported against, in words, or null for the
           process at large
    during what it was doing, or null when nothing named it
*/
static void writeCrashReport(EXCEPTION_POINTERS* info, const char* who,
                             const char* during) {
    if (!info || !info->ExceptionRecord) return;
    const EXCEPTION_RECORD* record = info->ExceptionRecord;
    const uint64_t at = (uint64_t)record->ExceptionAddress;

    char where[MAX_PATH + 64];
    describeAddress(at, where, sizeof(where));
    writeLog("CRASH %s%s%s: %s, code %08lX at %s",
             who ? who : "the process", during ? " while " : "",
             during ? during : "", faultCodeName(record->ExceptionCode),
             (unsigned long)record->ExceptionCode, where);
    if (at >= global_gameTextBase
        && at < global_gameTextBase + global_gameTextSize)
        writeLog("CRASH   that is the game at rva %08X",
                 (unsigned)(at - global_gameModuleBase));

    if ((record->ExceptionCode == EXCEPTION_ACCESS_VIOLATION
         || record->ExceptionCode == EXCEPTION_IN_PAGE_ERROR)
        && record->NumberParameters >= 2) {
        const ULONG_PTR kind = record->ExceptionInformation[0];
        writeLog("CRASH   while %s address %016llx",
                 kind == 1 ? "writing" : kind == 8 ? "executing" : "reading",
                 (unsigned long long)record->ExceptionInformation[1]);
    }

    if (info->ContextRecord) {
        const CONTEXT* c = info->ContextRecord;
        writeLog("CRASH   rax %016llx rcx %016llx rdx %016llx rbx %016llx",
                 (unsigned long long)c->Rax, (unsigned long long)c->Rcx,
                 (unsigned long long)c->Rdx, (unsigned long long)c->Rbx);
        writeLog("CRASH   rsp %016llx rbp %016llx rsi %016llx rdi %016llx",
                 (unsigned long long)c->Rsp, (unsigned long long)c->Rbp,
                 (unsigned long long)c->Rsi, (unsigned long long)c->Rdi);
        writeLog("CRASH   r8  %016llx r9  %016llx r10 %016llx r11 %016llx",
                 (unsigned long long)c->R8, (unsigned long long)c->R9,
                 (unsigned long long)c->R10, (unsigned long long)c->R11);
        writeLog("CRASH   r12 %016llx r13 %016llx r14 %016llx r15 %016llx",
                 (unsigned long long)c->R12, (unsigned long long)c->R13,
                 (unsigned long long)c->R14, (unsigned long long)c->R15);
    }

    if (info->ContextRecord) {
        const uint64_t* stack = (const uint64_t*)info->ContextRecord->Rsp;
        int written = 0;
        for (int i = 0; i < 256 && written < 16; ++i) {
            uint64_t value = 0;
            if (!readQwordGuarded((uint64_t)(stack + i), &value)) break;
            if (value < SMALLEST_POINTER) continue;
            if (!addressIsCode(value)) continue;
            if (value >= global_gameTextBase
                && value < global_gameTextBase + global_gameTextSize)
                writeLog("CRASH   came through the game at rva %08X",
                         (unsigned)(value - global_gameModuleBase));
            else {
                char frame[MAX_PATH + 64];
                describeAddress(value, frame, sizeof(frame));
                writeLog("CRASH   came through %s", frame);
            }
            ++written;
        }
    }
}

/* The addresses of the faulting instructions already reported. */
#define CRASH_ADDRESSES_HELD 64

static uint64_t      global_crashAddress[CRASH_ADDRESSES_HELD];
static volatile LONG global_crashAddresses = 0;

/* Not exact when two threads fault at once: a fault may be reported twice. */
static int firstFaultHere(uint64_t at) {
    const LONG held = global_crashAddresses;
    for (LONG i = 0; i < held && i < CRASH_ADDRESSES_HELD; ++i)
        if (global_crashAddress[i] == at) return 0;
    if (held >= CRASH_ADDRESSES_HELD) return 0;
    global_crashAddress[held] = at;
    InterlockedIncrement(&global_crashAddresses);
    return 1;
}

static LONG WINAPI reportCrash(EXCEPTION_POINTERS* info) {
    writeCrashReport(info, nullptr, nullptr);
    return EXCEPTION_CONTINUE_SEARCH;
}

static LONG CALLBACK watchExceptions(EXCEPTION_POINTERS* info) {
    if (!info || !info->ExceptionRecord) return EXCEPTION_CONTINUE_SEARCH;
    const DWORD code = info->ExceptionRecord->ExceptionCode;
    /* The fault codes, not the exceptions a program throws and catches. */
    if (code != EXCEPTION_ACCESS_VIOLATION && code != EXCEPTION_STACK_OVERFLOW
        && code != EXCEPTION_ILLEGAL_INSTRUCTION && code != 0xC0000409
        && code != EXCEPTION_IN_PAGE_ERROR
        && code != EXCEPTION_INT_DIVIDE_BY_ZERO)
        return EXCEPTION_CONTINUE_SEARCH;
    if (!firstFaultHere((uint64_t)info->ExceptionRecord->ExceptionAddress))
        return EXCEPTION_CONTINUE_SEARCH;
    char inWhat[MAX_PATH];
    char caught[MAX_PATH + 32];
    moduleNameAt((uint64_t)info->ExceptionRecord->ExceptionAddress,
                 inWhat, sizeof(inWhat));
    _snprintf_s(caught, sizeof(caught), _TRUNCATE, "a fault caught in %s",
                inWhat);
    writeCrashReport(info, caught, nullptr);
    return EXCEPTION_CONTINUE_SEARCH;
}

static void narrowText(char* out, size_t room, const wchar_t* text) {
    size_t at = 0;
    if (text) for (; text[at] && at + 1 < room; ++at) out[at] = (char)text[at];
    out[at] = 0;
}

static void reportBadArgument(const wchar_t* expression, const wchar_t* function,
                              const wchar_t* file, unsigned int line,
                              uintptr_t reserved) {
    (void)reserved;
    char saidWhat[256], inWhat[128], whichFile[MAX_PATH];
    narrowText(saidWhat, sizeof(saidWhat), expression);
    narrowText(inWhat, sizeof(inWhat), function);
    narrowText(whichFile, sizeof(whichFile), file);
    writeLog("CRASH a bounded call refused its arguments: %s in %s, %s line %u",
             saidWhat[0] ? saidWhat : "no expression given",
             inWhat[0] ? inWhat : "an unnamed function",
             whichFile[0] ? whichFile : "no file", line);
}

/*
    A plugin's own bounded call refusing its arguments.

    plugin      the plugin's name, for the line
    saidWhat    the expression the CRT complained about, or null
    inWhat      the function it was in, or null
    whichFile   the source file, or null
    line        the source line, or 0

    A release build gives none of the four. Each plugin installs its own
    handler; the one set here covers the loader alone.
*/
LOADER_EXPORT void LoaderReportBadArgument(LoaderHandle, const char* plugin,
                                           const char* saidWhat,
                                           const char* inWhat,
                                           const char* whichFile,
                                           unsigned int line) {
    writeLog("CRASH %s handed a bounded call more than it could hold: %s in "
             "%s, %s line %u. The call was refused rather than allowed to end "
             "the process, so what it was writing is empty or cut short",
             plugin && *plugin ? plugin : "a plugin",
             saidWhat && *saidWhat ? saidWhat : "no expression given",
             inWhat && *inWhat ? inWhat : "an unnamed function",
             whichFile && *whichFile ? whichFile : "no file", line);
}

static void watchForCrashes() {
    SetUnhandledExceptionFilter(reportCrash);
    AddVectoredExceptionHandler(1, watchExceptions);
    _set_invalid_parameter_handler(reportBadArgument);
}

/*
    For a plugin's own __except filter:

        __except (loaderDescribeFault(GetExceptionInformation(), "the plugin",
                                      "what it was doing"))

    pointers  the EXCEPTION_POINTERS the filter was handed
    plugin    the plugin's name, for the line
    during    what it was doing, in words, or null

    Answers EXCEPTION_EXECUTE_HANDLER. The same instruction faulting again
    writes nothing.
*/
LOADER_EXPORT int LoaderDescribeFault(LoaderHandle, void* pointers,
                                      const char* plugin, const char* during) {
    EXCEPTION_POINTERS* info = (EXCEPTION_POINTERS*)pointers;
    if (info && info->ExceptionRecord
        && firstFaultHere((uint64_t)info->ExceptionRecord->ExceptionAddress)) {
        char caught[256];
        _snprintf_s(caught, sizeof(caught), _TRUNCATE, "a fault caught by %s",
                    plugin ? plugin : "a plugin");
        writeCrashReport(info, caught, during);
    }
    return EXCEPTION_EXECUTE_HANDLER;
}

/* ------------------------------------------------------------------------ */

static bool describeGame() {
    HMODULE mod = GetModuleHandleW(nullptr);
    char path[MAX_PATH];
    const DWORD length = GetModuleFileNameA(mod, path, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) return false;
    const char* slash = strrchr(path, '\\');
    formatInto(global_exeName, sizeof(global_exeName), "%s",
               slash ? slash + 1 : path);

    auto* dos = (IMAGE_DOS_HEADER*)mod;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
    auto* nt = (IMAGE_NT_HEADERS64*)((uint8_t*)mod + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return false;

    global_gameModuleBase = (uint64_t)mod;
    global_gameModuleSize = nt->OptionalHeader.SizeOfImage;
    global_gameTimeDateStamp = nt->FileHeader.TimeDateStamp;
    global_gameCheckSum = nt->OptionalHeader.CheckSum;

    auto* sec = IMAGE_FIRST_SECTION(nt);
    for (int i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++sec)
        if (memcmp(sec->Name, ".text", 5) == 0) {
            global_gameTextBase = (uint64_t)mod + sec->VirtualAddress;
            global_gameTextSize = sec->Misc.VirtualSize;
        }
    if (!global_gameTextBase) return false;

    /* The version must come from VS_FIXEDFILEINFO, not the string resource. */
    DWORD dummy = 0;
    DWORD sz = GetFileVersionInfoSizeA(path, &dummy);
    if (sz) {
        void* buf = malloc(sz);
        if (buf && GetFileVersionInfoA(path, 0, sz, buf)) {
            VS_FIXEDFILEINFO* ffi = nullptr; UINT len = 0;
            if (VerQueryValueA(buf, "\\", (LPVOID*)&ffi, &len) && ffi) {
                global_gameFileVersion[0] = HIWORD(ffi->dwFileVersionMS);
                global_gameFileVersion[1] = LOWORD(ffi->dwFileVersionMS);
                global_gameFileVersion[2] = HIWORD(ffi->dwFileVersionLS);
                global_gameFileVersion[3] = LOWORD(ffi->dwFileVersionLS);
            }
        }
        free(buf);
    }
    return true;
}

#define MAX_PLUGINS 128

/*
    A mod is a folder inside Mods holding its own DLL and everything else it
    needs.

        file      the DLL, <Mods>\<folder>\<name>
        name      the DLL's file name
        folder    the mod's folder, which is also where its ini lives
        orderKey  position in loadorder.txt, or INT32_MAX
*/
struct Candidate {
    char     file[MAX_PATH];
    char     name[64];
    char     folder[MAX_PATH];
    int32_t  orderKey;
};

static Candidate global_cand[MAX_PLUGINS];
static int global_candCount = 0;

/* The file name the loader had as a DLL in Mods\Plugin Loader. */
static const char loaderDllName[] = "tlou_plugin_loader.dll";

static bool sameName(const char* a, const char* b) {
    return _stricmp(a, b) == 0;
}

static int loadorderFromFile(const char* fileName) {
    FILE* f = nullptr;
    if (fopen_s(&f, global_loadorderPath, "r") != 0 || !f) return INT32_MAX;
    char line[MAX_PATH];
    int idx = 0, found = INT32_MAX;
    while (fgets(line, sizeof(line), f)) {
        if (!strchr(line, '\n') && !feof(f)) {
            int c;
            while ((c = fgetc(f)) != '\n' && c != EOF) {}
            continue;
        }
        char* s = line;
        while (*s == ' ' || *s == '\t') ++s;
        char* e = s + strlen(s);
        while (e > s && (e[-1] == '\n' || e[-1] == '\r' || e[-1] == ' '
                         || e[-1] == '\t')) --e;
        *e = 0;
        if (!*s || *s == ';' || *s == '#') continue;
        if (sameName(s, fileName)) { found = idx; break; }
        ++idx;
    }
    fclose(f);
    return found;
}

static int compareCandidates(const void* pa, const void* pb) {
    const Candidate* a = (const Candidate*)pa;
    const Candidate* b = (const Candidate*)pb;
    if (a->orderKey != b->orderKey) return a->orderKey < b->orderKey ? -1 : 1;
    return _stricmp(a->name, b->name);
}

/* The first DLL directly inside a mod's folder. A folder with no DLL in it is
   not a mod and is passed over without comment. */
static int dllInFolder(const char* folder, char* nameOut, size_t nameSize) {
    char pattern[MAX_PATH];
    if (!formatInto(pattern, sizeof(pattern), "%s\\*.dll", folder)) return 0;
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;
    int found = 0;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        if (!formatInto(nameOut, nameSize, "%s", fd.cFileName)) {
            writeLog("%s in %s is passed over: a plugin's file name has room "
                     "for %u character(s)", fd.cFileName, folder,
                     (unsigned)(nameSize - 1));
            continue;
        }
        found = 1;
        break;
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    return found;
}

static void discover() {
    char pattern[MAX_PATH];
    if (!formatInto(pattern, sizeof(pattern), "%s\\*", global_modsDir)) return;
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) {
        writeLog("no mod folders found in %s", global_modsDir);
        return;
    }
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
        if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0)
            continue;
        if (global_candCount >= MAX_PLUGINS) {
            writeLog("more than %d mod(s) present; the rest are ignored",
                 MAX_PLUGINS);
            break;
        }
        char folder[MAX_PATH], dll[64];
        if (!formatInto(folder, sizeof(folder), "%s\\%s", global_modsDir,
                        fd.cFileName)) {
            writeLog("the mod folder %s is passed over: its path is longer "
                     "than %u character(s)", fd.cFileName,
                     (unsigned)(MAX_PATH - 1));
            continue;
        }
        /* The loader's own folder is not a plugin. */
        if (_stricmp(folder, global_ownFolder) == 0) continue;
        if (!dllInFolder(folder, dll, sizeof(dll))) continue;
        if (sameName(dll, loaderDllName)) {
            writeLog("%s in %s is passed over: it is a copy of the plugin "
                     "loader, not a plugin", dll, fd.cFileName);
            continue;
        }

        Candidate* c = &global_cand[global_candCount];
        if (!formatInto(c->file, sizeof(c->file), "%s\\%s", folder, dll)) {
            writeLog("%s in %s is passed over: its path is longer than %u "
                     "character(s)", dll, fd.cFileName,
                     (unsigned)(MAX_PATH - 1));
            continue;
        }
        ++global_candCount;
        formatInto(c->folder, sizeof(c->folder), "%s", folder);
        formatInto(c->name, sizeof(c->name), "%s", dll);
        /* loadorder.txt may name either the folder or the DLL. */
        c->orderKey = loadorderFromFile(fd.cFileName);
        if (c->orderKey == INT32_MAX) c->orderKey = loadorderFromFile(dll);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
}

static void loadAll() {
    LoaderHandle handle = (LoaderHandle)global_selfModule;

    for (int i = 0; i < global_candCount; ++i) {
        Candidate* c = &global_cand[i];
        HMODULE h = LoadLibraryA(c->file);
        if (!h) {
            writeLog("%-20s FAILED to load, error %lu", c->name, GetLastError());
            continue;
        }

        auto init = (int (*)(LoaderHandle))GetProcAddress(h, "Init");
        if (!init) {
            writeLog("%-20s not a plugin (no Init export), unloaded", c->name);
            FreeLibrary(h);
            continue;
        }

        auto supportsGame = (int (*)(LoaderHandle))GetProcAddress(h, "SupportsGame");

        /* A plugin that does not export SupportsGame is loaded. One whose
           SupportsGame answers 0 is not loaded unless the ini overrides it. */
        if (supportsGame && !supportsGame(handle)) {
            char key[80];
            formatInto(key, sizeof(key), "force_%s", c->name);
            int forced = GetPrivateProfileIntA("overrides", key, 0, global_iniPath);
            if (!forced) {
                writeLog("%-20s reports it does not support game %u.%u.%u.%u "
                     "(timestamp %08x) - NOT LOADED", c->name,
                     global_gameFileVersion[0], global_gameFileVersion[1],
                     global_gameFileVersion[2], global_gameFileVersion[3],
                     global_gameTimeDateStamp);
                writeLog("%-20s   set [overrides] %s=1 in tlou_plugin_loader.ini to "
                     "load it anyway", "", key);
                FreeLibrary(h);
                continue;
            }
            writeLog("%-20s reports no support, OVERRIDDEN by tlou_plugin_loader.ini",
                 c->name);
        }

        formatInto(global_modFolder, sizeof(global_modFolder), "%s", c->folder);

        if (!init(handle)) writeLog("%-20s Init reported failure", c->name);
    }
}

/* --------------------------------------------------------------------------
   The game only

   The Ultimate ASI Loader (winmm.dll in the game folder) loads every .asi
   file into each program in that folder that imports winmm.dll: the game,
   and also its crash reporter (crs-handler.exe, crs-uploader.exe,
   crs-video.exe) and launcher.exe. The loader works only in the game's own
   executables; in any other program it does nothing at all.
   -------------------------------------------------------------------------- */

/* The executables of The Last of Us Part I and of Part II. */
static const wchar_t* const gamePrograms[] = { L"tlou-i.exe", L"tlou-i-l.exe",
                                               L"tlou-ii.exe", L"tlou-ii-l.exe" };

/* The file name of the program this process runs. Empty when it cannot be
   read. */
static void programName(wchar_t* out, size_t room) {
    wchar_t path[MAX_PATH];
    out[0] = 0;
    const DWORD length = GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) return;
    const wchar_t* slash = wcsrchr(path, L'\\');
    wcsncpy_s(out, room, slash ? slash + 1 : path, _TRUNCATE);
}

/* Whether this process runs one of the game's own executables. */
static int inTheGame() {
    wchar_t name[MAX_PATH];
    programName(name, MAX_PATH);
    for (const wchar_t* game : gamePrograms)
        if (_wcsicmp(name, game) == 0) return 1;
    return 0;
}

/* A wide string as UTF-8, on the heap, for the log; the caller frees it. Null
   when it cannot be converted. */
static char* utf8Copy(const wchar_t* text) {
    if (!text) return nullptr;
    const int need = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0,
                                         nullptr, nullptr);
    if (need <= 0) return nullptr;
    char* out = (char*)malloc((size_t)need);
    if (!out) return nullptr;
    if (!WideCharToMultiByte(CP_UTF8, 0, text, -1, out, need, nullptr, nullptr)) {
        free(out);
        return nullptr;
    }
    return out;
}

/* A FILETIME as one number of 100 ns units. */
static unsigned long long fileTimeNumber(const FILETIME& when) {
    ULARGE_INTEGER number;
    number.LowPart = when.dwLowDateTime;
    number.HighPart = when.dwHighDateTime;
    return number.QuadPart;
}

/* When this process was created, or 0 when that cannot be read. */
static unsigned long long thisProcessCreated() {
    FILETIME created, ended, kernel, user;
    if (!GetProcessTimes(GetCurrentProcess(), &created, &ended, &kernel, &user))
        return 0;
    return fileTimeNumber(created);
}

/*
    The process that started this one, by the process list: its id and its
    program's file name.

    parentId    the id, or 0 when it cannot be read
    parentName  the file name; empty when that process has ended, or when its
                id now belongs to a program started after this one
*/
static void parentOfThisProcess(DWORD* parentId, wchar_t* parentName,
                                size_t room) {
    *parentId = 0;
    parentName[0] = 0;
    const DWORD mine = GetCurrentProcessId();
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return;

    PROCESSENTRY32W entry;
    entry.dwSize = sizeof(entry);
    if (Process32FirstW(snapshot, &entry)) {
        do {
            if (entry.th32ProcessID == mine) {
                *parentId = entry.th32ParentProcessID;
                break;
            }
        } while (Process32NextW(snapshot, &entry));
    }
    if (*parentId) {
        entry.dwSize = sizeof(entry);
        if (Process32FirstW(snapshot, &entry)) {
            do {
                if (entry.th32ProcessID == *parentId) {
                    wcsncpy_s(parentName, room, entry.szExeFile, _TRUNCATE);
                    break;
                }
            } while (Process32NextW(snapshot, &entry));
        }
    }
    CloseHandle(snapshot);

    /* A process under that id created after this one is another program that
       took the id once the parent ended. */
    if (parentName[0]) {
        HANDLE parent = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE,
                                    *parentId);
        if (parent) {
            FILETIME created, ended, kernel, user;
            const unsigned long long mineCreated = thisProcessCreated();
            if (mineCreated
                && GetProcessTimes(parent, &created, &ended, &kernel, &user)
                && fileTimeNumber(created) > mineCreated)
                parentName[0] = 0;
            CloseHandle(parent);
        }
    }
}

/* Writes which program this process runs, what started it, and its command
   line. Each line opens with lead. */
static void logThisProcess(const char* lead) {
    wchar_t name[MAX_PATH], parentName[MAX_PATH];
    DWORD parentId = 0;
    programName(name, MAX_PATH);
    parentOfThisProcess(&parentId, parentName, MAX_PATH);

    unsigned long long runningMs = 0;
    const unsigned long long created = thisProcessCreated();
    FILETIME now;
    GetSystemTimeAsFileTime(&now);
    if (created && fileTimeNumber(now) > created)
        runningMs = (fileTimeNumber(now) - created) / 10000;

    char* nameText = utf8Copy(name);
    char* parentText = utf8Copy(parentName);
    char* commandLine = utf8Copy(GetCommandLineW());
    const char* shownName = nameText && *nameText ? nameText
                                                  : "a program of unknown name";
    if (parentText && *parentText)
        writeLog("%sit is %s, process %lu, started %llu ms ago by %s "
                 "(process %lu)", lead, shownName, GetCurrentProcessId(),
                 runningMs, parentText, parentId);
    else
        writeLog("%sit is %s, process %lu, started %llu ms ago by process %lu, "
                 "which has since ended", lead, shownName,
                 GetCurrentProcessId(), runningMs, parentId);
    writeLog("%sits command line: %s", lead,
             commandLine ? commandLine : "(not readable)");
    free(nameText);
    free(parentText);
    free(commandLine);
}

#if LOADER_BETA_LINES
/* The beta's note, in a program that is not the game, of which program it is. */
static DWORD WINAPI noteOtherProgram(LPVOID) {
    writeLog("TLOU Plugin Loader " LOADER_VERSION " was loaded by a program that "
             "is not the game, and does nothing in it");
    logThisProcess("  ");
    return 0;
}
#endif

/* --------------------------------------------------------------------------
   Loading the plugins at the right point in the game's start

   The ASI loader loads the loader on the game's main thread at the game's
   entry point, before the game's own start-up code runs, and the game waits
   until DllMain returns: DllMain only sets up and starts the loader's thread.
   A plugin can need the game a certain way into its start: the framework
   enlarges the game's text cache, which has to exist by then, and adds its
   entry to the menus, which must not be built yet; loading after a fixed wait
   would land there only on the hardware the wait was timed for.

   The loader's thread waits until the game's code is in place - it polls the
   read-only data for a known signature - and then loads the plugins; the
   loading is heavy, parses menu files and places hooks, and stays off the
   game's thread. There is no fixed wait, so it holds on any hardware.
   -------------------------------------------------------------------------- */

static const char* const listenerUpdateSignature =
    "void __cdecl ScriptManager::ListenerUpdate(void)";

static volatile LONG global_pluginsLoaded = 0;

/* Discovers and loads the plugins, once however many times it is called. */
static void loadPluginsOnce() {
    if (InterlockedCompareExchange(&global_pluginsLoaded, 1, 0) != 0) return;
    discover();
    if (global_candCount > 1)
        qsort(global_cand, global_candCount, sizeof(Candidate), compareCandidates);
    loadAll();
}

#define GAME_CODE_POLL_MS  100
#define GAME_CODE_CAP_MS   120000

/* Whether every page from first for size bytes can be read now. Asking first,
   rather than reading and catching the fault, keeps the check out of the crash
   lines. */
static int canRead(const unsigned char* first, size_t size) {
    const unsigned char* at = first;
    const unsigned char* end = first + size;
    const DWORD readable = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY
                         | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE
                         | PAGE_EXECUTE_WRITECOPY;
    while (at < end) {
        MEMORY_BASIC_INFORMATION about;
        if (VirtualQuery(at, &about, sizeof(about)) != sizeof(about)) return 0;
        if (about.State != MEM_COMMIT) return 0;
        if (about.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return 0;
        if (!(about.Protect & readable)) return 0;
        const unsigned char* next = (const unsigned char*)about.BaseAddress
                                  + about.RegionSize;
        if (next <= at) return 0;
        at = next;
    }
    return 1;
}

/* Whether the game's read-only data holds this signature. A section that
   cannot be read yet counts as not in place. */
static int readOnlyDataHas(const char* signature) {
    int found = 0;
    GUARD_TRY {
        unsigned char* base = (unsigned char*)global_gameModuleBase;
        IMAGE_DOS_HEADER* dos = (IMAGE_DOS_HEADER*)base;
        IMAGE_NT_HEADERS64* nt = (IMAGE_NT_HEADERS64*)(base + dos->e_lfanew);
        if (base && dos->e_magic == IMAGE_DOS_SIGNATURE
            && nt->Signature == IMAGE_NT_SIGNATURE) {
            const size_t sigLen = strlen(signature);
            IMAGE_SECTION_HEADER* sh = IMAGE_FIRST_SECTION(nt);
            for (int i = 0; i < nt->FileHeader.NumberOfSections && !found; ++i) {
                if (sh[i].Characteristics & IMAGE_SCN_MEM_EXECUTE) continue;
                if (!(sh[i].Characteristics & IMAGE_SCN_MEM_READ)) continue;
                unsigned char* p = base + sh[i].VirtualAddress;
                const size_t n = sh[i].Misc.VirtualSize;
                if (n < sigLen + 1) continue;
                if (!canRead(p, n)) continue;
                for (size_t o = 0; o + sigLen + 1 <= n; ++o)
                    if (p[o] == (unsigned char)signature[0]
                        && memcmp(p + o, signature, sigLen + 1) == 0) {
                        found = 1;
                        break;
                    }
            }
        }
    } GUARD_EXCEPT { found = 0; }
    return found;
}

/* The beta's line saying at which check the game's code was in place. */
static void noteGameCodeInPlace(int check, ULONGLONG waitedMs) {
#if LOADER_BETA_LINES
    writeLog("TLOU Plugin Loader " LOADER_VERSION " in %s, process %lu: the "
             "game's code was in place at check %d, %llu ms into the wait",
             global_exeName, GetCurrentProcessId(), check,
             (unsigned long long)waitedMs);
#else
    (void)check;
    (void)waitedMs;
#endif
}

/* Waits until the game's read-only data is in place, so the finder can work.
   Answers 1 when it is, 0 when the wait ran out. */
static int waitForGameCode() {
    const ULONGLONG began = GetTickCount64();
    const int checks = GAME_CODE_CAP_MS / GAME_CODE_POLL_MS;
    for (int check = 1; check <= checks; ++check) {
        if (readOnlyDataHas(listenerUpdateSignature)) {
            noteGameCodeInPlace(check, GetTickCount64() - began);
            return 1;
        }
        Sleep(GAME_CODE_POLL_MS);
    }
    return 0;
}

static const char* protectionName(DWORD protect) {
    switch (protect & 0xFF) {
        case PAGE_NOACCESS:          return "no access";
        case PAGE_READONLY:          return "read";
        case PAGE_READWRITE:         return "read-write";
        case PAGE_WRITECOPY:         return "read-write (copy on write)";
        case PAGE_EXECUTE:           return "execute";
        case PAGE_EXECUTE_READ:      return "execute-read";
        case PAGE_EXECUTE_READWRITE: return "execute-read-write";
        case PAGE_EXECUTE_WRITECOPY: return "execute-read-write (copy on write)";
        default:                     return "an unknown protection";
    }
}

/* One line naming each section of the game's image and how its memory can be
   used now, opened with lead. */
static void logSections(const char* lead) {
    char line[1024];
    size_t used = 0;
    line[0] = 0;
    GUARD_TRY {
        unsigned char* base = (unsigned char*)global_gameModuleBase;
        IMAGE_DOS_HEADER* dos = (IMAGE_DOS_HEADER*)base;
        IMAGE_NT_HEADERS64* nt = (IMAGE_NT_HEADERS64*)(base + dos->e_lfanew);
        IMAGE_SECTION_HEADER* sh = IMAGE_FIRST_SECTION(nt);
        for (int i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
            char name[9];
            memcpy(name, sh[i].Name, 8);
            name[8] = 0;
            MEMORY_BASIC_INFORMATION about;
            memset(&about, 0, sizeof(about));
            const char* state = "not readable";
            const char* guard = "";
            if (VirtualQuery(base + sh[i].VirtualAddress, &about, sizeof(about))
                == sizeof(about)) {
                if (about.State == MEM_COMMIT) {
                    state = protectionName(about.Protect);
                    if (about.Protect & PAGE_GUARD) guard = ", guarded";
                } else if (about.State == MEM_RESERVE) {
                    state = "reserved, not committed";
                } else {
                    state = "free";
                }
            }
            const int wrote = _snprintf_s(line + used, sizeof(line) - used,
                                          _TRUNCATE, "%s%s %s%s",
                                          used ? "; " : "", name, state, guard);
            if (wrote < 0) break;
            used += (size_t)wrote;
        }
    } GUARD_EXCEPT { }
    writeLog("%sits sections now: %s", lead,
             line[0] ? line : "(its header could not be read)");
}

static DWORD WINAPI start(LPVOID) {
    /* tlou_plugin_loader.ini and loadorder.txt are put in place in
       Mods\Plugin Loader before either is read. */
    writeDefaultSettings(global_selfModule, global_iniPath);
    moveOldLoadOrder(global_loadorderPath);
    writeDefaultLoadOrder(global_selfModule, global_loadorderPath);
    if (!describeGame()) { writeLog("could not describe the game module"); return 1; }

    SetLog(hookFinderLogSink);
    SetCacheFolder(global_ownFolder);
    prepareHookSlots();
    SetOriginalBytesSource(originalBytesAt);

    /* Wait for the game's code to be in place, then load. */
    if (!waitForGameCode()) {
        writeLog("the game's code was not in place within %d s: its read-only "
                 "data never held the signature \"%s\", so no plugins are loaded "
                 "into this process", GAME_CODE_CAP_MS / 1000,
                 listenerUpdateSignature);
        logThisProcess("  ");
        logSections("  ");
        return 0;
    }
    loadPluginsOnce();
    return 0;
}

/*
    The game's folder, Mods, the loader's folder Mods\Plugin Loader and the
    paths of the loader's files in them, each built from the folder of the
    game's program, never from the folder of the loader's own file. Answers 0
    where a path does not fit in MAX_PATH.
*/
static int setPaths() {
    return moduleFolder(nullptr, global_gameDir, sizeof(global_gameDir))
        && formatInto(global_modsDir, sizeof(global_modsDir), "%s\\Mods",
                      global_gameDir)
        && formatInto(global_ownFolder, sizeof(global_ownFolder),
                      "%s\\Plugin Loader", global_modsDir)
        && formatInto(global_loadorderPath, sizeof(global_loadorderPath),
                      "%s\\loadorder.txt", global_ownFolder)
        && formatInto(global_logPath, sizeof(global_logPath),
                      "%s\\tlou_plugin_loader.log", global_ownFolder)
        && formatInto(global_iniPath, sizeof(global_iniPath),
                      "%s\\tlou_plugin_loader.ini", global_ownFolder);
}

#define MAX_LISTED_MODULES 1024

/*
    Another copy of the plugin loader already loaded in this process: a module
    other than self that exports both LoaderHookFunction and LoaderLog, as
    every version of the loader does. Answers 1, with that module's path in
    out (empty where it cannot be read), or 0 where there is none or the list
    of modules cannot be read.
*/
static int otherLoaderCopy(HMODULE self, char* out, size_t room) {
    out[0] = 0;
    HMODULE modules[MAX_LISTED_MODULES];
    DWORD bytes = 0;
    if (!K32EnumProcessModules(GetCurrentProcess(), modules,
                               (DWORD)sizeof(modules), &bytes))
        return 0;
    DWORD count = bytes / (DWORD)sizeof(HMODULE);
    if (count > MAX_LISTED_MODULES) count = MAX_LISTED_MODULES;
    for (DWORD i = 0; i < count; ++i) {
        if (modules[i] == self) continue;
        if (!GetProcAddress(modules[i], "LoaderHookFunction")
            || !GetProcAddress(modules[i], "LoaderLog"))
            continue;
        const DWORD length = GetModuleFileNameA(modules[i], out, (DWORD)room);
        if (length == 0 || length >= room) out[0] = 0;
        return 1;
    }
    return 0;
}

BOOL APIENTRY DllMain(HMODULE self, DWORD reason, LPVOID) {
    if (reason != DLL_PROCESS_ATTACH) return TRUE;

    /* In a program that is not the game the loader does nothing at all. */
    if (!inTheGame()) {
#if LOADER_BETA_LINES
        if (setPaths()) {
            const HANDLE note = CreateThread(nullptr, 0, noteOtherProgram,
                                             nullptr, 0, nullptr);
            if (note) CloseHandle(note);
        }
#endif
        return TRUE;
    }

    DisableThreadLibraryCalls(self);
    global_selfModule = self;

    /* A path that does not fit in MAX_PATH leaves the game running with no
       plugin loaded and nothing written. */
    if (!setPaths()) return TRUE;

    /* A second copy of the loader in the process writes this one line to the
       log and does nothing else; the copy loaded first runs. */
    char other[MAX_PATH];
    if (otherLoaderCopy(self, other, sizeof(other))) {
        char mine[MAX_PATH];
        const DWORD length = GetModuleFileNameA(self, mine, MAX_PATH);
        if (length == 0 || length >= MAX_PATH) mine[0] = 0;
        writeLog("TLOU Plugin Loader " LOADER_VERSION " at %s does nothing: "
                 "another copy of the plugin loader, %s, was loaded into the "
                 "game first and loads the plugins. Remove one of the two",
                 mine[0] ? mine : "(path not readable)",
                 other[0] ? other : "(path not readable)");
        return TRUE;
    }

    /* Each level must exist before the next can be created. */
    CreateDirectoryA(global_modsDir, nullptr);
    CreateDirectoryA(global_ownFolder, nullptr);
    InitializeCriticalSection(&global_logLock);
    global_logLockReady = true;
    watchForCrashes();

    const HANDLE thread = CreateThread(nullptr, 0, start, nullptr, 0, nullptr);
    if (thread) CloseHandle(thread);
    return TRUE;
}
