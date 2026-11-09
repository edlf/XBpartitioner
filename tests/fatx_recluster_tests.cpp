// Tests for changing the cluster size. Every file, name, attribute, time and
// header field must survive, including a power cut during the copy back.

#include <gtest/gtest.h>
#include <string.h>
#include "fatx_check.hpp"
#include "fatx_recluster.hpp"
#include "fatx_writer.hpp"
#include "test_images.hpp"

namespace
{

const uint32_t SERIAL = 0xCAFEBABE;

// 200 MB with 16 KB clusters: big folders, files of all sizes, custom
// attributes and times, and a volume name
void MakeOriginal(SparseDevice &device, uint64_t lba_size, uint32_t cluster_bytes = CLUSTER)
{
    FatxWriter writer(device, lba_size * HDD_SECTOR_SIZE, cluster_bytes);
    ASSERT_TRUE(writer.Format(SERIAL));

    AddFile(writer, writer.RootCluster(), "default.xbe", CLUSTER * 7 + 3, 1);
    AddFile(writer, writer.RootCluster(), "empty", 0, 0);
    uint32_t saves;
    ASSERT_TRUE(writer.MakeDirectory(writer.RootCluster(), "UDATA", &saves));
    for (int i = 0; i < 600; i++) // Several folder clusters, at any cluster size up to 32 KB
    {
        char name[16];
        sprintf_s(name, "save%03d", i);
        AddFile(writer, saves, name, (i % 5) * 5000 + i, 50 + i);
    }
    uint32_t deep;
    ASSERT_TRUE(writer.MakeDirectory(saves, "a", &deep));
    ASSERT_TRUE(writer.MakeDirectory(deep, "b", &deep));
    AddFile(writer, deep, "big.bin", 3 * 1024 * 1024 + 77, 9); // Spans clusters of every size

    // Give a few entries their own attributes and times
    for (uint32_t i = 0; i < 3; i++)
    {
        FatxDirent entry;
        uint64_t offset;
        ASSERT_TRUE(writer.GetEntry(writer.RootCluster(), i, &entry, &offset));
        entry.attributes |= (unsigned char)(0x01 << i); // Read-only, hidden, system
        entry.modified_date = (uint16_t)(0x2A21 + i);
        entry.created_time = (uint16_t)(0x6000 + i * 33);
        entry.accessed_date = (uint16_t)(0x2B00 + i);
        ASSERT_TRUE(writer.SetEntry(offset, entry));
    }
    ASSERT_TRUE(writer.Flush());

    // A volume name in the header
    const wchar_t name[] = L"MY XBOX";
    ASSERT_TRUE(device.Write(16, name, sizeof(name)));
}

std::vector<unsigned char> Header(FatxDevice &device)
{
    std::vector<unsigned char> header(FATX_HEADER_BYTES);
    device.Read(0, header.data(), FATX_HEADER_BYTES);
    return header;
}

// Builds and copies back; returns the status
FatxReclusterStatus Convert(SparseDevice &device, uint64_t lba_size, uint32_t cluster_bytes, FatxReclusterResult *result = NULL)
{
    SparseDevice build_disk;
    FatxRangeRecorder build(build_disk);
    FatxReclusterResult local;
    FatxReclusterResult *out = result ? result : &local;
    if (!FatxBuildReclustered(device, lba_size, cluster_bytes, &build, out))
    {
        return out->status;
    }
    EXPECT_TRUE(FatxCopyReclustered(build, device, build.Ranges()));
    return out->status;
}

// Everything in the header except the cluster size and root must match
void ExpectSameHeader(const std::vector<unsigned char> &before, const std::vector<unsigned char> &after)
{
    for (size_t i = 0; i < before.size(); i++)
    {
        const bool changes = (i >= FATX_SECTORS_PER_CLUSTER_OFFSET && i < FATX_ROOT_CLUSTER_OFFSET + 4);
        if (!changes && before[i] != after[i])
        {
            ADD_FAILURE() << "header byte " << i << " changed";
            return;
        }
    }
}

} // namespace

