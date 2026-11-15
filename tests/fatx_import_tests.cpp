// FatxImporter: copying PC files into a FATX volume that holds data

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>
#include "fatx_folder.hpp"
#include "fatx_format.hpp"
#include "fatx_import.hpp"
#include "pc_image.hpp"
#include "test_images.hpp"

namespace fs = std::filesystem;

namespace
{

const uint64_t IMAGE = 8 * MB;

// A PC folder for the sources, removed afterwards
class PcFolder
{
  public:
    PcFolder()
    {
        wchar_t temp[MAX_PATH];
        GetTempPathW(MAX_PATH, temp);
        m_root = fs::path(temp) / ("fatx_import_" + std::to_string(GetCurrentProcessId()) + "_" + std::to_string(s_count++));
        fs::create_directories(m_root);
    }
    ~PcFolder()
    {
        std::error_code error;
        fs::remove_all(m_root, error);
    }
    fs::path File(const std::string &name, const std::string &contents) const
    {
        const fs::path path = m_root / name;
        fs::create_directories(path.parent_path());
        std::ofstream(path, std::ios::binary) << contents;
        return path;
    }
    fs::path Folder(const std::string &name) const
    {
        fs::create_directories(m_root / name);
        return m_root / name;
    }

  private:
    fs::path m_root;
    static int s_count;
};

int PcFolder::s_count = 0;

struct NoteCounter : public FatxImportLog
{
    void Note(const std::string &) override
    {
        notes++;
    }
    int notes = 0;
};

// A formatted image
class ImportTest : public testing::Test
{
  protected:
    ImportTest() : m_device(IMAGE)
    {
        FatxWriter writer(m_device, IMAGE, CLUSTER);
        EXPECT_TRUE(writer.Format());
        EXPECT_TRUE(writer.AddFile(writer.RootCluster(), "Save.dat", "old", 3));
        EXPECT_TRUE(writer.Flush());
        m_root = writer.RootCluster();
    }

    FatxImportTarget Root() const
    {
        return FatxImportTarget{m_root, ""};
    }

    FatxImportResult Import(FatxDevice &device, const std::vector<fs::path> &sources)
    {
        FatxImporter importer(device, IMAGE);
        EXPECT_EQ(importer.Open(), "");
        const FatxImportPlan plan = importer.Plan(Root(), sources);
        NoteCounter log;
        return importer.Run(Root(), plan, log);
    }

    FatxImportPlan PlanOnly(const FatxImportTarget &target, const std::vector<fs::path> &sources)
    {
        FatxImporter importer(m_device, IMAGE);
        EXPECT_EQ(importer.Open(), "");
        return importer.Plan(target, sources);
    }

    std::vector<FatxFolderEntry> List(uint32_t cluster)
    {
        FatxFolderReader reader(m_device, IMAGE);
        EXPECT_EQ(reader.Open(), FATX_FOLDER_OK);
        std::vector<FatxFolderEntry> entries;
        EXPECT_EQ(reader.List(cluster, &entries), FATX_FOLDER_OK);
        return entries;
    }

    FatxCheckResult Check(FatxDevice &device)
    {
        std::unique_ptr<FatxCheckResult> result = std::make_unique<FatxCheckResult>();
        FatxCheck(device, IMAGE, result.get());
        return *result;
    }

    FatxMemoryDevice m_device;
    uint32_t m_root;
    PcFolder m_pc;
};

// Collects a file read back
struct StringSink : public FatxFileSink
{
    bool Write(const void *data, uint32_t length) override
    {
        text.append((const char *)data, length);
        return true;
    }
    std::string text;
};

} // namespace

TEST_F(ImportTest, AddsFilesAndFoldersThatCheckClean)
{
    const std::string big(40000, 'b');
    m_pc.File("Game/media/big.bin", big);
    m_pc.File("Game/default.xbe", "xbe");
    const FatxImportResult result = Import(m_device, {m_pc.Folder("Game"), m_pc.File("hello.txt", "hello")});
    EXPECT_EQ(result.failed, 0);
    EXPECT_EQ(result.files, 3);
    EXPECT_EQ(result.folders, 2);

    const FatxCheckResult check = Check(m_device);
    EXPECT_EQ(check.status, FATX_CHECK_DONE);
    EXPECT_EQ(check.errors, 0u);
    EXPECT_EQ(check.warnings, 0u);
}

