#include "stdafx.h"
#include "fsck_workflow.h"
#include "array_length.h"
#include "disk_partition_device.h"
#include "fatx_check_report.h"
#include "fatx_format.hpp"
#include "partition_format.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// For dry runs, which return before anything is written
class NoTableWrites : public FatxResizeTable
{
  public:
    bool SetSize(uint64_t)
    {
        return false;
    }
};

enum JobAccess
{
    JOB_READS,
    JOB_WRITES
};

// Disk work for a worker thread, so the screen keeps animating. Each kind of
// work is a subclass; it runs on the partition's device.
class FsckJob : public FatxProgress
{
  public:
    explicit FsckJob(JobAccess access) : m_start_lba(0), m_lba_size(0), m_write(access == JOB_WRITES), m_percent(0)
    {
    }
    virtual ~FsckJob()
    {
    }

    // The partition it works on, before it runs
    void Place(ULONGLONG start_lba, ULONGLONG lba_size)
    {
        m_start_lba = start_lba;
        m_lba_size = lba_size;
        m_percent = 0;
    }

    // On the worker thread
    void RunOnDisk()
    {
        DiskPartitionDevice device(m_start_lba, m_write);
        Run(device);
    }

    // Read by the main thread
    volatile LONG *Percent()
    {
        return &m_percent;
    }

    void Update(int value)
    {
        InterlockedExchange((LPLONG)&m_percent, value); // The XDK version takes a non-volatile pointer
    }

  protected:
    virtual void Run(FatxDevice &device) = 0;

    ULONGLONG LbaSize() const
    {
        return m_lba_size;
    }
    bool Writes() const
    {
        return m_write;
    }

  private:
    ULONGLONG m_start_lba;
    ULONGLONG m_lba_size;
    const bool m_write;
    volatile LONG m_percent;
};

// Check, plus a repair or (reading only) a dry run of it
class RepairJob : public FsckJob
{
  public:
    RepairJob(FatxRepairResult *repair, JobAccess access) : FsckJob(access), m_repair(repair)
    {
    }

  protected:
    void Run(FatxDevice &device)
    {
        FatxRepair(device, LbaSize() * HDD_SECTOR_SIZE, m_repair, Writes(), this);
    }

  private:
    FatxRepairResult *const m_repair;
};

// Can a table change alone keep the data?
class ResizeCheckJob : public FsckJob
{
  public:
    ResizeCheckJob(ULONGLONG new_lba_size, FatxResizeResult *resize) : FsckJob(JOB_READS), m_new_lba_size(new_lba_size), m_resize(resize)
    {
    }

  protected:
    void Run(FatxDevice &device)
    {
        FatxCheckResize(device, LbaSize(), m_new_lba_size, m_resize, this);
    }

  private:
    const ULONGLONG m_new_lba_size;
    FatxResizeResult *const m_resize;
};

// Resize with moves, or (reading only) check it can be done
class MoveResizeJob : public FsckJob
{
  public:
    MoveResizeJob(const MoveResizeRequest &request, FatxMoveResult *move, JobAccess access)
        : FsckJob(access), m_new_lba_size(request.new_lba_size), m_table(*request.table), m_move(move)
    {
    }

  protected:
    void Run(FatxDevice &device)
    {
        FatxResizeWithMoves(device, LbaSize(), m_new_lba_size, m_table, Writes(), m_move, this);
    }

  private:
    const ULONGLONG m_new_lba_size;
    FatxResizeTable &m_table;
    FatxMoveResult *const m_move;
};

// Finishes an interrupted resize
class ResumeJob : public FsckJob
{
  public:
    ResumeJob(FatxResizeTable &table, FatxMoveResult *move) : FsckJob(JOB_WRITES), m_table(table), m_move(move)
    {
    }

  protected:
    void Run(FatxDevice &device)
    {
        FatxResumeResize(device, m_table, m_move, this);
    }

  private:
    FatxResizeTable &m_table;
    FatxMoveResult *const m_move;
};

// How much is in use
class UsageJob : public FsckJob
{
  public:
    explicit UsageJob(FatxUsage *usage) : FsckJob(JOB_READS), m_usage(usage), m_ok(false)
    {
    }
    bool Succeeded() const
    {
        return m_ok;
    }

  protected:
    void Run(FatxDevice &device)
    {
        m_ok = FatxReadUsage(device, LbaSize(), m_usage, this);
    }

  private:
    FatxUsage *const m_usage;
    bool m_ok;
};

// A new, empty file system
class FormatJob : public FsckJob
{
  public:
    FormatJob(uint32_t cluster_bytes, uint32_t serial) : FsckJob(JOB_WRITES), m_cluster_bytes(cluster_bytes), m_serial(serial), m_ok(false)
    {
    }
    bool Succeeded() const
    {
        return m_ok;
    }

  protected:
    void Run(FatxDevice &device)
    {
        m_ok = FatxFormat(device, LbaSize(), m_cluster_bytes, m_serial, this);
    }

