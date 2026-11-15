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
#include <memory>
#include <stdarg.h>
#include <stdexcept>
#include <string>
#include <vector>
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
            "  fatxtool defrag <image> [--partition N] [--write]\n"
            "  fatxtool defrag <image> --resume [--partition N]\n"
            "      Defragments: recluster to the cluster size it has, so every file\n"
            "      ends up unfragmented. check shows how fragmented it is.\n"
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

// How many files are fragmented, and the worst ones
void print_fragmentation(const FatxCheckResult *result)
{
    if (result->files == 0)
    {
        return;
    }
    if (result->fragmented_files == 0)
    {
        printf("  Fragmentation: none, no file is fragmented.\n");
        return;
    }
    printf("  Fragmentation: %I64u of %I64u files fragmented (%I64u extra fragments). Most:\n",
           result->fragmented_files,
           result->files,
           result->extra_pieces);
    for (uint32_t i = 0; i < result->worst_count; i++)
    {
        printf("    %u fragments: %s\n", result->worst[i].pieces, result->worst[i].path);
    }
}

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
        print_fragmentation(result);
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

// ---- errors and options

// Bad arguments: main prints the usage
class UsageError : public std::exception
{
};

// The command couldn't start (an unusable file, the wrong partition): main
// prints why
class ToolError : public std::runtime_error
{
  public:
    explicit ToolError(const std::string &message) : std::runtime_error(message)
    {
    }
};

// printf into a ToolError's message
std::string format_message(const char *format, ...)
{
    char text[300];
    va_list args;
    va_start(args, format);
    vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    return text;
}

// Closes the file when it goes out of scope
class OpenFile
{
  public:
    OpenFile() : m_file(nullptr)
    {
    }
    ~OpenFile()
    {
        Close();
    }
    OpenFile(const OpenFile &) = delete;
    OpenFile &operator=(const OpenFile &) = delete;

    bool Open(const std::string &path, const char *mode)
    {
        return fopen_s(&m_file, path.c_str(), mode) == 0 && m_file != nullptr;
    }
    FILE *Get() const
    {
        return m_file;
    }
    void Close()
    {
        if (m_file != nullptr)
        {
            fclose(m_file);
            m_file = nullptr;
        }
    }

  private:
    FILE *m_file;
};

// What --partition, --write and --list say
struct CommandOptions
{
    int partition; // 1-based, 0 for none
    bool write;
    bool list;
    bool range; // --offset or --size given
    uint64_t offset;
    uint64_t size; // 0: to the end
};

// Which of --write, --list and --offset/--size a command takes (--partition always)
struct OptionRules
{
    bool write;
    bool list;
    bool range;
};

CommandOptions parse_options(int count, char **options, OptionRules rules)
{
    CommandOptions parsed = {0, false, false, false, 0, 0};
    for (int i = 0; i < count; i++)
    {
        if (strcmp(options[i], "--partition") == 0 && i + 1 < count)
        {
            parsed.partition = atoi(options[++i]);
        }
        else if (strcmp(options[i], "--offset") == 0 && i + 1 < count && rules.range)
        {
            parsed.offset = parse_size(options[++i]);
            parsed.range = true;
        }
        else if (strcmp(options[i], "--size") == 0 && i + 1 < count && rules.range)
        {
            parsed.size = parse_size(options[++i]);
            parsed.range = true;
        }
        else if (strcmp(options[i], "--write") == 0 && rules.write)
        {
            parsed.write = true;
        }
        else if (strcmp(options[i], "--list") == 0 && rules.list)
        {
            parsed.list = true;
        }
        else
        {
            throw UsageError();
        }
    }
    return parsed;
}

// Where a command's partition is: the whole image, or one from a disk's table
struct PartitionPlace
{
    bool in_table;
    int index; // In the table, -1 for a whole image
    uint64_t start;
    uint64_t lba_size;
};

// "Partition 6 (F): " or "Partition: ", before what is said about it
void print_partition_label(const PartitionPlace &place)
{
    if (place.in_table)
    {
        printf("Partition %d (%c): ", place.index + 1, partition_letter(place.index));
    }
    else
    {
        printf("Partition: ");
    }
}

// "1823.50 MB"
void print_megabytes(const char *label, uint64_t lba)
{
    printf("%s%I64u.%02I64u MB", label, (lba * HDD_SECTOR_SIZE) >> 20, ((lba * HDD_SECTOR_SIZE) % (1 << 20)) * 100 >> 20);
}

// Checks it and prints the result; returns the exit code
int run_check(FatxDevice &device, uint64_t size, ConsoleProgress &progress)
{
    FatxCheckResult *result = new FatxCheckResult;
    FatxCheck(device, size, result, &progress);
    const int code = print_check(result);
    delete result;
    return code;
}

// Checks a partition after it was changed
int check_resized(FatxDevice &device, uint64_t lba_size, ConsoleProgress &progress)
{
    printf("  Checking the result:\n");
    fflush(stdout);
    return run_check(device, lba_size * HDD_SECTOR_SIZE, progress);
}

void print_nothing_written(bool drive)
{
    printf(drive ? "  Nothing written: add --write to do it (back the drive up first).\n" : "  Nothing written: add --write to do it (copy the image first).\n");
}

// ---- check and repair

// What check and repair work on: one disk, and how
struct CheckJob
{
    FatxDevice &disk;
    uint64_t disk_size;
    Mode mode;
    bool drive;
    ConsoleProgress &progress;
};

// Bytes on the disk: where a partition starts and how long it is
struct ByteRange
{
    uint64_t offset;
    uint64_t size;
};

// Repairs one partition (or says what it would repair); returns its exit code
int repair_partition(const CheckJob &job, FatxDevice &device, uint64_t size)
{
    FatxRepairResult *repair = new FatxRepairResult;
    FatxRepair(device, size, repair, job.mode == MODE_REPAIR_WRITE, &job.progress);
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
        if (job.mode == MODE_REPAIR_DRY_RUN)
        {
            print_repairs(repair, false);
            print_nothing_written(job.drive);
            break;
        }
        print_repairs(repair, true);
        printf("  Checking again:\n");
        fflush(stdout);
        code = run_check(device, size, job.progress);
        break;
    default:
        break; // print_check() explained
    }

    delete repair;
    return code;
}

