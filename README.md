# XBpartitioner

XBpartitioner updated with new features and lba48v4 support. Requires a lba48 v4 compatible bios, Cerbios recommended.

## Features
- LBA48 v4 support (+2TB HDDs)
- Reworked interface for ease of use, no need to remember what every button does anymore
- 512e/4K sector support
- Partition table backup restore for HDDs formatted with FATXplorer
- 720p support
- Not really useful but theme support


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
