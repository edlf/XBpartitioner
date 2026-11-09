#include "fatx_resize.hpp"
#include <stdlib.h>
#include <string.h>

namespace
{

// The journal lives in the header's last sector, which FATX doesn't use
const uint64_t JOURNAL_SECTOR = FATX_HEADER_BYTES - HDD_SECTOR_SIZE;
const uint32_t JOURNAL_OFFSET = 0x100; // Within that sector
const char JOURNAL_MAGIC[8] = {'X', 'B', 'P', 'R', 'S', 'Z', '0', '1'};

// Clusters moved per batch
const uint32_t MOVE_BATCH = 4096;

const uint32_t FAT_CHUNK_BYTES = 0x10000;

enum JournalPhase
{
    PHASE_FAT = 1,   // units_done counts FAT pieces written
    PHASE_DIRS = 2,  // units_done counts folder clusters, in walk order
    PHASE_TABLE = 3  // All renumbered; record the size, restore the signature
};

#pragma pack(push, 1)
typedef struct
{
    char magic[8];
    uint64_t old_lba_size;
    uint64_t new_lba_size;
    int64_t shift;
    uint32_t cluster_bytes;
    uint32_t phase;
    uint32_t root_cluster; // New numbering
    uint64_t units_done;
    uint64_t scratch;      // Byte offset of the spare cluster
    uint32_t pending;      // The spare cluster holds data still to be copied to pending_target
    uint32_t pending_length;
    uint64_t pending_target;
    uint32_t pending_checksum;
    uint32_t checksum;     // Covers the fields above
} Journal;
#pragma pack(pop)

static_assert(JOURNAL_OFFSET + sizeof(Journal) <= HDD_SECTOR_SIZE, "The journal must fit its sector!");

uint32_t fnv1a(const void *data, size_t length)
{
    const unsigned char *bytes = (const unsigned char *)data;
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < length; i++)
    {
        hash = (hash ^ bytes[i]) * 16777619u;
    }
    return hash;
}

bool journal_valid(const Journal &journal)
{
    return memcmp(journal.magic, JOURNAL_MAGIC, sizeof(JOURNAL_MAGIC)) == 0 &&
           journal.checksum == fnv1a(&journal, offsetof(Journal, checksum));
}

uint32_t entry_value(const unsigned char *entry, uint32_t bytes_per_entry)
{
    if (bytes_per_entry == 2)
    {
        uint16_t value16;
        memcpy(&value16, entry, 2);
        return (value16 >= 0xFFF0) ? (0xFFFF0000u | value16) : value16;
    }
    uint32_t value;
    memcpy(&value, entry, 4);
    return value;
}

void set_entry_value(unsigned char *entry, uint32_t bytes_per_entry, uint32_t value)
{
    if (bytes_per_entry == 2)
    {
        const uint16_t value16 = (uint16_t)value;
        memcpy(entry, &value16, 2);
    }
    else
    {
        memcpy(entry, &value, 4);
    }
}

// Caches one chunk of the FAT, writing it back when moving on or on Flush()
class FatCache
{
public:
    explicit FatCache(FatxDevice &device)
        : m_device(device), m_chunk(NULL), m_offset(0), m_length(0), m_valid(false), m_dirty(false)
    {
        memset(&m_layout, 0, sizeof(m_layout));
    }
    ~FatCache() { free(m_chunk); }

    bool Init()
    {
        m_chunk = (unsigned char *)malloc(FAT_CHUNK_BYTES);
        return m_chunk != NULL;
    }

    // Switches layout, writing back anything pending
    bool Use(const FatxLayout &layout)
    {
        if (!Flush())
        {
            return false;
        }
        m_layout = layout;
        m_valid = false;
        return true;
    }

    bool Get(uint32_t cluster, uint32_t *value)
    {
        unsigned char *entry;
        if (!Load(cluster, &entry))
        {
            return false;
        }
        *value = entry_value(entry, m_layout.bytes_per_entry);
        return true;
    }

    bool Set(uint32_t cluster, uint32_t value)
    {
        unsigned char *entry;
        if (!Load(cluster, &entry))
        {
            return false;
        }
        set_entry_value(entry, m_layout.bytes_per_entry, value);
        m_dirty = true;
        return true;
    }

    bool Flush()
    {
        if (!m_dirty)
        {
            return true;
        }
        if (!m_device.Write(FATX_HEADER_BYTES + m_offset, m_chunk, m_length))
        {
            return false;
        }
        m_dirty = false;
        return true;
    }

private:
    bool Load(uint32_t cluster, unsigned char **entry)
    {
        const uint64_t byte = (uint64_t)cluster * m_layout.bytes_per_entry;
        const uint64_t offset = byte - byte % FAT_CHUNK_BYTES;
        if (!m_valid || offset != m_offset)
        {
            if (!Flush())
            {
                return false;
            }
            const uint64_t left = m_layout.bytes_per_fat - offset;
            m_length = (uint32_t)(left < FAT_CHUNK_BYTES ? left : FAT_CHUNK_BYTES);
            if (!m_device.Read(FATX_HEADER_BYTES + offset, m_chunk, m_length))
            {
                m_valid = false;
                return false;
            }
            m_offset = offset;
            m_valid = true;
        }
        *entry = m_chunk + (byte - offset);
        return true;
    }