// Checks or repairs one partition; returns its exit code
int check_partition(const CheckJob &job, const char *label, ByteRange range)
{
    printf("%s: %I64u MB at byte %I64u\n", label, range.size >> 20, range.offset);
    fflush(stdout); // Progress goes to stderr

    FatxOffsetDevice device(job.disk, range.offset);
    if (job.mode == MODE_CHECK)
    {
        return run_check(device, range.size, job.progress);
    }
    return repair_partition(job, device, range.size);
}

// A disk image or drive: every partition in its table, or only one
int check_table_partitions(const CheckJob &job, const XboxPartitionTable &table, int only)
{
    const bool high_bits = table.Version != XBOX_TABLE_VERSION_LBA48_V3;
    int code = EXIT_CLEAN;
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
        if ((start + sectors) * HDD_SECTOR_SIZE > job.disk_size)
        {
            printf("%s: ends past the end of the image, not checked.\n", label);
            continue;
        }
        const ByteRange range = {start * HDD_SECTOR_SIZE, sectors * HDD_SECTOR_SIZE};
        const int result = check_partition(job, label, range);
        code = (result > code) ? result : code;
        checked++;
    }
    if (checked == 0)
    {
        printf("No partition to check.\n");
        return EXIT_FAILED;
    }
    return code;
}

// A partition image, or the part --offset and --size pick
int check_byte_range(const CheckJob &job, const CommandOptions &options)
{
    if (options.partition != 0)
    {
        printf("--partition needs a whole disk image (with a partition table).\n");
        return EXIT_FAILED;
    }
    ByteRange range = {options.offset, options.size};
    if (range.size == 0)
    {
        range.size = (job.disk_size > range.offset) ? job.disk_size - range.offset : 0;
    }
    range.size -= range.size % HDD_SECTOR_SIZE;
    return check_partition(job, "Partition", range);
}

// check and repair, on a disk image or a single partition
int command_partitions(int argc, char **argv, Mode mode)
{
    if (argc < 1)
    {
        throw UsageError();
    }
    const char *path = argv[0];
    const OptionRules rules = {mode != MODE_CHECK, false, true};
    const CommandOptions options = parse_options(argc - 1, argv + 1, rules);
    if (options.write)
    {
        mode = MODE_REPAIR_WRITE;
    }

    DiskTarget target;
    std::string error;
    if (!target.Open(path, mode == MODE_REPAIR_WRITE, false, &error))
    {
        throw ToolError(error);
    }
    XboxPartitionTable table;
    const bool has_table = target.ReadTable(&table);
    if (target.IsDrive() && !has_table && !options.range)
    {
        throw ToolError(format_message("No Xbox partition table on %s.", path));
    }
    if (target.IsDrive() && mode == MODE_REPAIR_WRITE)
    {
        printf("Writing to the drive itself.\n");
    }

    ConsoleProgress progress;
    const CheckJob job = {target.Device(), target.Size(), mode, target.IsDrive(), progress};
    if (!options.range && has_table)
    {
        return check_table_partitions(job, table, options.partition);
    }
    return check_byte_range(job, options);
}

// ---- make

struct MakeArgs
{
    std::string path;
    uint64_t size;
    uint32_t cluster_bytes; // 0: the size picks it
    std::string from;       // Empty: no files
};

bool is_valid_cluster_size(uint32_t cluster_bytes)
{
    return cluster_bytes >= 16 * 1024 && cluster_bytes <= 1024 * 1024 && (cluster_bytes & (cluster_bytes - 1)) == 0;
}

MakeArgs parse_make_args(int argc, char **argv)
{
    if (argc < 2)
    {
        throw UsageError();
    }
    MakeArgs args = {argv[0], parse_size(argv[1]), 0, ""};
    args.size -= args.size % FATX_HEADER_BYTES;
    for (int i = 2; i < argc; i++)
    {
        if (strcmp(argv[i], "--cluster") == 0 && i + 1 < argc)
        {
            args.cluster_bytes = (uint32_t)atoi(argv[++i]) * 1024;
        }
        else if (strcmp(argv[i], "--from") == 0 && i + 1 < argc)
        {
            args.from = argv[++i];
        }
        else
        {
            throw UsageError();
        }
    }
    if (args.size < 1024 * 1024)
    {
        throw ToolError("The size must be at least 1M.");
    }
    if (args.cluster_bytes != 0 && !is_valid_cluster_size(args.cluster_bytes))
    {
        throw ToolError("--cluster must be a power of two from 16 to 1024.");
    }
    return args;
}

// Sparse if the file system allows; unused clusters are never written
void create_image_file(OpenFile &file, const MakeArgs &args)
{
    if (!file.Open(args.path, "w+b"))
    {
        throw ToolError("Can't create " + args.path);
    }
    if (_chsize_s(_fileno(file.Get()), (__int64)args.size) != 0)
    {
        throw ToolError("Can't make " + args.path + " that big");
    }
}

void print_skipped_item(const std::string &line)
{
    fprintf(stderr, "%s\n", line.c_str());
}

// Formats it and copies the files in; false if something failed
bool fill_image(FatxWriter &writer, const std::string &from, int *skipped)
{
    bool ok = writer.Format();
    if (ok && !from.empty())
    {
        ok = copy_tree(writer, fs::u8path(from), writer.RootCluster(), skipped, print_skipped_item);
    }
    return writer.Flush() && ok;
}

