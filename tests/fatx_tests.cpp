// Tests for the FATX check (src/fatx_check.cpp): images are made in memory
// with tools/fatxtool's writer, then broken on purpose.

#include <gtest/gtest.h>
#include <string.h>
#include <string>
#include <vector>
#include "fatx_check.hpp"
#include "fatx_writer.hpp"

namespace
{

const uint64_t SMALL = 8 * 1024 * 1024; // 16 KB clusters: 511 of them
const uint32_t CLUSTER = 16 * 1024;

struct Check
{
    FatxCheckResult result;
    bool ran;
};

Check RunCheck(FatxDevice &device, uint64_t size)
{
    Check check;
    check.ran = FatxCheck(device, size, &check.result);
    return check;
}

bool HasMessage(const FatxCheckResult &result, bool error, const char *text)
{
    for (uint32_t i = 0; i < result.message_count; i++)
    {
        if (result.messages[i].error == error && strstr(result.messages[i].text, text) != NULL)
        {
            return true;
        }
    }
    return false;
}

std::string Messages(const FatxCheckResult &result)
{
    std::string out;
    for (uint32_t i = 0; i < result.message_count; i++)
    {
        out += result.messages[i].error ? "ERROR: " : "warning: ";
        out += result.messages[i].text;
        out += "\n";
    }
    return out;
}

// \apps (dir) with a.txt (3 clusters) and b.txt (1), \c.bin (empty)
struct Image
{
    FatxMemoryDevice device;
    FatxWriter writer;
    uint32_t apps;
    uint32_t a_first;
    uint32_t b_first;

    Image() : device(SMALL), writer(device, SMALL, CLUSTER), apps(0), a_first(0), b_first(0)
    {
        EXPECT_TRUE(writer.Format());
        std::vector<unsigned char> a(CLUSTER * 2 + 100, 'a');
        std::vector<unsigned char> b(10, 'b');
        EXPECT_TRUE(writer.MakeDirectory(writer.RootCluster(), "apps", &apps));
        EXPECT_TRUE(writer.AddFile(apps, "a.txt", a.data(), (uint32_t)a.size(), &a_first));
        EXPECT_TRUE(writer.AddFile(apps, "b.txt", b.data(), (uint32_t)b.size(), &b_first));
        EXPECT_TRUE(writer.AddFile(writer.RootCluster(), "c.bin", NULL, 0));
        EXPECT_TRUE(writer.Flush());
    }

    Check Run() { return RunCheck(device, SMALL); }

    // Changes an entry of \apps (0 a.txt, 1 b.txt) through a function
    template <typename F> void EditEntry(uint32_t index, F edit)
    {
        FatxDirent entry;
        uint64_t offset;
        ASSERT_TRUE(writer.GetEntry(apps, index, &entry, &offset));
        edit(entry);
        ASSERT_TRUE(writer.SetEntry(offset, entry));
    }

    // A cluster nothing uses
    uint32_t FreeCluster() const
    {
        for (uint32_t c = 1; c < writer.Layout().cluster_limit; c++)
        {
            if (writer.GetFat(c) == FATX_FAT_FREE)
            {
                return c;
            }
        }
        return 0;
    }
};

class CountingProgress : public FatxProgress
{
public:
    CountingProgress() : last(-1), calls(0), decreased(false) {}
    void Update(int percent) override
    {
        decreased = decreased || percent < last;
        last = percent;
        calls++;
    }
    int last;
    int calls;
    bool decreased;
};

} // namespace

TEST(FatxCheck, EmptyIsClean)
{
    FatxMemoryDevice device(SMALL);
    FatxWriter writer(device, SMALL, CLUSTER);
    ASSERT_TRUE(writer.Format());
    const Check check = RunCheck(device, SMALL);
    ASSERT_TRUE(check.ran);
    EXPECT_EQ(check.result.status, FATX_CHECK_DONE);
    EXPECT_EQ(check.result.errors, 0u);
    EXPECT_EQ(check.result.warnings, 0u);
    EXPECT_EQ(check.result.used_clusters, 1u); // The root
    EXPECT_EQ(check.result.cluster_bytes, CLUSTER);
}

