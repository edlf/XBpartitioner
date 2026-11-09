// See pc_image.hpp

// Lean, because the full windows.h pulls in <winioctl.h>, which would pick
// up the Xbox version in src/
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include "pc_image.hpp"

#include <io.h>
#include <string.h>

namespace fs = std::filesystem;

char partition_letter(int index)
{
    static const char standard[] = "ECXYZ";
    return (index < XBOX_STANDARD_PARTITION_COUNT) ? standard[index] : (char)('F' + index - XBOX_STANDARD_PARTITION_COUNT);
}

uint64_t file_size(FILE *file)
{
    _fseeki64(file, 0, SEEK_END);
    return (uint64_t)_ftelli64(file);
}

bool FileDevice::Read(uint64_t offset, void *buffer, uint32_t length)
{
    return _fseeki64(m_file, (__int64)(m_base + offset), SEEK_SET) == 0 && fread(buffer, 1, length, m_file) == length;
}

bool FileDevice::Write(uint64_t offset, const void *buffer, uint32_t length)
{
    return _fseeki64(m_file, (__int64)(m_base + offset), SEEK_SET) == 0 && fwrite(buffer, 1, length, m_file) == length;
}

bool FileDevice::Sync()
{
    return fflush(m_file) == 0 && _commit(_fileno(m_file)) == 0;
}

void update_backup_tables(FatxDevice &disk, uint64_t disk_bytes, const XboxPartitionTable &old_table, const XboxPartitionTable &new_table)
{
    const uint64_t sectors = disk_bytes / HDD_SECTOR_SIZE;
    const uint64_t candidates[] = {(sectors & ~(uint64_t)(LBA_ALIGNMENT - 1)) - LBA_ALIGNMENT, sectors - 1};
    for (uint64_t lba : candidates)
    {
        XboxPartitionTable backup;
        if (!disk.Read(lba * HDD_SECTOR_SIZE, &backup, sizeof(backup)))
        {
            continue;
        }
        XboxPartitionTable a = backup;
        XboxPartitionTable b = old_table;
        a.Version = b.Version = 0;
        if (memcmp(&a, &b, sizeof(a)) != 0)
        {
            continue;
        }
        XboxPartitionTable updated = new_table;
        updated.Version = backup.Version; // Keep a Titan backup's version
        disk.Write(lba * HDD_SECTOR_SIZE, &updated, sizeof(updated));
    }
}

bool DiskTable::SetSize(uint64_t new_lba_size)
{
    XboxPartitionTable table;
    if (!m_disk.Read(0, &table, sizeof(table)) || !XboxPartitionTableManipulator::HasMagic(table))
    {
        return false;
    }
    const XboxPartitionTable old_table = table;
    XboxPartitionTableEntry &entry = table.TableEntries[m_index];
    entry.LBASize = (uint32_t)new_lba_size;
    if (table.Version != XBOX_TABLE_VERSION_LBA48_V3)
    {
        entry.LBASize_high = (uint16_t)(new_lba_size >> 32);
    }
    if (!m_disk.Write(0, &table, sizeof(table)))
    {
        return false;
    }
    update_backup_tables(m_disk, m_disk_bytes, old_table, table);
    return m_disk.Sync();
}

bool ImageTable::SetSize(uint64_t new_lba_size)
{
    if (m_index < 0)
    {
        return fflush(m_file) == 0 && _chsize_s(_fileno(m_file), (__int64)(new_lba_size * HDD_SECTOR_SIZE)) == 0;
    }

    FileDevice image(m_file, 0);
    DiskTable table(image, file_size(m_file), m_index);
    return table.SetSize(new_lba_size);
}

// ---- recluster

const char RECLUSTER_MAGIC[8] = {'X', 'B', 'P', 'R', 'C', 'L', '0', '1'};

static uint32_t fnv1a(const void *data, size_t length, uint32_t hash = 2166136261u)
{
    const unsigned char *bytes = (const unsigned char *)data;
    for (size_t i = 0; i < length; i++)
    {
        hash = (hash ^ bytes[i]) * 16777619u;
    }
    return hash;
}

