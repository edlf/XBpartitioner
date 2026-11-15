#include "fatx_check_report.h"
#include "partition_format.h"
#include <string.h>

namespace
{

// A dialog line holds about this many characters
const size_t MESSAGE_MAX_CHARS = 54;

wchar_t *AddLine(CheckReport &report)
{
    wchar_t *line = report.lines[report.line_count++];
    line[0] = L'\0';
    return line;
}

// "Error: \games\x: ...", cut to fit a dialog line
void format_check_message(const FatxCheckMessage &message, wchar_t *out)
{
    const wchar_t *prefix = message.error ? L"Error: " : L"Warning: ";
    size_t length = wcslen(prefix);
    memcpy(out, prefix, length * sizeof(wchar_t));

    for (const char *text = message.text; *text != '\0' && length < FATX_CHECK_MESSAGE_LENGTH - 1; text++)
    {
        out[length++] = (wchar_t)(unsigned char)*text;
    }
    out[length] = L'\0';

    if (length > MESSAGE_MAX_CHARS)
    {
        memcpy(out + MESSAGE_MAX_CHARS - 3, L"...", 4 * sizeof(wchar_t));
    }
}

// Why there are no counts: nothing could be checked
bool AddUncheckedLines(const FatxCheckResult &result, CheckReport &report)
{
    switch (result.status)
    {
    case FATX_CHECK_RESIZING:
        FormatWide(AddLine(report), FATX_CHECK_MESSAGE_LENGTH, L"A resize or cluster size change was interrupted.");
        FormatWide(AddLine(report), FATX_CHECK_MESSAGE_LENGTH, L"Finish it on a PC: fatxtool resize/recluster --resume.");
        return true;
    case FATX_CHECK_NOT_FATX:
        FormatWide(AddLine(report), FATX_CHECK_MESSAGE_LENGTH, L"No FATX file system: the partition isn't formatted.");
        return true;
    case FATX_CHECK_READ_ERROR:
        FormatWide(AddLine(report), FATX_CHECK_MESSAGE_LENGTH, L"The partition could not be read.");
        return true;
    case FATX_CHECK_NO_MEMORY:
        FormatWide(AddLine(report), FATX_CHECK_MESSAGE_LENGTH, L"Not enough memory to check this partition.");
        return true;
    default:
        return false;
    }
}

// "No errors, 2 warnings. 3 files fragmented." The dialog's lines are all
// spoken for, so fragmentation goes on this line.
void FormatCounts(const FatxCheckResult &result, wchar_t *line)
{
    if (result.errors != 0)
    {
        FormatWide(line, FATX_CHECK_MESSAGE_LENGTH, L"%u errors, %u warnings.", result.errors, result.warnings);
        return;
    }

    wchar_t fragmented[40] = L"";
    if (result.fragmented_files > 0)
    {
        FormatWide(fragmented, 40, L" %I64u files fragmented.", result.fragmented_files);
    }
    if (result.warnings == 0)
    {
        FormatWide(line, FATX_CHECK_MESSAGE_LENGTH, L"No errors or warnings found.%s", fragmented);
    }
    else
    {
        FormatWide(line, FATX_CHECK_MESSAGE_LENGTH, L"No errors, %u warnings.%s", result.warnings, fragmented);
    }
}

// What was checked: files and folders (or a damaged header), the counts and
// lost space
void AddCheckedLines(int partition, const FatxCheckResult &result, CheckReport &report)
{
    if (result.status == FATX_CHECK_BAD_HEADER)
    {
        // The file count is meaningless here
        report.title = L"Errors found";
        report.tone = CHECK_TONE_ERRORS;
        FormatWide(AddLine(report), FATX_CHECK_MESSAGE_LENGTH, L"The FATX header is damaged.");
    }
    else
    {
        const bool clean = (result.errors == 0);
        report.title = clean ? L"No errors" : L"Errors found";
        report.tone = clean ? CHECK_TONE_CLEAN : CHECK_TONE_ERRORS;
        FormatWide(AddLine(report), FATX_CHECK_MESSAGE_LENGTH, L"Partition %d (%c): %I64u files, %I64u folders.", partition + 1,
                    PartitionLetter(partition), result.files, result.directories);
        FormatCounts(result, AddLine(report));
    }

    if (result.lost_clusters > 0)
    {
        wchar_t size[HUMAN_SIZE_TEXT_COUNT];
        FormatHumanSize(result.lost_clusters * result.cluster_bytes / HDD_SECTOR_SIZE, size);
        FormatWide(AddLine(report), FATX_CHECK_MESSAGE_LENGTH, L"Lost space: %I64u clusters (%s).", result.lost_clusters, size);
    }
}

// A few messages, errors first, then how many more there are
void AddMessageLines(const FatxCheckResult &result, CheckReport &report)
{
    int shown = 0;
    for (int pass = 0; pass < 2; pass++)
    {
        const bool want_error = (pass == 0);
        for (uint32_t i = 0; i < result.message_count && shown < CHECK_REPORT_MAX_MESSAGES; i++)
        {
            const FatxCheckMessage &message = result.messages[i];
            // Already shown as the lost space line
            const bool lost_space = !want_error && result.lost_clusters > 0 && strstr(message.text, "lost space") != NULL;
            if (message.error == want_error && !lost_space)
            {
                format_check_message(message, AddLine(report));
                shown++;
            }
        }
    }

    const uint32_t total = result.errors + result.warnings - (result.lost_clusters > 0 ? 1 : 0);
    if (total > (uint32_t)shown && shown > 0)
    {
        FormatWide(AddLine(report), FATX_CHECK_MESSAGE_LENGTH, L"...and %u more.", total - shown);
    }
}

} // namespace

CheckReport BuildCheckReport(int partition, const FatxCheckResult &result)
{
    CheckReport report;
    report.title = L"Not checked";
    report.tone = CHECK_TONE_UNCHECKED;
    report.line_count = 0;

    if (!AddUncheckedLines(result, report))
    {
        AddCheckedLines(partition, result, report);
    }
    AddMessageLines(result, report);
    return report;
}
