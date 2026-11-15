// FatxFolderReader: listing folders for fatxgui's file browser

#include <gtest/gtest.h>
#include <string.h>
#include <string>
#include <vector>
#include "fatx_folder.hpp"
#include "fatx_resize.hpp"
#include "fatx_writer.hpp"

namespace
{

const uint64_t IMAGE_BYTES = 8 * 1024 * 1024;
const uint32_t CLUSTER_BYTES = 16 * 1024;
const uint32_t ENTRIES_PER_CLUSTER = CLUSTER_BYTES / sizeof(FatxDirent);

// A formatted image with a file and a folder holding one file
class FolderImage : public testing::Test
{
  protected:
    FolderImage() : m_device(IMAGE_BYTES), m_writer(m_device, IMAGE_BYTES, CLUSTER_BYTES), m_games(0)
    {
        EXPECT_TRUE(m_writer.Format());
        EXPECT_TRUE(m_writer.AddFile(m_writer.RootCluster(), "default.xbe", "xbe", 3));
        EXPECT_TRUE(m_writer.MakeDirectory(m_writer.RootCluster(), "Games", &m_games));
        EXPECT_TRUE(m_writer.AddFile(m_games, "save.dat", "12345", 5));
        EXPECT_TRUE(m_writer.Flush());
    }

    std::vector<FatxFolderEntry> List(uint32_t cluster, FatxFolderStatus expected = FATX_FOLDER_OK)
    {
        FatxFolderReader reader(m_device, IMAGE_BYTES);
        EXPECT_EQ(reader.Open(), FATX_FOLDER_OK);
        std::vector<FatxFolderEntry> entries;
        EXPECT_EQ(reader.List(cluster == 0 ? reader.RootCluster() : cluster, &entries), expected);
        return entries;
    }

    FatxMemoryDevice m_device;
    FatxWriter m_writer;
    uint32_t m_games;
};

} // namespace

TEST_F(FolderImage, ListsTheRoot)
{
    const std::vector<FatxFolderEntry> entries = List(0);
    ASSERT_EQ(entries.size(), 2u);
    EXPECT_EQ(entries[0].name, "default.xbe");
    EXPECT_FALSE(entries[0].directory);
    EXPECT_EQ(entries[0].size, 3u);
    EXPECT_EQ(entries[1].name, "Games");
    EXPECT_TRUE(entries[1].directory);
}

TEST_F(FolderImage, ListsASubfolder)
{
    const std::vector<FatxFolderEntry> entries = List(m_games);
    ASSERT_EQ(entries.size(), 1u);
    EXPECT_EQ(entries[0].name, "save.dat");
    EXPECT_EQ(entries[0].size, 5u);
}

TEST_F(FolderImage, LeavesOutDeletedEntries)
{
    FatxDirent entry;
    uint64_t offset;
    ASSERT_TRUE(m_writer.GetEntry(m_writer.RootCluster(), 0, &entry, &offset));
    entry.name_length = FATX_DIRENT_DELETED;
    ASSERT_TRUE(m_writer.SetEntry(offset, entry));

    const std::vector<FatxFolderEntry> entries = List(0);
    ASSERT_EQ(entries.size(), 1u);
    EXPECT_EQ(entries[0].name, "Games");
}

TEST_F(FolderImage, FollowsAFolderOverSeveralClusters)
{
    for (uint32_t i = 0; i < ENTRIES_PER_CLUSTER + 10; i++)
    {
        ASSERT_TRUE(m_writer.AddFile(m_games, ("f" + std::to_string(i)).c_str(), "", 0));
    }
    ASSERT_TRUE(m_writer.Flush());
    EXPECT_EQ(List(m_games).size(), ENTRIES_PER_CLUSTER + 11);
}

