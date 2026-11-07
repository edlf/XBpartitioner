// Helpers VS2015+ emits calls to that the XDK's 2003 libcmt.lib lacks (RXDK builds).

#if defined(_MSC_VER) && _MSC_VER >= 1400

// Float to int with /arch:SSE. Without SSE2 (the Xbox's Pentium III) the
// runtime's version is just _ftol2.
extern "C" void __cdecl _ftol2();

extern "C" __declspec(naked) void __cdecl _ftol2_sse()
{
    __asm jmp _ftol2
}

#endif
