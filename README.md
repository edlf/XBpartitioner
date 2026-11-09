# XBpartitioner

XBpartitioner updated with new features and lba48v4 support. Requires a lba48 v4 compatible bios, Cerbios recommended.

## Features
- LBA48 v4 support (+2TB HDDs)
- Reworked interface for ease of use, no need to remember what every button does anymore
- 512e/4K sector support
- Partition table backup restore for HDDs formatted with FATXplorer
- 720p support
- Not really useful but theme support


## Settings

Settings are kept in `xbpartitioner.ini`, next to the `.xbe`. It's written when you pick a theme, and can be edited by hand, one `key=value` per line:

```
theme=Nord
animations=0
force_lba48_v3=0
```

| Key | Values |
|---|---|
| `theme` | A theme name, as listed in Start > Theme |
| `animations` | `1` (default): windows grow from a line when they open and shrink back when they close. `0`: they appear and disappear at once. `on`/`off`, `yes`/`no` and `true`/`false` work too. |
| `force_lba48_v3` | `0` (default): use the BIOS's LBA48 patch as it is. `1`: work as with an LBA48 v3 BIOS even on v4: the high LBA bits are ignored (32 bit LBAs, drives used up to 2TB), and partitions are at most 1TB, so they never need clusters bigger than 64 KB. The same limits apply on a real v3 BIOS. |

A missing file, key or value means the default. The file isn't written when running from a DVD, or once the partition the `.xbe` is on has been changed.


## Building

XBpartitioner now builds with the [RXDK](https://github.com/Team-Resurgent/RXDK) and Visual Studio 2022 or newer.

Open `XBpartitioner.sln` and build, or from a Developer Command Prompt:

```
msbuild XBpartitioner.sln -p:Configuration=Release -p:Platform=x86
```

The output goes to `build\<Configuration>\`: `XBpartitioner.xbe`, `Build\XBpartitioner\default.xbe` that is ready to be used.


## Fonts

The UI uses the [DejaVu fonts](https://dejavu-fonts.github.io/) (free to redistribute), pre-rendered into bitmap fonts that are compiled into the `.xbe`, so only the `.xbe` needs to be copied to the console.

The `src/font_*.h` headers are generated from the `.ttf` files in `fonts/` by `tools/makefonts.bat`, using the XDK's `makefont.exe`. Rerun it after changing a font, its size or the character set (`tools/fontchars.txt`, one hex character code per line):

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


## File system check

Start > Check file system on partition N checks the selected partition's FATX file system. The check itself only reads. It follows every file and folder from the root, then compares with the FAT:

- **Errors** are things the Xbox would read wrong: chains into free or out of range clusters, clusters used twice (cross-linked files or loops), files larger than their chain, damaged folder entries, the same name twice in a folder.
- **Warnings** are harmless but not clean: lost clusters (marked used, but by no file), chains longer than the file, names with characters the Xbox doesn't allow.

The check runs on the partition as it is on disk, not as edited. It also runs after shrinking a partition to keep the backup table.

When there are no errors but some warnings it can fix, the result offers **Repair**: the same safe repairs as `fatxtool repair` (below), which change nothing a file reads as. It lists the changes and asks first, unmounts the partition so the kernel's cached copy can't undo them, repairs, and checks again. Partitions with errors are never repaired, and neither is the partition XBpartitioner runs from.


## fatxtool (PC)

The same check on the PC, for disk and partition images, plus a way to make test images. Build it with `tools\fatxtool\build.bat` (Visual Studio 2022 or newer, no XDK needed); it ends up in `tools\fatxtool\build\Release\`.

```
fatxtool check disk.img                  every partition of a whole disk image
fatxtool check disk.img --partition 6    only F
fatxtool check part.img                  a single partition image
fatxtool make part.img 2G --from games   a formatted image with the games folder in it
```

`check` exits with 0 if there are no errors, 1 if there are, and 2 if it couldn't check. `make` uses the cluster size XBpartitioner would pick for that size, or `--cluster KB`.

```
fatxtool repair part.img                 shows what it would repair, writes nothing
fatxtool repair part.img --write         repairs, then checks again
```

`repair` only does the safe repairs, which change nothing a file reads as: it frees lost clusters, ends chains that are longer than their file, frees the clusters of empty files, and sets FAT entry 0 back. It only runs when the check finds no errors: with errors, clusters that look lost or extra may belong to a damaged or cross-linked file, and freeing them would lose its data. Errors are reported, never repaired. Copy the image before using `--write`.


## Tests

The partition table logic (`src/partition.cpp`) has [Google Test](https://github.com/google/googletest) tests in `tests/` that build as a normal Windows console app, no XDK needed. Visual Studio 2022 or newer with the C++ support is requried; Google Test comes from NuGet and is restored into `tests/packages` on the first build.

Open `tests/PartitionTests.sln` and use Test Explorer, or build and run them from a command prompt:

```
tests\run_tests.bat
```
