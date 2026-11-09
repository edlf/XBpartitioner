// fatxtool: XBpartitioner's FATX tools for the PC. Run it without arguments
// for usage.

// Lean, because the full windows.h pulls in <winioctl.h>, which would pick
// up the Xbox version in src/
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include "fatx_check.hpp"
#include "fatx_writer.hpp"
#include "fatx_resize.hpp"
#include "fatx_recluster.hpp"
#include "fatx_trim.hpp"
#include "pc_image.hpp"

#include <filesystem>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <io.h>

namespace fs = std::filesystem;

namespace
{

const int EXIT_CLEAN = 0;    // No errors (warnings are fine)
const int EXIT_ERRORS = 1;
const int EXIT_FAILED = 2;   // Bad usage, or the image couldn't be used

void usage()
{
    fprintf(stderr,
            "Usage:\n"
            "  fatxtool check <image> [--partition N] [--offset BYTES] [--size BYTES]\n"
            "      Checks FATX file systems without writing. <image> is a partition\n"
            "      image, a whole disk image, or an Xbox drive (\\\\.\\PhysicalDriveN, as\n"
            "      administrator); for a disk, every partition is checked unless you\n"
            "      pick one (1 = E, 2 = C, 3 = X, 4 = Y, 5 = Z, 6 = F...).\n"
            "      Exits with 0 (no errors), 1 (errors) or 2 (couldn't check).\n"
            "\n"
            "  fatxtool repair <image> [--write] [--partition N] [--offset BYTES] [--size BYTES]\n"
            "      Frees lost clusters, cuts over-long chains and fixes FAT entry 0.\n"
            "      File contents never change. Refuses if the check finds errors.\n"
            "\n"
            "  fatxtool resize <image> <size> [--partition N] [--write]\n"
            "  fatxtool resize <image> --resume [--partition N]\n"
            "      Moves a partition's end, keeping its data. Small changes only touch\n"
            "      the table; bigger ones move clusters and renumber the FAT. The size\n"
            "      is rounded down to one the FAT allows. --resume finishes after a\n"
            "      power cut.\n"
            "\n"
            "  fatxtool recluster <image> <cluster KB> [--partition N] [--write]\n"
            "  fatxtool recluster <image> --resume [--partition N]\n"
            "      Changes the cluster size (16 to 1024 KB), keeping every file. Builds\n"
            "      the new file system in <image>.recluster.tmp, then copies it back.\n"
            "      --resume repeats the copy after a power cut. Images only.\n"
            "\n"
            "  fatxtool trim <disk> [--partition N] [--list] [--write]\n"
            "      TRIMs the free clusters of every FATX partition that checks clean.\n"
            "      <disk> is \\\\.\\PhysicalDriveN (run as administrator), or a disk\n"
            "      image with --list. --list prints the byte ranges, e.g. for\n"
            "      blkdiscard on Linux. Needs a direct SATA connection to --write.\n"
            "\n"
            "  fatxtool make <image> <size> [--cluster KB] [--from DIR]\n"
            "      Creates a formatted partition image, optionally filled from DIR.\n"
            "      Sizes take K, M, G or T.\n"
            "\n"
            "Commands that change anything only do so with --write. Work on a copy of\n"
            "an image; on a drive, repair and resize write to the drive itself:\n"
            "back it up first.\n");
}

// Parses "512M", "8G" or "4096" into bytes; 0 if invalid
uint64_t parse_size(const char *text)
{
    char *end = NULL;
    const uint64_t value = strtoull(text, &end, 0);
    if (end == text)
    {
        return 0;
    }
    uint64_t scale = 1;
    switch (*end)
    {
    case '\0':
        break;
    case 'k':
    case 'K':
        scale = 1ULL << 10;
        end++;
        break;
    case 'm':
    case 'M':
        scale = 1ULL << 20;
        end++;
        break;
    case 'g':
    case 'G':
        scale = 1ULL << 30;
        end++;
        break;
    case 't':
    case 'T':
        scale = 1ULL << 40;
        end++;
        break;
    default:
        return 0;
    }
    if (*end == 'B' || *end == 'b')
    {
        end++;
    }
    return (*end == '\0') ? value * scale : 0;
}

class ConsoleProgress : public FatxProgress
{
  public:
    void Update(int percent) override
    {
        if (!_isatty(_fileno(stderr)))
        {
            return; // No progress when redirected
        }
        fprintf(stderr, "\r  %3d%%", percent);
        if (percent == 100)
        {
            fprintf(stderr, "\r      \r");
        }
    }
};

enum Mode
{
    MODE_CHECK,
    MODE_REPAIR_DRY_RUN,
    MODE_REPAIR_WRITE
};

// Prints a check's result and returns its exit code
int print_check(const FatxCheckResult *result)
{
    int code = EXIT_CLEAN;
    switch (result->status)
    {
    case FATX_CHECK_RESIZING:
        printf("  A resize or cluster size change was interrupted: finish it with\n");
        printf("  fatxtool resize (or recluster) <image> --resume.\n");
        return EXIT_ERRORS;
    case FATX_CHECK_NOT_FATX:
        printf("  Not FATX (unformatted, or another file system): not checked.\n");
        return EXIT_CLEAN;
    case FATX_CHECK_READ_ERROR:
        printf("  Read error (is the image shorter than the partition?).\n");
        code = EXIT_FAILED;
        break;
    case FATX_CHECK_NO_MEMORY:
        printf("  Out of memory.\n");
        code = EXIT_FAILED;
        break;
    case FATX_CHECK_BAD_HEADER:
        code = EXIT_ERRORS;
        break;
    default:
        printf("  FATX, %u KB clusters, %u bit FAT, %I64u files, %I64u folders\n",
               result->cluster_bytes / 1024,
               result->layout.bytes_per_entry * 8,
               result->files,
               result->directories);
        printf("  Clusters: %I64u in use of %I64u, %I64u lost, %I64u bad\n",
               result->used_clusters,
               result->layout.cluster_limit - 1,
               result->lost_clusters,
               result->bad_clusters);
        break;
    }

    for (uint32_t i = 0; i < result->message_count; i++)
    {
        printf("  %s %s\n", result->messages[i].error ? "ERROR:  " : "warning:", result->messages[i].text);
    }
    const uint32_t total = result->errors + result->warnings;
    if (total > result->message_count)
    {
        printf("  (%u more not shown)\n", total - result->message_count);
    }

    if (result->status == FATX_CHECK_DONE)
    {
        if (result->errors == 0)
        {
            printf("  OK: no errors, %u warnings.\n", result->warnings);
        }
        else
        {
            printf("  %u errors, %u warnings.\n", result->errors, result->warnings);
            code = EXIT_ERRORS;
        }
    }
    return code;
}

// Prints what a repair did or would do
void print_repairs(const FatxRepairResult *repair, bool done)
{
    printf(done ? "  Repaired:" : "  Would repair:");
    const char *separator = " ";
    if (repair->clusters_freed > 0)
    {
        printf("%sfree %I64u clusters (%I64u KB)", separator, repair->clusters_freed, repair->clusters_freed * repair->check.cluster_bytes / 1024);
        separator = ", ";
    }
    if (repair->files_trimmed > 0)
    {
        printf("%send the chains of %u files", separator, repair->files_trimmed);
        separator = ", ";
    }
    if (repair->empty_files_cleared > 0)
    {
        printf("%sclear %u empty files", separator, repair->empty_files_cleared);
        separator = ", ";
    }
    if (repair->media_fixed)
    {
        printf("%sset FAT entry 0 back", separator);
    }
    printf(".\n");
}

// Checks or repairs one partition; returns its exit code
int check_partition(FatxDevice &disk, uint64_t offset, uint64_t size, const char *label, Mode mode, bool drive)
{
    printf("%s: %I64u MB at byte %I64u\n", label, size >> 20, offset);
    fflush(stdout); // Progress goes to stderr

    FatxOffsetDevice device(disk, offset);
    ConsoleProgress progress;

    if (mode == MODE_CHECK)
    {
        FatxCheckResult *result = new FatxCheckResult;
        FatxCheck(device, size, result, &progress);
        const int code = print_check(result);
        delete result;
        return code;
    }

    FatxRepairResult *repair = new FatxRepairResult;
    FatxRepair(device, size, repair, mode == MODE_REPAIR_WRITE, &progress);
    int code = print_check(&repair->check);

    switch (repair->status)
    {
    case FATX_REPAIR_HAS_ERRORS:
        printf("  Not repaired: the safe repairs only run without errors.\n");
        break;
    case FATX_REPAIR_NOTHING_TO_DO:
        printf("  Nothing to repair.\n");
        break;
    case FATX_REPAIR_STOPPED_PARTWAY:
        printf("  The repair stopped partway (read or write error). Check it again.\n");
        code = EXIT_FAILED;
        break;
    case FATX_REPAIR_DONE:
        if (mode == MODE_REPAIR_DRY_RUN)
        {
            print_repairs(repair, false);
            printf(drive ? "  Nothing written: add --write to do it (back the drive up first).\n"
                         : "  Nothing written: add --write to do it (copy the image first).\n");
            break;
        }
        print_repairs(repair, true);
        {
            printf("  Checking again:\n");
            fflush(stdout);
            FatxCheckResult *after = new FatxCheckResult;
            FatxCheck(device, size, after, &progress);
            code = print_check(after);
            delete after;
        }
        break;
    default:
        break; // print_check() explained
    }

    delete repair;
    return code;
}

// check and repair, on a disk image or a single partition
int command_partitions(int argc, char **argv, Mode mode)
{
    if (argc < 1)
    {
        usage();
        return EXIT_FAILED;
    }
    const char *path = argv[0];
    int only = 0;
    uint64_t offset = 0, size = 0;
    bool manual = false;
    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "--partition") == 0 && i + 1 < argc)
        {
            only = atoi(argv[++i]);
        }
        else if (strcmp(argv[i], "--offset") == 0 && i + 1 < argc)
        {
            offset = parse_size(argv[++i]);
            manual = true;
        }
        else if (strcmp(argv[i], "--size") == 0 && i + 1 < argc)
        {
            size = parse_size(argv[++i]);
            manual = true;
        }
        else if (strcmp(argv[i], "--write") == 0 && mode != MODE_CHECK)
        {
            mode = MODE_REPAIR_WRITE;
        }
        else
        {
            usage();
            return EXIT_FAILED;
        }
    }

    DiskTarget target;
    std::string error;
    if (!target.Open(path, mode == MODE_REPAIR_WRITE, false, &error))
    {
        fprintf(stderr, "%s\n", error.c_str());
        return EXIT_FAILED;
    }
    const uint64_t image_size = target.Size();
    const bool drive = target.IsDrive();

    XboxPartitionTable table;
    const bool has_table = target.ReadTable(&table);
    if (drive && !has_table && !manual)
    {
        fprintf(stderr, "No Xbox partition table on %s.\n", path);
        return EXIT_FAILED;
    }
    if (drive && mode == MODE_REPAIR_WRITE)
    {
        printf("Writing to the drive itself.\n");
    }

    int code = EXIT_CLEAN;
    if (!manual && has_table)
    {
        // A disk image: go through its table
        const bool high_bits = table.Version != XBOX_TABLE_VERSION_LBA48_V3;
        int checked = 0;
        for (int i = 0; i < XboxPartitionTableEntryCount; i++)
        {
            const XboxPartitionTableEntry &entry = table.TableEntries[i];
            if (entry.Flags != PE_PARTFLAGS_IN_USE || (only != 0 && only != i + 1))
            {
                continue;
            }
            const uint64_t start = entry.LBAStart | (high_bits ? (uint64_t)entry.LBAStart_high << 32 : 0);
            const uint64_t sectors = entry.LBASize | (high_bits ? (uint64_t)entry.LBASize_high << 32 : 0);
            char label[40];
            sprintf_s(label, "Partition %d (%c)", i + 1, partition_letter(i));
            if ((start + sectors) * HDD_SECTOR_SIZE > image_size)
            {
                printf("%s: ends past the end of the image, not checked.\n", label);
                continue;
            }
            const int result = check_partition(target.Device(), start * HDD_SECTOR_SIZE, sectors * HDD_SECTOR_SIZE, label, mode, drive);
            code = (result > code) ? result : code;
            checked++;
        }
        if (checked == 0)
        {
            printf("No partition to check.\n");
            code = EXIT_FAILED;
        }
    }
    else if (only != 0)
    {
        printf("--partition needs a whole disk image (with a partition table).\n");
        code = EXIT_FAILED;
    }
    else
    {
        if (size == 0)
        {
            size = (image_size > offset) ? image_size - offset : 0;
        }
        size -= size % HDD_SECTOR_SIZE;
        code = check_partition(target.Device(), offset, size, "Partition", mode, drive);
    }
    return code;
}

