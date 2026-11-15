// See fatx_import.hpp

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include "fatx_import.hpp"

#include <algorithm>
#include <map>
#include <memory>
#include <set>
#include <stdio.h>
#include <string.h>

#include "fatx_edit.hpp"
#include "fatx_folder.hpp"
#include "pc_image.hpp" // xbox_name

namespace fs = std::filesystem;

namespace
{

const unsigned int FATX_FIRST_YEAR = 2000;
const unsigned int FATX_LAST_YEAR = FATX_FIRST_YEAR + 127; // 7 bits

std::string lower(const std::string &text)
{
    std::string result = text;
    for (char &c : result)
    {
        c = (char)tolower((unsigned char)c);
    }
    return result;
}

// For messages; names that don't convert still show something
std::string shown(const fs::path &path)
{
    const auto text = path.u8string(); // std::string in C++17, std::u8string in C++20
    return std::string(text.begin(), text.end());
}

uint64_t file_time_of(const fs::path &path)
{
    std::error_code error;
    const fs::file_time_type time = fs::last_write_time(path, error);
    // MSVC's file clock counts 100 ns steps from 1601, like FILETIME
    return error ? 0 : (uint64_t)time.time_since_epoch().count();
}

FatxDirent dirent_for(const FatxImportItem &item)
{
    return fatx_dirent(item.name, item.directory, item.size, item.date, item.time);
}

// A PC file read in order, moving the progress by its bytes
class ProgressSource : public FatxSource
{
  public:
    ProgressSource(FILE *file, FatxProgress *progress, uint64_t total, uint64_t *done) : m_file(file), m_progress(progress), m_total(total), m_done(done)
    {
    }
    bool Read(void *buffer, uint32_t length) override
    {
        if (fread(buffer, 1, length, m_file) != length)
        {
            return false;
        }
        *m_done += length;
        if (m_progress != NULL && m_total > 0)
        {
            m_progress->Update((int)(*m_done * 100 / m_total));
        }
        return true;
    }

  private:
    FILE *m_file;
    FatxProgress *m_progress;
    const uint64_t m_total;
    uint64_t *m_done;
};

// Names and entry counts per folder while planning: -1 is the target,
// others are indexes of planned folders
struct FolderUse
{
    std::set<std::string> names; // Lower case: the Xbox ignores case
    uint32_t entries = 0;
};

} // namespace

// What a plan keeps track of while it's made
struct FatxImporter::PlanState
{
    FatxImportPlan plan;
    std::map<int, FolderUse> folders;
};

void fatx_time_from_file_time(uint64_t file_time, uint16_t *date, uint16_t *time)
{
    FILETIME utc, local;
    utc.dwLowDateTime = (DWORD)file_time;
    utc.dwHighDateTime = (DWORD)(file_time >> 32);
    SYSTEMTIME parts;
    if (file_time == 0 || !FileTimeToLocalFileTime(&utc, &local) || !FileTimeToSystemTime(&local, &parts) || parts.wYear < FATX_FIRST_YEAR)
    {
        *date = (1 << 5) | 1; // 2000-01-01 00:00
        *time = 0;
        return;
    }
    const unsigned int year = std::min<unsigned int>(parts.wYear, FATX_LAST_YEAR);
    *date = (uint16_t)(((year - FATX_FIRST_YEAR) << 9) | (parts.wMonth << 5) | parts.wDay);
    *time = (uint16_t)((parts.wHour << 11) | (parts.wMinute << 5) | (parts.wSecond / 2));
}

FatxImporter::FatxImporter(FatxDevice &partition, uint64_t partition_bytes)
    : m_device(partition), m_partition_bytes(partition_bytes), m_writer(partition, partition_bytes), m_total_bytes(0)
{
}

std::string FatxImporter::Open(FatxProgress *progress, uint64_t clean_fingerprint)
{
    return fatx_open_for_changes(m_device, m_partition_bytes, m_writer, progress, clean_fingerprint);
}

uint64_t FatxImporter::ClustersFor(uint64_t bytes) const
{
    return (bytes + m_writer.ClusterBytes() - 1) / m_writer.ClusterBytes();
}

FatxImportPlan FatxImporter::Plan(const FatxImportTarget &target, const std::vector<fs::path> &sources)
{
    PlanState state;
    // The names already there can't be used again
    FatxFolderReader reader(m_device, m_partition_bytes);
    std::vector<FatxFolderEntry> existing;
    if (reader.Open() == FATX_FOLDER_OK)
    {
        reader.List(target.cluster, &existing);
    }
    FolderUse &into = state.folders[-1];
    for (const FatxFolderEntry &entry : existing)
    {
        into.names.insert(lower(entry.name));
    }
    into.entries = (uint32_t)existing.size();
    const uint32_t entries_before = into.entries;

    for (const fs::path &source : sources)
    {
        Visit(source, Placement{-1, target.path}, &state);
    }

    // The target folder may need more clusters for its new entries; counted
    // as if its last cluster had no room left
    const uint32_t per_cluster = m_writer.ClusterBytes() / sizeof(FatxDirent);
    const uint32_t added = state.folders[-1].entries - entries_before;
    state.plan.clusters_needed += (added + per_cluster - 1) / per_cluster;
    state.plan.clusters_free = m_writer.FreeClusters();
    return state.plan;
}

