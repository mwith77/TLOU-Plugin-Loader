; version_shim.asm - the forwarding thunks for the stand-in version.dll.
;
; version.dll must republish every export the real version.dll has. Each thunk
; here tail-jumps to the real function through a pointer version_shim.cpp fills
; in at load (resolveRealVersion). A pointer that did not resolve returns zero,
; the value these APIs report on failure, rather than jumping to nothing.
;
; A jump leaves the caller's stack and registers exactly as they arrived, so
; the real function runs and returns straight to the caller.

.CODE

EXTERN real_GetFileVersionInfoA:QWORD
EXTERN real_GetFileVersionInfoByHandle:QWORD
EXTERN real_GetFileVersionInfoExA:QWORD
EXTERN real_GetFileVersionInfoExW:QWORD
EXTERN real_GetFileVersionInfoSizeA:QWORD
EXTERN real_GetFileVersionInfoSizeExA:QWORD
EXTERN real_GetFileVersionInfoSizeExW:QWORD
EXTERN real_GetFileVersionInfoSizeW:QWORD
EXTERN real_GetFileVersionInfoW:QWORD
EXTERN real_VerFindFileA:QWORD
EXTERN real_VerFindFileW:QWORD
EXTERN real_VerInstallFileA:QWORD
EXTERN real_VerInstallFileW:QWORD
EXTERN real_VerQueryValueA:QWORD
EXTERN real_VerQueryValueW:QWORD

VERSION_PROXY MACRO fname
    LOCAL noReal
PUBLIC fname
fname PROC
    mov     rax, real_&fname
    test    rax, rax
    jz      noReal
    jmp     rax
noReal:
    xor     eax, eax
    ret
fname ENDP
ENDM

VERSION_PROXY GetFileVersionInfoA
VERSION_PROXY GetFileVersionInfoByHandle
VERSION_PROXY GetFileVersionInfoExA
VERSION_PROXY GetFileVersionInfoExW
VERSION_PROXY GetFileVersionInfoSizeA
VERSION_PROXY GetFileVersionInfoSizeExA
VERSION_PROXY GetFileVersionInfoSizeExW
VERSION_PROXY GetFileVersionInfoSizeW
VERSION_PROXY GetFileVersionInfoW
VERSION_PROXY VerFindFileA
VERSION_PROXY VerFindFileW
VERSION_PROXY VerInstallFileA
VERSION_PROXY VerInstallFileW
VERSION_PROXY VerQueryValueA
VERSION_PROXY VerQueryValueW

END