  private:
    const uint32_t m_cluster_bytes;
    const uint32_t m_serial;
    bool m_ok;
};

static DWORD WINAPI fsck_thread(LPVOID parameter)
{
    ((FsckJob *)parameter)->RunOnDisk();
    return 0;
}

// Formats a cluster count as "1.50 MB"
static void format_clusters_size(ULONGLONG clusters, uint32_t cluster_bytes, wchar_t *out)
{
    FormatHumanSize(clusters * cluster_bytes / HDD_SECTOR_SIZE, out);
}

FsckWorkflow::FsckWorkflow(DialogPresenter &dialogs, const PartitionTableSession &session, int launch_partition)
    : m_dialogs(dialogs), m_session(session), m_launch_partition(launch_partition)
{
}

void FsckWorkflow::RunCheck(int partition, FatxRepairResult *plan)
{
    RepairJob job(plan, JOB_READS);
    RunJob(partition, L"Checking", job);
}

void FsckWorkflow::RunRepair(int partition, FatxRepairResult *repair)
{
    RepairJob job(repair, JOB_WRITES);
    RunJob(partition, L"Repairing", job);
}

void FsckWorkflow::RunResizeCheck(int partition, ULONGLONG new_lba_size, FatxResizeResult *resize)
{
    ResizeCheckJob job(new_lba_size, resize);
    RunJob(partition, L"Checking", job);
}

// The dry run returns before it writes anything, the table included
void FsckWorkflow::PlanMoveResize(int partition, ULONGLONG new_lba_size, FatxMoveResult *move)
{
    NoTableWrites table;
    const MoveResizeRequest request = {partition, new_lba_size, &table};
    MoveResizeJob job(request, move, JOB_READS);
    RunJob(partition, L"Checking", job);
}

void FsckWorkflow::RunMoveResize(const MoveResizeRequest &request, FatxMoveResult *move)
{
    MoveResizeJob job(request, move, JOB_WRITES);
    RunJob(request.partition, L"Resizing", job);
}

bool FsckWorkflow::ReadUsage(int partition, FatxUsage *usage)
{
    UsageJob job(usage);
    RunJob(partition, L"Reading", job);
    return job.Succeeded();
}

bool FsckWorkflow::Format(int partition, uint32_t cluster_bytes)
{
    // A volume serial that differs from format to format, like the kernel's
    FILETIME now;
    GetSystemTimeAsFileTime(&now);
    const uint32_t serial = (now.dwLowDateTime ^ now.dwHighDateTime ^ GetTickCount()) * 2654435761u + (uint32_t)partition;
    FormatJob job(cluster_bytes, serial);
    RunJob(partition, L"Formatting", job);
    return job.Succeeded();
}

void FsckWorkflow::RunResume(int partition, FatxResizeTable &table, FatxMoveResult *move)
{
    ResumeJob job(table, move);
    RunJob(partition, L"Resizing", job);
}

// Uses the partition as it is on disk (the table in use)
void FsckWorkflow::RunJob(int partition, LPCWSTR verb, FsckJob &job)
{
    wchar_t busy[60];
    swprintf(busy, L"%s partition %d (%c)...", verb, partition + 1, PartitionLetter(partition));

    job.Place(m_session.InUse().GetPartitionStart(partition), m_session.InUse().GetPartitionSize(partition));
    const BusyWork work = {fsck_thread, &job, job.Percent()};
    m_dialogs.RunWithBusyWindow(work, busy);
}

// Checks the partition as it is on disk, not as edited
void FsckWorkflow::CheckFileSystem(int partition)
{
    if (!m_session.InUse().IsPartitionEnabled(partition))
    {
        m_dialogs.ShowMessage(L"Not checked", COLOR_YELLOW, L"This partition isn't in the table on disk yet.");
        return;
    }

    // ~10 KB, too big for the stack
    FatxRepairResult *plan = new FatxRepairResult;
    if (plan == NULL)
    {
        m_dialogs.ShowMessage(L"Not checked", COLOR_YELLOW, L"Not enough memory for the check.");
        return;
    }
    RunCheck(partition, plan);

    // Found something the repairs can fix
    const bool fixable = (plan->status == FATX_REPAIR_DONE);
    const bool can_repair = fixable && partition != m_launch_partition;
    LPCWSTR repair_line = NULL;
    if (can_repair)
    {
        repair_line = L"Repair fixes the warnings, file contents stay the same.";
    }
    else if (fixable)
    {
        repair_line = L"Not repairable here: this program runs from it.";
    }

    const DialogButtons buttons = can_repair ? DialogButtons{L"Repair", L"Close"} : DIALOG_OK_ONLY;
    const CheckResultPrompt prompt = {repair_line, buttons};
    if (ShowCheckResult(partition, plan->check, prompt) && can_repair)
    {
        RepairFileSystem(partition, *plan);
    }
    delete plan;
}