TEST_F(FolderImage, StopsAtALoop)
{
    // A full cluster has no end marker, so the chain decides where it ends
    for (uint32_t i = 1; i < ENTRIES_PER_CLUSTER; i++)
    {
        ASSERT_TRUE(m_writer.AddFile(m_games, ("f" + std::to_string(i)).c_str(), "", 0));
    }
    m_writer.SetFat(m_games, m_games);
    ASSERT_TRUE(m_writer.Flush());
    List(m_games, FATX_FOLDER_BROKEN_CHAIN);
}

TEST_F(FolderImage, RefusesAClusterOutsideThePartition)
{
    List(0xFFFFFF, FATX_FOLDER_BROKEN_CHAIN);
}

// Collects what ReadFile() sends, or refuses it
class CollectingSink : public FatxFileSink
{
  public:
    explicit CollectingSink(bool refuse = false) : m_refuse(refuse)
    {
    }
    bool Write(const void *data, uint32_t length) override
    {
        const unsigned char *bytes = (const unsigned char *)data;
        received.insert(received.end(), bytes, bytes + length);
        return !m_refuse;
    }

    std::vector<unsigned char> received;

  private:
    bool m_refuse;
};

// One file's entry, by name, in a folder
FatxFolderEntry FindEntry(FatxDevice &device, uint32_t folder, const std::string &name)
{
    FatxFolderReader reader(device, IMAGE_BYTES);
    EXPECT_EQ(reader.Open(), FATX_FOLDER_OK);
    std::vector<FatxFolderEntry> entries;
    EXPECT_EQ(reader.List(folder, &entries), FATX_FOLDER_OK);
    for (const FatxFolderEntry &entry : entries)
    {
        if (entry.name == name)
        {
            return entry;
        }
    }
    ADD_FAILURE() << name << " not found";
    return FatxFolderEntry();
}

FatxFolderStatus ReadInto(FatxDevice &device, const FatxFolderEntry &file, CollectingSink &sink)
{
    FatxFolderReader reader(device, IMAGE_BYTES);
    EXPECT_EQ(reader.Open(), FATX_FOLDER_OK);
    return reader.ReadFile(file, sink);
}

std::vector<unsigned char> Pattern(size_t length)
{
    std::vector<unsigned char> data(length);
    for (size_t i = 0; i < length; i++)
    {
        data[i] = (unsigned char)(i % 251);
    }
    return data;
}

TEST_F(FolderImage, ReadsASmallFile)
{
    CollectingSink sink;
    ASSERT_EQ(ReadInto(m_device, FindEntry(m_device, m_games, "save.dat"), sink), FATX_FOLDER_OK);
    EXPECT_EQ(std::string(sink.received.begin(), sink.received.end()), "12345");
}

TEST_F(FolderImage, ReadsAFileLongerThanOneRead)
{
    // Over 1 MB: more than one run of clusters is read
    const std::vector<unsigned char> data = Pattern(1536 * 1024 + 123);
    ASSERT_TRUE(m_writer.AddFile(m_games, "big.bin", data.data(), (uint32_t)data.size()));
    ASSERT_TRUE(m_writer.Flush());

    CollectingSink sink;
    ASSERT_EQ(ReadInto(m_device, FindEntry(m_device, m_games, "big.bin"), sink), FATX_FOLDER_OK);
    EXPECT_EQ(sink.received, data);
}

TEST_F(FolderImage, ReadsAFragmentedFileInChainOrder)
{
    // Three clusters, A B C on the disk, chained A C B
    std::vector<unsigned char> data(CLUSTER_BYTES * 3);
    memset(data.data(), 'A', CLUSTER_BYTES);
    memset(data.data() + CLUSTER_BYTES, 'B', CLUSTER_BYTES);
    memset(data.data() + 2 * CLUSTER_BYTES, 'C', CLUSTER_BYTES);
    uint32_t first;
    ASSERT_TRUE(m_writer.AddFile(m_games, "frag.bin", data.data(), (uint32_t)data.size(), &first));
    m_writer.SetFat(first, first + 2);
    m_writer.SetFat(first + 2, first + 1);
    m_writer.SetFat(first + 1, FATX_FAT_END);
    ASSERT_TRUE(m_writer.Flush());

    CollectingSink sink;
    ASSERT_EQ(ReadInto(m_device, FindEntry(m_device, m_games, "frag.bin"), sink), FATX_FOLDER_OK);
    ASSERT_EQ(sink.received.size(), data.size());
    EXPECT_EQ(sink.received[0], 'A');
    EXPECT_EQ(sink.received[CLUSTER_BYTES], 'C');
    EXPECT_EQ(sink.received[2 * CLUSTER_BYTES], 'B');
}

