#define _CRT_SECURE_NO_WARNINGS

#include "hook_finder.h"

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

/*
    Instruction encoding (x86-64):

        [REX] opcode ModRM [SIB] [disp8|disp32]

        REX   0x40..0x4F   bit 2 = R (extends ModRM.reg)
                           bit 1 = X (extends SIB.index)
                           bit 0 = B (extends ModRM.rm or SIB.base)
        ModRM mod(2) reg(3) rm(3)      mod == 3 means rm is a register
        SIB   scale(2) index(3) base(3), present when mod != 3 and rm == 4
              index 100b with REX.X clear means no index register
              base  101b with mod == 0 means no base register, disp32 follows
        disp  1 byte when mod == 1, 4 bytes when mod == 2,
              4 bytes when mod == 0 and (rm == 5 or SIB.base == 5)

    Sequence located:

        add   R1, [M]           opcode 03, memory operand M
        cmp   R1, R2            opcode 3B or 39, mod == 3
        cmov  RD, {R1|R2}       opcode 0F 4x, mod == 3
        mov   [M], RD           opcode 89, memory operand equal to M

    Only a REX prefix is accepted before the opcode.

    hook_cache.ini format:

        [build]
        timedatestamp=HEX
        sizeofimage=HEX
        checksum=HEX

        [hook:NAME]
        rva=HEX
        bytes=HEX pairs, no separators
        base=DECIMAL register number, -1 for none
        index=DECIMAL register number, -1 for none
        scale=1|2|4|8
        disp=DECIMAL
        adddest=DECIMAL register number
        storesrc=DECIMAL register number
        addlen=DECIMAL
        seqlen=DECIMAL

    Entries are written only by MarkVerified and MarkFunctionVerified.
*/

#define SEARCH_WINDOW   32
#define MAX_CANDIDATES  64
#define MAX_HOOKS       16

/* ---------------------------------------------------------------- logging */

static LogFn global_log = NULL;

void SetLog(LogFn fn)
{
    global_log = fn;
}

static void emitLog(const char *fmt, ...)
{
    if (!global_log) return;
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    buf[sizeof(buf) - 1] = 0;
    global_log(buf);
}

static void hexString(const unsigned char *p, int len, char *out, size_t outSize)
{
    static const char *digits = "0123456789ABCDEF";
    size_t o = 0;
    for (int i = 0; i < len && o + 3 < outSize; ++i) {
        out[o++] = digits[(p[i] >> 4) & 0xF];
        out[o++] = digits[p[i] & 0xF];
    }
    out[o] = 0;
}

/* ------------------------------------------------------------ hook state */

typedef struct {
    char name[64];
    int  disabled;
    int  verified;
    int  used;
} HookState;

static HookState global_state[MAX_HOOKS];

static void copyName(char *dst, size_t size, const char *src)
{
    snprintf(dst, size, "%s", src);
}

static HookState *stateForHook(const char *name)
{
    for (int i = 0; i < MAX_HOOKS; ++i)
        if (global_state[i].used && strcmp(global_state[i].name, name) == 0)
            return &global_state[i];
    for (int i = 0; i < MAX_HOOKS; ++i) {
        if (!global_state[i].used) {
            global_state[i].used = 1;
            global_state[i].disabled = 0;
            global_state[i].verified = 0;
            copyName(global_state[i].name, sizeof(global_state[i].name), name);
            return &global_state[i];
        }
    }
    return NULL;
}

int IsDisabled(const char *name)
{
    HookState *s = stateForHook(name);
    return s ? s->disabled : 1;
}

int EnableLogOnFire(const char *name)
{
    HookState *s = stateForHook(name);
    return s ? !s->verified : 1;
}

/* --------------------------------------------------------- module access */

typedef struct {
    unsigned int timeDateStamp;
    unsigned int sizeOfImage;
    unsigned int checkSum;
} BuildIdentity;