int command_make(int argc, char **argv)
{
    if (argc < 2)
    {
        usage();
        return EXIT_FAILED;
    }
    const char *path = argv[0];
    uint64_t size = parse_size(argv[1]);
    size -= size % FATX_HEADER_BYTES;
    uint32_t cluster_bytes = 0;
    const char *from = NULL;
    for (int i = 2; i < argc; i++)
    {
        if (strcmp(argv[i], "--cluster") == 0 && i + 1 < argc)
        {
            cluster_bytes = (uint32_t)atoi(argv[++i]) * 1024;
        }
        else if (strcmp(argv[i], "--from") == 0 && i + 1 < argc)
        {
            from = argv[++i];
        }
        else
        {
            usage();
            return EXIT_FAILED;
        }
    }
    if (size < 1024 * 1024)
    {
        fprintf(stderr, "The size must be at least 1M.\n");
        return EXIT_FAILED;
    }
    if (cluster_bytes != 0 && (cluster_bytes < 16 * 1024 || cluster_bytes > 1024 * 1024 || (cluster_bytes & (cluster_bytes - 1)) != 0))
    {
        fprintf(stderr, "--cluster must be a power of two from 16 to 1024.\n");
        return EXIT_FAILED;
    }

    FILE *file = NULL;
    if (fopen_s(&file, path, "w+b") != 0 || file == NULL)
    {
        fprintf(stderr, "Can't create %s\n", path);
        return EXIT_FAILED;
    }
    // Sparse if the file system allows; unused clusters are never written
    if (_chsize_s(_fileno(file), (__int64)size) != 0)
    {
        fprintf(stderr, "Can't make %s that big\n", path);
        fclose(file);
        return EXIT_FAILED;
    }

    FileDevice device(file, 0);
    FatxWriter writer(device, size, cluster_bytes);
    bool ok = writer.Format();
    int skipped = 0;
    if (ok && from != NULL)
    {
        ok = copy_tree(writer, fs::u8path(from), writer.RootCluster(), &skipped, [](const std::string &line) { fprintf(stderr, "%s\n", line.c_str()); });
    }
    ok = writer.Flush() && ok;
    fclose(file);

    if (!ok)
    {
        fprintf(stderr, "Failed to make %s\n", path);
        return EXIT_FAILED;
    }
    printf("Made %s: %I64u MB, %u KB clusters, %u bit FAT", path, size >> 20, writer.ClusterBytes() / 1024, writer.Layout().bytes_per_entry * 8);
    if (skipped > 0)
    {
        printf(", %d items skipped", skipped);
    }
    printf(".\n");
    return EXIT_CLEAN;
}