    FatxDevice &m_device;
    FatxLayout m_layout;
    unsigned char *m_chunk;
    uint64_t m_offset;
    uint32_t m_length;
    bool m_valid;
    bool m_dirty;
};

// Maps a step's 0 to 100 onto part of the overall progress
class PartProgress : public FatxProgress
{
public:
    PartProgress(FatxProgress *whole, int from, int to) : m_whole(whole), m_from(from), m_to(to) {}
    void Update(int percent)
    {
        if (m_whole != NULL)
        {
            m_whole->Update(m_from + percent * (m_to - m_from) / 100);
        }
    }

private:
    FatxProgress *m_whole;
    int m_from;
    int m_to;
};

// What points at a chain's first cluster
enum OwnerKind
{
    OWNER_HEADER, // The root folder
    OWNER_DIRENT  // A folder entry
};

typedef struct
{
    uint32_t first_cluster;
    uint64_t dirent; // Entry offset, 0 for the root
} PendingDir;

class Resizer
{
public:
    Resizer(FatxDevice &device, FatxResizeTable &table, FatxMoveResult *result, FatxProgress *progress)
        : m_device(device), m_table(table), m_result(result), m_progress(progress), m_fat(device), m_cluster(NULL),
          m_copy(NULL), m_unit(NULL), m_old(NULL), m_cluster_bytes(0), m_shift(0), m_keep_begin(0), m_keep_end(0),
          m_hint(0), m_pending(NULL), m_pending_count(0), m_pending_capacity(0), m_chain(NULL), m_chain_capacity(0),
          m_moved_from(NULL), m_moved_to(NULL)
    {
        memset(&m_old_layout, 0, sizeof(m_old_layout));
        memset(&m_new_layout, 0, sizeof(m_new_layout));
        memset(&m_journal, 0, sizeof(m_journal));
    }

    ~Resizer()
    {
        free(m_cluster);
        free(m_copy);
        free(m_unit);
        free(m_old);
        free(m_pending);
        free(m_chain);
        free(m_moved_from);
        free(m_moved_to);
    }

    bool Resize(uint64_t old_lba, uint64_t requested_lba, bool write)
    {
        uint32_t signature, sectors_per_cluster, root;
        if (!ReadHeaderFields(&signature, &sectors_per_cluster, &root))
        {
            return Fail(FATX_MOVE_NOT_CHECKED);
        }
        if (signature == FATX_RESIZING_SIGNATURE)
        {
            return Fail(FATX_MOVE_INTERRUPTED);
        }
        if (signature != FATX_signature)
        {
            return Fail(FATX_MOVE_NOT_FATX);
        }
        if (sectors_per_cluster == 0 || (sectors_per_cluster & (sectors_per_cluster - 1)) != 0 || sectors_per_cluster > 2048)
        {
            return Fail(FATX_MOVE_NOT_CHECKED);
        }

        m_cluster_bytes = sectors_per_cluster * HDD_SECTOR_SIZE;
        m_result->cluster_bytes = m_cluster_bytes;
        m_result->old_lba_size = old_lba;
        const uint64_t new_lba = FatxMoveResizeTarget(old_lba, m_cluster_bytes, requested_lba);
        m_result->new_lba_size = new_lba;
        if (new_lba == 0)
        {
            const FatxLayout asked = FatxLayoutFor(requested_lba, m_cluster_bytes);
            const FatxLayout now = FatxLayoutFor(old_lba, m_cluster_bytes);
            return Fail(asked.cluster_limit < 2 || asked.bytes_per_entry == now.bytes_per_entry ? FATX_MOVE_TOO_SMALL
                                                                                                : FATX_MOVE_ENTRY_SIZE);
        }
        if (new_lba > old_lba && ClusterSizeKbFor(new_lba) > m_cluster_bytes / 1024)
        {
            return Fail(FATX_MOVE_CLUSTER_SIZE);
        }

        SetLayouts(old_lba, new_lba);
        m_result->shift = m_shift;
        if (m_shift == 0)
        {
            return Fail(FATX_MOVE_SAME_FAT);
        }
        if (m_keep_end <= m_keep_begin)
        {
            return Fail(FATX_MOVE_TOO_SMALL);
        }

        // Step 1
        PartProgress repair_progress(m_progress, 0, 20);
        FatxRepair(m_device, old_lba * HDD_SECTOR_SIZE, &m_result->repair, write, &repair_progress);
        switch (m_result->repair.status)
        {
        case FATX_REPAIR_DONE:
        case FATX_REPAIR_NOTHING_TO_DO:
            break;
        case FATX_REPAIR_HAS_ERRORS:
            return Fail(FATX_MOVE_HAS_ERRORS);
        case FATX_REPAIR_STOPPED_PARTWAY:
            return Fail(FATX_MOVE_STOPPED_SAFE);
        default:
            return Fail(FATX_MOVE_NOT_CHECKED);
        }

        if (!Allocate() || !m_fat.Use(m_old_layout) || !CountRoom())
        {
            return Fail(FATX_MOVE_NOT_CHECKED);
        }
        // Plus one spare cluster for renumbering
        if (m_result->free_clusters < m_result->clusters_to_move + 1)
        {
            return Fail(FATX_MOVE_NO_ROOM);
        }
        if (!write)
        {
            m_result->status = FATX_MOVE_PLANNED;
            return true;
        }

        // Step 2. Until the journal exists, the old file system stays valid.
        if (!MoveAll() || !m_fat.Flush() || !m_device.Sync() || !EvacuatedAreEmpty())
        {
            return Fail(FATX_MOVE_STOPPED_SAFE);
        }
        SetProgress(50);

        // Leave the spare cluster free, and empty the cache so no stale old
        // FAT chunk can be written over the new FAT later.
        uint32_t scratch;
        if (!FindFree(&scratch, false) || !m_fat.Use(m_old_layout) ||
            !ReadHeaderFields(&signature, &sectors_per_cluster, &root))
        {
            return Fail(FATX_MOVE_STOPPED_SAFE);
        }

        // Step 3. Journal first, then the signature that blocks mounting.
        memcpy(m_journal.magic, JOURNAL_MAGIC, sizeof(JOURNAL_MAGIC));
        m_journal.old_lba_size = old_lba;
        m_journal.new_lba_size = new_lba;
        m_journal.shift = m_shift;
        m_journal.cluster_bytes = m_cluster_bytes;
        m_journal.phase = PHASE_FAT;
        m_journal.root_cluster = (uint32_t)((int64_t)root - m_shift);
        m_journal.units_done = 0;
        m_journal.scratch = ClusterOffset(m_old_layout, scratch);
        m_journal.pending = 0;
        if (!WriteJournal())
        {
            return Fail(FATX_MOVE_STOPPED_SAFE);
        }
        return Finish();
    }

