// FatxEditor: new folders, deleting and the volume label on a volume that
// holds data

#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <vector>
#include "fatx_edit.hpp"
#include "test_images.hpp"

namespace
{

const uint64_t IMAGE = 8 * MB;

// Root: "Save.dat" and "Games" holding "Halo" (a folder with "big.bin") and "x.txt"
class EditTest : public testing::Test
{
  protected:
    EditTest() : m_device(IMAGE)
    {
        FatxWriter writer(m_device, IMAGE, CLUSTER);
        EXPECT_TRUE(writer.Format());
        m_root = writer.RootCluster();
        EXPECT_TRUE(writer.AddFile(m_root, "Save.dat", "old", 3));
        EXPECT_TRUE(writer.MakeDirectory(m_root, "Games", &m_games));
        uint32_t halo;
        EXPECT_TRUE(writer.MakeDirectory(m_games, "Halo", &halo));
        const std::vector<unsigned char> big(CLUSTER * 5 + 7, 'b');
        EXPECT_TRUE(writer.AddFile(halo, "big.bin", big.data(), (uint32_t)big.size()));
        EXPECT_TRUE(writer.AddFile(m_games, "x.txt", "x", 1));
        EXPECT_TRUE(writer.Flush());
    }

    std::vector<FatxFolderEntry> List(FatxDevice &device, uint32_t folder)
    {
        FatxFolderReader reader(device, IMAGE);
        EXPECT_EQ(reader.Open(), FATX_FOLDER_OK);
        std::vector<FatxFolderEntry> entries;
        EXPECT_EQ(reader.List(folder, &entries), FATX_FOLDER_OK);
        return entries;
    }

    FatxFolderEntry Entry(uint32_t folder, const std::string &name)
    {
        for (const FatxFolderEntry &entry : List(m_device, folder))
        {
            if (entry.name == name)
            {
                return entry;
            }
        }
        ADD_FAILURE() << name << " not found";
        return FatxFolderEntry();
    }

    FatxCheckResult Checked(FatxDevice &device)
    {
        std::unique_ptr<FatxCheckResult> result = std::make_unique<FatxCheckResult>();
        FatxCheck(device, IMAGE, result.get());
        return *result;
    }

    bool IsSpotless()
    {
        const FatxCheckResult result = Checked(m_device);
        return result.status == FATX_CHECK_DONE && result.errors == 0 && result.warnings == 0;
    }

    FatxMemoryDevice m_device;
    uint32_t m_root;
    uint32_t m_games;
};

} // namespace

TEST_F(EditTest, MakesAFolderThatChecksClean)
{
    FatxEditor editor(m_device, IMAGE);
    ASSERT_EQ(editor.Open(), "");
    ASSERT_EQ(editor.NewFolderProblem(m_root, "", "Apps"), "");
    ASSERT_TRUE(editor.MakeFolder(m_root, "Apps", 0x2B6F, 0));
    EXPECT_TRUE(Entry(m_root, "Apps").directory);
    EXPECT_TRUE(IsSpotless());
}

TEST_F(EditTest, NoFolderWithANameAlreadyThereIgnoringCase)
{
    FatxEditor editor(m_device, IMAGE);
    ASSERT_EQ(editor.Open(), "");
    EXPECT_NE(editor.NewFolderProblem(m_root, "", "GAMES"), "");
}

TEST_F(EditTest, NoFolderWithANameTheXboxRefuses)
{
    FatxEditor editor(m_device, IMAGE);
    ASSERT_EQ(editor.Open(), "");
    EXPECT_NE(editor.NewFolderProblem(m_root, "", "a?b"), "");
    EXPECT_NE(editor.NewFolderProblem(m_root, "", std::string(43, 'n')), "");
}

TEST_F(EditTest, NoFolderPast240Characters)
{
    FatxEditor editor(m_device, IMAGE);
    ASSERT_EQ(editor.Open(), "");
    const std::string deep = "\\" + std::string(234, 'd'); // 235 + 1 + 5 = 241
    EXPECT_NE(editor.NewFolderProblem(m_games, deep, "Apps2"), "");
    EXPECT_EQ(editor.NewFolderProblem(m_games, deep, "Apps"), "");
}

TEST_F(EditTest, DeletesAFileAndFreesItsClusters)
{
    FatxEditor editor(m_device, IMAGE);
    ASSERT_EQ(editor.Open(), "");
    uint64_t freed;
    ASSERT_TRUE(editor.Delete(Entry(m_root, "Save.dat"), &freed));
    EXPECT_EQ(freed, 1u);
    EXPECT_EQ(List(m_device, m_root).size(), 1u);
    EXPECT_TRUE(IsSpotless());
}

TEST_F(EditTest, DeletesAFolderWithEverythingInIt)
{
    const uint64_t used_before = Checked(m_device).used_clusters;
    FatxEditor editor(m_device, IMAGE);
    ASSERT_EQ(editor.Open(), "");
    uint64_t freed;
    ASSERT_TRUE(editor.Delete(Entry(m_root, "Games"), &freed));
    // Games, Halo, big.bin's 6 and x.txt's 1
    EXPECT_EQ(freed, 9u);
    EXPECT_EQ(Checked(m_device).used_clusters, used_before - 9);
    EXPECT_TRUE(IsSpotless());
}

TEST_F(EditTest, RefusesAnEntryThatChangedSinceItWasListed)
{
    FatxFolderEntry listed = Entry(m_root, "Save.dat");
    listed.first_cluster += 1;
    FatxEditor editor(m_device, IMAGE);
    ASSERT_EQ(editor.Open(), "");
    uint64_t freed;
    EXPECT_FALSE(editor.Delete(listed, &freed));
    EXPECT_EQ(List(m_device, m_root).size(), 2u);
}

