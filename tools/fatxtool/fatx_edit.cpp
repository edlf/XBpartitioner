// See fatx_edit.hpp

#include "fatx_edit.hpp"

#include <memory>
#include <string.h>

#include "fatx_import.hpp" // The Xbox's limits

namespace
{

bool same_name_ignoring_case(const std::string &a, const std::string &b)
{
    if (a.size() != b.size())
    {
        return false;
    }
    for (size_t i = 0; i < a.size(); i++)
    {
        if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i]))
        {
            return false;
        }
    }
    return true;
}

} // namespace

const char FATX_NAME_RULE[] = "Names hold 1 to 42 characters, none of \\ / : * ? \" < > |.";

bool fatx_name_allowed(const std::string &name)
{
    if (name.empty() || name.size() > FATX_MAX_NAME_LENGTH)
    {
        return false;
    }
    for (char c : name)
    {
        const unsigned char u = (unsigned char)c;
        if (u < 0x20 || u >= 0x7F || strchr("\"*/:<>?\\|", u) != NULL)
        {
            return false;
        }
    }
    return true;
}

FatxDirent fatx_dirent(const std::string &name, bool directory, uint32_t size, uint16_t date, uint16_t time)
{
    FatxDirent entry;
    memset(&entry, 0xFF, sizeof(entry)); // Unused name bytes are 0xFF
    entry.name_length = (unsigned char)name.size();
    entry.attributes = directory ? FATX_ATTR_DIRECTORY : 0;
    memcpy(entry.name, name.data(), name.size());
    entry.first_cluster = 0;
    entry.file_size = directory ? 0 : size;
    entry.modified_date = entry.created_date = entry.accessed_date = date;
    entry.modified_time = entry.created_time = entry.accessed_time = time;
    return entry;
}

std::string fatx_open_for_changes(FatxDevice &partition, uint64_t partition_bytes, FatxWriter &writer, FatxProgress *progress,
                                  uint64_t clean_fingerprint)
{
    // The FAT is needed anyway; unchanged since it was known clean, the
    // check would find what it found then
    const bool loaded = writer.Load();
    if (loaded && clean_fingerprint != 0 && writer.FatFingerprint() == clean_fingerprint)
    {
        writer.WriteThrough();
        return std::string();
    }

    std::unique_ptr<FatxCheckResult> check = std::make_unique<FatxCheckResult>();
    FatxCheck(partition, partition_bytes, check.get(), progress);
    if (check->status == FATX_CHECK_RESIZING)
    {
        return "A resize was interrupted: finish it first.";
    }
    if (check->status != FATX_CHECK_DONE)
    {
        return "It isn't readable FATX.";
    }
    if (check->errors > 0)
    {
        return "Check finds errors: a damaged volume isn't changed.";
    }
    if (!loaded && !writer.Load())
    {
        return "Its FAT could not be read.";
    }
    writer.WriteThrough();
    return std::string();
}

std::string fatx_label_problem(const std::string &label)
{
    if (label.size() > FATX_LABEL_LENGTH)
    {
        return "A label holds up to 32 characters.";
    }
    for (char c : label)
    {
        const unsigned char u = (unsigned char)c;
        if (u < 0x20 || u >= 0x7F)
        {
            return "Only letters, digits, spaces and plain punctuation.";
        }
    }
    return std::string();
}

FatxEditor::FatxEditor(FatxDevice &partition, uint64_t partition_bytes)
    : m_device(partition), m_partition_bytes(partition_bytes), m_writer(partition, partition_bytes)
{
}

std::string FatxEditor::Open(FatxProgress *progress, uint64_t clean_fingerprint)
{
    return fatx_open_for_changes(m_device, m_partition_bytes, m_writer, progress, clean_fingerprint);
}

std::string FatxEditor::NewFolderProblem(uint32_t parent, const std::string &parent_path, const std::string &name)
{
    if (!fatx_name_allowed(name))
    {
        return FATX_NAME_RULE;
    }
    if (parent_path.size() + 1 + name.size() > FATX_MAX_PATH_LENGTH)
    {
        return "The path would be over 240 characters.";
    }
    FatxFolderReader reader(m_device, m_partition_bytes);
    std::vector<FatxFolderEntry> entries;
    if (reader.Open() != FATX_FOLDER_OK || reader.List(parent, &entries) != FATX_FOLDER_OK)
    {
        return "The folder it goes in can't be read.";
    }
    for (const FatxFolderEntry &entry : entries)
    {
        if (same_name_ignoring_case(entry.name, name))
        {
            return "Something of that name is already there.";
        }
    }
    if (entries.size() >= FATX_MAX_FOLDER_ENTRIES)
    {
        return "The folder already holds 4096 entries.";
    }
    // Its own cluster, and maybe one more for the folder it goes in
    if (m_writer.FreeClusters() < 2)
    {
        return "The volume is full.";
    }
    return std::string();
}

