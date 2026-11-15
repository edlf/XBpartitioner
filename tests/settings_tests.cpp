// Settings: reading and writing xbpartitioner.ini's text

#include <gtest/gtest.h>
#include <string>
#include "settings.h"

namespace
{

Settings Parsed(const char *text)
{
    Settings settings;
    settings.Parse(text);
    return settings;
}

std::string Formatted(const Settings &settings)
{
    char text[200];
    const size_t length = settings.Format(text, sizeof(text));
    return std::string(text, length);
}

} // namespace

TEST(Settings, DefaultsHaveAnimationsOn)
{
    EXPECT_TRUE(Settings().Animations());
}

TEST(Settings, DefaultsKeepResizedDataOff)
{
    EXPECT_FALSE(Settings().ResizeKeepsData());
}

TEST(Settings, DefaultsNameNoTheme)
{
    EXPECT_STREQ(Settings().ThemeName(), "");
}

TEST(Settings, ReadsTheTheme)
{
    EXPECT_STREQ(Parsed("theme=Midnight").ThemeName(), "Midnight");
}

TEST(Settings, KeysIgnoreCase)
{
    EXPECT_TRUE(Parsed("Force_LBA48_V3=1").ForceLba48V3());
}

TEST(Settings, SpacesAroundKeyAndValueAreIgnored)
{
    EXPECT_FALSE(Parsed("  animations \t=  off  ").Animations());
}

TEST(Settings, BoolsAcceptWords)
{
    EXPECT_TRUE(Parsed("resize_keeps_data=Yes").ResizeKeepsData());
}

TEST(Settings, UnknownBoolKeepsTheDefault)
{
    EXPECT_TRUE(Parsed("animations=maybe").Animations());
}

TEST(Settings, ReadsEveryLineWithAnyLineEnding)
{
    const Settings settings = Parsed("animations=0\r\nforce_lba48_v3=1\nresize_keeps_data=1");
    EXPECT_TRUE(!settings.Animations() && settings.ForceLba48V3() && settings.ResizeKeepsData());
}

TEST(Settings, LinesWithoutEqualsAreSkipped)
{
    EXPECT_FALSE(Parsed("# comment\nanimations=0").Animations());
}

TEST(Settings, ToggleFlipsResizeKeepsData)
{
    Settings settings;
    settings.ToggleResizeKeepsData();
    EXPECT_TRUE(settings.ResizeKeepsData());
}

TEST(Settings, SetThemeNameStoresAscii)
{
    Settings settings;
    settings.SetThemeName(L"Classic");
    EXPECT_STREQ(settings.ThemeName(), "Classic");
}

TEST(Settings, FormatWritesEverySetting)
{
    Settings settings;
    settings.SetThemeName(L"Classic");
    EXPECT_EQ(Formatted(settings), "theme=Classic\r\nanimations=1\r\nforce_lba48_v3=0\r\nresize_keeps_data=0\r\n");
}

TEST(Settings, FormattedTextParsesBack)
{
    const Settings original = Parsed("theme=Midnight\nanimations=0\nforce_lba48_v3=1\nresize_keeps_data=1");
    EXPECT_EQ(Formatted(Parsed(Formatted(original).c_str())), Formatted(original));
}

TEST(Settings, FormatCutsToTheBuffer)
{
    char text[10];
    EXPECT_EQ(Settings().Format(text, sizeof(text)), 9u);
}