int command_make(int argc, char **argv)
{
    const MakeArgs args = parse_make_args(argc, argv);
    OpenFile file;
    create_image_file(file, args);
    FileDevice device(file.Get(), 0);
    FatxWriter writer(device, args.size, args.cluster_bytes);
    int skipped = 0;
    const bool ok = fill_image(writer, args.from, &skipped);
    file.Close();
    if (!ok)
    {
        throw ToolError("Failed to make " + args.path);
    }

    printf("Made %s: %I64u MB, %u KB clusters, %u bit FAT", args.path.c_str(), args.size >> 20, writer.ClusterBytes() / 1024, writer.Layout().bytes_per_entry * 8);
    if (skipped > 0)
    {
        printf(", %d items skipped", skipped);
    }
    printf(".\n");
    return EXIT_CLEAN;
}

// ---- resize

struct ResizeArgs
{
    const char *path;
    bool resume;
    uint64_t new_lba;
    CommandOptions options;
};

// A resize under way: the partition and how it is changed
struct ResizeJob
{
    FatxDevice &device;
    FatxResizeTable &table;
    ConsoleProgress &progress;
    const char *path;
    uint64_t old_lba;
    uint64_t new_lba;
    bool write;
    bool resume;
    bool drive;
    bool in_table;
};

ResizeArgs parse_resize_args(int argc, char **argv)
{
    if (argc < 2)
    {
        throw UsageError();
    }
    ResizeArgs args;
    args.path = argv[0];
    args.resume = strcmp(argv[1], "--resume") == 0;
    const OptionRules rules = {!args.resume, false, false};
    args.options = parse_options(argc - 2, argv + 2, rules);

    const uint64_t new_bytes = args.resume ? 0 : parse_size(argv[1]);
    if (!args.resume && (new_bytes == 0 || new_bytes % FATX_HEADER_BYTES != 0))
    {
        throw ToolError("The size must be a whole number of 4 KB.");
    }
    args.new_lba = new_bytes / HDD_SECTOR_SIZE;
    return args;
}

// Growing stops where XBpartitioner stops it: at the next partition, or
// before the backup table at the end of the disk
void check_growth_limit(const PartitionTableEditor &editor, const ResizeArgs &args, const PartitionPlace &place)
{
    const uint64_t limit = editor.GetGrowthLimit(place.index);
    if (args.resume || args.new_lba <= place.lba_size || args.new_lba - place.lba_size <= limit)
    {
        return;
    }
    const uint64_t most = (place.lba_size + limit) * HDD_SECTOR_SIZE;
    throw ToolError(format_message("Partition %d can't grow that far: at most to %I64u.%02I64u MB (the next partition, or the end of the disk and its backup table).",
                                   place.index + 1,
                                   most >> 20,
                                   ((most % (1 << 20)) * 100) >> 20));
}

PartitionPlace locate_resize_partition(DiskTarget &target, const ResizeArgs &args)
{
    XboxPartitionTable table;
    const bool disk_image = target.ReadTable(&table);
    if (target.IsDrive() && !disk_image)
    {
        throw ToolError(format_message("No Xbox partition table on %s.", args.path));
    }

    const int only = args.options.partition;
    PartitionPlace place = {disk_image, only - 1, 0, target.Size() / HDD_SECTOR_SIZE};
    if (!disk_image)
    {
        if (only != 0)
        {
            throw ToolError("--partition needs a whole disk image (with a partition table).");
        }
        place.index = -1;
        return place;
    }
    if (only < 1 || only > (int)XboxPartitionTableEntryCount || table.TableEntries[only - 1].Flags != PE_PARTFLAGS_IN_USE)
    {
        throw ToolError("A disk needs --partition N, for a partition in its table.");
    }

    // The high bits only count from v4 on. 4K: keeps clear of the last 4K
    // block, where XBpartitioner puts the backup on 512e drives (and the
    // last sector, where it goes otherwise, is inside or past it).
    const PartitionTableEditor editor(table, target.Size() / HDD_SECTOR_SIZE, table.Version != XBOX_TABLE_VERSION_LBA48_V3, true);
    place.start = editor.GetPartitionStart(place.index);
    place.lba_size = editor.GetPartitionSize(place.index);
    check_growth_limit(editor, args, place);
    return place;
}

// Prints the nearest sizes that keep the data, from how it's used
void print_keeping_sizes(const ResizeJob &job)
{
    FatxUsage usage;
    if (!FatxReadUsage(job.device, job.old_lba, &usage))
    {
        return;
    }
    const uint64_t below = FatxKeepingSizeBelow(job.old_lba, usage, job.new_lba);
    const uint64_t above = FatxKeepingSizeAbove(job.old_lba, usage, job.new_lba);
    printf("  %I64u MB in use. Nearest sizes that keep the data:", (usage.used_clusters * usage.cluster_bytes) >> 20);
    if (below != 0)
    {
        print_megabytes(" ", below);
    }
    if (above != 0)
    {
        print_megabytes(below != 0 ? " or " : " ", above);
    }
    printf(below == 0 && above == 0 ? " none.\n" : ".\n");
}

// The size it rounds to, how the FAT changes and what moves first
void print_move_plan(const ResizeJob &job, const FatxMoveResult &move)
{
    if (move.new_lba_size != job.new_lba)
    {
        print_megabytes("  Rounded down to ", move.new_lba_size);
        printf(", so the FAT changes by whole clusters.\n");
    }
    const int64_t shift = move.shift > 0 ? move.shift : -move.shift;
    printf("  The FAT %s by %I64d cluster%s: every cluster gets a new number.\n", move.shift > 0 ? "grows" : "shrinks", shift, shift == 1 ? "" : "s");
    printf("  %I64u cluster%s to move first (%I64u free where they can go).\n", move.clusters_to_move, move.clusters_to_move == 1 ? "" : "s", move.free_clusters);
}