TEST(FatxCheck, FilesAndDirectoriesAreClean)
{
    Image image;
    const Check check = image.Run();
    ASSERT_TRUE(check.ran);
    EXPECT_EQ(check.result.errors, 0u) << Messages(check.result);
    EXPECT_EQ(check.result.warnings, 0u) << Messages(check.result);
    EXPECT_EQ(check.result.files, 3u);
    EXPECT_EQ(check.result.directories, 1u);
    EXPECT_EQ(check.result.used_clusters, 1u + 1u + 3u + 1u); // root, apps, a, b
    EXPECT_EQ(check.result.lost_clusters, 0u);
}

TEST(FatxCheck, DirectorySpanningClusters)
{
    // 256 entries per 16 KB cluster: 600 (empty) files need three
    FatxMemoryDevice device(SMALL);
    FatxWriter writer(device, SMALL, CLUSTER);
    ASSERT_TRUE(writer.Format());
    for (int i = 0; i < 600; i++)
    {
        char name[16];
        sprintf_s(name, "file%d", i);
        ASSERT_TRUE(writer.AddFile(writer.RootCluster(), name, NULL, 0));
    }
    ASSERT_TRUE(writer.Flush());
    const Check check = RunCheck(device, SMALL);
    EXPECT_EQ(check.result.errors, 0u) << Messages(check.result);
    EXPECT_EQ(check.result.files, 600u);
}

TEST(FatxCheck, ThirtyTwoBitFat)
{
    // 512 byte clusters so 0xFFF0 of them fit in a small image
    const uint64_t size = 0x10000ULL * 512;
    FatxMemoryDevice device(size);
    FatxWriter writer(device, size, 512);
    ASSERT_EQ(writer.Layout().bytes_per_entry, 4u);
    ASSERT_TRUE(writer.Format());
    std::vector<unsigned char> data(5000, 'x');
    ASSERT_TRUE(writer.AddFile(writer.RootCluster(), "data", data.data(), (uint32_t)data.size()));
    ASSERT_TRUE(writer.Flush());

    const Check check = RunCheck(device, size);
    ASSERT_TRUE(check.ran);
    EXPECT_EQ(check.result.layout.bytes_per_entry, 4u);
    EXPECT_EQ(check.result.errors, 0u) << Messages(check.result);
    EXPECT_EQ(check.result.files, 1u);
}

TEST(FatxCheck, NotFatx)
{
    FatxMemoryDevice device(SMALL);
    const Check check = RunCheck(device, SMALL);
    EXPECT_FALSE(check.ran);
    EXPECT_EQ(check.result.status, FATX_CHECK_NOT_FATX);
}

TEST(FatxCheck, BadClusterSize)
{
    Image image;
    const uint32_t sectors = 3;
    memcpy(image.device.Data() + FATX_SECTORS_PER_CLUSTER_OFFSET, &sectors, 4);
    const Check check = image.Run();
    EXPECT_FALSE(check.ran);
    EXPECT_EQ(check.result.status, FATX_CHECK_BAD_HEADER);
    EXPECT_EQ(check.result.errors, 1u);
}

TEST(FatxCheck, RootOutsideThePartition)
{
    Image image;
    const uint32_t root = 100000;
    memcpy(image.device.Data() + FATX_ROOT_CLUSTER_OFFSET, &root, 4);
    const Check check = image.Run();
    EXPECT_EQ(check.result.status, FATX_CHECK_BAD_HEADER);
}

TEST(FatxCheck, ImageShorterThanThePartition)
{
    // Its FAT would end past the 8 MB there are
    Image image;
    const Check check = RunCheck(image.device, SMALL * 4096);
    EXPECT_FALSE(check.ran);
    EXPECT_EQ(check.result.status, FATX_CHECK_READ_ERROR);
}

TEST(FatxCheck, CrossLinkedFiles)
{
    Image image;
    image.EditEntry(1, [&](FatxDirent &e) { e.first_cluster = image.a_first; });
    const Check check = image.Run();
    EXPECT_TRUE(HasMessage(check.result, true, "used twice")) << Messages(check.result);
    // b.txt's own cluster is now used by nothing
    EXPECT_EQ(check.result.lost_clusters, 1u);
    EXPECT_TRUE(HasMessage(check.result, false, "lost"));
}

TEST(FatxCheck, LoopInAChain)
{
    Image image;
    const uint32_t second = image.writer.GetFat(image.a_first);
    const uint32_t third = image.writer.GetFat(second);
    image.writer.SetFat(third, image.a_first);
    ASSERT_TRUE(image.writer.Flush());
    const Check check = image.Run();
    EXPECT_TRUE(HasMessage(check.result, true, "used twice")) << Messages(check.result);
}