TEST_F(FolderImage, AnEmptyFileSendsNothing)
{
    ASSERT_TRUE(m_writer.AddFile(m_games, "empty", "", 0));
    ASSERT_TRUE(m_writer.Flush());
    CollectingSink sink;
    EXPECT_EQ(ReadInto(m_device, FindEntry(m_device, m_games, "empty"), sink), FATX_FOLDER_OK);
    EXPECT_TRUE(sink.received.empty());
}

TEST_F(FolderImage, AChainShorterThanTheSizeIsBroken)
{
    FatxFolderEntry file = FindEntry(m_device, m_games, "save.dat");
    file.size = CLUSTER_BYTES * 3; // Claims three clusters, has one
    CollectingSink sink;
    EXPECT_EQ(ReadInto(m_device, file, sink), FATX_FOLDER_BROKEN_CHAIN);
}

TEST_F(FolderImage, ARefusedWriteStopsTheRead)
{
    CollectingSink sink(true);
    EXPECT_EQ(ReadInto(m_device, FindEntry(m_device, m_games, "save.dat"), sink), FATX_FOLDER_READ_ERROR);
}

TEST(FatxFolderReader, NotFatxWithoutAHeader)
{
    FatxMemoryDevice device(IMAGE_BYTES);
    FatxFolderReader reader(device, IMAGE_BYTES);
    EXPECT_EQ(reader.Open(), FATX_FOLDER_NOT_FATX);
}

TEST(FatxFolderReader, RefusesAPartitionMidResize)
{
    FatxMemoryDevice device(IMAGE_BYTES);
    const uint32_t signature = FATX_RESIZING_SIGNATURE;
    ASSERT_TRUE(device.Write(0, &signature, sizeof(signature)));
    FatxFolderReader reader(device, IMAGE_BYTES);
    EXPECT_EQ(reader.Open(), FATX_FOLDER_RESIZING);
}

TEST(FatxTimeText, ShowsDateAndTime)
{
    const uint16_t date = 15 | (11 << 5) | (1 << 9);
    const uint16_t time = (12 << 11) | (34 << 5);
    EXPECT_EQ(fatx_time_text(date, time), "2001-11-15 12:34");
}

TEST(WindowsFileName, KeepsAnOrdinaryName)
{
    EXPECT_EQ(windows_file_name("default.xbe"), "default.xbe");
}

TEST(WindowsFileName, ReplacesCharactersWindowsForbids)
{
    EXPECT_EQ(windows_file_name("a?b"), "a_b");
}

TEST(WindowsFileName, ReplacesATrailingDotOrSpace)
{
    EXPECT_EQ(windows_file_name("name."), "name_");
    EXPECT_EQ(windows_file_name("name "), "name_");
}

TEST(WindowsFileName, PrefixesDeviceNames)
{
    EXPECT_EQ(windows_file_name("con"), "_con");
    EXPECT_EQ(windows_file_name("NUL.txt"), "_NUL.txt");
    EXPECT_EQ(windows_file_name("COM1"), "_COM1");
    EXPECT_EQ(windows_file_name("COM10"), "COM10");
    EXPECT_EQ(windows_file_name("console"), "console");
}

TEST(FatxTimeText, EmptyWithoutADate)
{
    EXPECT_EQ(fatx_time_text(0, 0), "");
}
