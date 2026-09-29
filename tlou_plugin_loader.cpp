#include <windows.h>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <tlhelp32.h>

#undef near
#undef far

#include "tlou_plugin_sdk.h"
#include "hook_finder.h"

#pragma comment(lib, "version.lib")

#if defined(_MSC_VER)
#define GUARD_TRY    __try
#define GUARD_EXCEPT __except (EXCEPTION_EXECUTE_HANDLER)
#else
#define GUARD_TRY    if (1)
#define GUARD_EXCEPT else
#endif

static char global_gameDir[MAX_PATH];
static char global_modsDir[MAX_PATH];
static char global_logsDir[MAX_PATH];
/* The loader's own folder, Mods\Plugin Loader, which is where the DLL lives.
   Holds tlou_plugin_loader.ini, Logs and Cache. */
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

static void logRaw(const char* plugin, const char* fmt, va_list ap) {
    if (global_logLockReady) EnterCriticalSection(&global_logLock);
    FILE* f = nullptr;
    if (fopen_s(&f, global_logPath, "a") == 0 && f) {
        SYSTEMTIME st; GetLocalTime(&st);
        fprintf(f, "[%04d-%02d-%02d %02d:%02d:%02d.%03d] ",
                st.wYear, st.wMonth, st.wDay,
                st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
        if (plugin && *plugin) fprintf(f, "%-20s ", plugin);
        vfprintf(f, fmt, ap);
        fprintf(f, "\n");
        fclose(f);
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
        LoaderLogsFolder     Mods\<mod>\Logs
        LoaderCacheFolder    Mods\<mod>\Cache

    Logs and Cache are created before Init is called.
*/
static char global_modFolder[MAX_PATH];
static char global_modLogsDir[MAX_PATH];
static char global_modCacheDir[MAX_PATH];

LOADER_EXPORT const char* LoaderGameFolder(LoaderHandle)    { return global_gameDir; }
LOADER_EXPORT const char* LoaderPluginsFolder(LoaderHandle) { return global_modsDir; }
LOADER_EXPORT const char* LoaderModFolder(LoaderHandle)     { return global_modFolder; }
LOADER_EXPORT const char* LoaderLogsFolder(LoaderHandle)    { return global_modLogsDir; }
LOADER_EXPORT const char* LoaderConfigFolder(LoaderHandle)  { return global_modFolder; }
LOADER_EXPORT const char* LoaderCacheFolder(LoaderHandle)   { return global_modCacheDir; }

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
*/
struct HookSlot {
    uint64_t       stub;
    void**         callback;
    uint64_t*      returnAddress;
    const char*    name;
    uint8_t*       patchedAt;
    uint8_t        originalBytes[MAX_DISPLACED];
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
    Enough of the x86-64 encoding to measure an instruction's length and find a
    rip relative displacement in it.

    Accepts only an instruction that can be moved to another address
    unchanged, or unchanged but for a rip relative displacement the caller
    rewrites. Relative branches and unknown encodings are refused.
*/
static int decodeInstruction(const uint8_t* p, MovedInstruction* out) {
    int i = 0;
    out->length = 0;
    out->ripDisplacement = -1;

    /* Legacy prefixes. 0x66 also narrows an immediate from four bytes to two. */
    int operandSize32 = 1;
    for (;;) {
        const uint8_t prefix = p[i];
        if (prefix == 0x66) { operandSize32 = 0; ++i; continue; }
        if (prefix == 0x67 || prefix == 0xF2 || prefix == 0xF3
            || prefix == 0x2E || prefix == 0x36 || prefix == 0x3E
            || prefix == 0x26 || prefix == 0x64 || prefix == 0x65) {
            ++i; continue;
        }
        break;
    }
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
            case 0xFE: case 0xFF:                        /* inc, dec, push */
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
    s->patchedAt = site->address;
    s->name = name;
    return 1;
}

static void hookFinderLogSink(const char* text) { writeLog("%s", text); }

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
    same address is added as a subscriber and nothing is patched twice.

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
    FunctionSite site;
    if (!FindFunctionByName(name, signature, &site)) {
        writeLog("%s was not found", name);
        return 0;
    }
    MarkFunctionVerified(name, &site);
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
        char folder[MAX_PATH], dll[64], longest[MAX_PATH];
        if (!formatInto(folder, sizeof(folder), "%s\\%s", global_modsDir,
                        fd.cFileName)
            || !formatInto(longest, sizeof(longest), "%s\\Cache", folder)) {
            writeLog("the mod folder %s is passed over: its path is longer "
                     "than %u character(s)", fd.cFileName,
                     (unsigned)(MAX_PATH - 1));
            continue;
        }
        /* The loader's own folder is not a plugin. */
        if (_stricmp(folder, global_ownFolder) == 0) continue;
        if (!dllInFolder(folder, dll, sizeof(dll))) continue;

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
        formatInto(global_modLogsDir, sizeof(global_modLogsDir), "%s\\Logs",
                   c->folder);
        formatInto(global_modCacheDir, sizeof(global_modCacheDir), "%s\\Cache",
                   c->folder);
        CreateDirectoryA(global_modLogsDir, nullptr);
        CreateDirectoryA(global_modCacheDir, nullptr);

        if (!init(handle)) writeLog("%-20s Init reported failure", c->name);
    }
}

/* --------------------------------------------------------------------------
   Loading the plugins at the right point in the game's start

   As version.dll the loader attaches while the game is still starting, before
   the game's own code runs. A plugin can need the game a certain way into its
   start: the framework enlarges the game's text cache, which has to exist by
   then, and adds its entry to the menus, which must not be built yet. When the
   launcher injected the loader it landed in that window; loading after a fixed
   wait would land there only on the hardware the wait was timed for.

   The loader reaches the same point by waiting, in the game's own sequence,
   until the game's code is decrypted and in place - it polls the read-only data
   for a known signature, which appears only then, the same point the launcher's
   settle used to reach and before the game has built its menus. It then loads
   the plugins on its own thread; the loading is heavy, parses menu files and
   places hooks, and stays off the game's thread. There is no fixed wait, so it
   holds on any hardware.
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

/* Whether the game's read-only data holds this signature. Reading it while the
   executable is still being decrypted can fault, which counts as not yet in
   place. */
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

/* Waits until the game's read-only data is in place, so the finder can work. */
static void waitForGameCode() {
    for (int waited = 0; waited < GAME_CODE_CAP_MS; waited += GAME_CODE_POLL_MS) {
        if (readOnlyDataHas(listenerUpdateSignature)) return;
        Sleep(GAME_CODE_POLL_MS);
    }
    writeLog("the game's read-only data was not in place within %d ms, so the "
             "hooks may not be found", GAME_CODE_CAP_MS);
}

static DWORD WINAPI start(LPVOID) {
    if (!describeGame()) { writeLog("could not describe the game module"); return 1; }

    SetLog(hookFinderLogSink);
    char cache[MAX_PATH];
    if (formatInto(cache, sizeof(cache), "%s\\Cache", global_ownFolder)) {
        CreateDirectoryA(cache, nullptr);
        SetCacheFolder(cache);
    }
    prepareHookSlots();

    /* Wait, in the game's own sequence, for its code to be decrypted and in
       place - the point the launcher's settle used to reach - then load. */
    waitForGameCode();
    loadPluginsOnce();
    return 0;
}

/* The log of the previous run, moved into the Archive folder beside it and
   named for the time it was last written. */
static void archiveLog() {
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
    if (!formatInto(folder, sizeof(folder), "%s\\Archive", global_logsDir))
        return;
    CreateDirectoryA(folder, nullptr);
    if (!formatInto(kept, sizeof(kept),
                    "%s\\tlou_plugin_loader_%04u%02u%02u_%02u%02u%02u.log",
                    folder, when.wYear, when.wMonth, when.wDay, when.wHour,
                    when.wMinute, when.wSecond))
        return;
    MoveFileA(global_logPath, kept);
}

BOOL APIENTRY DllMain(HMODULE self, DWORD reason, LPVOID) {
    if (reason != DLL_PROCESS_ATTACH) return TRUE;
    DisableThreadLibraryCalls(self);
    global_selfModule = self;

    /* A path that does not fit in MAX_PATH leaves the game running with no
       plugin loaded and nothing written. */
    if (!moduleFolder(nullptr, global_gameDir, sizeof(global_gameDir))
        || !moduleFolder(self, global_ownFolder, sizeof(global_ownFolder))
        || !formatInto(global_modsDir, sizeof(global_modsDir), "%s\\Mods",
                       global_gameDir)
        || !formatInto(global_logsDir, sizeof(global_logsDir), "%s\\Logs",
                       global_ownFolder)
        || !formatInto(global_loadorderPath, sizeof(global_loadorderPath),
                       "%s\\loadorder.txt", global_modsDir)
        || !formatInto(global_logPath, sizeof(global_logPath),
                       "%s\\tlou_plugin_loader.log", global_logsDir)
        || !formatInto(global_iniPath, sizeof(global_iniPath),
                       "%s\\tlou_plugin_loader.ini", global_ownFolder))
        return TRUE;

    /* Each level must exist before the next can be created. */
    CreateDirectoryA(global_modsDir, nullptr);
    CreateDirectoryA(global_ownFolder, nullptr);
    CreateDirectoryA(global_logsDir, nullptr);
    archiveLog();
    InitializeCriticalSection(&global_logLock);
    global_logLockReady = true;
    watchForCrashes();

    CreateThread(nullptr, 0, start, nullptr, 0, nullptr);
    return TRUE;
}