TEST_F(ImportTest, CopiesTheContents)
{
    const std::string data = std::string(70000, 'x') + "end";
    Import(m_device, {m_pc.File("data.bin", data)});

    FatxFolderEntry copied = {};
    for (const FatxFolderEntry &entry : List(m_root))
    {
        copied = entry.name == "data.bin" ? entry : copied;
    }
    FatxFolderReader reader(m_device, IMAGE);
    ASSERT_EQ(reader.Open(), FATX_FOLDER_OK);
    StringSink sink;
    ASSERT_EQ(reader.ReadFile(copied, sink), FATX_FOLDER_OK);
    EXPECT_EQ(sink.text, data);
}

TEST_F(ImportTest, NeverReplacesANameAlreadyThereIgnoringCase)
{
    const FatxImportPlan plan = PlanOnly(Root(), {m_pc.File("save.DAT", "new")});
    EXPECT_TRUE(plan.items.empty());
    EXPECT_EQ(plan.skipped.size(), 1u);
}

TEST_F(ImportTest, SkipsANameOver42Characters)
{
    const FatxImportPlan plan = PlanOnly(Root(), {m_pc.File(std::string(39, 'n') + ".txt", "")});
    EXPECT_TRUE(plan.items.empty());
    EXPECT_EQ(plan.skipped.size(), 1u);
}

TEST_F(ImportTest, SkipsAPathOver240Characters)
{
    const FatxImportTarget deep = {m_root, "\\" + std::string(225, 'd')};
    const FatxImportPlan plan = PlanOnly(deep, {m_pc.File("a_long_name.txt", "")}); // 226 + 1 + 15
    EXPECT_TRUE(plan.items.empty());
    EXPECT_EQ(plan.skipped.size(), 1u);
}

TEST_F(ImportTest, APathOf240CharactersFits)
{
    const FatxImportTarget deep = {m_root, "\\" + std::string(223, 'd')};
    const FatxImportPlan plan = PlanOnly(deep, {m_pc.File("a_long_name.txt", "")}); // 224 + 1 + 15 = 240
    EXPECT_EQ(plan.items.size(), 1u);
}

TEST_F(ImportTest, RefusesWhatDoesNotFitAndWritesNothing)
{
    const FatxImportResult result = Import(m_device, {m_pc.File("huge.bin", std::string((size_t)(9 * MB), 'h'))});
    EXPECT_EQ(result.files, 0);
    EXPECT_EQ(List(m_root).size(), 1u);
}

TEST_F(ImportTest, SkipsEntriesPastAFullFolder)
{
    FatxWriter writer(m_device, IMAGE);
    ASSERT_TRUE(writer.Load());
    uint32_t full;
    ASSERT_TRUE(writer.MakeDirectory(m_root, "full", &full));
    for (uint32_t i = 0; i < FATX_MAX_FOLDER_ENTRIES; i++)
    {
        ASSERT_TRUE(writer.AddFile(full, ("f" + std::to_string(i)).c_str(), "", 0));
    }
    ASSERT_TRUE(writer.Flush());

    const FatxImportPlan plan = PlanOnly(FatxImportTarget{full, "\\full"}, {m_pc.File("one_more", "")});
    EXPECT_TRUE(plan.items.empty());
    EXPECT_EQ(plan.skipped.size(), 1u);
}

TEST_F(ImportTest, RefusesAVolumeWithErrors)
{
    // The existing file's cluster marked free: its chain is broken
    FatxWriter writer(m_device, IMAGE);
    ASSERT_TRUE(writer.Load());
    FatxDirent entry;
    ASSERT_TRUE(writer.GetEntry(m_root, 0, &entry));
    writer.SetFat(entry.first_cluster, FATX_FAT_FREE);
    ASSERT_TRUE(writer.Flush());

    FatxImporter importer(m_device, IMAGE);
    EXPECT_NE(importer.Open(), "");
}