// What a resize with moves did, or why not; returns the exit code
int report_move(const ResizeJob &job, const FatxMoveResult &move)
{
    switch (move.status)
    {
    case FATX_MOVE_PLANNED:
        print_nothing_written(job.drive);
        return EXIT_CLEAN;
    case FATX_MOVE_DONE:
        printf("  Resized: %I64u cluster%s moved, the FAT and every folder renumbered.\n", move.clusters_moved, move.clusters_moved == 1 ? "" : "s");
        return check_resized(job.device, move.new_lba_size, job.progress);
    case FATX_MOVE_SAME_FAT:
        printf("  The FAT can't change by whole clusters at that size.\n");
        return EXIT_ERRORS;
    case FATX_MOVE_HAS_ERRORS:
        print_check(&move.repair.check);
        printf("  A resize needs a check without errors.\n");
        return EXIT_ERRORS;
    case FATX_MOVE_ENTRY_SIZE:
        printf("  The FAT's entries would change between 2 and 4 bytes (at about 65,000 clusters): not supported.\n");
        return EXIT_ERRORS;
    case FATX_MOVE_CLUSTER_SIZE:
        printf("  That size needs bigger clusters than %u KB: format it instead.\n", move.cluster_bytes / 1024);
        return EXIT_ERRORS;
    case FATX_MOVE_TOO_SMALL:
        printf("  No size that small works with this FAT and cluster size.\n");
        return EXIT_ERRORS;
    case FATX_MOVE_NO_ROOM:
        printf("  Not enough room: %I64u clusters to move, %I64u free where they can go (one more is needed).\n", move.clusters_to_move, move.free_clusters);
        return EXIT_ERRORS;
    case FATX_MOVE_STOPPED_SAFE:
        printf("  Stopped while moving clusters (read or write error). It is still the old size and valid;\n");
        printf("  a check may show lost clusters, which repair frees.\n");
        return EXIT_FAILED;
    case FATX_MOVE_INTERRUPTED:
        printf("  Stopped while renumbering (read or write error): finish it with --resume.\n");
        return EXIT_FAILED;
    default:
        printf("  Could not be checked (read error, out of memory or damaged header).\n");
        return EXIT_FAILED;
    }
}

// The sizes that would work, after a size that doesn't
bool suggests_other_sizes(FatxMoveStatus status)
{
    return status == FATX_MOVE_SAME_FAT || status == FATX_MOVE_CLUSTER_SIZE || status == FATX_MOVE_TOO_SMALL || status == FATX_MOVE_NO_ROOM ||
           status == FATX_MOVE_ENTRY_SIZE;
}

// Resizes by moving clusters; returns the exit code
int resize_with_moves(const ResizeJob &job)
{
    FatxMoveResult *move = new FatxMoveResult;
    FatxResizeWithMoves(job.device, job.old_lba, job.new_lba, job.table, job.write, move, &job.progress);

    if (move->status == FATX_MOVE_PLANNED || move->status == FATX_MOVE_DONE)
    {
        print_move_plan(job, *move);
    }
    if (move->repair.written)
    {
        print_repairs(&move->repair, true);
    }
    const int code = report_move(job, *move);
    if (suggests_other_sizes(move->status))
    {
        print_keeping_sizes(job);
    }
    delete move;
    return code;
}

// Whether a table change alone does it; returns the exit code
int report_resize_check(const ResizeJob &job, const FatxResizeResult &resize)
{
    switch (resize.status)
    {
    case FATX_RESIZE_OK:
        printf("  The data is kept: only the table changes.\n");
        return EXIT_CLEAN;
    case FATX_RESIZE_NO_FILE_SYSTEM:
        printf("  Not FATX: there is no data to keep.\n");
        return EXIT_CLEAN;
    case FATX_RESIZE_OUT_OF_RANGE:
        return resize_with_moves(job);
    case FATX_RESIZE_CLUSTERS_IN_USE:
        printf("  %I64u clusters past the new end are in use%s.\n",
               resize.clusters_in_use,
               resize.check.lost_clusters > 0 ? " (lost ones count too: try repair first)" : "");
        print_keeping_sizes(job);
        return EXIT_ERRORS;
    case FATX_RESIZE_FAT_NOT_CLEAN:
        printf("  The FAT entries for the new clusters aren't 0: not grown.\n");
        return EXIT_ERRORS;
    case FATX_RESIZE_HAS_ERRORS:
        print_check(&resize.check);
        printf("  Shrinking needs a check without errors.\n");
        return EXIT_ERRORS;
    default:
        printf("  Could not be checked (read error or damaged header).\n");
        return EXIT_FAILED;
    }
}

// Small changes only touch the table; bigger ones move clusters
int resize_partition(const ResizeJob &job)
{
    print_megabytes("", job.old_lba);
    print_megabytes(" -> ", job.new_lba);
    printf("\n");
    fflush(stdout);

    FatxResizeResult *resize = new FatxResizeResult;
    FatxCheckResize(job.device, job.old_lba, job.new_lba, resize, &job.progress);
    if (resize->status != FATX_RESIZE_NO_FILE_SYSTEM && resize->status != FATX_RESIZE_NOT_CHECKED)
    {
        print_megabytes("  Sizes that keep the FAT's size: ", resize->min_lba);
        print_megabytes(" to ", resize->max_lba);
        printf(" (%u KB clusters)\n", resize->cluster_bytes / 1024);
    }
    int code = report_resize_check(job, *resize);
    const bool in_place = (resize->status == FATX_RESIZE_OK || resize->status == FATX_RESIZE_NO_FILE_SYSTEM);
    delete resize;

    if (!in_place)
    {
        return code;
    }
    if (!job.write)
    {
        print_nothing_written(job.drive);
        return code;
    }
    if (!job.table.SetSize(job.new_lba))
    {
        printf("  Writing failed.\n");
        return EXIT_FAILED;
    }
    printf("  Resized.\n");
    return check_resized(job.device, job.new_lba, job.progress);
}

int finish_interrupted_resize(const ResizeJob &job)
{
    fflush(stdout);
    FatxMoveResult *move = new FatxMoveResult;
    int code = EXIT_FAILED;
    if (FatxResumeResize(job.device, job.table, move, &job.progress))
    {
        printf("  Finished.\n");
        code = check_resized(job.device, move->new_lba_size, job.progress);
    }
    else
    {
        printf("  Could not be finished (read or write error). Try again.\n");
    }
    delete move;
    return code;
}

