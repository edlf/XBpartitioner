// fatxtool: the XBpartitioner FATX check on the PC, and a way to make test
// images. Run it without arguments for the usage.

#include "fatx_check.hpp"
#include "fatx_writer.hpp"

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
const int EXIT_ERRORS = 1;   // The check found errors
const int EXIT_FAILED = 2;   // Usage, or the image couldn't be read or made

void usage()
{
    fprintf(stderr,
            "Usage:\n"
            "  fatxtool check <image> [--partition N] [--offset BYTES] [--size BYTES]\n"
            "      Checks a FATX file system, read only. <image> is a partition image\n"
            "      (starts with FATX) or a whole Xbox disk image (starts with the\n"
            "      ****PARTINFO**** table): then every partition, or only N (1 = E,\n"
            "      2 = C, 3 = X, 4 = Y, 5 = Z, 6 = F, ...). --offset and --size pick a\n"
            "      partition by hand. Exit code: 0 no errors, 1 errors, 2 not checked.\n"
            "\n"
            "  fatxtool repair <image> [--write] [--partition N] [--offset BYTES] [--size BYTES]\n"
            "      The safe repairs: frees lost clusters, ends chains longer than their\n"
            "      file, frees the clusters of empty files, fixes FAT entry 0. Nothing a\n"
            "      file reads as changes. Only when the check finds no errors. Without\n"
            "      --write it only shows what it would do; copy the image before using\n"
            "      it. Exit code: 0 repaired or nothing to do, 1 errors, 2 failed.\n"
            "\n"
            "  fatxtool make <image> <size> [--cluster KB] [--from DIR]\n"
            "      Makes a formatted partition image (sizes take K, M, G or T, and\n"
            "      are rounded down to 4 KB). --cluster: 16 to 1024, by default what\n"
            "      XBpartitioner picks for the size. --from: copies DIR into it.\n");
}

// "512M", "8G", "4096": bytes, 0 if not a size
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
    case '\0': break;
    case 'k': case 'K': scale = 1ULL << 10; end++; break;
    case 'm': case 'M': scale = 1ULL << 20; end++; break;
    case 'g': case 'G': scale = 1ULL << 30; end++; break;
    case 't': case 'T': scale = 1ULL << 40; end++; break;
    default: return 0;
    }
    if (*end == 'B' || *end == 'b')
    {
        end++;
    }
    return (*end == '\0') ? value * scale : 0;
}

// E, C, X, Y, Z, then F, G, ...
char partition_letter(int index)
{
    static const char standard[] = "ECXYZ";
    return (index < XBOX_STANDARD_PARTITION_COUNT) ? standard[index] : (char)('F' + index - XBOX_STANDARD_PARTITION_COUNT);
}

// A file, from base on (a partition inside a disk image)
class FileDevice : public FatxDevice
{
public:
    FileDevice(FILE *file, uint64_t base) : m_file(file), m_base(base) {}
    bool Read(uint64_t offset, void *buffer, uint32_t length) override
    {
        return _fseeki64(m_file, (__int64)(m_base + offset), SEEK_SET) == 0 &&
               fread(buffer, 1, length, m_file) == length;
    }
    bool Write(uint64_t offset, const void *buffer, uint32_t length) override
    {
        return _fseeki64(m_file, (__int64)(m_base + offset), SEEK_SET) == 0 &&
               fwrite(buffer, 1, length, m_file) == length;
    }

private:
    FILE *m_file;
    uint64_t m_base;
};

class ConsoleProgress : public FatxProgress
{
public:
    void Update(int percent) override
    {
        if (!_isatty(_fileno(stderr)))
        {
            return; // Redirected: no progress lines in the file
        }
        fprintf(stderr, "\r  %3d%%", percent);
        if (percent == 100)
        {
            fprintf(stderr, "\r      \r");
        }
    }
};

uint64_t file_size(FILE *file)
{
    _fseeki64(file, 0, SEEK_END);
    return (uint64_t)_ftelli64(file);
}

enum Mode
{
    MODE_CHECK,
    MODE_REPAIR_DRY_RUN,
    MODE_REPAIR_WRITE
};

