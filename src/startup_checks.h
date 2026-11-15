#ifndef STARTUP_CHECKS_H_
#define STARTUP_CHECKS_H_

#include <xtl.h>
#include "dialog_presenter.h"
#include "fsck_workflow.h"
#include "hdd_access.h"
#include "info_screens.h"
#include "partition_table_session.h"
#include "partition_table_store.h"
#include "settings.h"
#include "table_writer.h"

// What the startup checks read, ask and write with: they must outlive them
struct StartupCheckParts
{
    PartitionTableStore &tables;
    const PartitionTableSession &session;
    HddAccess &hdd;
    DialogPresenter &dialogs;
    TableWriter &writer;
    FsckWorkflow &fsck;
    InfoScreens &info;
    const Settings &settings;
    int launch_partition; // The partition this program runs from, -1 if none
};

// What the program checks before the main screen: the BIOS patch, the tables
// and their versions, the backup, interrupted resizes, and what to do about them
class StartupChecks
{
  public:
    explicit StartupChecks(const StartupCheckParts &parts);

    // False if the program must quit: no LBA48 patch and the user chose to,
    // or Stellar's BIOS is in use
    bool Run();

  private:
    void CheckTableVersions();
    // Only a backup table on disk: a Titan BIOS, or restore it as the main one
    void OfferTitanOrRestore();
    void OfferBackupRestore();
    void WarnUnalignedPartitions();
    void WarnStellarDetected();
    void CheckForV4TableOnOldBios();
    bool AskContinueWithoutPatch();
    void CheckTableStatus();
    void WarnBackupOverlapped();
    // Offers the 4 KB shrink that uncovers a matching backup. False if it
    // couldn't be offered.
    bool OfferShrinkForBackup();
    // Whether the 4 KB shrink is harmless to the file system
    bool ShrinkKeepsData(int partition);
    bool CheckStellar();

    PartitionTableStore &m_tables;
    const PartitionTableSession &m_session;
    HddAccess &m_hdd;
    DialogPresenter &m_dialogs;
    TableWriter &m_writer;
    FsckWorkflow &m_fsck;
    InfoScreens &m_info;
    const Settings &m_settings;
    const int m_launch_partition;
};

#endif // STARTUP_CHECKS_H_