TEST(FatxCheck, ChainRunsIntoFreeCluster)
{
    Image image;
    image.writer.SetFat(image.a_first, image.FreeCluster());
    ASSERT_TRUE(image.writer.Flush());
    const Check check = image.Run();
    EXPECT_TRUE(HasMessage(check.result, true, "free cluster")) << Messages(check.result);
    EXPECT_TRUE(HasMessage(check.result, true, "bytes need 3 clusters, the chain has 2")) << Messages(check.result);
}

TEST(FatxCheck, ChainPointsOutsideThePartition)
{
    Image image;
    image.writer.SetFat(image.a_first, (uint32_t)image.writer.Layout().cluster_limit + 5);
    ASSERT_TRUE(image.writer.Flush());
    const Check check = image.Run();
    EXPECT_TRUE(HasMessage(check.result, true, "outside the partition")) << Messages(check.result);
    EXPECT_TRUE(HasMessage(check.result, true, "FAT entries point outside")) << Messages(check.result);
}

TEST(FatxCheck, BadMarkerInAChain)
{
    Image image;
    image.writer.SetFat(image.a_first, FATX_FAT_BAD);
    ASSERT_TRUE(image.writer.Flush());
    const Check check = image.Run();
    EXPECT_TRUE(HasMessage(check.result, true, "marker 0xFFFFFFF7")) << Messages(check.result);
}

TEST(FatxCheck, SizeLongerThanTheChain)
{
    Image image;
    image.EditEntry(1, [](FatxDirent &e) { e.file_size = CLUSTER * 5; });
    const Check check = image.Run();
    EXPECT_TRUE(HasMessage(check.result, true, "need 5 clusters, the chain has 1")) << Messages(check.result);
}

TEST(FatxCheck, ChainLongerThanTheSizeIsAWarning)
{
    Image image;
    image.EditEntry(0, [](FatxDirent &e) { e.file_size = 10; });
    const Check check = image.Run();
    EXPECT_EQ(check.result.errors, 0u) << Messages(check.result);
    EXPECT_TRUE(HasMessage(check.result, false, "2 clusters more")) << Messages(check.result);
}

TEST(FatxCheck, FileWithSizeButNoCluster)
{
    Image image;
    image.EditEntry(1, [](FatxDirent &e) { e.first_cluster = 0; });
    const Check check = image.Run();
    EXPECT_TRUE(HasMessage(check.result, true, "but no clusters")) << Messages(check.result);
}

TEST(FatxCheck, LostClustersAreAWarning)
{
    Image image;
    image.writer.SetFat(image.FreeCluster(), FATX_FAT_END);
    ASSERT_TRUE(image.writer.Flush());
    const Check check = image.Run();
    EXPECT_EQ(check.result.errors, 0u) << Messages(check.result);
    EXPECT_EQ(check.result.lost_clusters, 1u);
}

TEST(FatxCheck, BadClustersAreCountedNotLost)
{
    Image image;
    image.writer.SetFat(image.FreeCluster(), FATX_FAT_BAD);
    ASSERT_TRUE(image.writer.Flush());
    const Check check = image.Run();
    EXPECT_EQ(check.result.errors, 0u) << Messages(check.result);
    EXPECT_EQ(check.result.bad_clusters, 1u);
    EXPECT_EQ(check.result.lost_clusters, 0u);
}

TEST(FatxCheck, DeletedEntriesAreSkipped)
{
    Image image;
    image.EditEntry(1, [](FatxDirent &e) { e.name_length = FATX_DIRENT_DELETED; });
    const Check check = image.Run();
    EXPECT_EQ(check.result.errors, 0u) << Messages(check.result);
    EXPECT_EQ(check.result.files, 2u);
    EXPECT_EQ(check.result.lost_clusters, 1u); // Its cluster, still marked used
}

TEST(FatxCheck, BadNameLength)
{
    Image image;
    image.EditEntry(1, [](FatxDirent &e) { e.name_length = 50; });
    const Check check = image.Run();
    EXPECT_TRUE(HasMessage(check.result, true, "bad name length (50)")) << Messages(check.result);
}

TEST(FatxCheck, NameWithForbiddenCharacters)
{
    Image image;
    image.EditEntry(1, [](FatxDirent &e) { e.name[0] = '*'; });
    const Check check = image.Run();
    EXPECT_EQ(check.result.errors, 0u) << Messages(check.result);
    EXPECT_TRUE(HasMessage(check.result, false, "\\apps\\*.txt")) << Messages(check.result);
}

