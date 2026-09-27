#pragma once

#include <windows.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*LogFn)(const char *text);

/*
    name                  identifies the hook in hook_cache.ini and in the log
    fixedPattern          primary search; NULL skips straight to the scan
    requireIndexRegister  memory operand must name an index register
    requireDisplacement   memory operand displacement must equal
                          expectedDisplacement
    requireContiguous     no other instructions between the four
*/
typedef struct {
    const char          *name;
    const unsigned char *fixedPattern;
    size_t               fixedPatternLen;
    int                  requireIndexRegister;
    int                  requireDisplacement;
    long long            expectedDisplacement;
    int                  requireContiguous;
    /* Find ignores any cache entry. With fixedPattern NULL as well, this
       forces discovery through the register-agnostic scan. */
    int                  ignoreCache;
} Request;

/*
    baseReg, indexReg     register numbers 0-15 (rax..r15), -1 when absent
    scale                 1, 2, 4 or 8
    addDestReg            register the add writes, 0-15
    storeSrcReg           register the store reads, 0-15, or -1 when found
                          by fixedPattern
    addLength             length of the add instruction alone
    sequenceLength        length of add through store inclusive
    fromCache             1 when taken from hook_cache.ini
*/
/* Size of the byte sequence recorded per hook. Shared with hook_finder.cpp
   and with the cache file format. */
#define MAX_SEQ_BYTES 64

typedef struct {
    unsigned char *address;
    unsigned int   rva;
    int            addLength;
    int            sequenceLength;
    int            fromCache;
    int            baseReg;
    int            indexReg;
    int            scale;
    long long      displacement;
    int            addDestReg;
    int            storeSrcReg;

    /* The bytes at the site as they were when it was located, captured before
       the caller patches anything. The cache must record these, not whatever
       is in memory later. */
    unsigned char  originalBytes[MAX_SEQ_BYTES];
    int            originalLength;
} Site;

/* A located function. */
typedef struct {
    unsigned char *address;
    unsigned int   rva;
    unsigned int   size;
    int            fromCache;
} FunctionSite;

void SetLog(LogFn fn);

/* Where hook_cache.ini is kept. Call once before Find or FindFunctionByName,
   with the mod's own Cache folder. Naming none leaves the cache in
   <game>\Mods\Cache. */
void SetCacheFolder(const char *folder);

/*
    Locates the function that references the given signature string, for
    example

        "const struct ScriptValue *__cdecl ScriptManager::Lookup(class StringId64)"

    name is the key the result is cached under. Returns 1 on success. Returns 0
    after logging the reason, exactly as Find does, and requires the signature
    to appear once and to be referenced from exactly one function.
*/
int FindFunctionByName(const char *name, const char *signature, FunctionSite *out);

/* Records a located function in the cache. */
void MarkFunctionVerified(const char *name, const FunctionSite *site);

/* Returns 1 and fills out on success. Returns 0 after logging the reason and
   marking the hook disabled. */
int Find(const Request *req, Site *out);

/* Call once the hook has executed and confirmed it is operating on the
   expected data. Writes the cache entry and stops per-execution logging. */
void MarkVerified(const Request *req, const Site *site);

/* Logs the failure, discards any cache entry for this hook, and disables it
   for the remainder of the session. */
void Fail(const char *name, const char *fmt, ...);

int IsDisabled(const char *name);

/* 1 until the hook has been verified in this session. */
int EnableLogOnFire(const char *name);

/*
    The function whose body contains rva, taken from the exception directory of
    the running game module. Returns 1 and fills beginOut and endOut with RVAs.
    Returns 0 when rva is outside every function, which is normal for data and
    for code the directory does not cover.
*/
int FunctionContaining(unsigned int rva, unsigned int *beginOut,
                       unsigned int *endOut);

/* Effective address for a site, given the values of the base and index
   registers at the moment the hook fired. */
unsigned char *EffectiveAddress(const Site *site,
                                          uint64_t baseValue,
                                          uint64_t indexValue);

#ifdef __cplusplus
}
#endif