// An interrupted resize or cluster size change must be finished first;
// --resume finishes a resize
int resize_or_resume(const ResizeJob &job)
{
    uint64_t interrupted_old = 0;
    uint64_t interrupted_new = 0;
    const bool interrupted = FatxFindInterruptedResize(job.device, &interrupted_old, &interrupted_new);
    std::error_code fs_error;
    if (!interrupted && !job.drive && fs::exists(std::string(job.path) + ".recluster", fs_error))
    {
        printf("unfinished cluster size change: finish it with fatxtool recluster %s --resume.\n", job.path);
        return EXIT_FAILED;
    }
    if (!job.resume && !interrupted)
    {
        return resize_partition(job);
    }
    if (!interrupted)
    {
        printf("no interrupted resize to finish.\n");
        return EXIT_FAILED;
    }

    print_megabytes("interrupted resize from ", interrupted_old);
    print_megabytes(" to ", interrupted_new);
    printf("\n");
    if (!job.resume)
    {
        printf("  Finish it first: fatxtool resize %s --resume%s.\n", job.path, job.in_table ? " --partition N" : "");
        return EXIT_FAILED;
    }
    return finish_interrupted_resize(job);
}

int command_resize(int argc, char **argv)
{
    const ResizeArgs args = parse_resize_args(argc, argv);
    const bool writes = args.options.write || args.resume;
    DiskTarget target;
    std::string error;
    if (!target.Open(args.path, writes, false, &error))
    {
        throw ToolError(error);
    }
    const PartitionPlace place = locate_resize_partition(target, args);
    print_partition_label(place);
    if (target.IsDrive() && writes)
    {
        printf("(writing to the drive itself) ");
    }

    FatxOffsetDevice device(target.Device(), place.start * HDD_SECTOR_SIZE);
    std::unique_ptr<FatxResizeTable> table = target.Table(place.index);
    ConsoleProgress progress;
    const ResizeJob job = {device, *table, progress, args.path, place.lba_size, args.new_lba, args.options.write, args.resume, target.IsDrive(), place.in_table};
    return resize_or_resume(job);
}

// ---- recluster and defrag

struct ReclusterArgs
{
    std::string path;
    bool resume;
    bool defrag;                // The cluster size stays what it is
    uint32_t new_cluster_bytes; // 0 for --resume and defrag
    CommandOptions options;
};

// A cluster size change under way on an image
struct ReclusterJob
{
    FILE *image;
    const ReclusterArgs &args;
    uint64_t start;
    uint64_t lba_size;
    std::string plan_path;
    std::string build_path;
    ConsoleProgress &progress;
};

// What building the new file system came to
struct BuildOutcome
{
    int code;
    bool built;
};

// Images only: the new file system is built as a full copy first
void refuse_drive(const ReclusterArgs &args)
{
    if (DiskTarget::IsDrivePath(args.path))
    {
        throw ToolError(std::string(args.defrag ? "defrag" : "recluster") + " needs an image: it builds the new file system as a full copy first.");
    }
}

ReclusterArgs parse_recluster_args(int argc, char **argv)
{
    if (argc < 2)
    {
        throw UsageError();
    }
    ReclusterArgs args;
    args.path = argv[0];
    args.resume = strcmp(argv[1], "--resume") == 0;
    args.defrag = false;
    args.new_cluster_bytes = args.resume ? 0 : (uint32_t)atoi(argv[1]) * 1024;
    const OptionRules rules = {!args.resume, false, false};
    args.options = parse_options(argc - 2, argv + 2, rules);
    refuse_drive(args);
    return args;
}

// defrag <image> [--resume] [--partition N] [--write]: --resume anywhere
ReclusterArgs parse_defrag_args(int argc, char **argv)
{
    if (argc < 1)
    {
        throw UsageError();
    }
    ReclusterArgs args;
    args.path = argv[0];
    args.resume = false;
    args.defrag = true;
    args.new_cluster_bytes = 0;
    std::vector<char *> options;
    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "--resume") == 0)
        {
            args.resume = true;
        }
        else
        {
            options.push_back(argv[i]);
        }
    }
    const OptionRules rules = {!args.resume, false, false};
    args.options = parse_options((int)options.size(), options.data(), rules);
    refuse_drive(args);
    return args;
}

PartitionPlace locate_recluster_partition(FILE *file, const ReclusterArgs &args)
{
    const uint64_t image_size = file_size(file);
    XboxPartitionTable table;
    memset(&table, 0, sizeof(table));
    _fseeki64(file, 0, SEEK_SET);
    fread(&table, 1, sizeof(table), file);

    const int only = args.options.partition;
    PartitionPlace place = {false, -1, 0, image_size / HDD_SECTOR_SIZE};
    if (!PartitionTableEditor::HasMagic(table))
    {
        if (only != 0)
        {
            throw ToolError("--partition needs a whole disk image (with a partition table).");
        }
        return place;
    }
    if (only < 1 || only > (int)XboxPartitionTableEntryCount || table.TableEntries[only - 1].Flags != PE_PARTFLAGS_IN_USE)
    {
        throw ToolError("A disk image needs --partition N, for a partition in its table.");
    }
    const PartitionTableEditor editor(table, image_size / HDD_SECTOR_SIZE, table.Version != XBOX_TABLE_VERSION_LBA48_V3, false);
    place.in_table = true;
    place.index = only - 1;
    place.start = editor.GetPartitionStart(place.index);
    place.lba_size = editor.GetPartitionSize(place.index);
    return place;
}