// Prints a check's result; returns the exit code for it
int print_check(const FatxCheckResult *result)
{
    int code = EXIT_CLEAN;
    switch (result->status)
    {
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
        printf("  FATX, %u KB clusters, %u bit FAT, %I64u files, %I64u folders\n", result->cluster_bytes / 1024,
               result->layout.bytes_per_entry * 8, result->files, result->directories);
        printf("  Clusters: %I64u in use of %I64u, %I64u lost, %I64u bad\n", result->used_clusters,
               result->layout.cluster_limit - 1, result->lost_clusters, result->bad_clusters);
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

// "free 12 clusters (192 KB), end the chains of 2 files": what a repair did or would do
void print_repairs(const FatxRepairResult *repair, bool done)
{
    printf(done ? "  Repaired:" : "  Would repair:");
    const char *separator = " ";
    if (repair->clusters_freed > 0)
    {
        printf("%sfree %I64u clusters (%I64u KB)", separator, repair->clusters_freed,
               repair->clusters_freed * repair->check.cluster_bytes / 1024);
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

// Prints the result; returns the exit code for this partition
int check_partition(FILE *file, uint64_t offset, uint64_t size, const char *label, Mode mode)
{
    printf("%s: %I64u MB at byte %I64u\n", label, size >> 20, offset);
    fflush(stdout); // Before the progress on stderr

    FileDevice device(file, offset);
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
            printf("  Nothing written: add --write to do it (copy the image first).\n");
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
        break; // Not checked: print_check() said why
    }

    delete repair;
    return code;
}

// check and repair: the partitions of a disk image, or one partition
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

    FILE *file = NULL;
    if (fopen_s(&file, path, mode == MODE_REPAIR_WRITE ? "r+b" : "rb") != 0 || file == NULL)
    {
        fprintf(stderr, "Can't open %s\n", path);
        return EXIT_FAILED;
    }
    const uint64_t image_size = file_size(file);

    XboxPartitionTable table;
    memset(&table, 0, sizeof(table));
    _fseeki64(file, 0, SEEK_SET);
    fread(&table, 1, sizeof(table), file);

    int code = EXIT_CLEAN;
    if (!manual && XboxPartitionTableManipulator::HasMagic(table))
    {
        // Whole disk image: the partitions in its table
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
            const int result = check_partition(file, start * HDD_SECTOR_SIZE, sectors * HDD_SECTOR_SIZE, label, mode);
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
        code = check_partition(file, offset, size, "Partition", mode);
    }

    fclose(file);
    return code;
}

// Path -> name the Xbox accepts, or empty
std::string xbox_name(const fs::path &path)
{
    const std::string name = path.filename().string();
    if (name.empty() || name.size() > FATX_MAX_NAME_LENGTH)
    {
        return std::string();
    }
    for (char c : name)
    {
        const unsigned char u = (unsigned char)c;
        if (u < 0x20 || u >= 0x7F || strchr("\"*/:<>?\\|", u) != NULL)
        {
            return std::string();
        }
    }
    return name;
}

class StreamSource : public FatxSource
{
public:
    explicit StreamSource(FILE *file) : m_file(file) {}
    bool Read(void *buffer, uint32_t length) override { return fread(buffer, 1, length, m_file) == length; }

private:
    FILE *m_file;
};

bool copy_tree(FatxWriter &writer, const fs::path &from, uint32_t dir, int *skipped)
{
    std::error_code error;
    for (const fs::directory_entry &item : fs::directory_iterator(from, error))
    {
        const std::string name = xbox_name(item.path());
        if (name.empty())
        {
            fprintf(stderr, "Skipped (name too long or not allowed): %s\n", item.path().string().c_str());
            (*skipped)++;
            continue;
        }

        if (item.is_directory(error))
        {
            uint32_t sub;
            if (!writer.MakeDirectory(dir, name.c_str(), &sub) || !copy_tree(writer, item.path(), sub, skipped))
            {
                return false;
            }
            continue;
        }

        const uint64_t size = item.file_size(error);
        if (error || size > 0xFFFFFFFFULL)
        {
            fprintf(stderr, "Skipped (4 GB or bigger, or unreadable): %s\n", item.path().string().c_str());
            (*skipped)++;
            continue;
        }
        FILE *file = NULL;
        if (_wfopen_s(&file, item.path().wstring().c_str(), L"rb") != 0 || file == NULL)
        {
            fprintf(stderr, "Skipped (can't open): %s\n", item.path().string().c_str());
            (*skipped)++;
            continue;
        }
        StreamSource source(file);
        const bool ok = writer.AddFile(dir, name.c_str(), (uint32_t)size, source);
        fclose(file);
        if (!ok)
        {
            fprintf(stderr, "Couldn't add %s (image full?)\n", item.path().string().c_str());
            return false;
        }
    }
    return !error;
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
    if (cluster_bytes != 0 && (cluster_bytes < 16 * 1024 || cluster_bytes > 1024 * 1024 ||
                               (cluster_bytes & (cluster_bytes - 1)) != 0))
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
    // Sparse where the file system allows: the unused clusters are never written
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
        ok = copy_tree(writer, fs::u8path(from), writer.RootCluster(), &skipped);
    }
    ok = writer.Flush() && ok;
    fclose(file);

    if (!ok)
    {
        fprintf(stderr, "Failed to make %s\n", path);
        return EXIT_FAILED;
    }
    printf("Made %s: %I64u MB, %u KB clusters, %u bit FAT", path, size >> 20, writer.ClusterBytes() / 1024,
           writer.Layout().bytes_per_entry * 8);
    if (skipped > 0)
    {
        printf(", %d items skipped", skipped);
    }
    printf(".\n");
    return EXIT_CLEAN;
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
    if (argc >= 2 && strcmp(argv[1], "make") == 0)
    {
        return command_make(argc - 2, argv + 2);
    }
    usage();
    return EXIT_FAILED;
}
