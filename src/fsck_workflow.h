#ifndef FSCK_WORKFLOW_H_
#define FSCK_WORKFLOW_H_

#include <xtl.h>
#include "dialog_presenter.h"
#include "fatx_check.hpp"
#include "fatx_resize.hpp"
#include "partition_table_session.h"

class FsckJob; // src/fsck_workflow.cpp

// A resize with moves to run (FsckWorkflow::RunMoveResize)
struct MoveResizeRequest
{
    int partition;
    ULONGLONG new_lba_size;
    FatxResizeTable *table; // Writes the new size to the partition table
};

// File system work on the partitions as they are on disk (the table in use):
// the check and repair screens, and the disk jobs behind them, each on a
// worker thread behind the busy window so the screen keeps moving
class FsckWorkflow
{
  public:
    // launch_partition: the one this program runs from (-1: none), which is
    // never repaired
    FsckWorkflow(DialogPresenter &dialogs, const PartitionTableSession &session, int launch_partition);

    // Checks it and offers the repairs, when they can fix something
    void CheckFileSystem(int partition);

    // Whether a table change alone can resize it to new_lba_size
    void RunResizeCheck(int partition, ULONGLONG new_lba_size, FatxResizeResult *resize);
    // Whether moving clusters can resize it; nothing is written
    void PlanMoveResize(int partition, ULONGLONG new_lba_size, FatxMoveResult *move);
    // The partition must be unmounted first
    void RunMoveResize(const MoveResizeRequest &request, FatxMoveResult *move);
    // Finishes an interrupted resize
    void RunResume(int partition, FatxResizeTable &table, FatxMoveResult *move);
    // How much is in use, from a scan of its FAT. False if it can't be read.
    bool ReadUsage(int partition, FatxUsage *usage);
    // A new, empty file system. It must be unmounted first. False if a write failed.
    bool Format(int partition, uint32_t cluster_bytes);

  private:
    // What a check result adds below the findings
    struct CheckResultPrompt
    {
        LPCWSTR extra_line; // Or NULL
        DialogButtons buttons;
    };

    // A dry run of the repair: what it would fix, nothing written
    void RunCheck(int partition, FatxRepairResult *plan);
    void RunRepair(int partition, FatxRepairResult *repair);
    void RunJob(int partition, LPCWSTR verb, FsckJob &job);
    // True if the first button was chosen (with only OK, always)
    bool ShowCheckResult(int partition, const FatxCheckResult &result, const CheckResultPrompt &prompt);
    void RepairFileSystem(int partition, const FatxRepairResult &plan);
    bool ConfirmRepair(const FatxRepairResult &plan);
    void ReportRepair(int partition, FatxRepairStatus status);

    DialogPresenter &m_dialogs;
    const PartitionTableSession &m_session;
    const int m_launch_partition;
};

#endif // FSCK_WORKFLOW_H_