TEST(FatxRecluster, EveryClusterSizeKeepsEverything)
{
    const uint64_t lba = 200 * MB / HDD_SECTOR_SIZE;
    SparseDevice device;
    MakeOriginal(device, lba);
    const auto before = Contents(device, lba);
    const std::vector<unsigned char> header = Header(device);

    // 603 files need at least 603 clusters, so 256 KB is about the limit at 200 MB
    const uint32_t sizes[] = {32, 64, 128, 256, 16};
    for (uint32_t kb : sizes)
    {
        SCOPED_TRACE(std::to_string(kb) + " KB clusters");
        FatxReclusterResult *result = new FatxReclusterResult;
        ASSERT_EQ(Convert(device, lba, kb * 1024, result), FATX_RECLUSTER_DONE);
        EXPECT_EQ(result->files, 603u);
        EXPECT_EQ(result->folders, 3u);
        delete result;

        const FatxCheckResult check = Check(device, lba);
        ASSERT_EQ(check.status, FATX_CHECK_DONE);
        EXPECT_EQ(check.cluster_bytes, kb * 1024);
        EXPECT_EQ(check.errors, 0u);
        EXPECT_EQ(check.warnings, 0u);
        ExpectSameFiles(before, Contents(device, lba));
        ExpectSameHeader(header, Header(device));
    }
}

TEST(FatxRecluster, LeavesDeletedEntriesAndLostClustersBehind)
{
    const uint64_t lba = 200 * MB / HDD_SECTOR_SIZE;
    SparseDevice device;
    MakeOriginal(device, lba);
    {
        FatxWriter writer(device, lba * HDD_SECTOR_SIZE);
        ASSERT_TRUE(writer.Load());
        FatxDirent entry;
        uint64_t offset;
        ASSERT_TRUE(writer.GetEntry(writer.RootCluster(), 1, &entry, &offset)); // "empty"
        entry.name_length = FATX_DIRENT_DELETED;
        ASSERT_TRUE(writer.SetEntry(offset, entry));
        writer.SetFat(9000, FATX_FAT_END); // Lost
        ASSERT_TRUE(writer.Flush());
    }
    const auto before = Contents(device, lba);
    ASSERT_EQ(Check(device, lba).lost_clusters, 1u);

    ASSERT_EQ(Convert(device, lba, 32 * 1024), FATX_RECLUSTER_DONE);
    const FatxCheckResult check = Check(device, lba);
    EXPECT_EQ(check.errors, 0u);
    EXPECT_EQ(check.warnings, 0u);
    ExpectSameFiles(before, Contents(device, lba));
}

TEST(FatxRecluster, DryRunWritesNothing)
{
    const uint64_t lba = 200 * MB / HDD_SECTOR_SIZE;
    SparseDevice device;
    MakeOriginal(device, lba);
    PowerCutDevice no_writes(device, 0);
    FatxReclusterResult *result = new FatxReclusterResult;
    EXPECT_TRUE(FatxBuildReclustered(no_writes, lba, 64 * 1024, NULL, result));
    EXPECT_EQ(result->status, FATX_RECLUSTER_PLANNED);
    EXPECT_EQ(no_writes.Writes(), 0u);
    EXPECT_GT(result->clusters_needed, 0u);
    delete result;
}