static int moduleInfo(unsigned char **textPtr, size_t *textSize,
                      unsigned int *textRva, BuildIdentity *id,
                      unsigned char **imageBase)
{
    unsigned char *base = (unsigned char *)GetModuleHandleW(NULL);
    if (!base) return 0;

    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)base;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return 0;
    IMAGE_NT_HEADERS64 *nt = (IMAGE_NT_HEADERS64 *)(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return 0;

    id->timeDateStamp = (unsigned int)nt->FileHeader.TimeDateStamp;
    id->sizeOfImage   = (unsigned int)nt->OptionalHeader.SizeOfImage;
    id->checkSum      = (unsigned int)nt->OptionalHeader.CheckSum;
    *imageBase = base;

    IMAGE_SECTION_HEADER *sh = IMAGE_FIRST_SECTION(nt);
    for (int i = 0; i < (int)nt->FileHeader.NumberOfSections; ++i) {
        if (!(sh[i].Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
        *textPtr  = base + sh[i].VirtualAddress;
        *textSize = sh[i].Misc.VirtualSize;
        *textRva  = sh[i].VirtualAddress;
        return 1;
    }
    return 0;
}

/* --------------------------------------------------------------- decoder */

typedef struct {
    int mod, rm, hasSib, sib, dispLen;
    long long disp;
    int rexX, rexB;
} Operand;

typedef struct {
    int len;
    int reg;
    int rmReg;
    int mod;
    Operand mem;
} Decoded;

static Decoded decodeOp(const unsigned char *p, size_t avail, int twoByte,
                        unsigned char opcode, unsigned char mask)
{
    Decoded d;
    memset(&d, 0, sizeof(d));

    size_t i = 0;
    int rex = 0;
    if (i < avail && p[i] >= 0x40 && p[i] <= 0x4F) { rex = p[i]; i++; }

    if (twoByte) {
        if (i >= avail || p[i] != 0x0F) return d;
        i++;
    }
    if (i >= avail || (p[i] & mask) != opcode) return d;
    i++;
    if (i >= avail) return d;

    unsigned char modrm = p[i]; i++;
    int rexR = (rex >> 2) & 1;
    int rexX = (rex >> 1) & 1;
    int rexB = rex & 1;

    d.mod = modrm >> 6;
    d.reg = ((modrm >> 3) & 7) | (rexR << 3);

    if (d.mod == 3) {
        d.rmReg = (modrm & 7) | (rexB << 3);
        d.len = (int)i;
        return d;
    }

    d.mem.mod  = d.mod;
    d.mem.rm   = modrm & 7;
    d.mem.rexX = rexX;
    d.mem.rexB = rexB;

    if (d.mem.rm == 4) {
        if (i >= avail) return d;
        d.mem.hasSib = 1;
        d.mem.sib = p[i];
        i++;
    }

    int dispLen = 0;
    if (d.mod == 1)                                dispLen = 1;
    else if (d.mod == 2)                           dispLen = 4;
    else if (d.mem.rm == 5)                        dispLen = 4;
    else if (d.mem.hasSib && (d.mem.sib & 7) == 5) dispLen = 4;

    if (i + (size_t)dispLen > avail) return d;
    if (dispLen == 1) {
        d.mem.disp = (signed char)p[i];
    } else if (dispLen == 4) {
        int v; memcpy(&v, p + i, 4); d.mem.disp = v;
    }
    i += (size_t)dispLen;

    d.mem.dispLen = dispLen;
    d.len = (int)i;
    return d;
}

static int sameOperand(const Operand *a, const Operand *b)
{
    if (a->mod != b->mod || a->rm != b->rm) return 0;
    if (a->hasSib != b->hasSib) return 0;
    if (a->hasSib && a->sib != b->sib) return 0;
    if (a->dispLen != b->dispLen || a->disp != b->disp) return 0;
    if (a->rexX != b->rexX || a->rexB != b->rexB) return 0;
    return 1;
}

static int operandHasIndex(const Operand *m)
{
    if (!m->hasSib) return 0;
    return !(((m->sib >> 3) & 7) == 4 && m->rexX == 0);
}

static int operandIndexReg(const Operand *m)
{
    if (!operandHasIndex(m)) return -1;
    return ((m->sib >> 3) & 7) | (m->rexX << 3);
}

static int operandBaseReg(const Operand *m)
{
    if (!m->hasSib) {
        if (m->mod == 0 && m->rm == 5) return -1;
        return m->rm | (m->rexB << 3);
    }
    if (m->mod == 0 && (m->sib & 7) == 5) return -1;
    return (m->sib & 7) | (m->rexB << 3);
}

static int operandScale(const Operand *m)
{
    if (!m->hasSib) return 1;
    return 1 << ((m->sib >> 6) & 3);
}

/* ------------------------------------------------------------ candidates */

typedef struct {
    unsigned int rva;
    unsigned char *address;
    int addLength;
    int sequenceLength;
    int contiguous;
    Operand mem;
    int addDestReg;
    int storeSrcReg;
} Candidate;

static int findCandidates(unsigned char *text, size_t n, unsigned int textRva,
                          Candidate *out, int maxOut)
{
    int count = 0;
    size_t acceptedEnd = 0;

    for (size_t i = 0; i + 4 < n && count < maxOut; ++i) {
        if (text[i] != 0x03 &&
            !(text[i] >= 0x40 && text[i] <= 0x4F && text[i + 1] == 0x03))
            continue;

        Decoded a = decodeOp(text + i, n - i, 0, 0x03, 0xFF);
        if (!a.len || a.mod == 3) continue;

        size_t wEnd = (size_t)a.len + SEARCH_WINDOW;
        if (wEnd > n - i) wEnd = n - i;

        int r1 = a.reg, r2 = -1, rd = -1;
        size_t cmpOff = 0, cmovOff = 0;
        int cmpLen = 0, cmovLen = 0;

        for (size_t o = (size_t)a.len; o < wEnd && r2 < 0; ++o) {
            Decoded c = decodeOp(text + i + o, n - i - o, 0, 0x3B, 0xFF);
            if (!c.len || c.mod != 3)
                c = decodeOp(text + i + o, n - i - o, 0, 0x39, 0xFF);
            if (!c.len || c.mod != 3) continue;
            if (c.reg == r1 && c.rmReg != r1)      { r2 = c.rmReg; cmpOff = o; cmpLen = c.len; }
            else if (c.rmReg == r1 && c.reg != r1) { r2 = c.reg;   cmpOff = o; cmpLen = c.len; }
        }
        if (r2 < 0) continue;

        for (size_t o = cmpOff + (size_t)cmpLen; o < wEnd && rd < 0; ++o) {
            Decoded c = decodeOp(text + i + o, n - i - o, 1, 0x40, 0xF0);
            if (!c.len || c.mod != 3) continue;
            if ((c.reg == r1 && c.rmReg == r2) || (c.reg == r2 && c.rmReg == r1)) {
                rd = c.reg; cmovOff = o; cmovLen = c.len;
            }
        }
        if (rd < 0) continue;

        for (size_t o = cmovOff + (size_t)cmovLen; o < wEnd; ++o) {
            Decoded t = decodeOp(text + i + o, n - i - o, 0, 0x89, 0xFF);
            if (!t.len || t.mod == 3) continue;
            if (!sameOperand(&t.mem, &a.mem)) continue;
            if (t.reg != rd) continue;

            /* A sequence starting with a REX prefix also decodes one byte
               later as a valid sequence with different registers. */
            if (i < acceptedEnd) break;

            Candidate *c = &out[count++];
            c->rva            = textRva + (unsigned int)i;
            c->address        = text + i;
            c->addLength      = a.len;
            c->sequenceLength = (int)(o + (size_t)t.len);
            c->contiguous     = (cmpOff == (size_t)a.len &&
                                 cmovOff == cmpOff + (size_t)cmpLen &&
                                 o == cmovOff + (size_t)cmovLen) ? 1 : 0;
            c->mem            = a.mem;
            c->addDestReg     = r1;
            c->storeSrcReg    = rd;
            acceptedEnd = i + o + (size_t)t.len;
            break;
        }
    }
    return count;
}

static size_t countOccurrences(const unsigned char *text, size_t n,
                               const unsigned char *pat, size_t len)
{
    size_t total = 0;
    if (n < len) return 0;
    for (size_t i = 0; i + len <= n; ++i)
        if (text[i] == pat[0] && memcmp(text + i, pat, len) == 0) total++;
    return total;
}

/* ----------------------------------------------------------------- cache */

typedef struct {
    char name[64];
    unsigned int rva;
    unsigned char bytes[MAX_SEQ_BYTES];
    int byteLen;
    int baseReg, indexReg, scale;
    long long disp;
    int addDest, storeSrc;
    int addLen, seqLen;
    int used;
} CacheEntry;

static CacheEntry  global_cache[MAX_HOOKS];
static int         global_cacheLoaded = 0;
static WCHAR       global_cachePath[MAX_PATH];

/* Set by SetCacheFolder. Empty until a caller names one. */
static WCHAR global_cacheFolder[MAX_PATH];

void SetCacheFolder(const char *folder)
{
    global_cacheFolder[0] = 0;
    global_cachePath[0] = 0;
    if (!folder || !*folder) return;
    MultiByteToWideChar(CP_ACP, 0, folder, -1, global_cacheFolder, MAX_PATH);
    global_cacheFolder[MAX_PATH - 1] = 0;
}

/*
    The cache lives in the folder named by SetCacheFolder, or in
    <game>\Mods\Cache when none is named.
*/
static int buildCachePath(void)
{
    WCHAR cacheDir[MAX_PATH];

    if (global_cacheFolder[0]) {
        if (_snwprintf(cacheDir, MAX_PATH, L"%s", global_cacheFolder) < 0)
            return 0;
        CreateDirectoryW(cacheDir, NULL);
    } else {
        WCHAR exePath[MAX_PATH];
        DWORD len = GetModuleFileNameW(NULL, exePath, MAX_PATH);
        if (len == 0 || len >= MAX_PATH) return 0;
        for (int i = (int)len - 1; i >= 0; --i)
            if (exePath[i] == L'\\') { exePath[i] = 0; break; }

        WCHAR modsRoot[MAX_PATH];
        if (_snwprintf(modsRoot, MAX_PATH, L"%s\\Mods", exePath) < 0) return 0;
        if (_snwprintf(cacheDir, MAX_PATH, L"%s\\Mods\\Cache", exePath) < 0) return 0;
        /* Each level must exist before the next can be created. */
        CreateDirectoryW(modsRoot, NULL);
        CreateDirectoryW(cacheDir, NULL);
    }

    if (_snwprintf(global_cachePath, MAX_PATH, L"%s\\hook_cache.ini", cacheDir) < 0)
        return 0;
    return 1;
}

static char *readToMemory(const WCHAR *path, DWORD *sizeOut)
{
    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return NULL;
    DWORD size = GetFileSize(h, NULL);
    if (size == INVALID_FILE_SIZE || size > (1 << 20)) { CloseHandle(h); return NULL; }
    char *buf = (char *)malloc(size + 1);
    if (!buf) { CloseHandle(h); return NULL; }
    DWORD got = 0;
    if (!ReadFile(h, buf, size, &got, NULL)) { free(buf); CloseHandle(h); return NULL; }
    CloseHandle(h);
    buf[got] = 0;
    if (sizeOut) *sizeOut = got;
    return buf;
}

static int hexValue(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

static int parseHexBytes(const char *s, unsigned char *out, int maxOut)
{
    int n = 0;
    while (s[0] && s[1] && n < maxOut) {
        int hi = hexValue(s[0]), lo = hexValue(s[1]);
        if (hi < 0 || lo < 0) break;
        out[n++] = (unsigned char)((hi << 4) | lo);
        s += 2;
    }
    return n;
}

static void loadCache(const BuildIdentity *id)
{
    if (global_cacheLoaded) return;
    global_cacheLoaded = 1;
    memset(global_cache, 0, sizeof(global_cache));

    if (!buildCachePath()) {
        emitLog("hook_finder: cannot determine the cache path; cache disabled");
        return;
    }

    char *text = readToMemory(global_cachePath, NULL);
    if (!text) return;

    BuildIdentity cached;
    memset(&cached, 0, sizeof(cached));
    CacheEntry *cur = NULL;
    int inBuild = 0;

    char *line = text;
    while (line && *line) {
        char *end = strpbrk(line, "\r\n");
        if (end) { *end = 0; }

        while (*line == ' ' || *line == '\t') line++;

        if (line[0] == '[') {
            inBuild = 0;
            cur = NULL;
            if (strncmp(line, "[build]", 7) == 0) {
                inBuild = 1;
            } else if (strncmp(line, "[hook:", 6) == 0) {
                char nameBuf[64];
                const char *p = line + 6;
                int k = 0;
                while (p[k] && p[k] != ']' && k < (int)sizeof(nameBuf) - 1) { nameBuf[k] = p[k]; k++; }
                nameBuf[k] = 0;
                for (int i = 0; i < MAX_HOOKS; ++i) {
                    if (!global_cache[i].used) {
                        global_cache[i].used = 1;
                        copyName(global_cache[i].name, sizeof(global_cache[i].name), nameBuf);
                        cur = &global_cache[i];
                        break;
                    }
                }
            }
        } else if (line[0] && line[0] != ';') {
            char *eq = strchr(line, '=');
            if (eq) {
                *eq = 0;
                const char *key = line, *val = eq + 1;
                if (inBuild) {
                    if (strcmp(key, "timedatestamp") == 0) cached.timeDateStamp = (unsigned int)strtoul(val, NULL, 16);
                    else if (strcmp(key, "sizeofimage") == 0) cached.sizeOfImage = (unsigned int)strtoul(val, NULL, 16);
                    else if (strcmp(key, "checksum") == 0)    cached.checkSum    = (unsigned int)strtoul(val, NULL, 16);
                } else if (cur) {
                    if (strcmp(key, "rva") == 0)           cur->rva      = (unsigned int)strtoul(val, NULL, 16);
                    else if (strcmp(key, "bytes") == 0)    cur->byteLen  = parseHexBytes(val, cur->bytes, MAX_SEQ_BYTES);
                    else if (strcmp(key, "base") == 0)     cur->baseReg  = (int)strtol(val, NULL, 10);
                    else if (strcmp(key, "index") == 0)    cur->indexReg = (int)strtol(val, NULL, 10);
                    else if (strcmp(key, "scale") == 0)    cur->scale    = (int)strtol(val, NULL, 10);
                    else if (strcmp(key, "disp") == 0)     cur->disp     = strtoll(val, NULL, 10);
                    else if (strcmp(key, "adddest") == 0)  cur->addDest  = (int)strtol(val, NULL, 10);
                    else if (strcmp(key, "storesrc") == 0) cur->storeSrc = (int)strtol(val, NULL, 10);
                    else if (strcmp(key, "addlen") == 0)   cur->addLen   = (int)strtol(val, NULL, 10);
                    else if (strcmp(key, "seqlen") == 0)   cur->seqLen   = (int)strtol(val, NULL, 10);
                }
            }
        }

        if (!end) break;
        line = end + 1;
        while (*line == '\r' || *line == '\n') line++;
    }
    free(text);

    if (cached.timeDateStamp != id->timeDateStamp ||
        cached.sizeOfImage   != id->sizeOfImage   ||
        cached.checkSum      != id->checkSum)
        memset(global_cache, 0, sizeof(global_cache));
}

static void writeCache(const BuildIdentity *id)
{
    if (!global_cachePath[0] && !buildCachePath()) return;

    char body[8192];
    int o = snprintf(body, sizeof(body),
                     "[build]\r\ntimedatestamp=%08X\r\nsizeofimage=%08X\r\nchecksum=%08X\r\n",
                     id->timeDateStamp, id->sizeOfImage, id->checkSum);

    for (int i = 0; i < MAX_HOOKS && o > 0 && o < (int)sizeof(body); ++i) {
        if (!global_cache[i].used) continue;
        char hex[MAX_SEQ_BYTES * 2 + 1];
        hexString(global_cache[i].bytes, global_cache[i].byteLen, hex, sizeof(hex));
        int n = snprintf(body + o, sizeof(body) - (size_t)o,
                         "\r\n[hook:%s]\r\nrva=%08X\r\nbytes=%s\r\nbase=%d\r\nindex=%d\r\n"
                         "scale=%d\r\ndisp=%lld\r\nadddest=%d\r\nstoresrc=%d\r\n"
                         "addlen=%d\r\nseqlen=%d\r\n",
                         global_cache[i].name, global_cache[i].rva, hex,
                         global_cache[i].baseReg, global_cache[i].indexReg, global_cache[i].scale,
                         global_cache[i].disp, global_cache[i].addDest, global_cache[i].storeSrc,
                         global_cache[i].addLen, global_cache[i].seqLen);
        if (n < 0) break;
        o += n;
    }

    HANDLE h = CreateFileW(global_cachePath, GENERIC_WRITE, 0, NULL,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        emitLog("hook_finder: cannot write the cache file, error %lu",
                (unsigned long)GetLastError());
        return;
    }
    DWORD written = 0;
    WriteFile(h, body, (DWORD)strlen(body), &written, NULL);
    CloseHandle(h);
}

static CacheEntry *cacheEntry(const char *name)
{
    for (int i = 0; i < MAX_HOOKS; ++i)
        if (global_cache[i].used && strcmp(global_cache[i].name, name) == 0)
            return &global_cache[i];
    return NULL;
}

/* ------------------------------------------------------------------- API */

int Find(const Request *req, Site *out)
{
    HookState *st = stateForHook(req->name);
    memset(out, 0, sizeof(*out));
    out->baseReg = -1;
    out->indexReg = -1;

    unsigned char *text = NULL, *imageBase = NULL;
    size_t textSize = 0;
    unsigned int textRva = 0;
    BuildIdentity id;
    memset(&id, 0, sizeof(id));

    if (!moduleInfo(&text, &textSize, &textRva, &id, &imageBase)) {
        Fail(req->name, "cannot read the game module headers");
        return 0;
    }

    loadCache(&id);

    CacheEntry *ce = req->ignoreCache ? NULL : cacheEntry(req->name);
    if (ce) {
        unsigned char *addr = imageBase + ce->rva;
        if (ce->rva < id.sizeOfImage && ce->byteLen > 0 &&
            memcmp(addr, ce->bytes, (size_t)ce->byteLen) == 0) {
            out->address        = addr;
            out->rva            = ce->rva;
            out->addLength      = ce->addLen;
            out->sequenceLength = ce->seqLen;
            out->fromCache      = 1;
            out->baseReg        = ce->baseReg;
            out->indexReg       = ce->indexReg;
            out->scale          = ce->scale;
            out->displacement   = ce->disp;
            out->addDestReg     = ce->addDest;
            out->storeSrcReg    = ce->storeSrc;
            out->originalLength = ce->byteLen;
            memcpy(out->originalBytes, ce->bytes, (size_t)ce->byteLen);
            return 1;
        }
        emitLog("hook_finder [%s]: cache entry for rva %08X no longer matches "
                "the bytes in memory; discarding it and searching again",
                req->name, ce->rva);
        ce->used = 0;
    }

    if (req->fixedPattern && req->fixedPatternLen) {
        size_t hits = countOccurrences(text, textSize,
                                       req->fixedPattern, req->fixedPatternLen);

        if (hits == 1) {
            for (size_t i = 0; i + req->fixedPatternLen <= textSize; ++i) {
                if (memcmp(text + i, req->fixedPattern, req->fixedPatternLen) != 0)
                    continue;
                Decoded a = decodeOp(text + i, textSize - i, 0, 0x03, 0xFF);
                if (!a.len || a.mod == 3) {
                    emitLog("hook_finder [%s]: the fixed pattern does not begin "
                            "with an add from memory; falling back to the scan",
                            req->name);
                    break;
                }
                Candidate c;
                memset(&c, 0, sizeof(c));
                c.rva = textRva + (unsigned int)i;
                c.address = text + i;
                c.addLength = a.len;
                c.sequenceLength = (int)req->fixedPatternLen;
                c.contiguous = 1;
                c.mem = a.mem;
                c.addDestReg = a.reg;
                c.storeSrcReg = -1;

                out->address        = c.address;
                out->rva            = c.rva;
                out->addLength      = c.addLength;
                out->sequenceLength = c.sequenceLength;
                out->baseReg        = operandBaseReg(&c.mem);
                out->indexReg       = operandIndexReg(&c.mem);
                out->scale          = operandScale(&c.mem);
                out->displacement   = c.mem.disp;
                out->addDestReg     = c.addDestReg;
                out->storeSrcReg    = c.storeSrcReg;
                out->originalLength = c.sequenceLength < MAX_SEQ_BYTES
                                    ? c.sequenceLength : MAX_SEQ_BYTES;
                memcpy(out->originalBytes, c.address, (size_t)out->originalLength);
                return 1;
            }
        } else {
            emitLog("hook_finder [%s]: fixed pattern unusable (%s); using the "
                    "register-agnostic scan",
                    req->name, hits == 0 ? "not found" : "more than one match");
        }
    }

    Candidate *cands = (Candidate *)malloc(sizeof(Candidate) * MAX_CANDIDATES);
    if (!cands) {
        Fail(req->name, "out of memory allocating the candidate list");
        return 0;
    }

    int total = findCandidates(text, textSize, textRva, cands, MAX_CANDIDATES);

    int kept[MAX_CANDIDATES];
    int keptCount = 0;
    for (int i = 0; i < total; ++i) {
        Candidate *c = &cands[i];
        if (req->requireIndexRegister && !operandHasIndex(&c->mem)) continue;
        if (req->requireDisplacement && c->mem.disp != req->expectedDisplacement)
            continue;
        if (req->requireContiguous && !c->contiguous) continue;
        kept[keptCount++] = i;
    }

    if (keptCount != 1) {
        Fail(req->name,
                       "the scan produced %d acceptable candidate(s) from %d shape "
                       "match(es); exactly one is required, so the hook has been "
                       "disabled and the game is left unmodified",
                       keptCount, total);
        free(cands);
        return 0;
    }

    Candidate *c = &cands[kept[0]];
    int seqLen = c->sequenceLength < MAX_SEQ_BYTES ? c->sequenceLength : MAX_SEQ_BYTES;
    size_t occ = countOccurrences(text, textSize, c->address, (size_t)seqLen);
    if (occ != 1) {
        Fail(req->name,
                       "the byte sequence at the chosen site is not unique (%zu "
                       "occurrences), so it cannot be cached as a pattern; the hook "
                       "has been disabled and the game is left unmodified", occ);
        free(cands);
        return 0;
    }

    out->address        = c->address;
    out->rva            = c->rva;
    out->addLength      = c->addLength;
    out->sequenceLength = c->sequenceLength;
    out->baseReg        = operandBaseReg(&c->mem);
    out->indexReg       = operandIndexReg(&c->mem);
    out->scale          = operandScale(&c->mem);
    out->displacement   = c->mem.disp;
    out->addDestReg     = c->addDestReg;
    out->storeSrcReg    = c->storeSrcReg;
    out->originalLength = seqLen;
    memcpy(out->originalBytes, c->address, (size_t)seqLen);

    free(cands);
    (void)st;
    return 1;
}

void MarkVerified(const Request *req, const Site *site)
{
    HookState *st = stateForHook(req->name);
    if (st) st->verified = 1;
    if (site->fromCache) return;

    unsigned char *text = NULL, *imageBase = NULL;
    size_t textSize = 0;
    unsigned int textRva = 0;
    BuildIdentity id;
    memset(&id, 0, sizeof(id));
    if (!moduleInfo(&text, &textSize, &textRva, &id, &imageBase)) return;

    CacheEntry *ce = cacheEntry(req->name);
    if (!ce) {
        for (int i = 0; i < MAX_HOOKS; ++i) {
            if (!global_cache[i].used) {
                global_cache[i].used = 1;
                copyName(global_cache[i].name, sizeof(global_cache[i].name), req->name);
                ce = &global_cache[i];
                break;
            }
        }
    }
    if (!ce) {
        emitLog("hook_finder [%s]: no free cache slot; the site will be searched "
                "for again on the next launch", req->name);
        return;
    }

    if (site->originalLength <= 0) {
        emitLog("hook_finder [%s]: no original bytes were captured, so nothing "
                "can be cached", req->name);
        return;
    }
    ce->rva      = site->rva;
    ce->byteLen  = site->originalLength;
    memcpy(ce->bytes, site->originalBytes, (size_t)site->originalLength);
    ce->baseReg  = site->baseReg;
    ce->indexReg = site->indexReg;
    ce->scale    = site->scale;
    ce->disp     = site->displacement;
    ce->addDest  = site->addDestReg;
    ce->storeSrc = site->storeSrcReg;
    ce->addLen   = site->addLength;
    ce->seqLen   = site->sequenceLength;

    writeCache(&id);
}

void Fail(const char *name, const char *fmt, ...)
{
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    buf[sizeof(buf) - 1] = 0;

    emitLog("hook_finder [%s]: FAILURE - %s", name, buf);

    HookState *st = stateForHook(name);
    if (st) {
        st->disabled = 1;
        st->verified = 0;
    }

    CacheEntry *ce = cacheEntry(name);
    if (ce) {
        emitLog("hook_finder [%s]: discarding the cache entry for rva %08X",
                name, ce->rva);
        ce->used = 0;

        unsigned char *text = NULL, *imageBase = NULL;
        size_t textSize = 0;
        unsigned int textRva = 0;
        BuildIdentity id;
        memset(&id, 0, sizeof(id));
        if (moduleInfo(&text, &textSize, &textRva, &id, &imageBase))
            writeCache(&id);
    }

    emitLog("hook_finder [%s]: hook disabled for this session", name);
}

/* ------------------------------------------------ find a function by name */

/*
    Layout used below:

      IMAGE_RUNTIME_FUNCTION_ENTRY  { u32 begin; u32 end; u32 unwind; }
        sorted by begin, in the exception directory, one per function that
        has unwind data.

      lea reg, [rip + disp32]  encodes as
        REX.W 8D /r   with ModRM mod = 00 and rm = 101
        so the bytes are 48 or 4C, then 8D, then a ModRM whose low 3 bits
        are 101 and whose top 2 bits are 00, then a signed 32 bit
        displacement. The target is the address of the next instruction plus
        that displacement.
*/

static int sectionsOf(unsigned char *base, IMAGE_SECTION_HEADER **out, int *count)
{
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)base;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return 0;
    IMAGE_NT_HEADERS64 *nt = (IMAGE_NT_HEADERS64 *)(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return 0;
    *out = IMAGE_FIRST_SECTION(nt);
    *count = (int)nt->FileHeader.NumberOfSections;
    return 1;
}

/* The function whose range contains rva, from the exception directory. */
static int functionRangeFor(unsigned char *base, unsigned int rva,
                            unsigned int *beginOut, unsigned int *endOut)
{
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)base;
    IMAGE_NT_HEADERS64 *nt = (IMAGE_NT_HEADERS64 *)(base + dos->e_lfanew);
    IMAGE_DATA_DIRECTORY *dir =
        &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];
    if (!dir->VirtualAddress || dir->Size < 12) return 0;

    RUNTIME_FUNCTION *fn = (RUNTIME_FUNCTION *)(base + dir->VirtualAddress);
    int n = (int)(dir->Size / sizeof(RUNTIME_FUNCTION));
    int lo = 0, hi = n - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (rva < fn[mid].BeginAddress)      hi = mid - 1;
        else if (rva >= fn[mid].EndAddress)  lo = mid + 1;
        else {
            *beginOut = fn[mid].BeginAddress;
            *endOut   = fn[mid].EndAddress;
            return 1;
        }
    }
    return 0;
}