// "1823.50 MB"
void print_mb(const char *label, uint64_t lba)
{
    printf("%s%I64u.%02I64u MB", label, (lba * HDD_SECTOR_SIZE) >> 20, ((lba * HDD_SECTOR_SIZE) % (1 << 20)) * 100 >> 20);
}

// Checks a partition after it was changed
int check_resized(FatxDevice &device, uint64_t lba_size, ConsoleProgress &progress)
{
    printf("  Checking the result:\n");
    fflush(stdout);
    FatxCheckResult *after = new FatxCheckResult;
    FatxCheck(device, lba_size * HDD_SECTOR_SIZE, after, &progress);
    const int code = print_check(after);
    delete after;
    return code;
}

// Prints the nearest sizes that keep the data, from how it's used
void print_keeping_sizes(FatxDevice &device, uint64_t old_lba, uint64_t new_lba)
{
    FatxUsage usage;
    if (!FatxReadUsage(device, old_lba, &usage))
    {
        return;
    }
    const uint64_t below = FatxKeepingSizeBelow(old_lba, usage, new_lba);
    const uint64_t above = FatxKeepingSizeAbove(old_lba, usage, new_lba);
    printf("  %I64u MB in use. Nearest sizes that keep the data:", (usage.used_clusters * usage.cluster_bytes) >> 20);
    if (below != 0)
    {
        print_mb(" ", below);
    }
    if (above != 0)
    {
        print_mb(below != 0 ? " or " : " ", above);
    }
    printf(below == 0 && above == 0 ? " none.\n" : ".\n");
}