void FatxImporter::Visit(const fs::path &source, const Placement &place, PlanState *state)
{
    FatxImportPlan &plan = state->plan;
    std::string name;
    try
    {
        name = xbox_name(source);
    }
    catch (const std::exception &)
    {
        name.clear(); // Not representable: not an Xbox name either
    }
    if (name.empty())
    {
        plan.skipped.push_back("Name too long (over 42) or not allowed on the Xbox: " + shown(source));
        return;
    }
    const std::string path = place.parent_path + "\\" + name;
    if (path.size() > FATX_MAX_PATH_LENGTH)
    {
        plan.skipped.push_back("Path over 240 characters on the Xbox: " + shown(source));
        return;
    }
    FolderUse &parent = state->folders[place.parent];
    if (parent.names.count(lower(name)) > 0)
    {
        plan.skipped.push_back("Already there (nothing is replaced): " + shown(source));
        return;
    }
    if (parent.entries >= FATX_MAX_FOLDER_ENTRIES)
    {
        plan.skipped.push_back("The folder it goes in would have over 4096 entries: " + shown(source));
        return;
    }

    std::error_code error;
    const bool directory = fs::is_directory(source, error);
    if (error)
    {
        plan.skipped.push_back("Can't read: " + shown(source));
        return;
    }
    if (directory)
    {
        parent.names.insert(lower(name));
        parent.entries++;
        AddFolder(source, name, place, state);
        return;
    }

    const uint64_t size = fs::file_size(source, error);
    if (error || size > FATX_MAX_FILE_SIZE)
    {
        plan.skipped.push_back(error ? "Can't read: " + shown(source) : "4 GB or bigger, too big for FATX: " + shown(source));
        return;
    }
    parent.names.insert(lower(name));
    parent.entries++;

    FatxImportItem item = {source, name, false, (uint32_t)size, 0, 0, place.parent};
    fatx_time_from_file_time(file_time_of(source), &item.date, &item.time);
    plan.items.push_back(item);
    plan.bytes += size;
    plan.clusters_needed += ClustersFor(size);
}

void FatxImporter::AddFolder(const fs::path &source, const std::string &name, const Placement &place, PlanState *state)
{
    FatxImportPlan &plan = state->plan;
    const int index = (int)plan.items.size();
    FatxImportItem item = {source, name, true, 0, 0, 0, place.parent};
    fatx_time_from_file_time(file_time_of(source), &item.date, &item.time);
    plan.items.push_back(item);

    std::error_code error;
    const Placement inside = {index, place.parent_path + "\\" + name};
    for (const fs::directory_entry &child : fs::directory_iterator(source, error))
    {
        Visit(child.path(), inside, state);
    }
    if (error)
    {
        plan.skipped.push_back("Couldn't read all of: " + shown(source));
    }

    // Its entries, in whole clusters; at least one even when empty
    const uint32_t per_cluster = m_writer.ClusterBytes() / sizeof(FatxDirent);
    const uint32_t entries = state->folders[index].entries;
    plan.clusters_needed += std::max<uint64_t>(1, (entries + per_cluster - 1) / per_cluster);
}

FatxImportResult FatxImporter::Run(const FatxImportTarget &target, const FatxImportPlan &plan, FatxImportLog &log, FatxProgress *progress)
{
    FatxImportResult result;
    if (!plan.Fits())
    {
        result.failed = (int)plan.items.size();
        log.Note("Not enough free space: nothing was added.");
        return result;
    }
    m_total_bytes = plan.bytes;
    uint64_t done = 0;
    // Each planned folder's first cluster, once made (0: not made)
    std::vector<uint32_t> clusters(plan.items.size(), 0);
    for (size_t i = 0; i < plan.items.size(); i++)
    {
        const FatxImportItem &item = plan.items[i];
        const uint32_t parent = item.parent < 0 ? target.cluster : clusters[item.parent];
        if (parent == 0)
        {
            result.failed++; // Its folder couldn't be made, already noted
            continue;
        }
        if (!AddOne(item, parent, &clusters[i], progress, &done))
        {
            result.failed++;
            log.Note("Couldn't add: " + shown(item.source));
            continue;
        }
        result.folders += item.directory ? 1 : 0;
        result.files += item.directory ? 0 : 1;
        result.bytes += item.size;
    }
    if (!m_writer.Flush())
    {
        result.failed++;
        log.Note("The last FAT changes could not be written: check the volume.");
    }
    return result;
}

bool FatxImporter::AddOne(const FatxImportItem &item, uint32_t parent_cluster, uint32_t *cluster, FatxProgress *progress, uint64_t *done)
{
    const FatxDirent like = dirent_for(item);
    if (item.directory)
    {
        return m_writer.MakeDirectory(parent_cluster, like, cluster);
    }
    FILE *file = NULL;
    if (_wfopen_s(&file, item.source.c_str(), L"rb") != 0 || file == NULL)
    {
        return false;
    }
    ProgressSource source(file, progress, m_total_bytes, done);
    const bool added = m_writer.AddFile(parent_cluster, like, source);
    fclose(file);
    return added;
}