// Copies the built file system back; also used by --resume
int copy_reclustered(const ReclusterJob &job, const std::vector<FatxRange> &ranges)
{
    OpenFile build_file;
    if (!build_file.Open(job.build_path, "rb"))
    {
        printf("  Can't open %s: the copy can't be finished.\n", job.build_path.c_str());
        return EXIT_FAILED;
    }
    FileDevice build(build_file.Get(), 0);
    FileDevice partition(job.image, job.start * HDD_SECTOR_SIZE);
    printf("  Copying it onto the partition...\n");
    fflush(stdout);
    const bool copied = FatxCopyReclustered(build, partition, ranges, &job.progress);
    build_file.Close();
    if (!copied)
    {
        printf("  Stopped partway (read or write error): finish it with --resume.\n");
        return EXIT_FAILED;
    }
    remove_recluster_files(job.plan_path, job.build_path);
    return check_resized(partition, job.lba_size, job.progress);
}

// A plan left behind: --resume repeats the copy, anything else waits for it
int finish_pending_recluster(const ReclusterJob &job, const ReclusterPlanHeader &plan, const std::vector<FatxRange> &ranges)
{
    printf("unfinished change to %u KB clusters.\n", plan.new_cluster_bytes / 1024);
    if (!job.args.resume)
    {
        printf("  Finish it first: fatxtool recluster %s --resume%s.\n", job.args.path.c_str(), job.start != 0 ? " --partition N" : "");
        return EXIT_FAILED;
    }
    if (plan.partition_offset != job.start * HDD_SECTOR_SIZE || plan.lba_size != job.lba_size)
    {
        printf("  It was for another partition: use the --partition it was started with.\n");
        return EXIT_FAILED;
    }
    return copy_reclustered(job, ranges);
}

// Rebuilding a partition with no file fragmented would change nothing
bool nothing_to_defragment(const ReclusterJob &job, FatxDevice &device)
{
    FatxCheckResult *before = new FatxCheckResult;
    FatxCheck(device, job.lba_size * HDD_SECTOR_SIZE, before, &job.progress);
    const bool nothing = before->status == FATX_CHECK_DONE && before->errors == 0 && before->fragmented_files == 0;
    if (nothing)
    {
        print_fragmentation(before);
        printf("  Nothing to defragment.\n");
    }
    delete before;
    return nothing;
}

// What the build found, or why it wasn't done; returns the exit code
int report_recluster(const ReclusterJob &job, const FatxReclusterResult &result, uint32_t new_cluster_bytes)
{
    if (result.status == FATX_RECLUSTER_PLANNED || result.status == FATX_RECLUSTER_DONE || result.status == FATX_RECLUSTER_NO_ROOM)
    {
        printf("  %I64u files, %I64u folders: %I64u clusters of %u KB now (%I64u in use), %I64u of %u KB needed, %I64u there.\n",
               result.files,
               result.folders,
               result.check.layout.cluster_limit - 1,
               result.old_cluster_bytes / 1024,
               result.old_clusters_used,
               result.clusters_needed,
               new_cluster_bytes / 1024,
               result.clusters_available);
    }
    if (result.new_cluster_bytes > 64 * 1024 && result.status == FATX_RECLUSTER_PLANNED)
    {
        printf("  It fits. Nothing written: add --write to do it (copy the image first).\n");
        printf("  Note: BIOSes with the LBA48 v3 patch only mount clusters up to 64 KB.\n");
        return EXIT_CLEAN;
    }

    switch (result.status)
    {
    case FATX_RECLUSTER_PLANNED:
        printf("  It fits. Nothing written: add --write to do it (copy the image first).\n");
        return EXIT_CLEAN;
    case FATX_RECLUSTER_DONE:
        return EXIT_ERRORS; // Not final: the copy decides
    case FATX_RECLUSTER_BAD_SIZE:
        printf("  The cluster size must be a power of two from 16 to 1024 KB.\n");
        return EXIT_ERRORS;
    case FATX_RECLUSTER_TOO_SMALL:
        printf("  XBpartitioner uses %u KB clusters at least for this size.\n", ClusterSizeKbFor(job.lba_size));
        return EXIT_ERRORS;
    case FATX_RECLUSTER_NOT_FATX:
        printf("  Not FATX.\n");
        return EXIT_ERRORS;
    case FATX_RECLUSTER_RESIZING:
        printf("  A resize was interrupted: finish it first with fatxtool resize %s --resume.\n", job.args.path.c_str());
        return EXIT_ERRORS;
    case FATX_RECLUSTER_HAS_ERRORS:
        print_check(&result.check);
        printf("  Changing the cluster size needs a check without errors.\n");
        return EXIT_ERRORS;
    case FATX_RECLUSTER_NO_ROOM:
        printf("  Not enough room: each file takes whole clusters, and bigger ones waste more.\n");
        return EXIT_ERRORS;
    case FATX_RECLUSTER_WRITE_ERROR:
        printf("  Couldn't write %s (disk full?). The image wasn't changed.\n", job.build_path.c_str());
        return EXIT_FAILED;
    case FATX_RECLUSTER_BUILD_FAILED:
        printf("  The new file system didn't check clean (a bug). The image wasn't changed.\n");
        return EXIT_FAILED;
    default:
        printf("  Read error. The image wasn't changed.\n");
        return EXIT_FAILED;
    }
}

// Builds the new file system: into <image>.recluster.tmp with --write, else
// only to see whether it fits. *ranges: what the copy back has to write.
BuildOutcome build_reclustered(const ReclusterJob &job, FatxDevice &device, uint32_t new_cluster_bytes, std::vector<FatxRange> *ranges)
{
    OpenFile build_file;
    std::unique_ptr<FileDevice> build;
    std::unique_ptr<FatxRangeRecorder> recorder;
    if (job.args.options.write)
    {
        if (!build_file.Open(job.build_path, "w+b"))
        {
            throw ToolError("Can't create " + job.build_path);
        }
        make_sparse(build_file.Get());
        build = std::make_unique<FileDevice>(build_file.Get(), 0);
        recorder = std::make_unique<FatxRangeRecorder>(*build);
    }

    FatxReclusterResult *result = new FatxReclusterResult;
    FatxBuildReclustered(device, job.lba_size, new_cluster_bytes, recorder.get(), result, &job.progress);
    if (job.args.defrag && result->check.status == FATX_CHECK_DONE)
    {
        print_fragmentation(&result->check);
    }
    const BuildOutcome outcome = {report_recluster(job, *result, new_cluster_bytes), result->status == FATX_RECLUSTER_DONE};
    delete result;

    if (outcome.built && recorder)
    {
        *ranges = recorder->Ranges();
    }
    return outcome;
}