// Resizes by moving clusters; returns the exit code
int resize_with_moves(FatxDevice &device, FatxResizeTable &table, uint64_t old_lba, uint64_t new_lba, bool write, bool drive, ConsoleProgress &progress)
{
    FatxMoveResult *move = new FatxMoveResult;
    FatxResizeWithMoves(device, old_lba, new_lba, table, write, move, &progress);

    int code = EXIT_ERRORS;
    const bool planned = move->status == FATX_MOVE_PLANNED || move->status == FATX_MOVE_DONE;
    if (planned)
    {
        if (move->new_lba_size != new_lba)
        {
            print_mb("  Rounded down to ", move->new_lba_size);
            printf(", so the FAT changes by whole clusters.\n");
        }
        const int64_t shift = move->shift > 0 ? move->shift : -move->shift;
        printf("  The FAT %s by %I64d cluster%s: every cluster gets a new number.\n", move->shift > 0 ? "grows" : "shrinks", shift, shift == 1 ? "" : "s");
        printf("  %I64u cluster%s to move first (%I64u free where they can go).\n",
               move->clusters_to_move,
               move->clusters_to_move == 1 ? "" : "s",
               move->free_clusters);
    }
    if (move->repair.written)
    {
        print_repairs(&move->repair, true);
    }

    switch (move->status)
    {
    case FATX_MOVE_PLANNED:
        printf(drive ? "  Nothing written: add --write to do it (back the drive up first).\n"
                     : "  Nothing written: add --write to do it (copy the image first).\n");
        code = EXIT_CLEAN;
        break;
    case FATX_MOVE_DONE:
        printf("  Resized: %I64u cluster%s moved, the FAT and every folder renumbered.\n", move->clusters_moved, move->clusters_moved == 1 ? "" : "s");
        code = check_resized(device, move->new_lba_size, progress);
        break;
    case FATX_MOVE_SAME_FAT:
        printf("  The FAT can't change by whole clusters at that size.\n");
        break;
    case FATX_MOVE_HAS_ERRORS:
        print_check(&move->repair.check);
        printf("  A resize needs a check without errors.\n");
        break;
    case FATX_MOVE_ENTRY_SIZE:
        printf("  The FAT's entries would change between 2 and 4 bytes (at about 65,000 clusters): not supported.\n");
        break;
    case FATX_MOVE_CLUSTER_SIZE:
        printf("  That size needs bigger clusters than %u KB: format it instead.\n", move->cluster_bytes / 1024);
        break;
    case FATX_MOVE_TOO_SMALL:
        printf("  No size that small works with this FAT and cluster size.\n");
        break;
    case FATX_MOVE_NO_ROOM:
        printf("  Not enough room: %I64u clusters to move, %I64u free where they can go (one more is needed).\n", move->clusters_to_move, move->free_clusters);
        break;
    case FATX_MOVE_STOPPED_SAFE:
        printf("  Stopped while moving clusters (read or write error). It is still the old size and valid;\n");
        printf("  a check may show lost clusters, which repair frees.\n");
        code = EXIT_FAILED;
        break;
    case FATX_MOVE_INTERRUPTED:
        printf("  Stopped while renumbering (read or write error): finish it with --resume.\n");
        code = EXIT_FAILED;
        break;
    default:
        printf("  Could not be checked (read error, out of memory or damaged header).\n");
        code = EXIT_FAILED;
        break;
    }

    switch (move->status)
    {
    case FATX_MOVE_SAME_FAT:
    case FATX_MOVE_CLUSTER_SIZE:
    case FATX_MOVE_TOO_SMALL:
    case FATX_MOVE_NO_ROOM:
    case FATX_MOVE_ENTRY_SIZE:
        print_keeping_sizes(device, old_lba, new_lba);
        break;
    default:
        break;
    }
    delete move;
    return code;
}