TEST_F(ImportTest, TakingTheEndSlotKeepsOldBytesOutOfTheFolder)
{
    // Root: "Save.dat", a 0x00 end marker, then bytes that look like an entry
    FatxWriter writer(m_device, IMAGE);
    ASSERT_TRUE(writer.Load());
    FatxDirent ghost;
    uint64_t offset;
    ASSERT_TRUE(writer.GetEntry(m_root, 0, &ghost, &offset));
    memcpy(ghost.name, "ghost", 5);
    ghost.name_length = 5;
    ghost.first_cluster = 0;
    ghost.file_size = 0;
    FatxDirent end = {};
    ASSERT_TRUE(writer.SetEntry(offset + sizeof(FatxDirent), end));
    ASSERT_TRUE(writer.SetEntry(offset + 2 * sizeof(FatxDirent), ghost));

    Import(m_device, {m_pc.File("new.txt", "n")});
    std::vector<std::string> names;
    for (const FatxFolderEntry &listed : List(m_root))
    {
        names.push_back(listed.name);
    }
    EXPECT_EQ(names, (std::vector<std::string>{"Save.dat", "new.txt"}));
}

TEST_F(ImportTest, APowerCutAnywhereLeavesNoErrors)
{
    m_pc.File("Game/media/big.bin", std::string(40000, 'b'));
    m_pc.File("Game/default.xbe", "xbe");
    const std::vector<fs::path> sources = {m_pc.Folder("Game"), m_pc.File("hello.txt", "hello")};
    const std::vector<unsigned char> before(m_device.Data(), m_device.Data() + m_device.Size());

    // How many writes a whole copy takes
    PowerCutDevice counting(m_device, ~0ULL);
    Import(counting, sources);
    const uint64_t writes = counting.Writes();
    ASSERT_GT(writes, 0u);

    for (uint64_t cut = 0; cut < writes; cut++)
    {
        memcpy(m_device.Data(), before.data(), before.size());
        PowerCutDevice device(m_device, cut);
        FatxImporter importer(device, IMAGE);
        ASSERT_EQ(importer.Open(), "");
        NoteCounter log;
        importer.Run(Root(), importer.Plan(Root(), sources), log);

        const FatxCheckResult check = Check(m_device);
        ASSERT_EQ(check.status, FATX_CHECK_DONE) << "power cut after write " << cut;
        EXPECT_EQ(check.errors, 0u) << "power cut after write " << cut << ": " << (check.message_count ? check.messages[0].text : "");
    }
}

TEST(ImportIntoAMemoryUnit, FillsItAndChecksClean)
{
    // As Create image fills a new memory unit
    FatxMemoryDevice image(IMAGE);
    ASSERT_TRUE(FatxFormatMemoryUnit(image, IMAGE / HDD_SECTOR_SIZE, FATX_MEMORY_UNIT_CLUSTER_BYTES, 1));
    FatxMediumDevice memory_unit(image, FATX_MEDIUM_MEMORY_UNIT);
    PcFolder pc;
    pc.File("UDATA/4d530004/save.dat", std::string(50000, 's'));

    FatxImporter importer(memory_unit, IMAGE);
    ASSERT_EQ(importer.Open(), "");
    EXPECT_EQ(importer.ClusterBytes(), FATX_MEMORY_UNIT_CLUSTER_BYTES);
    const FatxImportTarget root = {1, ""};
    NoteCounter log;
    const FatxImportResult result = importer.Run(root, importer.Plan(root, {pc.Folder("UDATA")}), log);
    EXPECT_EQ(result.files, 1);
    EXPECT_EQ(result.folders, 2);

    std::unique_ptr<FatxCheckResult> check = std::make_unique<FatxCheckResult>();
    FatxCheck(memory_unit, IMAGE, check.get());
    EXPECT_EQ(check->status, FATX_CHECK_DONE);
    EXPECT_EQ(check->errors, 0u);
    EXPECT_EQ(check->files, 1u);
}

TEST(FatxTimeFromFileTime, UnknownBecomesTheFirstFatxDay)
{
    uint16_t date, time;
    fatx_time_from_file_time(0, &date, &time);
    EXPECT_EQ(fatx_time_text(date, time), "2000-01-01 00:00");
}