// Records what the copy back has to do, so --resume can repeat it, then copies
int save_and_copy(const ReclusterJob &job, uint32_t new_cluster_bytes, const std::vector<FatxRange> &ranges)
{
    ReclusterPlanHeader plan;
    memcpy(plan.magic, RECLUSTER_MAGIC, sizeof(RECLUSTER_MAGIC));
    plan.partition_offset = job.start * HDD_SECTOR_SIZE;
    plan.lba_size = job.lba_size;
    plan.new_cluster_bytes = new_cluster_bytes;
    plan.range_count = (uint32_t)ranges.size();
    if (!save_recluster_plan(job.plan_path, plan, ranges))
    {
        printf("  Couldn't write %s. The image wasn't changed.\n", job.plan_path.c_str());
        remove_recluster_files(job.plan_path, job.build_path);
        return EXIT_FAILED;
    }
    printf("  Built and checked.\n");
    return copy_reclustered(job, ranges);
}

int recluster(const ReclusterJob &job)
{
    FileDevice device(job.image, job.start * HDD_SECTOR_SIZE);
    uint32_t new_cluster_bytes = job.args.new_cluster_bytes;
    if (job.args.defrag)
    {
        uint32_t sectors_per_cluster = 0;
        device.Read(FATX_SECTORS_PER_CLUSTER_OFFSET, &sectors_per_cluster, sizeof(sectors_per_cluster));
        new_cluster_bytes = sectors_per_cluster * HDD_SECTOR_SIZE;
    }

    print_megabytes("", job.lba_size);
    printf(job.args.defrag ? ", defragmenting (%u KB clusters)\n" : ", to %u KB clusters\n", new_cluster_bytes / 1024);
    fflush(stdout);
    if (job.args.defrag && nothing_to_defragment(job, device))
    {
        return EXIT_CLEAN;
    }

    std::vector<FatxRange> ranges;
    const BuildOutcome outcome = build_reclustered(job, device, new_cluster_bytes, &ranges);
    if (!job.args.options.write)
    {
        return outcome.code;
    }
    if (!outcome.built)
    {
        remove_recluster_files(job.plan_path, job.build_path);
        return outcome.code;
    }
    return save_and_copy(job, new_cluster_bytes, ranges);
}

int run_recluster(const ReclusterArgs &args)
{
    OpenFile file;
    if (!file.Open(args.path, (args.options.write || args.resume) ? "r+b" : "rb"))
    {
        throw ToolError("Can't open " + args.path);
    }
    const PartitionPlace place = locate_recluster_partition(file.Get(), args);
    print_partition_label(place);

    ConsoleProgress progress;
    const ReclusterJob job = {file.Get(), args, place.start, place.lba_size, args.path + ".recluster", args.path + ".recluster.tmp", progress};
    ReclusterPlanHeader plan;
    std::vector<FatxRange> ranges;
    if (load_recluster_plan(job.plan_path, &plan, &ranges))
    {
        return finish_pending_recluster(job, plan, ranges);
    }
    if (args.resume)
    {
        printf("no unfinished cluster size change (%s).\n", job.plan_path.c_str());
        return EXIT_FAILED;
    }
    return recluster(job);
}

int command_recluster(int argc, char **argv)
{
    return run_recluster(parse_recluster_args(argc, argv));
}

int command_defrag(int argc, char **argv)
{
    return run_recluster(parse_defrag_args(argc, argv));
}

// ---- trim

// The disk trim reads: a drive, or an image for --list
struct TrimSource
{
    bool physical;
    WindowsDisk windows;
    OpenFile file;
    std::unique_ptr<FileDevice> image;

    FatxReader &Reader()
    {
        if (physical)
        {
            return windows;
        }
        return *image;
    }
    uint64_t Size()
    {
        return physical ? windows.Size() : file_size(file.Get());
    }
};

// Free space found so far, as ranges on the disk
struct TrimRanges
{
    std::vector<FatxRange> ranges;
    uint64_t total;
};

void open_drive(TrimSource &source, const char *path)
{
    unsigned long error = 0;
    if (source.windows.Open(path, true, &error))
    {
        return;
    }
    if (error == ERROR_ACCESS_DENIED)
    {
        throw ToolError(format_message("Can't open %s: run fatxtool as administrator.", path));
    }
    if (error == ERROR_FILE_NOT_FOUND)
    {
        throw ToolError(format_message("There is no %s (Disk Management shows the disk numbers).", path));
    }
    throw ToolError(format_message("Can't open %s (error %lu).", path, error));
}

void open_trim_source(TrimSource &source, const char *path, bool write)
{
    source.physical = strncmp(path, "\\\\.\\", 4) == 0;
    if (source.physical)
    {
        open_drive(source, path);
        return;
    }
    if (write)
    {
        throw ToolError("An image has nothing to trim: --list prints the ranges.");
    }
    if (!source.file.Open(path, "rb"))
    {
        throw ToolError(format_message("Can't open %s", path));
    }
    source.image = std::make_unique<FileDevice>(source.file.Get(), 0);
}

// Partitions from the table, or the whole image as one
std::vector<PartitionPlace> list_trim_partitions(TrimSource &source, const char *path, int only)
{
    XboxPartitionTable table;
    memset(&table, 0, sizeof(table));
    source.Reader().Read(0, &table, sizeof(table));
    const uint64_t disk_size = source.Size();

    std::vector<PartitionPlace> parts;
    if (PartitionTableEditor::HasMagic(table))
    {
        const PartitionTableEditor editor(table, disk_size / HDD_SECTOR_SIZE, table.Version != XBOX_TABLE_VERSION_LBA48_V3, false);
        for (int i = 0; i < XboxPartitionTableEntryCount; i++)
        {
            if (editor.IsPartitionEnabled(i) && (only == 0 || only == i + 1))
            {
                const PartitionPlace part = {true, i, editor.GetPartitionStart(i), editor.GetPartitionSize(i)};
                parts.push_back(part);
            }
        }
        return parts;
    }
    if (source.physical || only != 0)
    {
        throw ToolError(format_message("No Xbox partition table at the start of %s.", path));
    }
    const PartitionPlace whole = {false, -1, 0, disk_size / HDD_SECTOR_SIZE};
    parts.push_back(whole);
    return parts;
}