int command_resize(int argc, char **argv)
{
    if (argc < 2)
    {
        usage();
        return EXIT_FAILED;
    }
    const char *path = argv[0];
    const bool resume = strcmp(argv[1], "--resume") == 0;
    const uint64_t new_bytes = resume ? 0 : parse_size(argv[1]);
    int only = 0;
    bool write = false;
    for (int i = 2; i < argc; i++)
    {
        if (strcmp(argv[i], "--partition") == 0 && i + 1 < argc)
        {
            only = atoi(argv[++i]);
        }
        else if (strcmp(argv[i], "--write") == 0 && !resume)
        {
            write = true;
        }
        else
        {
            usage();
            return EXIT_FAILED;
        }
    }
    if (!resume && (new_bytes == 0 || new_bytes % FATX_HEADER_BYTES != 0))
    {
        fprintf(stderr, "The size must be a whole number of 4 KB.\n");
        return EXIT_FAILED;
    }
    const uint64_t new_lba = new_bytes / HDD_SECTOR_SIZE;

    DiskTarget target;
    std::string error;
    if (!target.Open(path, write || resume, false, &error))
    {
        fprintf(stderr, "%s\n", error.c_str());
        return EXIT_FAILED;
    }
    const uint64_t image_size = target.Size();
    const bool drive = target.IsDrive();

    XboxPartitionTable table;
    const bool disk_image = target.ReadTable(&table);
    if (drive && !disk_image)
    {
        fprintf(stderr, "No Xbox partition table on %s.\n", path);
        return EXIT_FAILED;
    }

    uint64_t start = 0;
    uint64_t old_lba = image_size / HDD_SECTOR_SIZE;
    if (disk_image)
    {
        if (only < 1 || only > (int)XboxPartitionTableEntryCount || table.TableEntries[only - 1].Flags != PE_PARTFLAGS_IN_USE)
        {
            fprintf(stderr, "A disk needs --partition N, for a partition in its table.\n");
            return EXIT_FAILED;
        }
        // The high bits only count from v4 on. 4K: keeps clear of the last 4K
        // block, where XBpartitioner puts the backup on 512e drives (and the
        // last sector, where it goes otherwise, is inside or past it).
        const XboxPartitionTableManipulator editor(table, image_size / HDD_SECTOR_SIZE, table.Version != XBOX_TABLE_VERSION_LBA48_V3, true);
        start = editor.GetPartitionStart(only - 1);
        old_lba = editor.GetPartitionSize(only - 1);

        // Growing stops where XBpartitioner stops it: at the next partition,
        // or before the backup table at the end of the disk
        if (!resume && new_lba > old_lba && new_lba - old_lba > editor.GetGrowthLimit(only - 1))
        {
            const uint64_t most = (old_lba + editor.GetGrowthLimit(only - 1)) * HDD_SECTOR_SIZE;
            fprintf(stderr,
                    "Partition %d can't grow that far: at most to %I64u.%02I64u MB (the next partition, or the end of the disk and its backup table).\n",
                    only,
                    most >> 20,
                    ((most % (1 << 20)) * 100) >> 20);
            return EXIT_FAILED;
        }
        printf("Partition %d (%c): ", only, partition_letter(only - 1));
    }
    else if (only != 0)
    {
        fprintf(stderr, "--partition needs a whole disk image (with a partition table).\n");
        return EXIT_FAILED;
    }
    else
    {
        printf("Partition: ");
    }
    if (drive && (write || resume))
    {
        printf("(writing to the drive itself) ");
    }

    FatxOffsetDevice device(target.Device(), start * HDD_SECTOR_SIZE);
    std::unique_ptr<FatxResizeTable> table_owner = target.Table(disk_image ? only - 1 : -1);
    FatxResizeTable &table_writer = *table_owner;
    ConsoleProgress progress;
    int code = EXIT_CLEAN;

    uint64_t interrupted_old, interrupted_new;
    const bool interrupted = FatxFindInterruptedResize(device, &interrupted_old, &interrupted_new);
    std::error_code fs_error;
    if (!interrupted && !drive && fs::exists(std::string(path) + ".recluster", fs_error))
    {
        printf("unfinished cluster size change: finish it with fatxtool recluster %s --resume.\n", path);
        return EXIT_FAILED;
    }
    if (resume || interrupted)
    {
        if (!interrupted)
        {
            printf("no interrupted resize to finish.\n");
            return EXIT_FAILED;
        }
        print_mb("interrupted resize from ", interrupted_old);
        print_mb(" to ", interrupted_new);
        printf("\n");
        if (!resume)
        {
            printf("  Finish it first: fatxtool resize %s --resume%s.\n", path, disk_image ? " --partition N" : "");
            return EXIT_FAILED;
        }
        fflush(stdout);
        FatxMoveResult *move = new FatxMoveResult;
        if (FatxResumeResize(device, table_writer, move, &progress))
        {
            printf("  Finished.\n");
            code = check_resized(device, move->new_lba_size, progress);
        }
        else
        {
            printf("  Could not be finished (read or write error). Try again.\n");
            code = EXIT_FAILED;
        }
        delete move;
        return code;
    }

    print_mb("", old_lba);
    print_mb(" -> ", new_lba);
    printf("\n");
    fflush(stdout);

    FatxResizeResult *resize = new FatxResizeResult;
    FatxCheckResize(device, old_lba, new_lba, resize, &progress);

    if (resize->status != FATX_RESIZE_NO_FILE_SYSTEM && resize->status != FATX_RESIZE_NOT_CHECKED)
    {
        print_mb("  Sizes that keep the FAT's size: ", resize->min_lba);
        print_mb(" to ", resize->max_lba);
        printf(" (%u KB clusters)\n", resize->cluster_bytes / 1024);
    }

    switch (resize->status)
    {
    case FATX_RESIZE_OK:
        printf("  The data is kept: only the table changes.\n");
        break;
    case FATX_RESIZE_NO_FILE_SYSTEM:
        printf("  Not FATX: there is no data to keep.\n");
        break;
    case FATX_RESIZE_OUT_OF_RANGE:
        code = resize_with_moves(device, table_writer, old_lba, new_lba, write, drive, progress);
        break;
    case FATX_RESIZE_CLUSTERS_IN_USE:
        printf("  %I64u clusters past the new end are in use%s.\n",
               resize->clusters_in_use,
               resize->check.lost_clusters > 0 ? " (lost ones count too: try repair first)" : "");
        print_keeping_sizes(device, old_lba, new_lba);
        code = EXIT_ERRORS;
        break;
    case FATX_RESIZE_FAT_NOT_CLEAN:
        printf("  The FAT entries for the new clusters aren't 0: not grown.\n");
        code = EXIT_ERRORS;
        break;
    case FATX_RESIZE_HAS_ERRORS:
        print_check(&resize->check);
        printf("  Shrinking needs a check without errors.\n");
        code = EXIT_ERRORS;
        break;
    default:
        printf("  Could not be checked (read error or damaged header).\n");
        code = EXIT_FAILED;
        break;
    }
    const bool in_place = (resize->status == FATX_RESIZE_OK || resize->status == FATX_RESIZE_NO_FILE_SYSTEM);
    delete resize;

    if (in_place && !write)
    {
        printf(drive ? "  Nothing written: add --write to do it (back the drive up first).\n"
                     : "  Nothing written: add --write to do it (copy the image first).\n");
    }
    else if (in_place && write)
    {
        if (!table_writer.SetSize(new_lba))
        {
            printf("  Writing failed.\n");
            code = EXIT_FAILED;
        }
        else
        {
            printf("  Resized.\n");
            code = check_resized(device, new_lba, progress);
        }
    }
    return code;
}

// ---- recluster