TEST(FatxRecluster, Refusals)
{
    const uint64_t lba = 64 * MB / HDD_SECTOR_SIZE;
    SparseDevice device;
    MakeOriginal(device, lba);
    FatxReclusterResult *result = new FatxReclusterResult;

    EXPECT_EQ(Convert(device, lba, 8 * 1024, result), FATX_RECLUSTER_BAD_SIZE);
    EXPECT_EQ(Convert(device, lba, 24 * 1024, result), FATX_RECLUSTER_BAD_SIZE);
    EXPECT_EQ(Convert(device, lba, 2048 * 1024, result), FATX_RECLUSTER_BAD_SIZE);

    // Past 256 GB XBpartitioner uses 32 KB at least (only the header is read)
    SparseDevice big;
    MakeOriginal(big, lba, 64 * 1024);
    EXPECT_EQ(Convert(big, 0x30000000, 16 * 1024, result), FATX_RECLUSTER_TOO_SMALL);

    // Errors: a file longer than its chain
    {
        FatxWriter writer(device, lba * HDD_SECTOR_SIZE);
        ASSERT_TRUE(writer.Load());
        FatxDirent entry;
        uint64_t offset;
        ASSERT_TRUE(writer.GetEntry(writer.RootCluster(), 0, &entry, &offset));
        entry.file_size *= 10;
        ASSERT_TRUE(writer.SetEntry(offset, entry));
    }
    EXPECT_EQ(Convert(device, lba, 32 * 1024, result), FATX_RECLUSTER_HAS_ERRORS);

    // An unfinished resize
    const uint32_t resizing = FATX_RESIZING_SIGNATURE;
    ASSERT_TRUE(device.Write(0, &resizing, 4));
    EXPECT_EQ(Convert(device, lba, 32 * 1024, result), FATX_RECLUSTER_RESIZING);

    SparseDevice blank;
    EXPECT_EQ(Convert(blank, lba, 32 * 1024, result), FATX_RECLUSTER_NOT_FATX);
    delete result;
}

TEST(FatxRecluster, NoRoom)
{
    // Many small files fit with 16 KB clusters, but not with 64 KB
    const uint64_t lba = 64 * MB / HDD_SECTOR_SIZE;
    SparseDevice device;
    FatxWriter writer(device, lba * HDD_SECTOR_SIZE, CLUSTER);
    ASSERT_TRUE(writer.Format());
    uint32_t dir = writer.RootCluster();
    for (int i = 0; i < 1500; i++)
    {
        if (i % 1000 == 0)
        {
            char folder[16];
            sprintf_s(folder, "f%d", i);
            ASSERT_TRUE(writer.MakeDirectory(writer.RootCluster(), folder, &dir));
        }
        char name[16];
        sprintf_s(name, "x%d", i);
        AddFile(writer, dir, name, 10, i);
    }
    ASSERT_TRUE(writer.Flush());

    FatxReclusterResult *result = new FatxReclusterResult;
    EXPECT_EQ(Convert(device, lba, 64 * 1024, result), FATX_RECLUSTER_NO_ROOM);
    EXPECT_GT(result->clusters_needed, result->clusters_available);
    EXPECT_EQ(Check(device, lba).cluster_bytes, CLUSTER); // Untouched
    delete result;
}

TEST(FatxRecluster, BuildFailureLeavesTheOriginal)
{
    const uint64_t lba = 200 * MB / HDD_SECTOR_SIZE;
    SparseDevice device;
    MakeOriginal(device, lba);
    const auto before = Contents(device, lba);

    // The build disk fills up partway
    SparseDevice build_disk;
    PowerCutDevice full(build_disk, 100);
    FatxRangeRecorder build(full);
    FatxReclusterResult *result = new FatxReclusterResult;
    EXPECT_FALSE(FatxBuildReclustered(device, lba, 32 * 1024, &build, result));
    EXPECT_EQ(result->status, FATX_RECLUSTER_WRITE_ERROR);
    delete result;

    const FatxCheckResult check = Check(device, lba);
    EXPECT_EQ(check.cluster_bytes, CLUSTER);
    EXPECT_EQ(check.errors, 0u);
    ExpectSameFiles(before, Contents(device, lba));
}

