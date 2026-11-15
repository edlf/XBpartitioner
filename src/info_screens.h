#ifndef INFO_SCREENS_H_
#define INFO_SCREENS_H_

#include <xtl.h>
#include "dialog_presenter.h"
#include "hdd_access.h"
#include "partition_table_session.h"
#include "partition_table_store.h"
#include "settings.h"

// What the info screens show: they must outlive them
struct InfoSources
{
    const PartitionTableStore &tables;
    const PartitionTableSession &session;
    const Settings &settings;
    const DriveInfo &drive;
    const Renderer &ui; // For the video mode's name
};

// The System information and About screens
class InfoScreens
{
  public:
    InfoScreens(DialogPresenter &dialogs, const InfoSources &sources);

    // True if System information shows something worth a look that no other
    // startup warning covers: no drive info, a BIOS size below the drive's, no
    // usable table at all, or the backup missing or at its old place
    bool SystemInfoNeedsAttention() const;
    void ShowSystemInfo();
    void ShowAbout();

  private:
    // An info panel's rows, filled one by one
    class InfoRowList
    {
      public:
        InfoRowList() : m_count(0)
        {
        }
        // The new row's value, to fill in
        wchar_t *Add(LPCWSTR label, Color color);
        const InfoRow *Rows() const
        {
            return m_rows;
        }
        int Count() const
        {
            return m_count;
        }

      private:
        static const int MAX_ROWS = 14;
        InfoRow m_rows[MAX_ROWS];
        int m_count;
    };

    // As the drive reports itself
    void AddDriveRows(InfoRowList &rows) const;
    void AddDriveTypeRow(InfoRowList &rows) const;
    // As the BIOS sees it
    void AddBiosSizeRows(InfoRowList &rows) const;
    void AddLba48Rows(InfoRowList &rows) const;
    void AddMainTableRow(InfoRowList &rows) const;
    void AddBackupTableRow(InfoRowList &rows) const;

    DialogPresenter &m_dialogs;
    const PartitionTableStore &m_tables;
    const PartitionTableSession &m_session;
    const Settings &m_settings;
    const DriveInfo &m_drive;
    const Renderer &m_ui;
};

#endif // INFO_SCREENS_H_
