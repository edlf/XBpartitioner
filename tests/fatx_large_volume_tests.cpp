// FATX volumes too big for an image in memory (on a sparse disk), and
// memory units, whose header counts clusters in 4 KB sectors

#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <vector>
#include "fatx_folder.hpp"
#include "fatx_format.hpp"
#include "test_images.hpp"

namespace
{

const uint64_t SMALL = 8 * MB;
// Over 0xFFF0 clusters of 16 KB, for 32 bit FATs
const uint64_t THIRTY_TWO_BIT = 0x10000ULL * CLUSTER;

struct CheckRun
{
    FatxCheckResult result;
    bool ran;
};

CheckRun RunCheck(FatxDevice &device, uint64_t size)
{
    std::unique_ptr<CheckRun> run = std::make_unique<CheckRun>();
    run->ran = FatxCheck(device, size, &run->result);
    return *run;
}

std::string Messages(const FatxCheckResult &result)
{
    std::string text;
    for (uint32_t i = 0; i < result.message_count; i++)
    {
        text += std::string(result.messages[i].text) + "\n";
    }
    return text;
}

bool IsSpotless(FatxDevice &device, uint64_t size)
{
    const CheckRun run = RunCheck(device, size);
    return run.ran && run.result.errors == 0 && run.result.warnings == 0;
}

} // namespace

TEST(FatxCheck, ThirtyTwoBitFat)
{
    const uint64_t size = THIRTY_TWO_BIT;
    SparseDevice device;
    FatxWriter writer(device, size, CLUSTER);
    ASSERT_EQ(writer.Layout().bytes_per_entry, 4u);
    ASSERT_TRUE(writer.Format());
    std::vector<unsigned char> data(5000, 'x');
    ASSERT_TRUE(writer.AddFile(writer.RootCluster(), "data", data.data(), (uint32_t)data.size()));
    ASSERT_TRUE(writer.Flush());

    const CheckRun check = RunCheck(device, size);
    ASSERT_TRUE(check.ran);
    EXPECT_EQ(check.result.layout.bytes_per_entry, 4u);
    EXPECT_EQ(check.result.errors, 0u) << Messages(check.result);
    EXPECT_EQ(check.result.files, 1u);
}

TEST(FatxRepair, ThirtyTwoBitFatAcrossChunks)
{
    // A 256 KB FAT read in 64 KB chunks, with changes in the first and last
    // chunk, so the repair has to write back a chunk before moving on
    const uint64_t size = THIRTY_TWO_BIT;
    SparseDevice device;
    FatxWriter writer(device, size, CLUSTER);
    ASSERT_TRUE(writer.Format());
    std::vector<unsigned char> data(CLUSTER * 300, 'x');
    uint32_t first;
    ASSERT_TRUE(writer.AddFile(writer.RootCluster(), "data", data.data(), (uint32_t)data.size(), &first));
    const uint32_t last = (uint32_t)writer.Layout().cluster_limit - 1;
    writer.SetFat(last, FATX_FAT_END);
    ASSERT_TRUE(writer.Flush());

    // Shrink it to 1 cluster: 299 to free, plus the lost one
    FatxDirent entry;
    uint64_t offset;
    ASSERT_TRUE(writer.GetEntry(writer.RootCluster(), 0, &entry, &offset));
    entry.file_size = 1000;
    ASSERT_TRUE(writer.SetEntry(offset, entry));

    FatxRepairResult *repair = new FatxRepairResult;
    ASSERT_TRUE(FatxRepair(device, size, repair, true));
    EXPECT_EQ(repair->files_trimmed, 1u);
    EXPECT_EQ(repair->clusters_freed, 300u);
    delete repair;
    EXPECT_TRUE(IsSpotless(device, size));
}

// --- Memory units: the header counts 4 KB sectors (FatxClusterBytes)

TEST(FatxClusterBytes, HardDrivesCount512ByteSectors)
{
    EXPECT_EQ(FatxClusterBytes(32, FATX_MEDIUM_UNKNOWN), 16u * 1024);
    EXPECT_EQ(FatxClusterBytes(2048, FATX_MEDIUM_UNKNOWN), 1024u * 1024);
}