    bool Resume()
    {
        uint32_t signature, sectors_per_cluster, root;
        if (!ReadHeaderFields(&signature, &sectors_per_cluster, &root) || !ReadJournal(&m_journal))
        {
            return Fail(FATX_MOVE_NOT_CHECKED);
        }
        if (!journal_valid(m_journal))
        {
            return Fail(FATX_MOVE_NOTHING_TO_RESUME);
        }

        m_cluster_bytes = m_journal.cluster_bytes;
        m_result->cluster_bytes = m_cluster_bytes;
        m_result->old_lba_size = m_journal.old_lba_size;
        m_result->new_lba_size = m_journal.new_lba_size;
        SetLayouts(m_journal.old_lba_size, m_journal.new_lba_size);
        m_result->shift = m_shift;
        if (m_shift != m_journal.shift || !Allocate())
        {
            return Fail(FATX_MOVE_NOT_CHECKED);
        }
        return Finish();
    }

private:
    bool Fail(FatxMoveStatus status)
    {
        m_result->status = status;
        return false;
    }

    void SetProgress(int percent)
    {
        if (m_progress != NULL)
        {
            m_progress->Update(percent);
        }
    }

    void SetLayouts(uint64_t old_lba, uint64_t new_lba)
    {
        m_old_layout = FatxLayoutFor(old_lba, m_cluster_bytes);
        m_new_layout = FatxLayoutFor(new_lba, m_cluster_bytes);
        m_shift = ((int64_t)m_new_layout.bytes_per_fat - (int64_t)m_old_layout.bytes_per_fat) / (int64_t)m_cluster_bytes;

        // Clusters that stay put, in old numbering. Growing loses the first
        // m_shift to the FAT; shrinking loses the end.
        if (m_shift > 0)
        {
            m_keep_begin = (uint32_t)m_shift + 1;
            m_keep_end = (uint32_t)m_old_layout.cluster_limit;
        }
        else
        {
            m_keep_begin = 1;
            const int64_t end = (int64_t)m_new_layout.cluster_limit + m_shift;
            m_keep_end = (end > 1) ? (uint32_t)end : 1;
            if (m_keep_end > m_old_layout.cluster_limit)
            {
                m_keep_end = (uint32_t)m_old_layout.cluster_limit;
            }
        }
        m_hint = m_keep_begin;
    }

    bool Allocate()
    {
        m_cluster = (unsigned char *)malloc(m_cluster_bytes);
        m_copy = (unsigned char *)malloc(m_cluster_bytes);
        m_unit = (unsigned char *)malloc(m_cluster_bytes);
        m_old = (unsigned char *)malloc(m_cluster_bytes + 2 * HDD_SECTOR_SIZE);
        m_moved_from = (uint32_t *)malloc(MOVE_BATCH * sizeof(uint32_t));
        m_moved_to = (uint32_t *)malloc(MOVE_BATCH * sizeof(uint32_t));
        return m_cluster != NULL && m_copy != NULL && m_unit != NULL && m_old != NULL && m_moved_from != NULL &&
               m_moved_to != NULL && m_fat.Init();
    }

    uint64_t ClusterOffset(const FatxLayout &layout, uint32_t cluster) const
    {
        return layout.data_offset + (uint64_t)(cluster - 1) * m_cluster_bytes;
    }