TEST(FatxRecluster, PowerCutWhileCopyingBack)
{
    // Cut the power at each write of the copy back. Copying again, as
    // --resume does, must give the same result.
    const uint64_t lba = 200 * MB / HDD_SECTOR_SIZE;
    SparseDevice original;
    MakeOriginal(original, lba);
    const auto before = Contents(original, lba);

    SparseDevice build_disk;
    FatxRangeRecorder build(build_disk);
    FatxReclusterResult *result = new FatxReclusterResult;
    ASSERT_TRUE(FatxBuildReclustered(original, lba, 64 * 1024, &build, result));
    delete result;
    const std::vector<FatxRange> ranges = build.Ranges();

    uint64_t total;
    {
        SparseDevice copy = original;
        PowerCutDevice counter(copy, ~0ULL);
        ASSERT_TRUE(FatxCopyReclustered(build, counter, ranges));
        total = counter.Writes();
    }
    ASSERT_GT(total, 3u);

    for (uint64_t cut = 0; cut < total; cut++)
    {
        SCOPED_TRACE("power cut at write " + std::to_string(cut) + " of " + std::to_string(total));
        SparseDevice disk = original;
        {
            PowerCutDevice cut_device(disk, cut);
            EXPECT_FALSE(FatxCopyReclustered(build, cut_device, ranges));
        }
        // Unmountable in between, unless nothing was written yet
        const FatxCheckStatus status = Check(disk, lba).status;
        EXPECT_TRUE(cut == 0 ? status == FATX_CHECK_DONE : status == FATX_CHECK_RESIZING);

        ASSERT_TRUE(FatxCopyReclustered(build, disk, ranges));
        const FatxCheckResult check = Check(disk, lba);
        EXPECT_EQ(check.cluster_bytes, 64u * 1024);
        EXPECT_EQ(check.errors, 0u);
        EXPECT_EQ(check.warnings, 0u);
        ExpectSameFiles(before, Contents(disk, lba));
        if (::testing::Test::HasFailure())
        {
            return;
        }
    }
}

namespace
{

// Small files placed in the way, then two files allocated across them:
// "two" in 2 pieces, "three" in 3 (allocation skips used clusters)
void MakeFragmented(SparseDevice &device, uint64_t lba_size)
{
    FatxWriter writer(device, lba_size * HDD_SECTOR_SIZE, CLUSTER);
    ASSERT_TRUE(writer.Format(SERIAL));
    const uint32_t root = writer.RootCluster();
    std::vector<unsigned char> one(CLUSTER, 1);
    writer.SetNextFree(12);
    ASSERT_TRUE(writer.AddFile(root, "block12", one.data(), CLUSTER));
    writer.SetNextFree(22);
    ASSERT_TRUE(writer.AddFile(root, "block22", one.data(), CLUSTER));
    writer.SetNextFree(24);
    ASSERT_TRUE(writer.AddFile(root, "block24", one.data(), CLUSTER));

    std::vector<unsigned char> data(6 * CLUSTER);
    for (size_t i = 0; i < data.size(); i++)
    {
        data[i] = (unsigned char)(i * 13);
    }
    writer.SetNextFree(10); // 10, 11, then 13 to 15
    ASSERT_TRUE(writer.AddFile(root, "two", data.data(), 5 * CLUSTER));
    writer.SetNextFree(20); // 20, 21, 23, then 25 to 27
    ASSERT_TRUE(writer.AddFile(root, "three", data.data(), 6 * CLUSTER));
    ASSERT_TRUE(writer.Flush());
}

} // namespace

TEST(FatxFragmentation, CheckCountsThePieces)
{
    const uint64_t lba = 64 * MB / HDD_SECTOR_SIZE;
    SparseDevice device;
    MakeFragmented(device, lba);

    FatxCheckResult *result = new FatxCheckResult;
    ASSERT_TRUE(FatxCheck(device, lba * HDD_SECTOR_SIZE, result));
    EXPECT_EQ(result->errors, 0u);
    EXPECT_EQ(result->files, 5u);
    EXPECT_EQ(result->fragmented_files, 2u);
    EXPECT_EQ(result->extra_pieces, 3u); // 1 + 2
    ASSERT_EQ(result->worst_count, 2u);
    EXPECT_EQ(result->worst[0].pieces, 3u);
    EXPECT_STREQ(result->worst[0].path, "\\three");
    EXPECT_EQ(result->worst[1].pieces, 2u);
    EXPECT_STREQ(result->worst[1].path, "\\two");
    delete result;
}

