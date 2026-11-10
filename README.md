# XBpartitioner

XBpartitioner updated with new features and lba48v4 support. Requires a lba48 v4 compatible bios, Cerbios recommended.

## Features
- LBA48 v4 support (+2TB HDDs)
- Reworked interface for ease of use, no need to remember what every button does anymore
- 512e/4K sector support
- Partition table backup restore for HDDs formatted with FATXplorer
- 720p support
- File system check and repair
- Its own FATX formatter, the same one the PC tools use, for clusters from 16 KB to 1 MB
- Resizing formatted partitions without losing data
- Not really useful but theme support
- `fatxtool`, a PC companion for disk images and SSD TRIM


## Settings

Settings are kept in `xbpartitioner.ini`, next to the `.xbe`. It's written when you pick a theme or switch Resizing keeps data, and can be edited by hand, one `key=value` per line:

```
theme=Nord
animations=0
force_lba48_v3=0
resize_keeps_data=1
```

| Key | Values |
|---|---|
| `theme` | A theme name, as listed in Start > Theme |
| `animations` | `1` (default): windows grow from a line when they open and shrink back when they close. `0`: they appear and disappear at once. `on`/`off`, `yes`/`no` and `true`/`false` work too. |
| `resize_keeps_data` | `0` (default): a resized partition is always formatted. `1`: resizing keeps the data when it can, see [Resizing without a format](#resizing-without-a-format). Also switched in Start > Resizing keeps data. |
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

Start > Check file system checks the selected partition. It walks every folder and file from the root and compares what it finds with the FAT. Nothing is written.

- **Errors** would make the Xbox read data wrong: chains pointing at free or invalid clusters, clusters used twice, files longer than their chain, broken folder entries, duplicate names.
- **Warnings** are harmless: lost clusters, chains longer than their file, names with characters the Xbox doesn't allow.

If there are warnings but no errors, the result offers **Repair**. It frees lost clusters, trims over-long chains and fixes FAT entry 0. No file contents change. The partition is unmounted first and checked again afterwards. Partitions with errors are never repaired, and neither is the one XBpartitioner runs from.


## Resizing without a format

Off by default: switch it on in Start > Resizing keeps data. Then you can move the end of a formatted partition and keep its data, as long as the new size suits the FAT. The editor's Status line tells you whether it does, and suggests the nearest sizes that work.

- **Small changes** that keep the FAT the same size only touch the partition table. That's up to roughly one FAT page of clusters: about 32 MB with 16 KB clusters.
- **Bigger changes** grow or shrink the FAT by whole clusters. Any data in the way is moved, then the FAT and folders are renumbered. This is journaled: if the power goes, the next start offers to finish it, and the partition can't be used until then.

Both need a clean check and enough free space. Shrinking partitions are done first, then the rest of the table, then growing ones, so space freed by one partition can be used by another. The partition XBpartitioner runs from can't be resized this way.

Not supported: crossing about 65,000 clusters (where FAT entries go from 2 to 4 bytes), growing past XBpartitioner's size limit for the cluster size, and changing the cluster size. That last one is PC only, see `recluster` below.


## fatxtool (PC)

A command line companion for disk and partition images, and for an Xbox drive connected to a PC. Build it with `tools\fatxtool\build.bat` (Visual Studio 2022 or newer, no XDK). Commands that write only do so with `--write`. Without it they show what they would do. Work on a copy of your image.

`check`, `repair`, `resize` and `trim` also take an Xbox drive, as `\\.\PhysicalDriveN` (Disk Management shows the number; run as administrator). With `--write` they change the drive itself, the partition table and its backup included, so back it up first and close FATXplorer or anything else that has it open. A partition can grow as far as XBpartitioner allows: up to the next partition, or to the end of the disk, leaving its backup table alone. `recluster` needs an image, since it builds a full copy first.

### check, make

```
fatxtool check disk.img                  every partition in a disk image
fatxtool check disk.img --partition 6    only F
fatxtool check part.img                  a single partition image
fatxtool make part.img 2G --from games   a new image with a folder copied in
```

`check` exits with 0 when there are no errors, 1 when there are, 2 when it couldn't check. `make` picks the cluster size XBpartitioner would use, unless you pass `--cluster KB`.

### repair

```
fatxtool repair part.img --write
```

Same repairs as on the Xbox, and only when the check finds no errors. With errors, clusters that look lost might belong to a damaged file.

### resize

```
fatxtool resize disk.img 200M --partition 6 --write
fatxtool resize disk.img --resume --partition 6
```

Same as on the Xbox. A partition image changes length; in a disk image the table entry (and its backup copy) changes. Sizes are rounded down to one the FAT allows, and the tool suggests sizes when nothing fits. While the FAT is being renumbered the header reads `XRSZ` instead of `FATX`, so nothing mounts a half-finished partition. `--resume` finishes after a power cut.

### recluster

```
fatxtool recluster disk.img 64 --partition 6 --write
fatxtool recluster disk.img --resume --partition 6
```

Changes the cluster size (16 KB to 1 MB) and keeps every file, folder, attribute and timestamp. Every byte has to move and there's no room for a second copy on the partition itself, so the new file system is built in `<image>.recluster.tmp` first. That's a sparse file needing about as much free space as the partition's data. It's checked, then copied back with the header last. If the copy is interrupted, `--resume` simply copies again.

Bigger clusters waste more space per file, so lots of small files may no longer fit; the tool tells you before writing anything. Clusters over 64 KB need an LBA48 v4 BIOS.

### defrag

```
fatxtool defrag disk.img --partition 6 --write
```

Rebuilds the partition the same way at the cluster size it already has, so no file is fragmented afterwards. It needs the same free space as `recluster`, finishes with `recluster --resume` if interrupted, and only works on images. `check` shows how fragmented a partition is: how many files are fragmented, and the worst five. When nothing is fragmented, `defrag` says so and writes nothing. On an SSD fragmentation barely matters; on a spinning drive it means seeking.

### trim

```
fatxtool trim \\.\PhysicalDrive2 --write
fatxtool trim \\.\PhysicalDrive2 --list
```

The Xbox can't send TRIM (its IDE pass-through has no 48 bit DMA commands), but System information shows whether the drive supports it. With the SSD connected to a PC, `trim` sends TRIM for the free clusters of each FATX partition that checks clean, and nothing else.

Run it as administrator. Disk Management shows the drive number, but don't let Windows initialise the disk. A direct SATA connection works; most USB adapters block the command. `--list` prints the byte ranges instead, e.g. for `blkdiscard -o OFFSET -l LENGTH /dev/sdX` on Linux.


## fatxgui (PC)

A window for the same work as fatxtool, built with `tools\fatxgui\build.bat` (Visual Studio 2022 or newer, no XDK): `tools\fatxgui\build\Release\fatxgui.exe`, one file with nothing to install.

Open a disk image, a partition image or an Xbox drive (or drop an image on the window). It shows the partitions, their file systems and how much is in use. For the selected partition:

- **Check** and **Repair**. Repair shows what it would do and asks first.
- **Resize, keeping the data**: type a size and the status says whether the data can be kept, or suggests the nearest sizes that keep it. Disk image partitions can grow up to the next partition.
- **Cluster size** and **Defragment**: checks that the files fit, then asks. It needs free space next to the image, like `recluster`. Check shows how fragmented a partition is.
- **Finish**: an interrupted resize or cluster size change shows up when the image is opened, with a button to finish it.
- **New image**: like `make`, empty or filled from a folder.

Like fatxtool, it also works on Xbox drives connected to the PC: check, repair, resize and finish a resize are written straight to the drive (the partition table and its backup included), and **TRIM** frees an SSD's unused space. Back the drive up first, and close FATXplorer or anything else that has it open. Changing the cluster size needs an image, since it builds a full copy first. Drives need administrator rights, and the window offers to restart with them. Only drives with an Xbox partition table are listed, so never the Windows one. With images, work on a copy.

It has the Xbox app's themes and moving background. Pick a theme at the top right; it's remembered in `fatxgui.ini` next to the `.exe`.

## Tests

The partition table and FATX code has [Google Test](https://github.com/google/googletest) tests in `tests/`, built as a normal Windows console app (no XDK). They need Visual Studio 2022 or newer with C++; Google Test is restored from NuGet on the first build. The resize tests cut the power at every single write and check that the result can be finished.

Open `tests/PartitionTests.sln` and use Test Explorer, or build and run them from a command prompt:

```
tests\run_tests.bat
```