    bool IsKept(uint32_t cluster) const
    {
        return cluster >= m_keep_begin && cluster < m_keep_end;
    }

    // ---- Sectors

    bool PatchSector(uint64_t offset, uint32_t at, const void *data, uint32_t length)
    {
        unsigned char sector[HDD_SECTOR_SIZE];
        if (!m_device.Read(offset, sector, sizeof(sector)))
        {
            return false;
        }
        memcpy(sector + at, data, length);
        return m_device.Write(offset, sector, sizeof(sector));
    }

    bool ReadHeaderFields(uint32_t *signature, uint32_t *sectors_per_cluster, uint32_t *root)
    {
        unsigned char sector[HDD_SECTOR_SIZE];
        if (!m_device.Read(0, sector, sizeof(sector)))
        {
            return false;
        }
        memcpy(signature, sector + FATX_SIGNATURE_OFFSET, 4);
        memcpy(sectors_per_cluster, sector + FATX_SECTORS_PER_CLUSTER_OFFSET, 4);
        memcpy(root, sector + FATX_ROOT_CLUSTER_OFFSET, 4);
        return true;
    }

    bool WriteHeaderField(uint32_t offset, uint32_t value)
    {
        return PatchSector(0, offset, &value, 4) && m_device.Sync();
    }

    bool ReadJournal(Journal *journal)
    {
        unsigned char sector[HDD_SECTOR_SIZE];
        if (!m_device.Read(JOURNAL_SECTOR, sector, sizeof(sector)))
        {
            return false;
        }
        memcpy(journal, sector + JOURNAL_OFFSET, sizeof(*journal));
        return true;
    }

    bool WriteJournal()
    {
        m_journal.checksum = fnv1a(&m_journal, offsetof(Journal, checksum));
        return PatchSector(JOURNAL_SECTOR, JOURNAL_OFFSET, &m_journal, sizeof(m_journal)) && m_device.Sync();
    }

    bool ClearJournal()
    {
        unsigned char blank[sizeof(Journal)];
        memset(blank, 0xFF, sizeof(blank));
        return PatchSector(JOURNAL_SECTOR, JOURNAL_OFFSET, blank, sizeof(blank)) && m_device.Sync();
    }

    // ---- Step 2: moves

    bool CountRoom()
    {
        const uint32_t limit = (uint32_t)m_old_layout.cluster_limit;
        for (uint32_t cluster = 1; cluster < limit; cluster++)
        {
            uint32_t value;
            if (!m_fat.Get(cluster, &value))
            {
                return false;
            }
            if (IsKept(cluster))
            {
                m_result->free_clusters += (value == FATX_FAT_FREE) ? 1 : 0;
            }
            else if (value != FATX_FAT_FREE && value != FATX_FAT_BAD)
            {
                m_result->clusters_to_move++;
            }
        }
        return true;
    }

    // Finds a free cluster that stays put, optionally marking it used
    bool FindFree(uint32_t *cluster, bool mark)
    {
        const uint32_t count = m_keep_end - m_keep_begin;
        for (uint32_t tried = 0; tried < count; tried++)
        {
            const uint32_t candidate = m_hint;
            m_hint = (m_hint + 1 < m_keep_end) ? m_hint + 1 : m_keep_begin;
            uint32_t value;
            if (!m_fat.Get(candidate, &value))
            {
                return false;
            }
            if (value == FATX_FAT_FREE)
            {
                *cluster = candidate;
                return !mark || m_fat.Set(candidate, FATX_FAT_END);
            }
        }
        return false;
    }

    bool CopyCluster(uint32_t from, uint32_t to)
    {
        return m_device.Read(ClusterOffset(m_old_layout, from), m_copy, m_cluster_bytes) &&
               m_device.Write(ClusterOffset(m_old_layout, to), m_copy, m_cluster_bytes);
    }

    bool SetOwner(OwnerKind kind, uint64_t dirent, uint32_t cluster)
    {
        if (kind == OWNER_HEADER)
        {
            return WriteHeaderField(FATX_ROOT_CLUSTER_OFFSET, cluster);
        }
        // Entries are aligned, so never span sectors
        const uint64_t sector = dirent - dirent % HDD_SECTOR_SIZE;
        return PatchSector(sector, (uint32_t)(dirent - sector) + offsetof(FatxDirent, first_cluster), &cluster, 4);
    }

    // Reads a chain into m_chain (the check found it intact)
    bool ReadChain(uint32_t first, uint32_t *count)
    {
        const uint32_t limit = (uint32_t)m_old_layout.cluster_limit;
        *count = 0;
        uint32_t cluster = first;
        while (true)
        {
            if (cluster < 1 || cluster >= limit || *count >= limit)
            {
                return false;
            }
            if (*count == m_chain_capacity)
            {
                const uint32_t capacity = m_chain_capacity ? m_chain_capacity * 2 : 1024;
                uint32_t *chain = (uint32_t *)realloc(m_chain, capacity * sizeof(uint32_t));
                if (chain == NULL)
                {
                    return false;
                }
                m_chain = chain;
                m_chain_capacity = capacity;
            }
            m_chain[(*count)++] = cluster;

            uint32_t next;
            if (!m_fat.Get(cluster, &next))
            {
                return false;
            }
            if (next == FATX_FAT_END)
            {
                return true;
            }
            if (next == FATX_FAT_FREE || next >= FATX_FAT_RESERVED)
            {
                return false;
            }
            cluster = next;
        }
    }