TEST(FatxCheck, SameNameTwiceIgnoringCase)
{
    Image image;
    image.EditEntry(1, [](FatxDirent &e) { memcpy(e.name, "A.TXT", 5); });
    const Check check = image.Run();
    EXPECT_TRUE(HasMessage(check.result, true, "this name twice")) << Messages(check.result);
}

TEST(FatxCheck, DirectoryWithoutCluster)
{
    Image image;
    FatxDirent entry;
    uint64_t offset;
    ASSERT_TRUE(image.writer.GetEntry(image.writer.RootCluster(), 0, &entry, &offset));
    ASSERT_TRUE(entry.attributes & FATX_ATTR_DIRECTORY);
    entry.first_cluster = 0;
    ASSERT_TRUE(image.writer.SetEntry(offset, entry));
    const Check check = image.Run();
    EXPECT_TRUE(HasMessage(check.result, true, "folder without a cluster")) << Messages(check.result);
}

TEST(FatxCheck, DirectoryLoop)
{
    // \apps\loop points back at \apps: its clusters are reached twice
    Image image;
    ASSERT_TRUE(image.writer.MakeDirectory(image.apps, "loop"));
    ASSERT_TRUE(image.writer.Flush());
    image.EditEntry(2, [&](FatxDirent &e) { e.first_cluster = image.apps; });
    const Check check = image.Run();
    ASSERT_TRUE(check.ran);
    EXPECT_TRUE(HasMessage(check.result, true, "used twice")) << Messages(check.result);
}

TEST(FatxCheck, MessagesAreCappedButCounted)
{
    FatxMemoryDevice device(SMALL);
    FatxWriter writer(device, SMALL, CLUSTER);
    ASSERT_TRUE(writer.Format());
    for (int i = 0; i < 100; i++)
    {
        char name[16];
        sprintf_s(name, "f%d", i);
        ASSERT_TRUE(writer.AddFile(writer.RootCluster(), name, "x", 1));
    }
    // Every file claims 2 clusters
    for (uint32_t i = 0; i < 100; i++)
    {
        FatxDirent entry;
        uint64_t offset;
        ASSERT_TRUE(writer.GetEntry(writer.RootCluster(), i, &entry, &offset));
        entry.file_size = CLUSTER * 2;
        ASSERT_TRUE(writer.SetEntry(offset, entry));
    }
    ASSERT_TRUE(writer.Flush());
    const Check check = RunCheck(device, SMALL);
    EXPECT_EQ(check.result.errors, 100u);
    EXPECT_EQ(check.result.message_count, (uint32_t)FATX_CHECK_MAX_MESSAGES);
}

TEST(FatxCheck, ProgressGoesUpTo100)
{
    Image image;
    CountingProgress progress;
    FatxCheckResult *result = new FatxCheckResult;
    EXPECT_TRUE(FatxCheck(image.device, SMALL, result, &progress));
    EXPECT_EQ(progress.last, 100);
    EXPECT_FALSE(progress.decreased);
    delete result;
}

TEST(FatxCheck, WriterMatchesTheKernelLayout)
{
    // The writer and the check both use FatxLayoutFor: the data starts
    // after the header and the page rounded FAT
    FatxMemoryDevice device(SMALL);
    FatxWriter writer(device, SMALL, CLUSTER);
    EXPECT_EQ(writer.Layout().data_offset, FATX_HEADER_BYTES + writer.Layout().bytes_per_fat);
    EXPECT_EQ(writer.ClusterOffset(1), writer.Layout().data_offset);
    // 8 MB / 16 KB + 1 entries of 2 bytes, rounded up to 4 KB
    EXPECT_EQ(writer.Layout().bytes_per_fat, 0x1000u);
}

// --- Safe repairs (FatxRepair)

