# XBpartitioner

Currently doesnt format big fatx partitions. Requires cerbios for lba48v4 support.

winioctl.h is not included due to MS copyright

## Fonts

The UI uses the [DejaVu fonts](https://dejavu-fonts.github.io/) (free to redistribute), pre-rendered into bitmap fonts that are compiled into the `.xbe`, so only the `.xbe` needs to be copied to the console.

The `font_*.h` headers are generated from the `.ttf` files in `fonts/` by `tools/makefonts.bat`, using the XDK's `makefont.exe`. Rerun it after changing a font, its size or the character set (`tools/fontchars.txt`, one hex character code per line):

```
set MAKEFONT=<XDK path>\xbox\bin\makefont.exe
tools\makefonts.bat
```

| Header | Font | Size |
|---|---|---|
| `font_text.h` | DejaVu Sans | 18px |
| `font_title.h` | DejaVu Sans Bold | 32px |
| `font_table.h` | DejaVu Sans Mono | 18px |
| `font_*_720.h` | same | 27 / 48 / 27px, used in 720p mode |

## Video modes

The video mode follows the dashboard settings and the AV cable: 720p with a component (HDTV) cable and 720p enabled, 480p with a component or VGA cable and 480p enabled, otherwise 480i. In 720p the 4:3 layout is scaled 1.5x and centered (960x720). The help screen (Back) shows the active mode.