TEST(FatxClusterBytes, MemoryUnitsCount4KbSectors)
{
    // The header of a USB stick the Xbox formatted: 4 sectors, 16 KB clusters
    EXPECT_EQ(FatxClusterBytes(4, FATX_MEDIUM_UNKNOWN), 16u * 1024);
    EXPECT_EQ(FatxClusterBytes(1, FATX_MEDIUM_UNKNOWN), 4u * 1024);
}

TEST(FatxClusterBytes, ZeroForAnOddValue)
{
    EXPECT_EQ(FatxClusterBytes(0, FATX_MEDIUM_UNKNOWN), 0u);
    EXPECT_EQ(FatxClusterBytes(3, FATX_MEDIUM_UNKNOWN), 0u);
    EXPECT_EQ(FatxClusterBytes(4096, FATX_MEDIUM_UNKNOWN), 0u);
}

TEST(FatxClusterBytes, AKnownMediumDecides)
{
    // The same header value on each medium
    EXPECT_EQ(FatxClusterBytes(16, FATX_MEDIUM_MEMORY_UNIT), 64u * 1024);
    EXPECT_EQ(FatxClusterBytes(64, FATX_MEDIUM_HARD_DRIVE), 32u * 1024);
}

TEST(FatxClusterBytes, ZeroForAValueTheMediumCantHave)
{
    // A memory unit's 4 opened as a hard drive partition dump, and back
    EXPECT_EQ(FatxClusterBytes(4, FATX_MEDIUM_HARD_DRIVE), 0u);
    EXPECT_EQ(FatxClusterBytes(32, FATX_MEDIUM_MEMORY_UNIT), 0u);
}

TEST(FatxSectorsPerCluster, EachKindCountsItsOwnSectors)
{
    EXPECT_EQ(FatxSectorsPerCluster(16 * 1024, false), 32u);
    EXPECT_EQ(FatxSectorsPerCluster(16 * 1024, true), 4u);
}

TEST(FatxSectorsPerCluster, ZeroForASizeTheKindCantCount)
{
    EXPECT_EQ(FatxSectorsPerCluster(8 * 1024, false), 0u);   // Hard drives: 16 KB and up
    EXPECT_EQ(FatxSectorsPerCluster(128 * 1024, true), 0u);  // Memory units: up to 64 KB
    EXPECT_EQ(FatxSectorsPerCluster(24 * 1024, false), 0u);  // Not a power of two
}

TEST(FatxFormatMemoryUnit, WritesFourSectorsFor16KbClusters)
{
    FatxMemoryDevice device(SMALL);
    ASSERT_TRUE(FatxFormatMemoryUnit(device, SMALL / HDD_SECTOR_SIZE, CLUSTER, 1));
    uint32_t sectors = 0;
    ASSERT_TRUE(device.Read(FATX_SECTORS_PER_CLUSTER_OFFSET, &sectors, 4));
    EXPECT_EQ(sectors, 4u);
}

TEST(FatxFormatMemoryUnit, ReadsBackAsAClean16KbVolume)
{
    // Like the 3.75 GB stick that read as 2 KB clusters before
    const uint64_t size = 3840ULL * MB;
    SparseDevice device;
    ASSERT_TRUE(FatxFormatMemoryUnit(device, size / HDD_SECTOR_SIZE, CLUSTER, 1));
    FatxWriter writer(device, size);
    ASSERT_TRUE(writer.Load());
    ASSERT_EQ(writer.ClusterBytes(), CLUSTER);
    ASSERT_TRUE(writer.AddFile(writer.RootCluster(), "save.dat", "12345", 5));
    ASSERT_TRUE(writer.Flush());

    const CheckRun check = RunCheck(device, size);
    ASSERT_TRUE(check.ran);
    EXPECT_EQ(check.result.cluster_bytes, CLUSTER);
    EXPECT_EQ(check.result.errors, 0u) << Messages(check.result);
    EXPECT_EQ(check.result.files, 1u);

    FatxFolderReader folders(device, size);
    ASSERT_EQ(folders.Open(), FATX_FOLDER_OK);
    std::vector<FatxFolderEntry> entries;
    ASSERT_EQ(folders.List(folders.RootCluster(), &entries), FATX_FOLDER_OK);
    ASSERT_EQ(entries.size(), 1u);
    EXPECT_EQ(entries[0].name, "save.dat");
}