TEST_F(EditTest, APowerCutWhileDeletingLeavesNoErrors)
{
    const FatxFolderEntry games = Entry(m_root, "Games");
    const std::vector<unsigned char> before(m_device.Data(), m_device.Data() + m_device.Size());
    PowerCutDevice counting(m_device, ~0ULL);
    {
        FatxEditor editor(counting, IMAGE);
        ASSERT_EQ(editor.Open(), "");
        uint64_t freed;
        ASSERT_TRUE(editor.Delete(games, &freed));
    }
    const uint64_t writes = counting.Writes();

    for (uint64_t cut = 0; cut < writes; cut++)
    {
        memcpy(m_device.Data(), before.data(), before.size());
        PowerCutDevice device(m_device, cut);
        FatxEditor editor(device, IMAGE);
        ASSERT_EQ(editor.Open(), "");
        uint64_t freed;
        editor.Delete(games, &freed);
        const FatxCheckResult result = Checked(m_device);
        ASSERT_EQ(result.status, FATX_CHECK_DONE) << "cut after write " << cut;
        EXPECT_EQ(result.errors, 0u) << "cut after write " << cut;
    }
}

TEST_F(EditTest, SetsAndReadsTheLabel)
{
    FatxEditor editor(m_device, IMAGE);
    ASSERT_EQ(editor.Open(), "");
    ASSERT_TRUE(editor.SetLabel("My Games"));
    FatxFolderReader reader(m_device, IMAGE);
    ASSERT_EQ(reader.Open(), FATX_FOLDER_OK);
    EXPECT_EQ(reader.Label(), "My Games");
    EXPECT_TRUE(IsSpotless());
}

TEST_F(EditTest, ClearsTheLabel)
{
    FatxEditor editor(m_device, IMAGE);
    ASSERT_EQ(editor.Open(), "");
    ASSERT_TRUE(editor.SetLabel("Old"));
    ASSERT_TRUE(editor.SetLabel(""));
    FatxFolderReader reader(m_device, IMAGE);
    ASSERT_EQ(reader.Open(), FATX_FOLDER_OK);
    EXPECT_EQ(reader.Label(), "");
}

TEST_F(EditTest, AFull32CharacterLabel)
{
    FatxEditor editor(m_device, IMAGE);
    ASSERT_EQ(editor.Open(), "");
    const std::string label(32, 'L');
    ASSERT_TRUE(editor.SetLabel(label));
    FatxFolderReader reader(m_device, IMAGE);
    ASSERT_EQ(reader.Open(), FATX_FOLDER_OK);
    EXPECT_EQ(reader.Label(), label);
}

// A folder that holds "Games" twice: an error the check finds, with the FAT untouched
void BreakWithoutTouchingTheFat(FatxMemoryDevice &device, uint32_t root)
{
    FatxWriter writer(device, IMAGE);
    ASSERT_TRUE(writer.Load());
    FatxDirent entry;
    uint64_t offset;
    ASSERT_TRUE(writer.GetEntry(root, 0, &entry, &offset)); // Save.dat
    entry.name_length = 5;
    memcpy(entry.name, "Games", 5);
    ASSERT_TRUE(writer.SetEntry(offset, entry));
}

TEST_F(EditTest, SkipsTheCheckWhileTheFatIsAsItWasKnownClean)
{
    FatxEditor first(m_device, IMAGE);
    ASSERT_EQ(first.Open(), "");
    const uint64_t clean = first.Fingerprint();
    BreakWithoutTouchingTheFat(m_device, m_root);

    FatxEditor trusting(m_device, IMAGE);
    EXPECT_EQ(trusting.Open(nullptr, clean), "");
    FatxEditor checking(m_device, IMAGE);
    EXPECT_NE(checking.Open(), "");
}

TEST_F(EditTest, ChecksAgainOnceTheFatChanged)
{
    FatxEditor first(m_device, IMAGE);
    ASSERT_EQ(first.Open(), "");
    const uint64_t clean = first.Fingerprint();
    {
        // Something else adds a file
        FatxWriter other(m_device, IMAGE);
        ASSERT_TRUE(other.Load());
        ASSERT_TRUE(other.AddFile(m_root, "new.txt", "n", 1));
        ASSERT_TRUE(other.Flush());
    }
    BreakWithoutTouchingTheFat(m_device, m_root);

    FatxEditor editor(m_device, IMAGE);
    EXPECT_NE(editor.Open(nullptr, clean), "");
}

TEST_F(EditTest, TheUsageComesFromTheFatInMemory)
{
    FatxEditor editor(m_device, IMAGE);
    ASSERT_EQ(editor.Open(), "");
    uint64_t freed;
    ASSERT_TRUE(editor.Delete(Entry(m_root, "Save.dat"), &freed));
    FatxUsage read;
    ASSERT_TRUE(FatxReadUsage(m_device, IMAGE / HDD_SECTOR_SIZE, &read));
    const FatxUsage held = editor.Usage();
    EXPECT_EQ(held.used_clusters, read.used_clusters);
    EXPECT_EQ(held.highest_used, read.highest_used);
    EXPECT_EQ(held.cluster_bytes, read.cluster_bytes);
}

TEST(FatxLabelProblem, TooLongOrNotPlainAscii)
{
    EXPECT_EQ(fatx_label_problem("Games"), "");
    EXPECT_NE(fatx_label_problem(std::string(33, 'L')), "");
    EXPECT_NE(fatx_label_problem("tab\there"), "");
}
