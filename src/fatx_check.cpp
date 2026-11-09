#include "fatx_check.hpp"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace
{

// FAT read in chunks this big (a multiple of 4 KB, so of the sector size)
const uint32_t FAT_CHUNK_BYTES = 0x10000;

// Progress of the check: FAT scan, directory walk, lost cluster scan
const int PROGRESS_SCAN_END = 15;
const int PROGRESS_WALK_END = 85;
// A repair's check takes this much, its own pass over the FAT the rest
const int PROGRESS_REPAIR_CHECK_SHARE = 80;

void format_message(char *out, size_t out_size, const char *format, va_list args)
{
#ifdef _XBOX
    _vsnprintf(out, out_size, format, args); // The XDK's runtime has no vsnprintf
#else
    vsnprintf(out, out_size, format, args);
#endif
    out[out_size - 1] = '\0';
}

char lower_ascii(char c)
{
    return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

// What the kernel accepts in a name: printable ASCII without these
bool is_valid_name_char(unsigned char c)
{
    return c >= 0x20 && c < 0x7F && strchr("\"*/:<>?\\|", c) == NULL;
}

// One name in the directory being read, for the duplicate check
typedef struct
{
    unsigned char length;
    char name[FATX_MAX_NAME_LENGTH];
} NameEntry;

// A safe repair found by the walk, see FatxRepair()
enum FixKind
{
    FIX_TRIM,       // End the chain after `keep` clusters, the rest is freed
    FIX_CLEAR_FIRST // Empty file with clusters: first cluster 0, the chain is freed
};

typedef struct
{
    FixKind kind;
    uint32_t first_cluster;
    uint32_t keep;
    uint64_t dirent_offset; // In the partition
} Fix;

// A directory waiting to be read
typedef struct
{
    uint32_t first_cluster;
    char *path; // "" for the root, else "\a\b"
} PendingDir;

class Checker
{
public:
    Checker(FatxReader &reader, uint64_t partition_bytes, FatxCheckResult *result, FatxProgress *progress)
        : m_reader(reader), m_partition_bytes(partition_bytes), m_result(result), m_progress(progress), m_last_percent(-1),
          m_referenced(NULL), m_fat_chunk(NULL), m_fat_chunk_offset(0), m_fat_chunk_length(0), m_fat_chunk_valid(false),
          m_fat_dirty(false), m_writer(NULL), m_write_failed(false), m_cluster(NULL), m_pending(NULL), m_pending_count(0),
          m_pending_capacity(0), m_names(NULL), m_name_count(0), m_name_capacity(0), m_fixes(NULL), m_fix_count(0),
          m_fix_capacity(0), m_media_wrong(false), m_out_of_memory(false), m_referenced_count(0),
          m_check_share(100), m_wrote(false)
    {
    }

    ~Checker()
    {
        free(m_referenced);
        free(m_fat_chunk);
        free(m_cluster);
        for (uint32_t i = 0; i < m_pending_count; i++)
        {
            free(m_pending[i].path);
        }
        free(m_pending);
        free(m_names);
        free(m_fixes);
    }

    bool Run()
    {
        if (!ReadHeader())
        {
            return false;
        }

        // A bit per cluster, the FAT a chunk at a time, one cluster to parse
        const uint64_t limit = m_result->layout.cluster_limit;
        m_referenced = (unsigned char *)calloc((size_t)((limit + 7) / 8), 1);
        m_fat_chunk = (unsigned char *)malloc(FAT_CHUNK_BYTES);
        m_cluster = (unsigned char *)malloc(m_result->cluster_bytes);
        if (m_referenced == NULL || m_fat_chunk == NULL || m_cluster == NULL)
        {
            m_result->status = FATX_CHECK_NO_MEMORY;
            return false;
        }

        if (!ScanFat() || !WalkDirectories() || !FindLostClusters())
        {
            m_result->status = m_out_of_memory ? FATX_CHECK_NO_MEMORY : FATX_CHECK_READ_ERROR;
            return false;
        }

        SetProgress(100);
        m_result->status = FATX_CHECK_DONE;
        return true;
    }

    // Before Run(), when Repair() follows: its share of the progress
    void ShareProgressWithRepair()
    {
        m_check_share = PROGRESS_REPAIR_CHECK_SHARE;
    }

    // After Run() found no errors. writer NULL: a dry run, only counts.
    // Order, so a power cut leaves at worst lost space: directory entries,
    // then chain ends (all written before anything is freed), then the
    // freed clusters.
    bool Repair(FatxDevice *writer, FatxRepairResult *repair)
    {
        m_writer = writer;

        for (uint32_t i = 0; i < m_fix_count; i++)
        {
            const Fix &fix = m_fixes[i];
            if (fix.kind == FIX_CLEAR_FIRST)
            {
                if (!ClearFirstCluster(fix.dirent_offset) || !Unmark(fix.first_cluster))
                {
                    return false;
                }
                repair->empty_files_cleared++;
                continue;
            }

            // The cluster to end at; what follows it is no longer the file's
            uint32_t last = fix.first_cluster;
            for (uint32_t kept = 1; kept < fix.keep; kept++)
            {
                if (!GetFat(last, &last))
                {
                    return false;
                }
            }
            uint32_t tail;
            if (!GetFat(last, &tail) || !SetFat(last, FATX_FAT_END) || !Unmark(tail))
            {
                return false;
            }
            repair->files_trimmed++;
        }
        if (!FlushFat())
        {
            return false;
        }

        // Lost clusters, and what was unmarked above
        const uint32_t limit = (uint32_t)m_result->layout.cluster_limit;
        for (uint32_t cluster = 1; cluster < limit; cluster++)
        {
            uint32_t value;
            if (!GetFat(cluster, &value))
            {
                return false;
            }
            if (value != FATX_FAT_FREE && value != FATX_FAT_BAD && !IsReferenced(cluster))
            {
                if (!SetFat(cluster, FATX_FAT_FREE))
                {
                    return false;
                }
                repair->clusters_freed++;
            }

            if ((cluster & 0xFFFF) == 0)
            {
                SetRawProgress(m_check_share + (int)((uint64_t)cluster * (100 - m_check_share) / limit));
            }
        }

        if (m_media_wrong)
        {
            if (!SetFat(0, FATX_FAT_MEDIA))
            {
                return false;
            }
            repair->media_fixed = true;
        }

        if (!FlushFat())
        {
            return false;
        }
        SetRawProgress(100);
        return true;
    }

    // Something was written before the repair stopped
    bool StoppedPartway() const { return m_wrote || m_write_failed; }

private:
    // A clean chain (it was walked to its end) back to unused in the bitmap.
    // In a dry run the FAT isn't changed, so this reads the chain as it is.
    bool Unmark(uint32_t cluster)
    {
        while (cluster >= 1 && cluster < m_result->layout.cluster_limit)
        {
            m_referenced[cluster / 8] &= (unsigned char)~(1 << (cluster % 8));
            if (!GetFat(cluster, &cluster))
            {
                return false;
            }
        }
        return true;
    }

    bool ClearFirstCluster(uint64_t dirent_offset)
    {
        if (m_writer == NULL)
        {
            return true;
        }
        // Entries are 64 bytes and aligned: never across a sector
        unsigned char sector[HDD_SECTOR_SIZE];
        const uint64_t sector_offset = dirent_offset - dirent_offset % HDD_SECTOR_SIZE;
        if (!m_reader.Read(sector_offset, sector, sizeof(sector)))
        {
            return false;
        }
        const uint32_t zero = 0;
        memcpy(sector + (dirent_offset - sector_offset) + offsetof(FatxDirent, first_cluster), &zero, 4);
        if (!m_writer->Write(sector_offset, sector, sizeof(sector)))
        {
            m_write_failed = true;
            return false;
        }
        m_wrote = true;
        return true;
    }

    // Through the chunk GetFat() holds; written back when it moves on
    bool SetFat(uint32_t cluster, uint32_t value)
    {
        if (m_writer == NULL)
        {
            return true;
        }
        uint32_t old;
        if (!GetFat(cluster, &old))
        {
            return false;
        }
        const uint64_t byte = (uint64_t)cluster * m_result->layout.bytes_per_entry;
        unsigned char *entry = m_fat_chunk + (byte - m_fat_chunk_offset);
        if (m_result->layout.bytes_per_entry == 2)
        {
            const uint16_t value16 = (uint16_t)value;
            memcpy(entry, &value16, 2);
        }
        else
        {
            memcpy(entry, &value, 4);
        }
        m_fat_dirty = true;
        return true;
    }

    bool FlushFat()
    {
        if (!m_fat_dirty)
        {
            return true;
        }
        if (!m_writer->Write(FATX_HEADER_BYTES + m_fat_chunk_offset, m_fat_chunk, m_fat_chunk_length))
        {
            m_write_failed = true;
            return false;
        }
        m_wrote = true;
        m_fat_dirty = false;
        return true;
    }

    bool AddFix(FixKind kind, uint32_t first_cluster, uint32_t keep, uint64_t dirent_offset)
    {
        if (m_fix_count == m_fix_capacity)
        {
            const uint32_t capacity = m_fix_capacity ? m_fix_capacity * 2 : 64;
            Fix *fixes = (Fix *)realloc(m_fixes, capacity * sizeof(Fix));
            if (fixes == NULL)
            {
                m_out_of_memory = true;
                return false;
            }
            m_fixes = fixes;
            m_fix_capacity = capacity;
        }
        Fix &fix = m_fixes[m_fix_count++];
        fix.kind = kind;
        fix.first_cluster = first_cluster;
        fix.keep = keep;
        fix.dirent_offset = dirent_offset;
        return true;
    }

    void Report(bool error, const char *format, ...)
    {
        if (error)
        {
            m_result->errors++;
        }
        else
        {
            m_result->warnings++;
        }

        if (m_result->message_count < (uint32_t)FATX_CHECK_MAX_MESSAGES)
        {
            FatxCheckMessage &message = m_result->messages[m_result->message_count++];
            message.error = error;
            va_list args;
            va_start(args, format);
            format_message(message.text, sizeof(message.text), format, args);
            va_end(args);
        }
    }

    // The check's 0 to 100, scaled to its share
    void SetProgress(int percent)
    {
        SetRawProgress(percent * m_check_share / 100);
    }

    void SetRawProgress(int percent)
    {
        if (m_progress != NULL && percent != m_last_percent)
        {
            m_last_percent = percent;
            m_progress->Update(percent);
        }
    }

    bool ReadHeader()
    {
        unsigned char header[HDD_SECTOR_SIZE];
        if (m_partition_bytes < FATX_HEADER_BYTES || !m_reader.Read(0, header, sizeof(header)))
        {
            m_result->status = FATX_CHECK_READ_ERROR;
            return false;
        }

        uint32_t signature, sectors_per_cluster, root_cluster;
        memcpy(&signature, header + FATX_SIGNATURE_OFFSET, 4);
        memcpy(&sectors_per_cluster, header + FATX_SECTORS_PER_CLUSTER_OFFSET, 4);
        memcpy(&root_cluster, header + FATX_ROOT_CLUSTER_OFFSET, 4);

        if (signature != FATX_signature)
        {
            m_result->status = FATX_CHECK_NOT_FATX;
            return false;
        }

        // A power of two up to 1 MB (the Xbox uses 16 KB and up)
        if (sectors_per_cluster == 0 || (sectors_per_cluster & (sectors_per_cluster - 1)) != 0 || sectors_per_cluster > 2048)
        {
            Report(true, "The cluster size in the header is invalid (%u sectors).", sectors_per_cluster);
            m_result->status = FATX_CHECK_BAD_HEADER;
            return false;
        }

        m_result->cluster_bytes = sectors_per_cluster * HDD_SECTOR_SIZE;
        m_result->root_cluster = root_cluster;
        m_result->layout = FatxLayoutFor(m_partition_bytes / HDD_SECTOR_SIZE, m_result->cluster_bytes);

        const FatxLayout &layout = m_result->layout;
        if (layout.cluster_limit < 2 || layout.cluster_limit > FATX_FAT_RESERVED)
        {
            Report(true, "The partition is too small or too big for its cluster size.");
            m_result->status = FATX_CHECK_BAD_HEADER;
            return false;
        }
        if (root_cluster < 1 || root_cluster >= layout.cluster_limit)
        {
            Report(true, "The root folder's cluster (%u) is outside the partition.", root_cluster);
            m_result->status = FATX_CHECK_BAD_HEADER;
            return false;
        }
        return true;
    }

    // As a 32 bit value, see FATX_FAT_END
    bool GetFat(uint32_t cluster, uint32_t *value)
    {
        const FatxLayout &layout = m_result->layout;
        const uint64_t byte = (uint64_t)cluster * layout.bytes_per_entry;
        const uint64_t chunk_offset = byte - byte % FAT_CHUNK_BYTES;

        if (!m_fat_chunk_valid || chunk_offset != m_fat_chunk_offset)
        {
            // The FAT is a whole number of 4 KB pages: so is the last chunk
            if (!FlushFat())
            {
                return false;
            }
            const uint64_t left = layout.bytes_per_fat - chunk_offset;
            const uint32_t length = (uint32_t)(left < FAT_CHUNK_BYTES ? left : FAT_CHUNK_BYTES);
            if (!m_reader.Read(FATX_HEADER_BYTES + chunk_offset, m_fat_chunk, length))
            {
                m_fat_chunk_valid = false;
                return false;
            }
            m_fat_chunk_offset = chunk_offset;
            m_fat_chunk_length = length;
            m_fat_chunk_valid = true;
        }

        const unsigned char *entry = m_fat_chunk + (byte - chunk_offset);
        if (layout.bytes_per_entry == 2)
        {
            uint16_t value16;
            memcpy(&value16, entry, 2);
            *value = (value16 >= 0xFFF0) ? (0xFFFF0000u | value16) : value16;
        }
        else
        {
            memcpy(value, entry, 4);
        }
        return true;
    }

    bool IsReferenced(uint32_t cluster) const
    {
        return (m_referenced[cluster / 8] & (1 << (cluster % 8))) != 0;
    }

    void MarkReferenced(uint32_t cluster)
    {
        m_referenced[cluster / 8] |= (unsigned char)(1 << (cluster % 8));
        m_referenced_count++;

        // The walk's share of the progress, by clusters reached
        if (m_result->used_clusters > 0 && (m_referenced_count & 0xFF) == 0)
        {
            const uint64_t done = m_referenced_count < m_result->used_clusters ? m_referenced_count : m_result->used_clusters;
            SetProgress(PROGRESS_SCAN_END + (int)(done * (PROGRESS_WALK_END - PROGRESS_SCAN_END) / m_result->used_clusters));
        }
    }

    // Counts the used and bad clusters (for the progress and the summary)
    bool ScanFat()
    {
        const uint32_t limit = (uint32_t)m_result->layout.cluster_limit;

        uint32_t media;
        if (!GetFat(0, &media))
        {
            return false;
        }
        if (media != FATX_FAT_MEDIA)
        {
            Report(false, "FAT entry 0 is 0x%08X, not the usual 0xFFFFFFF8.", media);
            m_media_wrong = true;
        }

        uint32_t bad_pointers = 0;
        for (uint32_t cluster = 1; cluster < limit; cluster++)
        {
            uint32_t value;
            if (!GetFat(cluster, &value))
            {
                return false;
            }
            if (value == FATX_FAT_BAD)
            {
                m_result->bad_clusters++;
            }
            else if (value != FATX_FAT_FREE)
            {
                m_result->used_clusters++;
                if (value < FATX_FAT_RESERVED && value >= limit)
                {
                    bad_pointers++;
                }
            }

            if ((cluster & 0xFFFF) == 0)
            {
                SetProgress((int)((uint64_t)cluster * PROGRESS_SCAN_END / limit));
            }
        }

        if (bad_pointers > 0)
        {
            Report(true, "%u FAT entries point outside the partition.", bad_pointers);
        }
        return true;
    }

    // Marks a chain's clusters. dir: also reads each cluster as directory
    // entries (subdirectories are queued). False only if reading failed or
    // memory ran out; problems with the chain are reported and end it.
    // *clean: it reached its end marker without a problem.
    bool FollowChain(uint32_t first, const char *path, bool dir, uint64_t *count, bool *clean = NULL)
    {
        const uint32_t limit = (uint32_t)m_result->layout.cluster_limit;
        bool dir_ended = false;
        uint32_t cluster = first;
        *count = 0;
        if (clean != NULL)
        {
            *clean = false;
        }

        while (true)
        {
            if (cluster < 1 || cluster >= limit)
            {
                Report(true, "%s: cluster %u is outside the partition.", PathOrRoot(path), cluster);
                return true;
            }
            if (IsReferenced(cluster))
            {
                Report(true, "%s: cluster %u is used twice (cross-linked or a loop).", PathOrRoot(path), cluster);
                return true;
            }
            MarkReferenced(cluster);
            (*count)++;

            if (dir && !dir_ended && !ReadDirectoryCluster(cluster, path, &dir_ended))
            {
                return false;
            }

            uint32_t next;
            if (!GetFat(cluster, &next))
            {
                return false;
            }
            if (next == FATX_FAT_END)
            {
                if (clean != NULL)
                {
                    *clean = true;
                }
                return true;
            }
            if (next == FATX_FAT_FREE)
            {
                Report(true, "%s: the chain runs into a free cluster after cluster %u.", PathOrRoot(path), cluster);
                return true;
            }
            if (next >= FATX_FAT_RESERVED)
            {
                Report(true, "%s: the chain hits marker 0x%08X after cluster %u.", PathOrRoot(path), next, cluster);
                return true;
            }
            cluster = next;
        }
    }

    static const char *PathOrRoot(const char *path)
    {
        return path[0] != '\0' ? path : "\\";
    }

    bool ReadDirectoryCluster(uint32_t cluster, const char *path, bool *ended)
    {
        const FatxLayout &layout = m_result->layout;
        const uint64_t offset = layout.data_offset + (uint64_t)(cluster - 1) * m_result->cluster_bytes;
        if (!m_reader.Read(offset, m_cluster, m_result->cluster_bytes))
        {
            return false;
        }

        const uint32_t entries = m_result->cluster_bytes / sizeof(FatxDirent);
        for (uint32_t i = 0; i < entries; i++)
        {
            FatxDirent entry;
            memcpy(&entry, m_cluster + i * sizeof(FatxDirent), sizeof(entry));

            if (entry.name_length == FATX_DIRENT_END || entry.name_length == FATX_DIRENT_END2)
            {
                *ended = true;
                return true;
            }
            if (entry.name_length == FATX_DIRENT_DELETED)
            {
                continue;
            }
            if (entry.name_length > FATX_MAX_NAME_LENGTH)
            {
                Report(true, "%s: entry %u in cluster %u has a bad name length (%u).", PathOrRoot(path), i, cluster,
                       entry.name_length);
                continue;
            }
            if (!CheckEntry(entry, path, offset + i * sizeof(FatxDirent)))
            {
                return false;
            }
        }
        return true;
    }

    bool CheckEntry(const FatxDirent &entry, const char *path, uint64_t dirent_offset)
    {
        // Printable for the messages; invalid characters become '?'
        char name[FATX_MAX_NAME_LENGTH + 1];
        bool valid_name = true;
        for (unsigned int i = 0; i < entry.name_length; i++)
        {
            const unsigned char c = (unsigned char)entry.name[i];
            valid_name = valid_name && is_valid_name_char(c);
            name[i] = (c >= 0x20 && c < 0x7F) ? (char)c : '?';
        }
        name[entry.name_length] = '\0';

        const size_t path_length = strlen(path) + 1 + entry.name_length + 1;
        char *full = (char *)malloc(path_length);
        if (full == NULL)
        {
            m_out_of_memory = true;
            return false;
        }
        const size_t parent_length = strlen(path);
        memcpy(full, path, parent_length);
        full[parent_length] = '\\';
        memcpy(full + parent_length + 1, name, entry.name_length + 1);

        if (!valid_name)
        {
            Report(false, "%s: the name has characters the Xbox doesn't allow.", full);
        }
        if (!AddName(entry, full))
        {
            free(full);
            return false;
        }

        if (entry.attributes & FATX_ATTR_DIRECTORY)
        {
            m_result->directories++;
            if (entry.first_cluster == 0)
            {
                Report(true, "%s: folder without a cluster.", full);
                free(full);
                return true;
            }
            return PushDir(entry.first_cluster, full); // Keeps full
        }

        m_result->files++;
        const uint64_t needed = ((uint64_t)entry.file_size + m_result->cluster_bytes - 1) / m_result->cluster_bytes;
        bool ok = true;
        if (entry.first_cluster == 0)
        {
            if (needed > 0)
            {
                Report(true, "%s: %u bytes but no clusters.", full, entry.file_size);
            }
        }
        else
        {
            uint64_t count;
            bool clean;
            ok = FollowChain(entry.first_cluster, full, false, &count, &clean);
            if (ok && count < needed)
            {
                Report(true, "%s: %u bytes need %I64u clusters, the chain has %I64u.", full, entry.file_size, needed,
                       count);
            }
            else if (ok && count > needed)
            {
                Report(false, "%s: %I64u clusters more than its size needs.", full, count - needed);
                if (clean)
                {
                    ok = (needed == 0) ? AddFix(FIX_CLEAR_FIRST, entry.first_cluster, 0, dirent_offset)
                                       : AddFix(FIX_TRIM, entry.first_cluster, (uint32_t)needed, dirent_offset);
                }
            }
        }
        free(full);
        return ok;
    }

    // Two entries with the same name (the Xbox ignores case) in one directory
    bool AddName(const FatxDirent &entry, const char *full)
    {
        for (uint32_t i = 0; i < m_name_count; i++)
        {
            const NameEntry &other = m_names[i];
            if (other.length != entry.name_length)
            {
                continue;
            }
            unsigned int j = 0;
            while (j < other.length && lower_ascii(other.name[j]) == lower_ascii(entry.name[j]))
            {
                j++;
            }
            if (j == other.length)
            {
                Report(true, "%s: the folder has this name twice.", full);
                break;
            }
        }

        if (m_name_count == m_name_capacity)
        {
            const uint32_t capacity = m_name_capacity ? m_name_capacity * 2 : 256;
            NameEntry *names = (NameEntry *)realloc(m_names, capacity * sizeof(NameEntry));
            if (names == NULL)
            {
                m_out_of_memory = true;
                return false;
            }
            m_names = names;
            m_name_capacity = capacity;
        }
        m_names[m_name_count].length = entry.name_length;
        memcpy(m_names[m_name_count].name, entry.name, FATX_MAX_NAME_LENGTH);
        m_name_count++;
        return true;
    }

    // Takes path (freed when done)
    bool PushDir(uint32_t first_cluster, char *path)
    {
        if (m_pending_count == m_pending_capacity)
        {
            const uint32_t capacity = m_pending_capacity ? m_pending_capacity * 2 : 64;
            PendingDir *pending = (PendingDir *)realloc(m_pending, capacity * sizeof(PendingDir));
            if (pending == NULL)
            {
                free(path);
                m_out_of_memory = true;
                return false;
            }
            m_pending = pending;
            m_pending_capacity = capacity;
        }
        m_pending[m_pending_count].first_cluster = first_cluster;
        m_pending[m_pending_count].path = path;
        m_pending_count++;
        return true;
    }

    // Depth first, with a list instead of recursion (the Xbox stack is small)
    bool WalkDirectories()
    {
        char *root = (char *)malloc(1);
        if (root == NULL)
        {
            m_out_of_memory = true;
            return false;
        }
        root[0] = '\0';
        if (!PushDir(m_result->root_cluster, root))
        {
            return false;
        }

        while (m_pending_count > 0)
        {
            PendingDir dir = m_pending[--m_pending_count];
            m_name_count = 0;

            uint64_t count;
            const bool ok = FollowChain(dir.first_cluster, dir.path, true, &count);
            free(dir.path);
            if (!ok)
            {
                return false;
            }
        }

        SetProgress(PROGRESS_WALK_END);
        return true;
    }

    bool FindLostClusters()
    {
        const uint32_t limit = (uint32_t)m_result->layout.cluster_limit;
        for (uint32_t cluster = 1; cluster < limit; cluster++)
        {
            uint32_t value;
            if (!GetFat(cluster, &value))
            {
                return false;
            }
            if (value != FATX_FAT_FREE && value != FATX_FAT_BAD && !IsReferenced(cluster))
            {
                m_result->lost_clusters++;
            }

            if ((cluster & 0xFFFF) == 0)
            {
                SetProgress(PROGRESS_WALK_END + (int)((uint64_t)cluster * (100 - PROGRESS_WALK_END) / limit));
            }
        }

        if (m_result->lost_clusters > 0)
        {
            Report(false, "%I64u clusters are marked used, but no file uses them (lost space).", m_result->lost_clusters);
        }
        return true;
    }

    FatxReader &m_reader;
    uint64_t m_partition_bytes;
    FatxCheckResult *m_result;
    FatxProgress *m_progress;
    int m_last_percent;

    unsigned char *m_referenced; // A bit per cluster reached from the root
    unsigned char *m_fat_chunk;
    uint64_t m_fat_chunk_offset; // In the FAT
    uint32_t m_fat_chunk_length;
    bool m_fat_chunk_valid;
    bool m_fat_dirty;     // Changed by SetFat(), not written yet
    FatxDevice *m_writer; // Repair only, NULL for a dry run
    bool m_write_failed;
    unsigned char *m_cluster; // The directory cluster being read

    PendingDir *m_pending;
    uint32_t m_pending_count;
    uint32_t m_pending_capacity;
    NameEntry *m_names; // In the directory being read
    uint32_t m_name_count;
    uint32_t m_name_capacity;

    Fix *m_fixes;
    uint32_t m_fix_count;
    uint32_t m_fix_capacity;
    bool m_media_wrong;

    bool m_out_of_memory;
    uint64_t m_referenced_count;
    int m_check_share; // Of the progress, see ShareProgressWithRepair()
    bool m_wrote;
};

} // namespace

bool FatxCheck(FatxReader &reader, uint64_t partition_bytes, FatxCheckResult *result, FatxProgress *progress)
{
    memset(result, 0, sizeof(*result));
    Checker checker(reader, partition_bytes, result, progress);
    return checker.Run();
}

bool FatxRepair(FatxDevice &device, uint64_t partition_bytes, FatxRepairResult *repair, bool write, FatxProgress *progress)
{
    memset(repair, 0, sizeof(*repair));
    Checker checker(device, partition_bytes, &repair->check, progress);
    checker.ShareProgressWithRepair();
    if (!checker.Run())
    {
        repair->status = FATX_REPAIR_NOT_CHECKED;
        return false;
    }
    if (repair->check.errors > 0)
    {
        repair->status = FATX_REPAIR_HAS_ERRORS;
        return false;
    }

    if (!checker.Repair(write ? &device : NULL, repair))
    {
        repair->status = checker.StoppedPartway() ? FATX_REPAIR_STOPPED_PARTWAY : FATX_REPAIR_NOT_CHECKED;
        return false;
    }

    const bool any = repair->files_trimmed > 0 || repair->empty_files_cleared > 0 || repair->clusters_freed > 0 ||
                     repair->media_fixed;
    repair->status = any ? FATX_REPAIR_DONE : FATX_REPAIR_NOTHING_TO_DO;
    repair->written = write && any;
    return true;
}