TEST(FatxFragmentation, KeepsTheWorstFive)
{
    const uint64_t lba = 64 * MB / HDD_SECTOR_SIZE;
    SparseDevice device;
    FatxWriter writer(device, lba * HDD_SECTOR_SIZE, CLUSTER);
    ASSERT_TRUE(writer.Format(SERIAL));
    const uint32_t root = writer.RootCluster();
    std::vector<unsigned char> data(10 * CLUSTER, 7);

    // File n (1 to 8) gets n + 1 pieces: blockers every other cluster
    uint32_t at = 100;
    for (int n = 1; n <= 8; n++)
    {
        for (int b = 0; b < n; b++)
        {
            writer.SetNextFree(at + 1 + 2 * b);
            ASSERT_TRUE(writer.AddFile(root, ("b" + std::to_string(n) + "_" + std::to_string(b)).c_str(), data.data(), CLUSTER));
        }
        writer.SetNextFree(at);
        ASSERT_TRUE(writer.AddFile(root, ("file" + std::to_string(n)).c_str(), data.data(), (uint32_t)(n + 1) * CLUSTER));
        at += 100;
    }
    ASSERT_TRUE(writer.Flush());

    FatxCheckResult *result = new FatxCheckResult;
    ASSERT_TRUE(FatxCheck(device, lba * HDD_SECTOR_SIZE, result));
    EXPECT_EQ(result->fragmented_files, 8u);
    ASSERT_EQ(result->worst_count, (uint32_t)FATX_CHECK_WORST_FILES);
    for (int i = 0; i < FATX_CHECK_WORST_FILES; i++)
    {
        EXPECT_EQ(result->worst[i].pieces, (uint32_t)(9 - i)) << i;
        EXPECT_EQ(std::string(result->worst[i].path), "\\file" + std::to_string(8 - i)) << i;
    }
    delete result;
}

// Defragmenting: a recluster to the size it has, no file fragmented
TEST(FatxFragmentation, SameClusterSizeDefragments)
{
    const uint64_t lba = 64 * MB / HDD_SECTOR_SIZE;
    SparseDevice device;
    MakeFragmented(device, lba);
    const std::vector<unsigned char> header = Header(device);

    FatxReclusterResult *result = new FatxReclusterResult;
    EXPECT_EQ(Convert(device, lba, CLUSTER, result), FATX_RECLUSTER_DONE);
    delete result;

    FatxCheckResult *check = new FatxCheckResult;
    ASSERT_TRUE(FatxCheck(device, lba * HDD_SECTOR_SIZE, check));
    EXPECT_EQ(check->errors, 0u);
    EXPECT_EQ(check->warnings, 0u);
    EXPECT_EQ(check->files, 5u);
    EXPECT_EQ(check->fragmented_files, 0u);
    EXPECT_EQ(check->cluster_bytes, CLUSTER);
    delete check;
    ExpectSameHeader(header, Header(device));

    // The contents came along
    FatxWriter reader(device, lba * HDD_SECTOR_SIZE);
    ASSERT_TRUE(reader.Load());
    FatxDirent entry;
    bool found = false;
    for (uint32_t i = 0; reader.GetEntry(reader.RootCluster(), i, &entry); i++)
    {
        if (entry.name_length == 5 && memcmp(entry.name, "three", 5) == 0)
        {
            found = true;
            std::vector<unsigned char> data(6 * CLUSTER);
            for (uint32_t c = 0; c < 6; c++)
            {
                ASSERT_TRUE(device.Read(reader.ClusterOffset(entry.first_cluster + c), data.data() + c * CLUSTER, CLUSTER));
            }
            for (size_t b = 0; b < data.size(); b++)
            {
                ASSERT_EQ(data[b], (unsigned char)(b * 13)) << "at byte " << b;
            }
        }
    }
    EXPECT_TRUE(found);
}

// Past 256 GB XBpartitioner would want bigger clusters, but keeping the
// size it has is still allowed: that's a defragment, not a change
TEST(FatxFragmentation, DefragmentsSmallerClustersThanXBpartitionerWouldUse)
{
    SparseDevice device;
    const uint64_t lba = 64 * MB / HDD_SECTOR_SIZE;
    MakeOriginal(device, lba, 16 * 1024);
    FatxReclusterResult *result = new FatxReclusterResult;
    // Only the header and FAT of the first 64 MB are there; the size just has to pass the size rules
    EXPECT_NE(Convert(device, 0x30000000, 16 * 1024, result), FATX_RECLUSTER_TOO_SMALL);
    delete result;
}