namespace
{

std::vector<unsigned char> Snapshot(FatxMemoryDevice &device)
{
    return std::vector<unsigned char>(device.Data(), device.Data() + device.Size());
}

bool IsSpotless(FatxDevice &device, uint64_t size)
{
    FatxCheckResult *result = new FatxCheckResult;
    const bool ran = FatxCheck(device, size, result);
    const bool clean = ran && result->errors == 0 && result->warnings == 0;
    delete result;
    return clean;
}

struct Repair
{
    FatxRepairResult result;
    bool ok;
};

Repair RunRepair(FatxDevice &device, uint64_t size, bool write)
{
    Repair repair;
    repair.ok = FatxRepair(device, size, &repair.result, write);
    return repair;
}

// Reads but never writes
class ReadOnlyDevice : public FatxDevice
{
public:
    explicit ReadOnlyDevice(FatxDevice &inner) : m_inner(inner) {}
    bool Read(uint64_t offset, void *buffer, uint32_t length) override { return m_inner.Read(offset, buffer, length); }
    bool Write(uint64_t, const void *, uint32_t) override { return false; }

private:
    FatxDevice &m_inner;
};

} // namespace

TEST(FatxRepair, NothingToDoWhenClean)
{
    Image image;
    const std::vector<unsigned char> before = Snapshot(image.device);
    const Repair repair = RunRepair(image.device, SMALL, true);
    EXPECT_TRUE(repair.ok);
    EXPECT_EQ(repair.result.status, FATX_REPAIR_NOTHING_TO_DO);
    EXPECT_FALSE(repair.result.written);
    EXPECT_TRUE(Snapshot(image.device) == before);
}

TEST(FatxRepair, FreesLostClustersBackToTheCleanImage)
{
    Image image;
    const std::vector<unsigned char> clean = Snapshot(image.device);
    const uint32_t lost = image.FreeCluster();
    image.writer.SetFat(lost, lost + 1);
    image.writer.SetFat(lost + 1, FATX_FAT_END);
    ASSERT_TRUE(image.writer.Flush());

    const Repair repair = RunRepair(image.device, SMALL, true);
    ASSERT_TRUE(repair.ok);
    EXPECT_EQ(repair.result.status, FATX_REPAIR_DONE);
    EXPECT_TRUE(repair.result.written);
    EXPECT_EQ(repair.result.clusters_freed, 2u);
    EXPECT_TRUE(Snapshot(image.device) == clean);
}

TEST(FatxRepair, DryRunWritesNothing)
{
    Image image;
    image.writer.SetFat(image.FreeCluster(), FATX_FAT_END);
    image.writer.SetFat(0, 0);
    ASSERT_TRUE(image.writer.Flush());
    const std::vector<unsigned char> before = Snapshot(image.device);

    const Repair repair = RunRepair(image.device, SMALL, false);
    ASSERT_TRUE(repair.ok);
    EXPECT_EQ(repair.result.status, FATX_REPAIR_DONE);
    EXPECT_FALSE(repair.result.written);
    EXPECT_EQ(repair.result.clusters_freed, 1u);
    EXPECT_TRUE(repair.result.media_fixed);
    EXPECT_TRUE(Snapshot(image.device) == before);
}

TEST(FatxRepair, TrimsLongChainsAndKeepsTheData)
{
    Image image;
    image.EditEntry(0, [](FatxDirent &e) { e.file_size = 10; }); // a.txt: 3 clusters, now needs 1

    const Repair repair = RunRepair(image.device, SMALL, true);
    ASSERT_TRUE(repair.ok);
    EXPECT_EQ(repair.result.files_trimmed, 1u);
    EXPECT_EQ(repair.result.clusters_freed, 2u);
    EXPECT_TRUE(IsSpotless(image.device, SMALL));

    // The chain ends after its first cluster, which still holds the data
    FatxWriter reloaded(image.device, SMALL);
    ASSERT_TRUE(reloaded.Load());
    EXPECT_EQ(reloaded.GetFat(image.a_first), FATX_FAT_END);
    unsigned char data[10];
    ASSERT_TRUE(image.device.Read(reloaded.ClusterOffset(image.a_first), data, sizeof(data)));
    for (unsigned char c : data)
    {
        EXPECT_EQ(c, 'a');
    }
}

TEST(FatxRepair, EmptyFileLosesItsClusters)
{
    Image image;
    image.EditEntry(1, [](FatxDirent &e) { e.file_size = 0; }); // b.txt keeps its cluster

    const Repair repair = RunRepair(image.device, SMALL, true);
    ASSERT_TRUE(repair.ok);
    EXPECT_EQ(repair.result.empty_files_cleared, 1u);
    EXPECT_EQ(repair.result.clusters_freed, 1u);
    EXPECT_TRUE(IsSpotless(image.device, SMALL));

    FatxDirent entry;
    ASSERT_TRUE(image.writer.GetEntry(image.apps, 1, &entry));
    EXPECT_EQ(entry.first_cluster, 0u);
    EXPECT_EQ(memcmp(entry.name, "b.txt", 5), 0); // Nothing else changed
}