int FindFunctionByName(const char *name, const char *signature, FunctionSite *out)
{
    memset(out, 0, sizeof(*out));
    if (!name || !signature || !*signature) {
        Fail(name ? name : "?", "no signature was supplied");
        return 0;
    }

    unsigned char *text = NULL, *imageBase = NULL;
    size_t textSize = 0;
    unsigned int textRva = 0;
    BuildIdentity id;
    memset(&id, 0, sizeof(id));
    if (!moduleInfo(&text, &textSize, &textRva, &id, &imageBase)) {
        Fail(name, "cannot read the game module headers");
        return 0;
    }

    loadCache(&id);
    CacheEntry *ce = cacheEntry(name);
    if (ce && ce->rva && ce->byteLen > 0) {
        unsigned char *addr = imageBase + ce->rva;
        if (memcmp(addr, ce->bytes, (size_t)ce->byteLen) == 0) {
            out->address   = addr;
            out->rva       = ce->rva;
            out->size      = (unsigned int)ce->seqLen;
            out->fromCache = 1;
            return 1;
        }
        emitLog("hook_finder [%s]: cache entry for rva %08X no longer matches; "
                "searching again", name, ce->rva);
        ce->used = 0;
    }

    IMAGE_SECTION_HEADER *sec = NULL;
    int secCount = 0;
    if (!sectionsOf(imageBase, &sec, &secCount)) {
        Fail(name, "the module has no readable section table");
        return 0;
    }

    /* 1. the string, in a readable section that is not code */
    size_t sigLen = strlen(signature);
    unsigned int stringRva = 0;
    size_t found = 0;
    for (int i = 0; i < secCount; ++i) {
        if (sec[i].Characteristics & IMAGE_SCN_MEM_EXECUTE) continue;
        if (!(sec[i].Characteristics & IMAGE_SCN_MEM_READ)) continue;
        unsigned char *p = imageBase + sec[i].VirtualAddress;
        size_t n = sec[i].Misc.VirtualSize;
        if (n < sigLen + 1) continue;
        for (size_t o = 0; o + sigLen + 1 <= n; ++o) {
            if (p[o] != (unsigned char)signature[0]) continue;
            if (memcmp(p + o, signature, sigLen + 1) != 0) continue;
            ++found;
            if (found == 1) stringRva = sec[i].VirtualAddress + (unsigned int)o;
        }
    }
    if (found != 1) {
        Fail(name, "the signature string was found %zu time(s) in read-only "
                   "data; exactly one is required", found);
        return 0;
    }

    /* 2. lea instructions that point at it, and the functions they sit in */
    unsigned int beginRva = 0, endRva = 0, refs = 0, distinct = 0;
    for (int i = 0; i < secCount; ++i) {
        if (!(sec[i].Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
        unsigned char *p = imageBase + sec[i].VirtualAddress;
        size_t n = sec[i].Misc.VirtualSize;
        for (size_t o = 0; o + 7 <= n; ++o) {
            if (p[o] != 0x48 && p[o] != 0x4C) continue;
            if (p[o + 1] != 0x8D) continue;
            if ((p[o + 2] & 0xC7) != 0x05) continue;
            int disp;
            memcpy(&disp, p + o + 3, 4);
            unsigned int here = sec[i].VirtualAddress + (unsigned int)o;
            if (here + 7 + (unsigned int)disp != stringRva) continue;
            ++refs;
            unsigned int b = 0, e = 0;
            if (!functionRangeFor(imageBase, here, &b, &e)) continue;
            if (beginRva && b == beginRva) continue;
            if (beginRva) { ++distinct; continue; }
            beginRva = b; endRva = e; distinct = 1;
        }
    }
    if (distinct != 1) {
        Fail(name, "the signature is referenced from %u instruction(s) in %u "
                   "distinct function(s); exactly one is required",
             refs, distinct);
        return 0;
    }

    out->address   = imageBase + beginRva;
    out->rva       = beginRva;
    out->size      = endRva - beginRva;
    out->fromCache = 0;
    return 1;
}

/* Records a located function in the cache. The first bytes are the fingerprint
   that a later cache hit is checked against. */
void MarkFunctionVerified(const char *name, const FunctionSite *site)
{
    HookState *st = stateForHook(name);
    if (st) st->verified = 1;
    if (site->fromCache) return;

    unsigned char *text = NULL, *imageBase = NULL;
    size_t textSize = 0;
    unsigned int textRva = 0;
    BuildIdentity id;
    memset(&id, 0, sizeof(id));
    if (!moduleInfo(&text, &textSize, &textRva, &id, &imageBase)) return;

    CacheEntry *ce = cacheEntry(name);
    if (!ce) {
        for (int i = 0; i < MAX_HOOKS; ++i) {
            if (!global_cache[i].used) {
                global_cache[i].used = 1;
                copyName(global_cache[i].name, sizeof(global_cache[i].name), name);
                ce = &global_cache[i];
                break;
            }
        }
    }
    if (!ce) return;

    int keep = 32;
    if ((unsigned int)keep > site->size) keep = (int)site->size;
    if (keep > MAX_SEQ_BYTES) keep = MAX_SEQ_BYTES;
    ce->rva     = site->rva;
    ce->byteLen = keep;
    memcpy(ce->bytes, site->address, (size_t)keep);
    ce->baseReg = -1; ce->indexReg = -1; ce->scale = 1; ce->disp = 0;
    ce->addDest = -1; ce->storeSrc = -1;
    ce->addLen  = keep;
    ce->seqLen  = (int)site->size;
    writeCache(&id);
}

int FunctionContaining(unsigned int rva, unsigned int *beginOut,
                       unsigned int *endOut)
{
    unsigned char *base = (unsigned char *)GetModuleHandleW(NULL);
    if (!base) return 0;
    unsigned int begin = 0, end = 0;
    if (!functionRangeFor(base, rva, &begin, &end)) return 0;
    if (beginOut) *beginOut = begin;
    if (endOut)   *endOut = end;
    return 1;
}

unsigned char *EffectiveAddress(const Site *site,
                                          uint64_t baseValue,
                                          uint64_t indexValue)
{
    uint64_t addr = 0;
    if (site->baseReg >= 0)  addr += baseValue;
    if (site->indexReg >= 0) addr += indexValue * (uint64_t)(site->scale ? site->scale : 1);
    addr += (uint64_t)site->displacement;
    return (unsigned char *)addr;
}