// Why a partition's free space isn't trimmed
void print_trim_skip(const FatxCheckResult &check)
{
    switch (check.status)
    {
    case FATX_CHECK_NOT_FATX:
        printf("not FATX, skipped.\n");
        break;
    case FATX_CHECK_RESIZING:
        printf("an interrupted resize, skipped (finish it first).\n");
        break;
    case FATX_CHECK_DONE:
        printf("%u errors, skipped (fatxtool check shows them).\n", check.errors);
        break;
    default:
        printf("could not be checked, skipped.\n");
        break;
    }
}

// Only a partition that checks clean; ranges are added as disk offsets
void add_free_ranges(TrimSource &source, const PartitionPlace &part, TrimRanges *found)
{
    print_partition_label(part);
    if ((part.start + part.lba_size) * HDD_SECTOR_SIZE > source.Size())
    {
        printf("ends past the end of the disk, skipped.\n");
        return;
    }
    fflush(stdout);

    FatxOffsetReader reader(source.Reader(), part.start * HDD_SECTOR_SIZE);
    FatxCheckResult *check = new FatxCheckResult;
    std::vector<FatxRange> free_ranges;
    const bool ok = FatxFreeRanges(reader, part.lba_size * HDD_SECTOR_SIZE, &free_ranges, check);
    if (!ok)
    {
        print_trim_skip(*check);
    }
    delete check;
    if (!ok)
    {
        return;
    }

    uint64_t free_bytes = 0;
    for (FatxRange range : free_ranges)
    {
        free_bytes += range.length;
        range.offset += part.start * HDD_SECTOR_SIZE;
        found->ranges.push_back(range);
    }
    found->total += free_bytes;
    printf("%I64u MB free, in %u ranges.\n", free_bytes >> 20, (unsigned int)free_ranges.size());
}

void print_trim_list(const TrimRanges &found)
{
    printf("# offset length (bytes, on the disk), e.g. for: blkdiscard -o OFFSET -l LENGTH /dev/sdX\n");
    for (const FatxRange &range : found.ranges)
    {
        printf("%I64u %I64u\n", range.offset, range.length);
    }
}

// Needs ATA commands to get through and the drive to support TRIM
int trim_drive(WindowsDisk &windows, const TrimRanges &found, bool write)
{
    uint16_t identify[256];
    ConsoleProgress progress;
    if (!windows.Identify(identify))
    {
        printf("ATA commands don't get through to this drive (USB adapters often block them):\n");
        printf("it can't be trimmed from here. --list gives the ranges for other tools.\n");
        return write ? EXIT_FAILED : EXIT_CLEAN;
    }

    const bool trim = (identify[169] & 0x0001) != 0;
    printf("Drive: %s, %s.\n", identify_string(&identify[27], 20).c_str(), trim ? "supports TRIM" : "no TRIM support");
    if (!trim)
    {
        return write ? EXIT_FAILED : EXIT_CLEAN;
    }
    if (!write)
    {
        printf("Nothing trimmed: add --write to do it.\n");
    }
    else if (found.ranges.empty())
    {
        printf("Nothing to trim.\n");
    }
    else if (windows.Trim(found.ranges, identify[105], &progress))
    {
        printf("Trimmed %I64u MB.\n", found.total >> 20);
    }
    else
    {
        printf("The drive refused the TRIM command (or the connection didn't pass it on).\n");
        return EXIT_FAILED;
    }
    return EXIT_CLEAN;
}

int command_trim(int argc, char **argv)
{
    if (argc < 1)
    {
        throw UsageError();
    }
    const char *path = argv[0];
    const OptionRules rules = {true, true, false};
    const CommandOptions options = parse_options(argc - 1, argv + 1, rules);

    TrimSource source;
    open_trim_source(source, path, options.write);
    TrimRanges found;
    found.total = 0;
    for (const PartitionPlace &part : list_trim_partitions(source, path, options.partition))
    {
        add_free_ranges(source, part, &found);
    }
    printf("To trim: %I64u MB in %u ranges.\n", found.total >> 20, (unsigned int)found.ranges.size());

    if (options.list)
    {
        print_trim_list(found);
    }
    return source.physical ? trim_drive(source.windows, found, options.write) : EXIT_CLEAN;
}

// ---- commands

int command_check(int argc, char **argv)
{
    return command_partitions(argc, argv, MODE_CHECK);
}

int command_repair(int argc, char **argv)
{
    return command_partitions(argc, argv, MODE_REPAIR_DRY_RUN);
}

struct Command
{
    const char *name;
    int (*run)(int argc, char **argv);
};

const Command COMMANDS[] = {
    {"check", command_check},
    {"repair", command_repair},
    {"trim", command_trim},
    {"recluster", command_recluster},
    {"defrag", command_defrag},
    {"resize", command_resize},
    {"make", command_make},
};

int run_command(int argc, char **argv)
{
    if (argc >= 2)
    {
        for (const Command &command : COMMANDS)
        {
            if (strcmp(argv[1], command.name) == 0)
            {
                return command.run(argc - 2, argv + 2);
            }
        }
    }
    throw UsageError();
}

} // namespace

int main(int argc, char **argv)
{
    try
    {
        return run_command(argc, argv);
    }
    catch (const UsageError &)
    {
        usage();
    }
    catch (const ToolError &error)
    {
        fprintf(stderr, "%s\n", error.what());
    }
    return EXIT_FAILED;
}