fs::path recluster_plan_path(const fs::path &image)
{
    return fs::path(image.native() + L".recluster");
}

fs::path recluster_build_path(const fs::path &image)
{
    return fs::path(image.native() + L".recluster.tmp");
}

bool save_recluster_plan(const fs::path &path, const ReclusterPlanHeader &header, const std::vector<FatxRange> &ranges)
{
    const fs::path part = fs::path(path.native() + L".part");
    FILE *file = NULL;
    if (_wfopen_s(&file, part.c_str(), L"wb") != 0 || file == NULL)
    {
        return false;
    }
    uint32_t checksum = fnv1a(&header, sizeof(header));
    bool ok = fwrite(&header, 1, sizeof(header), file) == sizeof(header);
    for (const FatxRange &range : ranges)
    {
        checksum = fnv1a(&range, sizeof(range), checksum);
        ok = ok && fwrite(&range, 1, sizeof(range), file) == sizeof(range);
    }
    ok = ok && fwrite(&checksum, 1, sizeof(checksum), file) == sizeof(checksum);
    ok = ok && fflush(file) == 0 && _commit(_fileno(file)) == 0;
    fclose(file);
    std::error_code error;
    if (ok)
    {
        fs::rename(part, path, error);
    }
    return ok && !error;
}

bool load_recluster_plan(const fs::path &path, ReclusterPlanHeader *header, std::vector<FatxRange> *ranges)
{
    FILE *file = NULL;
    if (_wfopen_s(&file, path.c_str(), L"rb") != 0 || file == NULL)
    {
        return false;
    }
    bool ok = fread(header, 1, sizeof(*header), file) == sizeof(*header) && memcmp(header->magic, RECLUSTER_MAGIC, sizeof(RECLUSTER_MAGIC)) == 0;
    uint32_t checksum = fnv1a(header, sizeof(*header));
    for (uint32_t i = 0; ok && i < header->range_count; i++)
    {
        FatxRange range;
        ok = fread(&range, 1, sizeof(range), file) == sizeof(range);
        checksum = fnv1a(&range, sizeof(range), checksum);
        ranges->push_back(range);
    }
    uint32_t stored = 0;
    ok = ok && fread(&stored, 1, sizeof(stored), file) == sizeof(stored) && stored == checksum;
    fclose(file);
    return ok;
}

void remove_recluster_files(const fs::path &plan_path, const fs::path &build_path)
{
    std::error_code error;
    fs::remove(plan_path, error);
    fs::remove(build_path, error);
}

void make_sparse(FILE *file)
{
#ifndef FSCTL_SET_SPARSE
#define FSCTL_SET_SPARSE 0x000900C4
#endif
    DWORD returned = 0;
    DeviceIoControl((HANDLE)_get_osfhandle(_fileno(file)), FSCTL_SET_SPARSE, NULL, 0, NULL, 0, &returned, NULL);
}

// ---- make

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

namespace
{

class StreamSource : public FatxSource
{
  public:
    explicit StreamSource(FILE *file) : m_file(file)
    {
    }
    bool Read(void *buffer, uint32_t length) override
    {
        return fread(buffer, 1, length, m_file) == length;
    }

  private:
    FILE *m_file;
};

// For messages; names that don't convert still show something
std::string display_path(const fs::path &path)
{
    const auto text = path.u8string(); // std::string in C++17, std::u8string in C++20
    return std::string(text.begin(), text.end());
}

} // namespace