TEST(FatxRepair, SetsFatEntry0Back)
{
    Image image;
    image.writer.SetFat(0, 0);
    ASSERT_TRUE(image.writer.Flush());
    const Repair repair = RunRepair(image.device, SMALL, true);
    ASSERT_TRUE(repair.ok);
    EXPECT_TRUE(repair.result.media_fixed);
    EXPECT_TRUE(IsSpotless(image.device, SMALL));
}

TEST(FatxRepair, RefusesWithErrors)
{
    // Cross-linked: b.txt's own cluster looks lost, but freeing it is only
    // right if b.txt really is the broken one
    Image image;
    image.EditEntry(1, [&](FatxDirent &e) { e.first_cluster = image.a_first; });
    const std::vector<unsigned char> before = Snapshot(image.device);

    const Repair repair = RunRepair(image.device, SMALL, true);
    EXPECT_FALSE(repair.ok);
    EXPECT_EQ(repair.result.status, FATX_REPAIR_HAS_ERRORS);
    EXPECT_GT(repair.result.check.errors, 0u);
    EXPECT_TRUE(Snapshot(image.device) == before);
}

TEST(FatxRepair, NotFatx)
{
    FatxMemoryDevice device(SMALL);
    const Repair repair = RunRepair(device, SMALL, true);
    EXPECT_FALSE(repair.ok);
    EXPECT_EQ(repair.result.status, FATX_REPAIR_NOT_CHECKED);
    EXPECT_EQ(repair.result.check.status, FATX_CHECK_NOT_FATX);
}

TEST(FatxRepair, WriteErrorStopsPartway)
{
    Image image;
    image.writer.SetFat(image.FreeCluster(), FATX_FAT_END);
    ASSERT_TRUE(image.writer.Flush());
    ReadOnlyDevice read_only(image.device);
    const Repair repair = RunRepair(read_only, SMALL, true);
    EXPECT_FALSE(repair.ok);
    EXPECT_EQ(repair.result.status, FATX_REPAIR_STOPPED_PARTWAY);
}

TEST(FatxRepair, ThirtyTwoBitFatAcrossChunks)
{
    // 512 byte clusters, 4 byte entries: a 256 KB FAT, read in 64 KB chunks.
    // The trimmed file is in the first chunk, the lost cluster in the last,
    // so the repair writes one chunk back before moving to another.
    const uint64_t size = 0x10000ULL * 512;
    FatxMemoryDevice device(size);
    FatxWriter writer(device, size, 512);
    ASSERT_TRUE(writer.Format());
    std::vector<unsigned char> data(512 * 300, 'x');
    uint32_t first;
    ASSERT_TRUE(writer.AddFile(writer.RootCluster(), "data", data.data(), (uint32_t)data.size(), &first));
    const uint32_t last = (uint32_t)writer.Layout().cluster_limit - 1;
    writer.SetFat(last, FATX_FAT_END);
    ASSERT_TRUE(writer.Flush());

    // Shrink it to 2 clusters: 298 to free, plus the lost one
    FatxDirent entry;
    uint64_t offset;
    ASSERT_TRUE(writer.GetEntry(writer.RootCluster(), 0, &entry, &offset));
    entry.file_size = 1000;
    ASSERT_TRUE(writer.SetEntry(offset, entry));

    FatxRepairResult *repair = new FatxRepairResult;
    ASSERT_TRUE(FatxRepair(device, size, repair, true));
    EXPECT_EQ(repair->files_trimmed, 1u);
    EXPECT_EQ(repair->clusters_freed, 299u);
    delete repair;
    EXPECT_TRUE(IsSpotless(device, size));
}

TEST(FatxRepair, ProgressGoesUpTo100)
{
    Image image;
    image.writer.SetFat(image.FreeCluster(), FATX_FAT_END);
    ASSERT_TRUE(image.writer.Flush());
    CountingProgress progress;
    FatxRepairResult *repair = new FatxRepairResult;
    EXPECT_TRUE(FatxRepair(image.device, SMALL, repair, true, &progress));
    EXPECT_EQ(progress.last, 100);
    EXPECT_FALSE(progress.decreased);
    delete repair;
}
