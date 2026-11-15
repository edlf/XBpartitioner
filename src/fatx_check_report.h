#ifndef FATX_CHECK_REPORT_H_
#define FATX_CHECK_REPORT_H_

// What a file system check found, as the lines of a dialog. Shared by the
// Xbox app and the host tests: no XDK headers.

#include "fatx_check.hpp"

enum CheckReportTone
{
    CHECK_TONE_UNCHECKED, // Not checked: yellow
    CHECK_TONE_CLEAN,     // No errors: green
    CHECK_TONE_ERRORS     // Errors found: red
};

// The summary, the counts, lost space, up to four messages (errors first)
// and how many more there are
const int CHECK_REPORT_MAX_LINES = 8;
const int CHECK_REPORT_MAX_MESSAGES = 4;

struct CheckReport
{
    const wchar_t *title;
    CheckReportTone tone;
    int line_count;
    wchar_t lines[CHECK_REPORT_MAX_LINES][FATX_CHECK_MESSAGE_LENGTH];
};

CheckReport BuildCheckReport(int partition, const FatxCheckResult &result);

#endif // FATX_CHECK_REPORT_H_