    // Moves a chain's clusters out of the way, leaving their new locations in
    // m_chain. For each run: write and link the copies, repoint whatever
    // pointed at the run, then free the originals, flushing between steps.
    bool MoveChain(uint32_t first, OwnerKind kind, uint64_t dirent, uint32_t *count)
    {
        if (!ReadChain(first, count))
        {
            return false;
        }

        uint32_t *moved_from = m_moved_from;
        uint32_t *moved_to = m_moved_to;
        uint32_t i = 0;
        while (i < *count)
        {
            if (IsKept(m_chain[i]))
            {
                i++;
                continue;
            }

            uint32_t run = 0;
            while (i + run < *count && !IsKept(m_chain[i + run]) && run < MOVE_BATCH)
            {
                moved_from[run] = m_chain[i + run];
                if (!FindFree(&moved_to[run], true) || !CopyCluster(moved_from[run], moved_to[run]))
                {
                    return false;
                }
                run++;
            }

            // Link the copies
            for (uint32_t t = 0; t < run; t++)
            {
                const uint32_t next = (t + 1 < run) ? moved_to[t + 1]
                                    : (i + run < *count) ? m_chain[i + run] : FATX_FAT_END;
                if (!m_fat.Set(moved_to[t], next))
                {
                    return false;
                }
            }
            if (!m_fat.Flush() || !m_device.Sync())
            {
                return false;
            }

            // Repoint whatever led to the run
            if (i == 0)
            {
                if (!SetOwner(kind, dirent, moved_to[0]))
                {
                    return false;
                }
            }
            else if (!m_fat.Set(m_chain[i - 1], moved_to[0]))
            {
                return false;
            }
            if (!m_fat.Flush() || !m_device.Sync())
            {
                return false;
            }

            for (uint32_t t = 0; t < run; t++)
            {
                if (!m_fat.Set(moved_from[t], FATX_FAT_FREE))
                {
                    return false;
                }
                m_chain[i + t] = moved_to[t];
            }
            if (!m_fat.Flush() || !m_device.Sync())
            {
                return false;
            }

            m_result->clusters_moved += run;
            if (m_result->clusters_to_move > 0)
            {
                SetProgress(20 + (int)(m_result->clusters_moved * 30 / m_result->clusters_to_move));
            }
            i += run;
        }
        return true;
    }

    bool PushDir(uint32_t first_cluster, uint64_t dirent)
    {
        if (m_pending_count == m_pending_capacity)
        {
            const uint32_t capacity = m_pending_capacity ? m_pending_capacity * 2 : 64;
            PendingDir *pending = (PendingDir *)realloc(m_pending, capacity * sizeof(PendingDir));
            if (pending == NULL)
            {
                return false;
            }
            m_pending = pending;
            m_pending_capacity = capacity;
        }
        m_pending[m_pending_count].first_cluster = first_cluster;
        m_pending[m_pending_count].dirent = dirent;
        m_pending_count++;
        return true;
    }

    // Moves every chain, walking from the root
    bool MoveAll()
    {
        uint32_t signature, sectors_per_cluster, root;
        if (!ReadHeaderFields(&signature, &sectors_per_cluster, &root) || !PushDir(root, 0))
        {
            return false;
        }

        const uint32_t entries = m_cluster_bytes / sizeof(FatxDirent);
        while (m_pending_count > 0)
        {
            const PendingDir dir = m_pending[--m_pending_count];
            uint32_t count;
            if (!MoveChain(dir.first_cluster, dir.dirent == 0 ? OWNER_HEADER : OWNER_DIRENT, dir.dirent, &count))
            {
                return false;
            }

            // MoveChain() reuses m_chain, so keep a copy
            uint32_t *clusters = (uint32_t *)malloc(count * sizeof(uint32_t));
            if (clusters == NULL)
            {
                return false;
            }
            memcpy(clusters, m_chain, count * sizeof(uint32_t));

            bool ended = false;
            bool ok = true;
            for (uint32_t c = 0; c < count && !ended && ok; c++)
            {
                const uint64_t offset = ClusterOffset(m_old_layout, clusters[c]);
                ok = m_device.Read(offset, m_cluster, m_cluster_bytes);
                for (uint32_t e = 0; e < entries && ok; e++)
                {
                    FatxDirent entry;
                    memcpy(&entry, m_cluster + e * sizeof(FatxDirent), sizeof(entry));
                    if (entry.name_length == FATX_DIRENT_END || entry.name_length == FATX_DIRENT_END2)
                    {
                        ended = true;
                        break;
                    }
                    if (entry.name_length == FATX_DIRENT_DELETED || entry.name_length > FATX_MAX_NAME_LENGTH ||
                        entry.first_cluster == 0)
                    {
                        continue;
                    }
                    const uint64_t dirent = offset + e * sizeof(FatxDirent);
                    uint32_t file_count;
                    ok = (entry.attributes & FATX_ATTR_DIRECTORY) ? PushDir(entry.first_cluster, dirent)
                                                                  : MoveChain(entry.first_cluster, OWNER_DIRENT, dirent, &file_count);
                }
            }
            free(clusters);
            if (!ok)
            {
                return false;
            }
        }
        return true;
    }