// Copies the built file system back; also used by --resume
int copy_reclustered(FILE *image, uint64_t partition_offset, uint64_t lba_size, const std::string &plan_path, const std::string &build_path,
                     const std::vector<FatxRange> &ranges, ConsoleProgress &progress)
{
    FILE *build_file = NULL;
    if (fopen_s(&build_file, build_path.c_str(), "rb") != 0 || build_file == NULL)
    {
        printf("  Can't open %s: the copy can't be finished.\n", build_path.c_str());
        return EXIT_FAILED;
    }
    FileDevice build(build_file, 0);
    FileDevice partition(image, partition_offset);
    printf("  Copying it onto the partition...\n");
    fflush(stdout);
    const bool copied = FatxCopyReclustered(build, partition, ranges, &progress);
    fclose(build_file);
    if (!copied)
    {
        printf("  Stopped partway (read or write error): finish it with --resume.\n");
        return EXIT_FAILED;
    }
    remove_recluster_files(plan_path, build_path);
    return check_resized(partition, lba_size, progress);
}

int command_recluster(int argc, char **argv)
{
    if (argc < 2)
    {
        usage();
        return EXIT_FAILED;
    }
    const std::string path = argv[0];
    const bool resume = strcmp(argv[1], "--resume") == 0;
    const uint32_t new_cluster_bytes = resume ? 0 : (uint32_t)atoi(argv[1]) * 1024;
    int only = 0;
    bool write = false;
    for (int i = 2; i < argc; i++)
    {
        if (strcmp(argv[i], "--partition") == 0 && i + 1 < argc)
        {
            only = atoi(argv[++i]);
        }
        else if (strcmp(argv[i], "--write") == 0 && !resume)
        {
            write = true;
        }
        else
        {
            usage();
            return EXIT_FAILED;
        }
    }
    if (DiskTarget::IsDrivePath(path))
    {
        fprintf(stderr, "recluster needs an image: it builds the new file system as a full copy first.\n");
        return EXIT_FAILED;
    }
    const std::string plan_path = path + ".recluster";
    const std::string build_path = path + ".recluster.tmp";

    FILE *file = NULL;
    if (fopen_s(&file, path.c_str(), (write || resume) ? "r+b" : "rb") != 0 || file == NULL)
    {
        fprintf(stderr, "Can't open %s\n", path.c_str());
        return EXIT_FAILED;
    }
    const uint64_t image_size = file_size(file);

    XboxPartitionTable table;
    memset(&table, 0, sizeof(table));
    _fseeki64(file, 0, SEEK_SET);
    fread(&table, 1, sizeof(table), file);
    uint64_t start = 0;
    uint64_t lba_size = image_size / HDD_SECTOR_SIZE;
    if (XboxPartitionTableManipulator::HasMagic(table))
    {
        if (only < 1 || only > (int)XboxPartitionTableEntryCount || table.TableEntries[only - 1].Flags != PE_PARTFLAGS_IN_USE)
        {
            fprintf(stderr, "A disk image needs --partition N, for a partition in its table.\n");
            fclose(file);
            return EXIT_FAILED;
        }
        const XboxPartitionTableManipulator editor(table, image_size / HDD_SECTOR_SIZE, table.Version != XBOX_TABLE_VERSION_LBA48_V3, false);
        start = editor.GetPartitionStart(only - 1);
        lba_size = editor.GetPartitionSize(only - 1);
        printf("Partition %d (%c): ", only, partition_letter(only - 1));
    }
    else if (only != 0)
    {
        fprintf(stderr, "--partition needs a whole disk image (with a partition table).\n");
        fclose(file);
        return EXIT_FAILED;
    }
    else
    {
        printf("Partition: ");
    }

    ConsoleProgress progress;
    ReclusterPlanHeader plan;
    std::vector<FatxRange> ranges;
    const bool pending = load_recluster_plan(plan_path, &plan, &ranges);
    if (resume || pending)
    {
        if (!pending)
        {
            printf("no unfinished cluster size change (%s).\n", plan_path.c_str());
            fclose(file);
            return EXIT_FAILED;
        }
        printf("unfinished change to %u KB clusters.\n", plan.new_cluster_bytes / 1024);
        if (!resume)
        {
            printf("  Finish it first: fatxtool recluster %s --resume%s.\n", path.c_str(), start != 0 ? " --partition N" : "");
            fclose(file);
            return EXIT_FAILED;
        }
        if (plan.partition_offset != start * HDD_SECTOR_SIZE || plan.lba_size != lba_size)
        {
            printf("  It was for another partition: use the --partition it was started with.\n");
            fclose(file);
            return EXIT_FAILED;
        }
        const int code = copy_reclustered(file, start * HDD_SECTOR_SIZE, lba_size, plan_path, build_path, ranges, progress);
        fclose(file);
        return code;
    }

    print_mb("", lba_size);
    printf(", to %u KB clusters\n", new_cluster_bytes / 1024);
    fflush(stdout);

    FileDevice device(file, start * HDD_SECTOR_SIZE);
    FatxReclusterResult *result = new FatxReclusterResult;
    FILE *build_file = NULL;
    FileDevice *build = NULL;
    FatxRangeRecorder *recorder = NULL;
    if (write)
    {
        if (fopen_s(&build_file, build_path.c_str(), "w+b") != 0 || build_file == NULL)
        {
            fprintf(stderr, "Can't create %s\n", build_path.c_str());
            fclose(file);
            delete result;
            return EXIT_FAILED;
        }
        make_sparse(build_file);
        build = new FileDevice(build_file, 0);
        recorder = new FatxRangeRecorder(*build);
    }
    FatxBuildReclustered(device, lba_size, new_cluster_bytes, recorder, result, &progress);

    int code = EXIT_ERRORS;
    if (result->status == FATX_RECLUSTER_PLANNED || result->status == FATX_RECLUSTER_DONE || result->status == FATX_RECLUSTER_NO_ROOM)
    {
        printf("  %I64u files, %I64u folders: %I64u clusters of %u KB now (%I64u in use), %I64u of %u KB needed, %I64u there.\n",
               result->files,
               result->folders,
               result->check.layout.cluster_limit - 1,
               result->old_cluster_bytes / 1024,
               result->old_clusters_used,
               result->clusters_needed,
               new_cluster_bytes / 1024,
               result->clusters_available);
    }
    switch (result->status)
    {
    case FATX_RECLUSTER_PLANNED:
        printf("  It fits. Nothing written: add --write to do it (copy the image first).\n");
        code = EXIT_CLEAN;
        break;
    case FATX_RECLUSTER_DONE:
        break;
    case FATX_RECLUSTER_SAME:
        printf("  It already has %u KB clusters.\n", new_cluster_bytes / 1024);
        break;
    case FATX_RECLUSTER_BAD_SIZE:
        printf("  The cluster size must be a power of two from 16 to 1024 KB.\n");
        break;
    case FATX_RECLUSTER_TOO_SMALL:
        printf("  XBpartitioner uses %u KB clusters at least for this size.\n", ClusterSizeKbFor(lba_size));
        break;
    case FATX_RECLUSTER_NOT_FATX:
        printf("  Not FATX.\n");
        break;
    case FATX_RECLUSTER_RESIZING:
        printf("  A resize was interrupted: finish it first with fatxtool resize %s --resume.\n", path.c_str());
        break;
    case FATX_RECLUSTER_HAS_ERRORS:
        print_check(&result->check);
        printf("  Changing the cluster size needs a check without errors.\n");
        break;
    case FATX_RECLUSTER_NO_ROOM:
        printf("  Not enough room: each file takes whole clusters, and bigger ones waste more.\n");
        break;
    case FATX_RECLUSTER_WRITE_ERROR:
        printf("  Couldn't write %s (disk full?). The image wasn't changed.\n", build_path.c_str());
        code = EXIT_FAILED;
        break;
    case FATX_RECLUSTER_BUILD_FAILED:
        printf("  The new file system didn't check clean (a bug). The image wasn't changed.\n");
        code = EXIT_FAILED;
        break;
    default:
        printf("  Read error. The image wasn't changed.\n");
        code = EXIT_FAILED;
        break;
    }
    if (result->new_cluster_bytes > 64 * 1024 && result->status == FATX_RECLUSTER_PLANNED)
    {
        printf("  Note: BIOSes with the LBA48 v3 patch only mount clusters up to 64 KB.\n");
    }

    const bool built = result->status == FATX_RECLUSTER_DONE;
    delete result;
    if (write)
    {
        if (built)
        {
            ranges = recorder->Ranges();
        }
        delete recorder;
        delete build;
        fclose(build_file);
        if (!built)
        {
            remove_recluster_files(plan_path, build_path);
            fclose(file);
            return code;
        }

        memcpy(plan.magic, RECLUSTER_MAGIC, sizeof(RECLUSTER_MAGIC));
        plan.partition_offset = start * HDD_SECTOR_SIZE;
        plan.lba_size = lba_size;
        plan.new_cluster_bytes = new_cluster_bytes;
        plan.range_count = (uint32_t)ranges.size();
        if (!save_recluster_plan(plan_path, plan, ranges))
        {
            printf("  Couldn't write %s. The image wasn't changed.\n", plan_path.c_str());
            remove_recluster_files(plan_path, build_path);
            fclose(file);
            return EXIT_FAILED;
        }
        printf("  Built and checked.\n");
        code = copy_reclustered(file, start * HDD_SECTOR_SIZE, lba_size, plan_path, build_path, ranges, progress);
    }
    fclose(file);
    return code;
}