// True if the first button was chosen (with only OK, always)
bool FsckWorkflow::ShowCheckResult(int partition, const FatxCheckResult &result, const CheckResultPrompt &prompt)
{
    static const Color TONE_COLORS[] = {COLOR_YELLOW, COLOR_GREEN, COLOR_RED}; // Indexed by CheckReportTone

    const CheckReport report = BuildCheckReport(partition, result);

    LPCWSTR lines[CHECK_REPORT_MAX_LINES + 1];
    int line_count = 0;
    for (int i = 0; i < report.line_count; i++)
    {
        lines[line_count++] = report.lines[i];
    }
    if (prompt.extra_line != NULL)
    {
        lines[line_count++] = prompt.extra_line;
    }
    return m_dialogs.RunDialog(DialogText{report.title, TONE_COLORS[report.tone], lines, line_count}, prompt.buttons);
}

// Confirms the repairs the dry run found, unmounts, repairs and checks again
void FsckWorkflow::RepairFileSystem(int partition, const FatxRepairResult &plan)
{
    // Never repair the partition we're running from
    if (partition == m_launch_partition)
    {
        m_dialogs.ShowMessage(L"Not repaired", COLOR_YELLOW, L"This program runs from that partition.");
        return;
    }
    if (!ConfirmRepair(plan))
    {
        return;
    }

    // Otherwise the kernel's cached FAT could overwrite the repair
    const NTSTATUS dismounted = DismountPartition(partition);
    if (!NT_SUCCESS(dismounted))
    {
        wchar_t error[80];
        swprintf(error, L"Unmounting failed (0x%08X): nothing was changed.", dismounted);
        m_dialogs.ShowMessage(L"Not repaired", COLOR_RED, error);
        return;
    }

    // Re-checks before writing; refuses if errors appeared
    FatxRepairResult *repair = new FatxRepairResult;
    if (repair == NULL)
    {
        m_dialogs.ShowMessage(L"Not repaired", COLOR_YELLOW, L"Not enough memory for the repair.");
        return;
    }
    RunRepair(partition, repair);
    const FatxRepairStatus status = repair->status;
    delete repair;
    ReportRepair(partition, status);
}

// Lists what the dry run would change. True to go ahead.
bool FsckWorkflow::ConfirmRepair(const FatxRepairResult &plan)
{
    wchar_t line_freed[80];
    wchar_t line_trimmed[80];
    wchar_t line_cleared[80];
    LPCWSTR lines[7];
    int line_count = 0;
    if (plan.clusters_freed > 0)
    {
        wchar_t size[32];
        format_clusters_size(plan.clusters_freed, plan.check.cluster_bytes, size);
        swprintf(line_freed, L"Free %I64u unused clusters (%s).", plan.clusters_freed, size);
        lines[line_count++] = line_freed;
    }
    if (plan.files_trimmed > 0)
    {
        swprintf(line_trimmed, L"End the chains of %u files at their size.", plan.files_trimmed);
        lines[line_count++] = line_trimmed;
    }
    if (plan.empty_files_cleared > 0)
    {
        swprintf(line_cleared, L"Free the clusters of %u empty files.", plan.empty_files_cleared);
        lines[line_count++] = line_cleared;
    }
    if (plan.media_fixed)
    {
        lines[line_count++] = L"Set FAT entry 0 back to its usual value.";
    }
    lines[line_count++] = L"No file's contents change. The partition is";
    lines[line_count++] = L"unmounted first, and mounted again when next used.";
    return m_dialogs.RunDialog(DialogText{L"Repair", COLOR_WARNING, lines, line_count}, DialogButtons{L"Repair", L"Cancel"});
}

// Repaired: the check afterwards; otherwise why not
void FsckWorkflow::ReportRepair(int partition, FatxRepairStatus status)
{
    switch (status)
    {
    case FATX_REPAIR_DONE: {
        FatxRepairResult *after = new FatxRepairResult;
        if (after == NULL)
        {
            m_dialogs.ShowMessage(L"Repaired", COLOR_GREEN, L"Repaired. Check it again to see the result.");
            break;
        }
        RunCheck(partition, after);
        const CheckResultPrompt prompt = {L"Repaired. This is the check afterwards.", DIALOG_OK_ONLY};
        ShowCheckResult(partition, after->check, prompt);
        delete after;
        break;
    }
    case FATX_REPAIR_NOTHING_TO_DO:
        m_dialogs.ShowMessage(L"Not repaired", COLOR_YELLOW, L"There was nothing left to repair.");
        break;
    case FATX_REPAIR_HAS_ERRORS:
        m_dialogs.ShowMessage(L"Not repaired", COLOR_RED, L"The check now finds errors: nothing was changed.");
        break;
    case FATX_REPAIR_STOPPED_PARTWAY:
        m_dialogs.ShowMessage(L"Error", COLOR_RED, L"The repair stopped partway. Check the partition again.");
        break;
    default:
        m_dialogs.ShowMessage(L"Not repaired", COLOR_RED, L"The partition could not be read: nothing was changed.");
        break;
    }
}