    // Checks nothing is left in the way
    bool EvacuatedAreEmpty()
    {
        const uint32_t limit = (uint32_t)m_old_layout.cluster_limit;
        for (uint32_t cluster = 1; cluster < limit; cluster++)
        {
            if (IsKept(cluster))
            {
                continue;
            }
            uint32_t value;
            if (!m_fat.Get(cluster, &value) || (value != FATX_FAT_FREE && value != FATX_FAT_BAD))
            {
                return false;
            }
        }
        return true;
    }

    // ---- Steps 3 and 4: renumbering

    // Writes via the spare cluster, so after a power cut the target is
    // either untouched or the journal says to copy it again
    bool Commit(uint64_t target, const unsigned char *bytes, uint32_t length)
    {
        if (!m_device.Write(m_journal.scratch, bytes, length) || !m_device.Sync())
        {
            return false;
        }
        m_journal.pending = 1;
        m_journal.pending_target = target;
        m_journal.pending_length = length;
        m_journal.pending_checksum = fnv1a(bytes, length);
        if (!WriteJournal() || !m_device.Write(target, bytes, length) || !m_device.Sync())
        {
            return false;
        }
        m_journal.pending = 0;
        m_journal.units_done++;
        return WriteJournal();
    }

    bool FinishPending()
    {
        if (!m_journal.pending)
        {
            return true;
        }
        const uint32_t length = m_journal.pending_length;
        if (length > m_cluster_bytes || !m_device.Read(m_journal.scratch, m_unit, length) ||
            fnv1a(m_unit, length) != m_journal.pending_checksum)
        {
            return false;
        }
        if (!m_device.Write(m_journal.pending_target, m_unit, length) || !m_device.Sync())
        {
            return false;
        }
        m_journal.pending = 0;
        m_journal.units_done++;
        return WriteJournal();
    }

    uint32_t Renumber(uint32_t value) const
    {
        if (value == FATX_FAT_FREE || value >= FATX_FAT_RESERVED)
        {
            return value;
        }
        return (uint32_t)((int64_t)value - m_shift);
    }

    uint64_t FatUnits() const
    {
        return (m_new_layout.bytes_per_fat + m_cluster_bytes - 1) / m_cluster_bytes;
    }

    // Writes one piece of the new FAT from the old one. Growing works upwards
    // and shrinking downwards, so unread old entries are never overwritten.
    bool WriteFatUnit(uint64_t unit)
    {
        const uint32_t width = m_new_layout.bytes_per_entry;
        const uint64_t begin = unit * m_cluster_bytes;
        const uint64_t end = (begin + m_cluster_bytes < m_new_layout.bytes_per_fat) ? begin + m_cluster_bytes
                                                                                     : m_new_layout.bytes_per_fat;
        const int64_t first = (int64_t)(begin / width);
        const int64_t last = (int64_t)(end / width);

        // Read the old entries it needs
        const int64_t old_limit = (int64_t)m_old_layout.cluster_limit;
        int64_t from = first + m_shift;
        int64_t to = last + m_shift;
        from = (from < 1) ? 1 : from;
        to = (to > old_limit) ? old_limit : to;
        uint64_t read_begin = 0;
        if (from < to)
        {
            read_begin = (uint64_t)from * width - ((uint64_t)from * width) % HDD_SECTOR_SIZE;
            const uint64_t read_end = ((uint64_t)to * width + HDD_SECTOR_SIZE - 1) / HDD_SECTOR_SIZE * HDD_SECTOR_SIZE;
            if (!m_device.Read(FATX_HEADER_BYTES + read_begin, m_old, (uint32_t)(read_end - read_begin)))
            {
                return false;
            }
        }

        for (int64_t entry = first; entry < last; entry++)
        {
            uint32_t value = FATX_FAT_FREE;
            const int64_t old_entry = entry + m_shift;
            if (entry == 0)
            {
                value = FATX_FAT_MEDIA;
            }
            else if (old_entry >= from && old_entry < to)
            {
                value = Renumber(entry_value(m_old + ((uint64_t)old_entry * width - read_begin), width));
            }
            set_entry_value(m_unit + (entry - first) * width, width, value);
        }
        return Commit(FATX_HEADER_BYTES + begin, m_unit, (uint32_t)(end - begin));
    }

    bool RenumberFat()
    {
        const uint64_t units = FatUnits();
        while (m_journal.units_done < units)
        {
            const uint64_t done = m_journal.units_done;
            if (!WriteFatUnit(m_shift > 0 ? done : units - 1 - done))
            {
                return false;
            }
            SetProgress(50 + (int)(m_journal.units_done * 30 / units));
        }
        return true;
    }