bool copy_tree(FatxWriter &writer, const fs::path &from, uint32_t dir, int *skipped, const std::function<void(const std::string &)> &note)
{
    std::error_code error;
    for (const fs::directory_entry &item : fs::directory_iterator(from, error))
    {
        std::string name;
        try
        {
            name = xbox_name(item.path());
        }
        catch (const std::exception &)
        {
            name.clear(); // Not representable: not an Xbox name either
        }
        if (name.empty())
        {
            note("Skipped (name too long or not allowed): " + display_path(item.path()));
            (*skipped)++;
            continue;
        }

        if (item.is_directory(error))
        {
            uint32_t sub;
            if (!writer.MakeDirectory(dir, name.c_str(), &sub) || !copy_tree(writer, item.path(), sub, skipped, note))
            {
                return false;
            }
            continue;
        }

        const uint64_t size = item.file_size(error);
        if (error || size > 0xFFFFFFFFULL)
        {
            note("Skipped (4 GB or bigger, or unreadable): " + display_path(item.path()));
            (*skipped)++;
            error.clear();
            continue;
        }
        FILE *file = NULL;
        if (_wfopen_s(&file, item.path().c_str(), L"rb") != 0 || file == NULL)
        {
            note("Skipped (can't open): " + display_path(item.path()));
            (*skipped)++;
            continue;
        }
        StreamSource source(file);
        const bool ok = writer.AddFile(dir, name.c_str(), (uint32_t)size, source);
        fclose(file);
        if (!ok)
        {
            note("Couldn't add " + display_path(item.path()) + " (image full?)");
            return false;
        }
    }
    return !error;
}

// ---- What a command works on

DiskTarget::~DiskTarget()
{
    m_image.reset();
    if (m_file != nullptr)
    {
        fclose(m_file);
    }
}

bool DiskTarget::IsDrivePath(const fs::path &path)
{
    return path.native().rfind(L"\\\\.\\", 0) == 0;
}

bool DiskTarget::Open(const fs::path &path, bool write, bool ata, std::string *error)
{
    if (IsDrivePath(path))
    {
        const std::string name = path.string();
        unsigned long code = 0;
        m_drive = std::make_unique<WindowsDisk>();
        if (!m_drive->Open(name.c_str(), write || ata, &code))
        {
            m_drive.reset();
            if (code == ERROR_ACCESS_DENIED)
            {
                *error = "Can't open " + name + ": run as administrator.";
            }
            else if (code == ERROR_FILE_NOT_FOUND)
            {
                *error = "There is no " + name + " (Disk Management shows the disk numbers).";
            }
            else
            {
                *error = "Can't open " + name + " (error " + std::to_string(code) + ").";
            }
            return false;
        }
        m_size = m_drive->Size();
        // Windows refuses writes to disks with mounted volumes: find out now
        unsigned char sector[HDD_SECTOR_SIZE];
        if (write && (!m_drive->Read(0, sector, sizeof(sector)) || !m_drive->Write(0, sector, sizeof(sector))))
        {
            *error = "Windows doesn't allow writing to " + name + " (error " + std::to_string(GetLastError()) + "). Is a volume on it mounted?";
            m_drive.reset();
            return false;
        }
        return true;
    }

    if (_wfopen_s(&m_file, path.c_str(), write ? L"r+b" : L"rb") != 0 || m_file == nullptr)
    {
        m_file = nullptr;
        const auto name = path.u8string();
        *error = "Can't open " + std::string(name.begin(), name.end()) + (write ? " for writing (read only, or open elsewhere?)." : ".");
        return false;
    }
    m_size = file_size(m_file);
    m_image = std::make_unique<FileDevice>(m_file, 0);
    return true;
}

bool DiskTarget::ReadTable(XboxPartitionTable *table)
{
    memset(table, 0, sizeof(*table));
    return Device().Read(0, table, sizeof(*table)) && XboxPartitionTableManipulator::HasMagic(*table);
}

std::unique_ptr<FatxResizeTable> DiskTarget::Table(int index)
{
    if (m_drive)
    {
        return index < 0 ? nullptr : std::make_unique<DiskTable>(*m_drive, m_size, index);
    }
    return std::make_unique<ImageTable>(m_file, index);
}

// ---- trim

std::string identify_string(const uint16_t *words, int count)
{
    std::string text;
    for (int i = 0; i < count; i++)
    {
        text += (char)(words[i] >> 8);
        text += (char)(words[i] & 0xFF);
    }
    while (!text.empty() && (text.back() == ' ' || text.back() == '\0'))
    {
        text.pop_back();
    }
    return text;
}