bool FatxEditor::MakeFolder(uint32_t parent, const std::string &name, uint16_t date, uint16_t time)
{
    return m_writer.MakeDirectory(parent, fatx_dirent(name, true, 0, date, time)) && m_writer.Flush();
}

bool FatxEditor::Delete(const FatxFolderEntry &entry, uint64_t *freed)
{
    *freed = 0;
    std::vector<uint32_t> clusters;
    if (!CollectTree(entry, &clusters) || !MarkDeleted(entry))
    {
        return false;
    }
    // Nothing points at them now: a power cut from here leaves lost space
    for (const uint32_t cluster : clusters)
    {
        m_writer.SetFat(cluster, FATX_FAT_FREE);
    }
    if (!m_writer.Flush())
    {
        return false;
    }
    *freed = clusters.size();
    return true;
}

bool FatxEditor::SetLabel(const std::string &label)
{
    if (!fatx_label_problem(label).empty())
    {
        return false;
    }
    unsigned char header[HDD_SECTOR_SIZE];
    if (!m_device.Read(0, header, sizeof(header)))
    {
        return false;
    }
    // The characters, a 0 after them if there's room, then 0xFFFF as the
    // format leaves it
    for (uint32_t i = 0; i < FATX_LABEL_LENGTH; i++)
    {
        const uint16_t c = (i < label.size()) ? (uint16_t)(unsigned char)label[i] : (i == label.size()) ? 0 : 0xFFFF;
        memcpy(header + FATX_LABEL_OFFSET + i * 2, &c, 2);
    }
    return m_device.Write(0, header, sizeof(header)) && m_device.Sync();
}

void FatxEditor::CollectChain(uint32_t first, std::vector<uint32_t> *clusters, std::unordered_set<uint32_t> *seen) const
{
    const uint32_t limit = (uint32_t)m_writer.Layout().cluster_limit;
    for (uint32_t cluster = first; cluster >= 1 && cluster < limit && seen->insert(cluster).second; cluster = m_writer.GetFat(cluster))
    {
        clusters->push_back(cluster);
    }
}

bool FatxEditor::CollectTree(const FatxFolderEntry &entry, std::vector<uint32_t> *clusters)
{
    std::unordered_set<uint32_t> seen;
    CollectChain(entry.first_cluster, clusters, &seen);
    if (!entry.directory)
    {
        return true;
    }
    FatxFolderReader reader(m_device, m_partition_bytes);
    if (reader.Open() != FATX_FOLDER_OK)
    {
        return false;
    }
    std::vector<uint32_t> folders(1, entry.first_cluster);
    while (!folders.empty())
    {
        const uint32_t folder = folders.back();
        folders.pop_back();
        std::vector<FatxFolderEntry> inside;
        if (reader.List(folder, &inside) != FATX_FOLDER_OK)
        {
            return false;
        }
        for (const FatxFolderEntry &item : inside)
        {
            if (item.directory && item.first_cluster != 0 && seen.count(item.first_cluster) == 0)
            {
                folders.push_back(item.first_cluster);
            }
            CollectChain(item.first_cluster, clusters, &seen);
        }
    }
    return true;
}

// Only if it's still the entry that was listed
bool FatxEditor::MarkDeleted(const FatxFolderEntry &entry)
{
    FatxDirent on_disk;
    if (entry.dirent_offset == 0 || !m_device.Read(entry.dirent_offset, &on_disk, sizeof(on_disk)))
    {
        return false;
    }
    const bool same = on_disk.name_length == entry.name.size() && memcmp(on_disk.name, entry.name.data(), entry.name.size()) == 0 &&
                      on_disk.first_cluster == entry.first_cluster;
    if (!same)
    {
        return false;
    }
    on_disk.name_length = FATX_DIRENT_DELETED;
    return m_device.Write(entry.dirent_offset, &on_disk, sizeof(on_disk)) && m_device.Sync();
}