    // Renumbers every folder cluster, walking the new FAT from the root. On
    // resume, the first units_done clusters are already done and their
    // entries lead to the rest.
    bool RenumberDirs()
    {
        if (!m_fat.Use(m_new_layout) || !WriteHeaderField(FATX_ROOT_CLUSTER_OFFSET, m_journal.root_cluster))
        {
            return false;
        }
        m_pending_count = 0;
        if (!PushDir(m_journal.root_cluster, 0))
        {
            return false;
        }

        const uint32_t entries = m_cluster_bytes / sizeof(FatxDirent);
        const uint32_t limit = (uint32_t)m_new_layout.cluster_limit;
        uint64_t index = 0;
        while (m_pending_count > 0)
        {
            uint32_t cluster = m_pending[--m_pending_count].first_cluster;
            bool ended = false;
            while (true)
            {
                if (cluster < 1 || cluster >= limit)
                {
                    return false;
                }
                const uint64_t offset = ClusterOffset(m_new_layout, cluster);
                if (!m_device.Read(offset, m_cluster, m_cluster_bytes))
                {
                    return false;
                }

                if (index >= m_journal.units_done)
                {
                    bool stop = ended;
                    for (uint32_t e = 0; e < entries && !stop; e++)
                    {
                        FatxDirent entry;
                        memcpy(&entry, m_cluster + e * sizeof(FatxDirent), sizeof(entry));
                        if (entry.name_length == FATX_DIRENT_END || entry.name_length == FATX_DIRENT_END2)
                        {
                            stop = true;
                        }
                        else if (entry.name_length != FATX_DIRENT_DELETED && entry.name_length <= FATX_MAX_NAME_LENGTH &&
                                 entry.first_cluster != 0)
                        {
                            entry.first_cluster = Renumber(entry.first_cluster);
                            memcpy(m_cluster + e * sizeof(FatxDirent), &entry, sizeof(entry));
                        }
                    }
                    if (!Commit(offset, m_cluster, m_cluster_bytes))
                    {
                        return false;
                    }
                }
                index++;

                for (uint32_t e = 0; e < entries && !ended; e++)
                {
                    FatxDirent entry;
                    memcpy(&entry, m_cluster + e * sizeof(FatxDirent), sizeof(entry));
                    if (entry.name_length == FATX_DIRENT_END || entry.name_length == FATX_DIRENT_END2)
                    {
                        ended = true;
                    }
                    else if (entry.name_length != FATX_DIRENT_DELETED && entry.name_length <= FATX_MAX_NAME_LENGTH &&
                             (entry.attributes & FATX_ATTR_DIRECTORY) && entry.first_cluster != 0 &&
                             !PushDir(entry.first_cluster, 1))
                    {
                        return false;
                    }
                }

                uint32_t next;
                if (!m_fat.Get(cluster, &next))
                {
                    return false;
                }
                if (next == FATX_FAT_END)
                {
                    break;
                }
                cluster = next;
            }
        }
        return true;
    }

    // Runs steps 3 and 4 from wherever the journal left off
    bool Finish()
    {
        uint32_t signature, sectors_per_cluster, root;
        if (!FinishPending() || !ReadHeaderFields(&signature, &sectors_per_cluster, &root))
        {
            return Fail(FATX_MOVE_INTERRUPTED);
        }
        if (m_journal.phase != PHASE_TABLE && signature != FATX_RESIZING_SIGNATURE &&
            !WriteHeaderField(FATX_SIGNATURE_OFFSET, FATX_RESIZING_SIGNATURE))
        {
            return Fail(FATX_MOVE_INTERRUPTED);
        }

        if (m_journal.phase == PHASE_FAT)
        {
            if (!RenumberFat())
            {
                return Fail(FATX_MOVE_INTERRUPTED);
            }
            m_journal.phase = PHASE_DIRS;
            m_journal.units_done = 0;
            if (!WriteJournal())
            {
                return Fail(FATX_MOVE_INTERRUPTED);
            }
        }
        if (m_journal.phase == PHASE_DIRS)
        {
            if (!RenumberDirs())
            {
                return Fail(FATX_MOVE_INTERRUPTED);
            }
            m_journal.phase = PHASE_TABLE;
            if (!WriteJournal())
            {
                return Fail(FATX_MOVE_INTERRUPTED);
            }
        }
        SetProgress(98);

        // Size before signature, so it's never mounted at the old size with
        // the new layout
        if (!m_table.SetSize(m_journal.new_lba_size) || !m_device.Sync() ||
            !WriteHeaderField(FATX_SIGNATURE_OFFSET, FATX_signature) || !ClearJournal())
        {
            return Fail(FATX_MOVE_INTERRUPTED);
        }
        SetProgress(100);
        m_result->status = FATX_MOVE_DONE;
        return true;
    }

    FatxDevice &m_device;
    FatxResizeTable &m_table;
    FatxMoveResult *m_result;
    FatxProgress *m_progress;
    FatCache m_fat;

    unsigned char *m_cluster; // A folder cluster
    unsigned char *m_copy;    // A cluster being moved
    unsigned char *m_unit;    // A FAT piece, or the spare cluster's contents
    unsigned char *m_old;     // Old FAT entries for a piece

    uint32_t m_cluster_bytes;
    FatxLayout m_old_layout;
    FatxLayout m_new_layout;
    int64_t m_shift;
    uint32_t m_keep_begin; // [begin, end) stay put (old numbering)
    uint32_t m_keep_end;
    uint32_t m_hint;       // Where FindFree() looks next

