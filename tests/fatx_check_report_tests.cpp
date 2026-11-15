// BuildCheckReport: the lines of the Xbox app's check result dialog

#include <gtest/gtest.h>
#include <memory>
#include <string.h>
#include <wchar.h>
#include "fatx_check_report.h"

namespace
{

const int F = 5;

// About 11 KB, so on the heap
class CheckReportTest : public testing::Test
{
  protected:
    CheckReportTest() : m_result(new FatxCheckResult)
    {
        memset(m_result.get(), 0, sizeof(FatxCheckResult));
        m_result->status = FATX_CHECK_DONE;
        m_result->cluster_bytes = 16 * 1024;
        m_result->files = 12;
        m_result->directories = 3;
    }

    void AddMessage(bool error, const char *text)
    {
        FatxCheckMessage &message = m_result->messages[m_result->message_count++];
        message.error = error;
        strncpy_s(message.text, text, _TRUNCATE);
        (error ? m_result->errors : m_result->warnings)++;
    }

    CheckReport Report() const
    {
        return BuildCheckReport(F, *m_result);
    }

    std::unique_ptr<FatxCheckResult> m_result;
};

} // namespace

TEST_F(CheckReportTest, CleanCheckIsGreen)
{
    EXPECT_EQ(Report().tone, CHECK_TONE_CLEAN);
}

TEST_F(CheckReportTest, SummaryCountsFilesAndFolders)
{
    EXPECT_STREQ(Report().lines[0], L"Partition 6 (F): 12 files, 3 folders.");
}

TEST_F(CheckReportTest, CleanCheckSaysSo)
{
    EXPECT_STREQ(Report().lines[1], L"No errors or warnings found.");
}

TEST_F(CheckReportTest, FragmentationJoinsTheCountsLine)
{
    m_result->fragmented_files = 7;
    EXPECT_STREQ(Report().lines[1], L"No errors or warnings found. 7 files fragmented.");
}

TEST_F(CheckReportTest, ErrorsMakeItRed)
{
    AddMessage(true, "bad chain");
    EXPECT_EQ(Report().tone, CHECK_TONE_ERRORS);
}

TEST_F(CheckReportTest, ErrorsAreCounted)
{
    AddMessage(true, "bad chain");
    AddMessage(false, "odd name");
    EXPECT_STREQ(Report().lines[1], L"1 errors, 1 warnings.");
}

TEST_F(CheckReportTest, DamagedHeaderReplacesTheSummary)
{
    m_result->status = FATX_CHECK_BAD_HEADER;
    EXPECT_STREQ(Report().lines[0], L"The FATX header is damaged.");
}

TEST_F(CheckReportTest, DamagedHeaderHasOneLine)
{
    m_result->status = FATX_CHECK_BAD_HEADER;
    EXPECT_EQ(Report().line_count, 1);
}

TEST_F(CheckReportTest, UnformattedPartitionIsNotChecked)
{
    m_result->status = FATX_CHECK_NOT_FATX;
    EXPECT_STREQ(Report().title, L"Not checked");
}

TEST_F(CheckReportTest, InterruptedResizePointsToThePc)
{
    m_result->status = FATX_CHECK_RESIZING;
    EXPECT_STREQ(Report().lines[1], L"Finish it on a PC: fatxtool resize/recluster --resume.");
}

TEST_F(CheckReportTest, LostSpaceGetsItsOwnLine)
{
    m_result->lost_clusters = 64;
    EXPECT_STREQ(Report().lines[2], L"Lost space: 64 clusters (1.00 MB).");
}

TEST_F(CheckReportTest, ErrorsAreListedBeforeWarnings)
{
    AddMessage(false, "odd name");
    AddMessage(true, "bad chain");
    EXPECT_STREQ(Report().lines[2], L"Error: bad chain");
}

TEST_F(CheckReportTest, LostSpaceWarningIsNotRepeated)
{
    m_result->lost_clusters = 64;
    AddMessage(false, "64 clusters of lost space");
    EXPECT_EQ(Report().line_count, 3);
}

TEST_F(CheckReportTest, LongMessagesAreCut)
{
    AddMessage(true, "\\games\\a very long folder name\\with a very long file name inside it.xbe");
    EXPECT_EQ(wcslen(Report().lines[2]), 54u);
}

TEST_F(CheckReportTest, AtMostFourMessagesThenACount)
{
    for (int i = 0; i < 6; i++)
    {
        AddMessage(true, "bad chain");
    }
    EXPECT_STREQ(Report().lines[6], L"...and 2 more.");
}
