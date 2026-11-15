// fatxgui's PC side of the browser: listing folders and drives

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include "local_folder.hpp"

using namespace fatxgui;

namespace
{

// A folder with "b.txt" (5 bytes), "A.txt" and a "zfolder" folder, removed afterwards
class LocalFolder : public testing::Test
{
  protected:
    LocalFolder()
    {
        wchar_t temp[MAX_PATH];
        GetTempPathW(MAX_PATH, temp);
        m_folder = std::filesystem::path(temp) / ("fatxgui_local_" + std::to_string(GetCurrentProcessId()));
        std::filesystem::create_directories(m_folder / "zfolder");
        std::ofstream(m_folder / "b.txt") << "hello";
        std::ofstream(m_folder / "A.txt");
    }
    ~LocalFolder()
    {
        std::error_code error;
        std::filesystem::remove_all(m_folder, error);
    }

    std::filesystem::path m_folder;
};

} // namespace

TEST_F(LocalFolder, ListsFoldersFirstThenNamesIgnoringCase)
{
    std::vector<LocalEntry> entries;
    std::string error;
    ASSERT_TRUE(list_local_folder(m_folder.wstring(), &entries, &error)) << error;
    ASSERT_EQ(entries.size(), 3u);
    EXPECT_EQ(entries[0].name, L"zfolder");
    EXPECT_TRUE(entries[0].directory);
    EXPECT_EQ(entries[1].name, L"A.txt");
    EXPECT_EQ(entries[2].name, L"b.txt");
    EXPECT_EQ(entries[2].size, 5u);
}

TEST_F(LocalFolder, AnEmptyFolderHasNoEntries)
{
    std::vector<LocalEntry> entries;
    std::string error;
    ASSERT_TRUE(list_local_folder((m_folder / "zfolder").wstring(), &entries, &error));
    EXPECT_TRUE(entries.empty());
}

TEST_F(LocalFolder, AMissingFolderIsAnError)
{
    std::vector<LocalEntry> entries;
    std::string error;
    EXPECT_FALSE(list_local_folder((m_folder / "missing").wstring(), &entries, &error));
    EXPECT_FALSE(error.empty());
}

TEST(LocalDrives, IncludeTheWindowsDrive)
{
    wchar_t windows[MAX_PATH];
    GetWindowsDirectoryW(windows, MAX_PATH);
    const std::wstring root = std::wstring(windows).substr(0, 3);
    bool found = false;
    for (const LocalDrive &drive : list_local_drives())
    {
        found = found || _wcsicmp(drive.root.c_str(), root.c_str()) == 0;
    }
    EXPECT_TRUE(found);
}

TEST(LocalTimeText, EmptyForAnUnsetTime)
{
    const FILETIME unset = {0, 0};
    EXPECT_EQ(local_time_text(unset), "");
}