// ---- trim

int command_trim(int argc, char **argv)
{
    if (argc < 1)
    {
        usage();
        return EXIT_FAILED;
    }
    const char *path = argv[0];
    int only = 0;
    bool list = false;
    bool write = false;
    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "--partition") == 0 && i + 1 < argc)
        {
            only = atoi(argv[++i]);
        }
        else if (strcmp(argv[i], "--list") == 0)
        {
            list = true;
        }
        else if (strcmp(argv[i], "--write") == 0)
        {
            write = true;
        }
        else
        {
            usage();
            return EXIT_FAILED;
        }
    }

    // A physical disk, or an image for --list
    const bool physical = strncmp(path, "\\\\.\\", 4) == 0;
    WindowsDisk windows;
    FILE *file = NULL;
    FileDevice *image = NULL;
    FatxReader *disk = NULL;
    if (physical)
    {
        unsigned long error = 0;
        if (!windows.Open(path, true, &error))
        {
            if (error == ERROR_ACCESS_DENIED)
            {
                fprintf(stderr, "Can't open %s: run fatxtool as administrator.\n", path);
            }
            else if (error == ERROR_FILE_NOT_FOUND)
            {
                fprintf(stderr, "There is no %s (Disk Management shows the disk numbers).\n", path);
            }
            else
            {
                fprintf(stderr, "Can't open %s (error %lu).\n", path, error);
            }
            return EXIT_FAILED;
        }
        disk = &windows;
    }
    else
    {
        if (write)
        {
            fprintf(stderr, "An image has nothing to trim: --list prints the ranges.\n");
            return EXIT_FAILED;
        }
        if (fopen_s(&file, path, "rb") != 0 || file == NULL)
        {
            fprintf(stderr, "Can't open %s\n", path);
            return EXIT_FAILED;
        }
        image = new FileDevice(file, 0);
        disk = image;
    }
    const uint64_t disk_size = physical ? windows.Size() : file_size(file);

    // Partitions from the table, or the whole image as one
    XboxPartitionTable table;
    memset(&table, 0, sizeof(table));
    disk->Read(0, &table, sizeof(table));
    struct Part
    {
        int index;
        uint64_t start;
        uint64_t sectors;
    };
    std::vector<Part> parts;
    if (XboxPartitionTableManipulator::HasMagic(table))
    {
        const XboxPartitionTableManipulator editor(table, disk_size / HDD_SECTOR_SIZE, table.Version != XBOX_TABLE_VERSION_LBA48_V3, false);
        for (int i = 0; i < XboxPartitionTableEntryCount; i++)
        {
            if (editor.isPartitionEnabled(i) && (only == 0 || only == i + 1))
            {
                Part part = {i, editor.GetPartitionStart(i), editor.GetPartitionSize(i)};
                parts.push_back(part);
            }
        }
    }
    else if (!physical && only == 0)
    {
        Part part = {-1, 0, disk_size / HDD_SECTOR_SIZE};
        parts.push_back(part);
    }
    else
    {
        fprintf(stderr, "No Xbox partition table at the start of %s.\n", path);
        delete image;
        if (file != NULL)
        {
            fclose(file);
        }
        return EXIT_FAILED;
    }

    ConsoleProgress progress;
    std::vector<FatxRange> ranges; // Disk offsets
    uint64_t total = 0;
    for (const Part &part : parts)
    {
        if (part.index >= 0)
        {
            printf("Partition %d (%c): ", part.index + 1, partition_letter(part.index));
        }
        else
        {
            printf("Partition: ");
        }
        if ((part.start + part.sectors) * HDD_SECTOR_SIZE > disk_size)
        {
            printf("ends past the end of the disk, skipped.\n");
            continue;
        }
        fflush(stdout);

        FatxOffsetReader reader(*disk, part.start * HDD_SECTOR_SIZE);
        FatxCheckResult *check = new FatxCheckResult;
        std::vector<FatxRange> free_ranges;
        const bool ok = FatxFreeRanges(reader, part.sectors * HDD_SECTOR_SIZE, &free_ranges, check);
        if (!ok)
        {
            switch (check->status)
            {
            case FATX_CHECK_NOT_FATX:
                printf("not FATX, skipped.\n");
                break;
            case FATX_CHECK_RESIZING:
                printf("an interrupted resize, skipped (finish it first).\n");
                break;
            case FATX_CHECK_DONE:
                printf("%u errors, skipped (fatxtool check shows them).\n", check->errors);
                break;
            default:
                printf("could not be checked, skipped.\n");
                break;
            }
            delete check;
            continue;
        }
        delete check;

        uint64_t free_bytes = 0;
        for (FatxRange range : free_ranges)
        {
            free_bytes += range.length;
            range.offset += part.start * HDD_SECTOR_SIZE;
            ranges.push_back(range);
        }
        total += free_bytes;
        printf("%I64u MB free, in %u ranges.\n", free_bytes >> 20, (unsigned int)free_ranges.size());
    }
    printf("To trim: %I64u MB in %u ranges.\n", total >> 20, (unsigned int)ranges.size());

    if (list)
    {
        printf("# offset length (bytes, on the disk), e.g. for: blkdiscard -o OFFSET -l LENGTH /dev/sdX\n");
        for (const FatxRange &range : ranges)
        {
            printf("%I64u %I64u\n", range.offset, range.length);
        }
    }

    int code = EXIT_CLEAN;
    if (physical)
    {
        uint16_t identify[256];
        if (!windows.Identify(identify))
        {
            printf("ATA commands don't get through to this drive (USB adapters often block them):\n");
            printf("it can't be trimmed from here. --list gives the ranges for other tools.\n");
            code = write ? EXIT_FAILED : EXIT_CLEAN;
        }
        else
        {
            const bool trim = (identify[169] & 0x0001) != 0;
            printf("Drive: %s, %s.\n", identify_string(&identify[27], 20).c_str(), trim ? "supports TRIM" : "no TRIM support");
            if (!trim)
            {
                code = write ? EXIT_FAILED : EXIT_CLEAN;
            }
            else if (!write)
            {
                printf("Nothing trimmed: add --write to do it.\n");
            }
            else if (ranges.empty())
            {
                printf("Nothing to trim.\n");
            }
            else if (windows.Trim(ranges, identify[105], &progress))
            {
                printf("Trimmed %I64u MB.\n", total >> 20);
            }
            else
            {
                printf("The drive refused the TRIM command (or the connection didn't pass it on).\n");
                code = EXIT_FAILED;
            }
        }
    }

    delete image;
    if (file != NULL)
    {
        fclose(file);
    }
    return code;
}

} // namespace

int main(int argc, char **argv)
{
    if (argc >= 2 && strcmp(argv[1], "check") == 0)
    {
        return command_partitions(argc - 2, argv + 2, MODE_CHECK);
    }
    if (argc >= 2 && strcmp(argv[1], "repair") == 0)
    {
        return command_partitions(argc - 2, argv + 2, MODE_REPAIR_DRY_RUN);
    }
    if (argc >= 2 && strcmp(argv[1], "trim") == 0)
    {
        return command_trim(argc - 2, argv + 2);
    }
    if (argc >= 2 && strcmp(argv[1], "recluster") == 0)
    {
        return command_recluster(argc - 2, argv + 2);
    }
    if (argc >= 2 && strcmp(argv[1], "resize") == 0)
    {
        return command_resize(argc - 2, argv + 2);
    }
    if (argc >= 2 && strcmp(argv[1], "make") == 0)
    {
        return command_make(argc - 2, argv + 2);
    }
    usage();
    return EXIT_FAILED;
}