    PendingDir *m_pending;
    uint32_t m_pending_count;
    uint32_t m_pending_capacity;
    uint32_t *m_chain;
    uint32_t m_chain_capacity;
    uint32_t *m_moved_from; // The run being moved
    uint32_t *m_moved_to;

    Journal m_journal;
};

} // namespace

uint64_t FatxMoveResizeTarget(uint64_t old_lba_size, uint32_t cluster_bytes, uint64_t requested_lba)
{
    const FatxLayout old_layout = FatxLayoutFor(old_lba_size, cluster_bytes);
    uint64_t size = requested_lba & ~(uint64_t)(LBA_ALIGNMENT - 1);
    while (size >= LBA_ALIGNMENT)
    {
        const FatxLayout layout = FatxLayoutFor(size, cluster_bytes);
        if (layout.cluster_limit < 2)
        {
            return 0;
        }
        if (layout.bytes_per_entry == old_layout.bytes_per_entry)
        {
            const int64_t change = (int64_t)layout.bytes_per_fat - (int64_t)old_layout.bytes_per_fat;
            if (change % (int64_t)cluster_bytes == 0)
            {
                return size;
            }
        }
        else if (size < old_lba_size)
        {
            return 0; // Smaller entries below; nothing fits
        }

        // Try the next smaller FAT
        uint64_t smallest, largest;
        FatxInPlaceResizeRange(size, cluster_bytes, &smallest, &largest);
        if (smallest < LBA_ALIGNMENT * 2 || smallest > size)
        {
            return 0;
        }
        size = smallest - LBA_ALIGNMENT;
    }
    return 0;
}

uint64_t FatxMoveResizeNextUp(uint64_t old_lba_size, uint32_t cluster_bytes, uint64_t from_lba)
{
    const FatxLayout old_layout = FatxLayoutFor(old_lba_size, cluster_bytes);
    uint64_t size = from_lba;
    for (int page = 0; page < 1024; page++)
    {
        uint64_t smallest, largest;
        FatxInPlaceResizeRange(size, cluster_bytes, &smallest, &largest);
        size = (largest & ~(uint64_t)(LBA_ALIGNMENT - 1)) + LBA_ALIGNMENT; // Next FAT size up
        const FatxLayout layout = FatxLayoutFor(size, cluster_bytes);
        if (layout.bytes_per_entry != old_layout.bytes_per_entry || ClusterSizeKbFor(size) > cluster_bytes / 1024)
        {
            return 0;
        }
        if ((layout.bytes_per_fat - old_layout.bytes_per_fat) % cluster_bytes == 0)
        {
            return size;
        }
    }
    return 0;
}

FatxResizeKind FatxClassifyResize(uint64_t old_lba_size, uint32_t cluster_bytes, uint64_t new_lba_size)
{
    uint64_t min_lba, max_lba;
    FatxInPlaceResizeRange(old_lba_size, cluster_bytes, &min_lba, &max_lba);
    if (new_lba_size >= min_lba && new_lba_size <= max_lba)
    {
        return FATX_RESIZE_KIND_IN_PLACE;
    }
    const FatxLayout old_layout = FatxLayoutFor(old_lba_size, cluster_bytes);
    const FatxLayout new_layout = FatxLayoutFor(new_lba_size, cluster_bytes);
    const bool grows_too_far = new_lba_size > old_lba_size && ClusterSizeKbFor(new_lba_size) > cluster_bytes / 1024;
    if (FatxMoveResizeTarget(old_lba_size, cluster_bytes, new_lba_size) == new_lba_size &&
        new_layout.bytes_per_fat != old_layout.bytes_per_fat && !grows_too_far)
    {
        return FATX_RESIZE_KIND_MOVES;
    }
    return FATX_RESIZE_KIND_FORMAT;
}

bool FatxResizeWithMoves(FatxDevice &device, uint64_t old_lba_size, uint64_t requested_lba, FatxResizeTable &table,
                         bool write, FatxMoveResult *result, FatxProgress *progress)
{
    memset(result, 0, sizeof(*result));
    Resizer resizer(device, table, result, progress);
    return resizer.Resize(old_lba_size, requested_lba, write);
}

bool FatxResumeResize(FatxDevice &device, FatxResizeTable &table, FatxMoveResult *result, FatxProgress *progress)
{
    memset(result, 0, sizeof(*result));
    Resizer resizer(device, table, result, progress);
    return resizer.Resume();
}

bool FatxFindInterruptedResize(FatxReader &reader, uint64_t *old_lba_size, uint64_t *new_lba_size)
{
    unsigned char sector[HDD_SECTOR_SIZE];
    if (!reader.Read(JOURNAL_SECTOR, sector, sizeof(sector)))
    {
        return false;
    }
    Journal journal;
    memcpy(&journal, sector + JOURNAL_OFFSET, sizeof(journal));
    if (!journal_valid(journal))
    {
        return false;
    }
    *old_lba_size = journal.old_lba_size;
    *new_lba_size = journal.new_lba_size;
    return true;
}
