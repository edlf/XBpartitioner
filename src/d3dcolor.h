#ifndef D3DCOLOR_H_
#define D3DCOLOR_H_

// D3DCOLOR (0xAARRGGBB) and its macros: from the XDK on the Xbox, defined
// here on the PC (fatxgui), so themes and the background build on both.

#ifdef _XBOX
#include <xtl.h>
#else
#include <windows.h>

#ifndef D3DCOLOR_DEFINED
typedef DWORD D3DCOLOR;
#define D3DCOLOR_DEFINED
#endif

#ifndef D3DCOLOR_ARGB
#define D3DCOLOR_ARGB(a, r, g, b) ((D3DCOLOR)((((a) & 0xff) << 24) | (((r) & 0xff) << 16) | (((g) & 0xff) << 8) | ((b) & 0xff)))
#define D3DCOLOR_XRGB(r, g, b) D3DCOLOR_ARGB(0xff, r, g, b)
#endif
#endif

#endif // D3DCOLOR_H_
