#ifndef TABLE_WRITER_H_
#define TABLE_WRITER_H_

#include <xtl.h>
#include "dialog_presenter.h"
#include "filesystem_cache.h"
#include "fsck_workflow.h"
#include "partition_set.h"
#include "partition_table_session.h"
#include "partition_table_store.h"
#include "settings.h"

// The resized partitions that keep their data (TableWriter::FindResizesKeepingData)
struct ResizesKeepingData
{
    PartitionSet kept;   // By a table change alone
    PartitionSet moved;  // By moving clusters
    wchar_t reason[100]; // Why the first one that can't keep it can't, or empty

    ResizesKeepingData()
    {
        reason[0] = L'\0';
    }

    PartitionSet Keeping() const
    {
        return kept.Union(moved);
    }
};

// What the table writer changes and asks: they must outlive it
struct TableWriterParts
{
    PartitionTableStore &tables;
    PartitionTableSession &session;
    FilesystemCache &filesystems;
    FsckWorkflow &fsck;
    DialogPresenter &dialogs;
    const Settings &settings;
    int launch_partition; // The partition this program runs from, -1 if none
};

// Writing partition tables, with every warning before it: writing the edited
// table, formatting what it changes, resizes that keep their data, erasing,
// and reloading the editor afterwards
class TableWriter
{
    friend class XboxResizeTable; // Writes the table from a worker thread

  public:
    explicit TableWriter(const TableWriterParts &parts);

    // Starts the editor from the kernel's table, or the stock layout without
    // the LBA48 patch. False without the patch.
    bool StartFromKernelTable();
    // The main table and, when possible, the backup. With a Titan BIOS, asks
    // whether to write sector 0 too.
    TableWriteOutcome WriteTables(const XboxPartitionTable &table);
    // At startup: offers to finish interrupted resizes
    void OfferFinishResizes();

    // Start > Partition table actions
    void WriteEditedTable();
    void WriteAndFormatChanged();
    void EraseTables();
    // Start > Format partition: only the layout on disk
    void FormatOne(int part);
    // A write that didn't finish, unless already explained: whether the
    // backup made it, else nothing_written
    void ShowWriteFailure(const TableWriteOutcome &outcome, LPCWSTR nothing_written);

    // Once D:'s partition has changed, writing through D: would hit the wrong
    // sectors, so the settings aren't saved anymore
    bool SettingsSaveBlocked() const
    {
        return m_settings_save_blocked;
    }

  private:
    // The "data kept" and "clusters move" lines of a warning
    struct ResizeNotes
    {
        wchar_t kept[120];
        wchar_t moved[120];
    };

    // What a partition's first sectors say about an unfinished change
    struct InterruptedResize
    {
        bool reclustering; // A cluster size change, only finished on a PC
        bool resizing;     // A resize with moves, which the journal can finish
        uint64_t new_size;
    };

    // Lists what will be formatted and calls out E and C. True to go ahead.
    bool ConfirmFormatWarning(const PartitionSet &marked, const ResizesKeepingData &resizes);
    // The notes for these resizes, in lines; returns how many were added
    int AddResizeNotes(const ResizesKeepingData &resizes, ResizeNotes *notes, LPCWSTR *lines);
    // The edited table moves or removes this program's partition, and doesn't keep its data
    bool LosesLaunchPartition(const PartitionSet &keeps) const;

    // Writing the edited table: a warning when data is lost, else a question
    bool ConfirmWrite(const ResizesKeepingData &resizes);
    bool ConfirmUnreadable(const ResizesKeepingData &resizes, const PartitionSet &resized);
    bool ConfirmPlainWrite(const ResizesKeepingData &resizes);
    void ReportWrite(const TableWriteOutcome &outcome);
    void FormatAfterWrite(const PartitionSet &to_format, const TableWriteOutcome &outcome);
    // "Partition N (X) holds this program: <what>"
    void FormatLaunchPartitionLine(wchar_t *out, size_t out_count, LPCWSTR what);
    // True if table moves, resizes or removes the partition this program runs from
    bool MovesLaunchPartition(const XboxPartitionTable &table) const;
    bool KeepsLaunchPartition(const PartitionSet &kept) const;
    // Explains why a table can't be written and returns true: a partition in
    // the reserved area, or overlapping or shared starts
    bool RefuseUnwritableTable(const XboxPartitionTable &table);
    // what: the end of the advice, e.g. "before writing."
    void ShowReservedAreaWarning(int partition, LPCWSTR what);
    bool FormatKeptLine(const PartitionSet &kept, wchar_t *out, size_t out_count);
    bool FormatMovedLine(const PartitionSet &moved, wchar_t *out, size_t out_count);
    bool FormatPartition(const int i);

    ResizesKeepingData FindResizesKeepingData();
    // NULL if the resize keeps the data, else why not ("" if it can't tell)
    LPCWSTR InPlaceResizeProblem(int partition, ULONGLONG new_size);
    LPCWSTR MoveResizeProblem(int partition, ULONGLONG new_size);
    bool RunMoveResizes(const PartitionSet &partitions, const ULONGLONG new_sizes[XboxPartitionTableEntryCount], bool write_main);
    // False, after explaining it, if it didn't finish
    bool RunMoveResize(int partition, ULONGLONG new_size, bool write_main);
    // Shrinks with moves finished, but the table after them wasn't written
    void ExplainRestNotWritten(const PartitionSet &shrunk);
    // False, after explaining it, if the partition couldn't be unmounted
    bool DismountForResize(int partition);
    void ExplainStoppedResize(int partition, const FatxMoveResult &move);
    InterruptedResize FindInterruptedResize(int partition);
    void OfferFinishResize(int partition);
    void FinishResize(int partition);
    // Writes the edited table with those resizes
    TableWriteOutcome WriteTableWithResizes(const ResizesKeepingData &resizes);
    // After writing: unmount so the kernel remounts them at the new size
    void UnmountResized(const PartitionSet &kept);

    // The steps of WriteTables(). WriteTablesRaw() has no dialogs or editor
    // updates, so a worker thread can call it.
    TableWritePlan PlanTableWrite(const XboxPartitionTable &table_in);
    TableWriteOutcome WriteTablesRaw(const XboxPartitionTable &table, bool write_main);
    // A table already planned (the Titan question answered), then the reload
    TableWriteOutcome WritePlanned(const XboxPartitionTable &table, bool write_main);
    void ReloadAfterWrite(const XboxPartitionTable &written);
    bool ErasePartitionTables();

    PartitionTableStore &m_tables;
    PartitionTableSession &m_session;
    FilesystemCache &m_filesystems;
    FsckWorkflow &m_fsck;
    DialogPresenter &m_dialogs;
    const Settings &m_settings;
    const int m_launch_partition;
    bool m_settings_save_blocked;
};

#endif // TABLE_WRITER_H_
