#include "stdafx.h"
#include "startup_checks.h"
#include "array_length.h"
#include "partition_format.h"
#include "disk_partition_device.h"
#include <stdio.h>
#include <string.h>

StartupChecks::StartupChecks(const StartupCheckParts &parts)
    : m_tables(parts.tables), m_session(parts.session), m_hdd(parts.hdd), m_dialogs(parts.dialogs), m_writer(parts.writer),
      m_fsck(parts.fsck), m_info(parts.info), m_settings(parts.settings), m_launch_partition(parts.launch_partition)
{
}

bool StartupChecks::Run()
{
    // Cancel quits
    if (!m_tables.HasLba48Patch() && !AskContinueWithoutPatch())
    {
        return false;
    }

    if (CheckStellar())
    {
        WarnStellarDetected();
        return false;
    }

    // The overview first, the specific warnings after it
    if (m_info.SystemInfoNeedsAttention())
    {
        m_info.ShowSystemInfo();
    }

    // Before anything offers to write
    CheckTableVersions();
    CheckForV4TableOnOldBios();

    // A backup in Titan's version was written for a Titan BIOS (by it, or here
    // with a copy in sector 0): keep writing it that way
    m_tables.SetTitanBios(m_tables.IsBackupTableUsable() && m_tables.BackupTable().Version == XBOX_TABLE_VERSION_TITAN);

    // On an older BIOS a restored v4 backup would be used at the wrong place.
    // Only a backup (sector 0 empty) may be a Titan BIOS's: asked first.
    const bool backup_needs_v4 = !m_tables.FullLba48() && PartitionTableEditor::HasHighLbaBits(m_tables.BackupTable());
    if (m_tables.IsBackupTableUsable() && !backup_needs_v4)
    {
        if (!PartitionTableEditor::HasMagic(m_tables.MainTable()))
        {
            OfferTitanOrRestore();
        }
        else if (!m_tables.IsMainTableUsable())
        {
            OfferBackupRestore();
        }
    }

    // An interrupted resize leaves a partition unusable, so deal with it early
    m_writer.OfferFinishResizes();

    WarnUnalignedPartitions();
    // A matching backup under the last partition can often be kept with a
    // 4 KB shrink; otherwise warn as usual
    if (m_tables.Backup() != BACKUP_OVERLAPPED_MATCH || !OfferShrinkForBackup())
    {
        WarnBackupOverlapped();
    }
    CheckTableStatus();
    return true;
}

// "kernel, hdd" for the tables that match
static void list_tables(const XboxPartitionTable *const *tables, const LPCWSTR *names, int count, bool (*match)(const XboxPartitionTable &), wchar_t *out)
{
    out[0] = L'\0';
    for (int i = 0; i < count; i++)
    {
        if (tables[i] != NULL && match(*tables[i]))
        {
            if (out[0] != L'\0')
            {
                wcscat(out, L", ");
            }
            wcscat(out, names[i]);
        }
    }
}

// A version other than v3, v4 or Titan is only warned about
void StartupChecks::CheckTableVersions()
{
    const XboxPartitionTable *tables[] = {m_tables.HasLba48Patch() ? &m_tables.KernelTable() : NULL, &m_tables.MainTable(), &m_tables.BackupTable()};
    static const LPCWSTR names[] = {L"kernel", L"hdd", L"hdd backup"};

    wchar_t found_in[40];
    list_tables(tables, names, ArrayLength(tables), PartitionTableEditor::HasUnknownVersion, found_in);
    if (found_in[0] == L'\0')
    {
        return;
    }

    // The first unknown one's version, for the message
    unsigned char version = 0;
    for (int i = ArrayLength(tables) - 1; i >= 0; i--)
    {
        if (tables[i] != NULL && PartitionTableEditor::HasUnknownVersion(*tables[i]))
        {
            version = tables[i]->Version;
        }
    }

    wchar_t line1[80];
    wchar_t line2[80];
    swprintf(line1, L"Unknown partition table version 0x%02X.", version);
    swprintf(line2, L"Found in: %s.", found_in);
    const LPCWSTR lines[] = {
        line1,
        line2,
        L"Known are 0x00 (LBA48 v3), 0x01 (v4) and 0x03 (Titan):",
        L"this one may be read wrong. Check it before writing.",
    };
    m_dialogs.ShowWarning(lines, ArrayLength(lines));
}

// Sector 0 is empty and the backup is fine. Titan patched BIOSes only use the
// backup table: with one, the backup is just written again (and from then on
// every write asks about sector 0, see TableWriter::WriteTables). Otherwise the
// usual restore question.
void StartupChecks::OfferTitanOrRestore()
{
    static const LPCWSTR lines[] = {
        L"There is no partition table in sector 0, only the",
        L"backup one. Titan patched BIOSes only use the backup.",
        L"Do you have a Titan patched BIOS?",
    };
    if (!m_dialogs.RunDialog(DialogText{L"Warning", COLOR_WARNING, lines, ArrayLength(lines)}, DialogButtons{L"Titan BIOS", L"No"}))
    {
        m_tables.SetTitanBios(false);
        OfferBackupRestore();
        return;
    }

    m_tables.SetTitanBios(true);
    const TableWriteOutcome outcome = m_writer.WriteTables(m_tables.BackupTable());
    if (outcome.written)
    {
        const LPCWSTR done = outcome.main_written ? L"Written as the backup and the main table." : L"The backup partition table was written.";
        m_dialogs.ShowMessage(L"Done", COLOR_GREEN, done);
    }
    else
    {
        m_dialogs.ShowMessage(L"Error", COLOR_RED, L"The backup partition table could not be written.");
    }
}




// Main table broken, backup fine: offer to restore it (No leaves the disk alone)
void StartupChecks::OfferBackupRestore()
{
    const LPCWSTR lines[] = {
        PartitionTableEditor::HasMagic(m_tables.MainTable()) ? L"The main partition table is damaged." : L"The main partition table is missing.",
        L"The backup partition table is valid.",
        L"Restore it? It is written as the main table.",
    };
    if (!m_dialogs.RunDialog(DialogText{L"Warning", COLOR_WARNING, lines, ArrayLength(lines)}, DialogButtons{L"Restore", L"No"}))
    {
        return;
    }

    // Also gives it to the kernel, so the partitions work right away
    if (m_writer.WriteTables(m_tables.BackupTable()).written)
    {
        m_dialogs.ShowMessage(L"Done", COLOR_GREEN, L"The backup partition table was restored.");
    }
    else
    {
        m_dialogs.ShowMessage(L"Error", COLOR_RED, L"The backup partition table could not be restored.");
    }
}

// Starts or sizes that aren't whole 4 KB blocks (other tools): editing
// one rounds it, which moves it
void StartupChecks::WarnUnalignedPartitions()
{
    const PartitionTableEditor &edited = m_session.Edited();
    PartitionSet unaligned;
    for (int i = 0; i < XboxPartitionTableEntryCount; i++)
    {
        if (edited.IsPartitionEnabled(i) && (!edited.IsAlignedTo4k(edited.GetPartitionStart(i)) || !edited.IsAlignedTo4k(edited.GetPartitionSize(i))))
        {
            unaligned.Add(i);
        }
    }
    if (unaligned.IsEmpty())
    {
        return;
    }

    wchar_t list[100];
    FormatPartitionList(unaligned, list, ArrayLength(list));
    wchar_t line1[120];
    swprintf(line1, L"Not aligned to 4 KB: %s.", list);

    const LPCWSTR lines[] = {
        line1,
        m_tables.Physical4k() ? L"This drive has 4K sectors, so they are slower." : L"That makes them slower on drives with 4K sectors.",
        L"Editing one rounds it to 4 KB, which moves it:",
        L"it then needs formatting, and its data is lost.",
    };
    m_dialogs.ShowWarning(lines, ArrayLength(lines));
}

void StartupChecks::WarnStellarDetected()
{
    const LPCWSTR lines[] = {
        L"You are currently running Stellar.",
        L"This tool has not been tested with it.",
        L"",
        L"Returning you to your launcher.",
    };
    m_dialogs.ShowError(lines, ArrayLength(lines));
}

// Pre v4 patches only use the low 32 bits of each LBA: a table with v4
// values (over 2TB) shows those partitions in the wrong place
void StartupChecks::CheckForV4TableOnOldBios()
{
    if (m_tables.FullLba48() || !m_tables.HasLba48Patch())
    {
        return;
    }

    wchar_t found_in[40] = L"";
    const struct
    {
        const XboxPartitionTable *table;
        LPCWSTR name;
    } tables[] = {
        {&m_tables.KernelTable(), L"kernel"},
        {&m_tables.MainTable(), L"hdd"},
        {&m_tables.BackupTable(), L"hdd backup"},
    };

    for (size_t i = 0; i < ArrayLength(tables); i++)
    {
        if (PartitionTableEditor::HasMagic(*tables[i].table) && PartitionTableEditor::HasHighLbaBits(*tables[i].table))
        {
            if (found_in[0] != L'\0')
            {
                wcscat(found_in, L", ");
            }
            wcscat(found_in, tables[i].name);
        }
    }

    if (found_in[0] == L'\0')
    {
        return;
    }

    // force_lba48_v3 on a v4 BIOS: worded for that
    const bool forced = m_settings.ForceLba48V3() && m_tables.Lba48Version() > 3;

    wchar_t line1[80];
    wchar_t line3[80];
    if (forced)
    {
        swprintf(line1, L"LBA48 v3 is forced in the settings, but a partition table");
    }
    else
    {
        swprintf(line1, L"This BIOS has LBA48 patch v%d, but a partition table", m_tables.Lba48Version());
    }
    swprintf(line3, L"Found in: %s.", found_in);

    const LPCWSTR lines[] = {
        line1,
        L"uses v4 values (over 2TB), likely from a v4 BIOS.",
        line3,
        L"Partitions over 2TB are shown in the wrong place.",
        forced ? L"Do not write or format. Turn force_lba48_v3 off." : L"Do not write or format. Use a v4 BIOS such as Cerbios.",
    };
    m_dialogs.ShowWarning(lines, ArrayLength(lines));
}

// True to continue without the LBA48 patch
bool StartupChecks::AskContinueWithoutPatch()
{
    static const LPCWSTR lines[] = {
        L"No LBA48 patch found: custom partitions will not work",
        L"with this BIOS. Update your BIOS to Cerbios.",
    };
    return m_dialogs.RunDialog(DialogText{L"Warning", COLOR_WARNING, lines, ArrayLength(lines)}, DialogButtons{L"Continue", L"Quit"});
}

void StartupChecks::CheckTableStatus()
{
    // Only when both are usable: a broken main gets the restore question,
    // and no backup is normal with other tools
    if (m_tables.IsMainTableUsable() && m_tables.IsBackupTableUsable() && !SameTableButVersion(m_tables.MainTable(), m_tables.BackupTable()))
    {
        static const LPCWSTR lines[] = {
            L"The main and backup partition tables don't match!",
            L"Check which one is correct before doing anything.",
            L"Compare them via Partition table > Load partition table.",
        };
        m_dialogs.ShowWarning(lines, ArrayLength(lines));
    }
}

// The last partition covers the backup sector: no backup until that changes
void StartupChecks::WarnBackupOverlapped()
{
    // A matching backup only gets here if OfferShrinkForBackup() couldn't help
    if (m_tables.Backup() != BACKUP_OVERLAPPED && m_tables.Backup() != BACKUP_OVERLAPPED_MATCH)
    {
        return;
    }

    static const LPCWSTR lines[] = {
        L"The last partition covers the backup table sector",
        L"(on 4K sector drives it is in the last 4K block).",
        L"No backup is written until the layout leaves room:",
        L"shrink the last partition, or use a new partition table.",
    };
    m_dialogs.ShowWarning(lines, ArrayLength(lines));
}

// The last partition covers the backup sector, but another tool left a valid
// backup there, so that 4 KB block holds no data. Shrinking by 4 KB keeps the
// data and needs no format.
bool StartupChecks::OfferShrinkForBackup()
{
    const int part = m_session.InUse().FindBackupShrinkCandidate();
    if (part < 0 || !ShrinkKeepsData(part))
    {
        return false;
    }

    wchar_t line1[80];
    swprintf(line1, L"Partition %d (%c) covers the backup table sector, but", part + 1, PartitionLetter(part));
    const LPCWSTR lines[] = {
        line1,
        L"the backup there matches the main partition table,",
        L"so its last 4 KB hold no data. Shrink it by 4 KB so",
        L"the backup is kept? Its data stays, no format needed.",
    };
    if (!m_dialogs.RunDialog(DialogText{L"Backup table", COLOR_WARNING, lines, ArrayLength(lines)}, DialogButtons{L"Shrink", L"No"}))
    {
        return true;
    }

    PartitionTableEditor shrunk = m_tables.Manipulate(m_session.InUse().GetPartitionTable());
    shrunk.PartitionDecreaseSizeBy(part, LBA_ALIGNMENT);

    const TableWriteOutcome outcome = m_writer.WriteTables(shrunk.GetPartitionTable());
    if (outcome.written)
    {
        wchar_t done[80];
        swprintf(done, outcome.backup_written ? L"Partition %d shrunk by 4 KB, backup table written." : L"Partition %d shrunk by 4 KB.", part + 1);
        m_dialogs.ShowMessage(L"Done", COLOR_GREEN, done);

        // Unmount so the kernel picks up the new size; until then it could
        // write into the freed block, where the backup now lives. Our own
        // partition can't be unmounted, but settings stop being saved once
        // it has changed size, so nothing writes there.
        if (part != m_launch_partition)
        {
            DismountPartition(part);
        }

        // Show that the file system still reads fine
        m_fsck.CheckFileSystem(part);
    }
    else
    {
        m_dialogs.ShowMessage(L"Error", COLOR_RED, L"The partition table could not be written.");
    }
    return true;
}

// Checks the 4 KB shrink is harmless: the FAT size (derived from the
// partition size) must not change, and the dropped block must not be part
// of a used cluster.
bool StartupChecks::ShrinkKeepsData(int partition)
{
    const ULONGLONG start = m_session.InUse().GetPartitionStart(partition);
    const ULONGLONG size = m_session.InUse().GetPartitionSize(partition);

    BYTE sector[HDD_SECTOR_SIZE];
    if (!m_hdd.ReadSector(start, sector))
    {
        return false;
    }

    FAT_VOLUME_METADATA header;
    memcpy(&header, sector, HDD_SECTOR_SIZE); // The fields needed are in the first sector
    if (header.Signature != FATX_signature)
    {
        return true; // No file system to keep
    }

    // Can't check an unknown (0) or odd cluster size. Clusters under 4 KB
    // don't occur on an Xbox.
    const uint32_t sectors_per_cluster = header.SectorsPerCluster;
    if (sectors_per_cluster < LBA_ALIGNMENT || (sectors_per_cluster & (sectors_per_cluster - 1)) != 0)
    {
        return false;
    }
    const uint32_t cluster_bytes = sectors_per_cluster * HDD_SECTOR_SIZE;

    const FatxLayout before = FatxLayoutFor(size, cluster_bytes);
    const FatxLayout after = FatxLayoutFor(size - LBA_ALIGNMENT, cluster_bytes);
    if (before.bytes_per_entry != after.bytes_per_entry || before.bytes_per_fat != after.bytes_per_fat)
    {
        return false;
    }

    // Clusters are at least 4 KB and aligned, so the block is in one cluster or none
    const uint64_t cluster = FatxClusterAt(before, cluster_bytes, (size - LBA_ALIGNMENT) * HDD_SECTOR_SIZE);
    if (cluster == 0)
    {
        return true; // In the unused tail
    }

    // The cluster must be free (0) in the FAT
    const ULONGLONG entry_byte = start * HDD_SECTOR_SIZE + FATX_HEADER_BYTES + cluster * before.bytes_per_entry;
    if (!m_hdd.ReadSector(entry_byte / HDD_SECTOR_SIZE, sector))
    {
        return false;
    }
    const BYTE *entry = sector + (entry_byte % HDD_SECTOR_SIZE);
    for (uint32_t i = 0; i < before.bytes_per_entry; i++)
    {
        if (entry[i] != 0)
        {
            return false;
        }
    }
    return true;
}

bool StartupChecks::CheckStellar()
{
    const char *stellarSignature = "stellar";
    size_t stellarSignatureLen = strlen(stellarSignature);
    BYTE *stellarScanStart = (BYTE *)0x80010000;
    BYTE *stellarScanEnd = (BYTE *)0x80026800; // 90kb

    for (BYTE *scanPtr = stellarScanStart; scanPtr < stellarScanEnd - stellarSignatureLen; scanPtr++)
    {
        if (_memicmp(scanPtr, stellarSignature, stellarSignatureLen) == 0)
        {
            return true;
        }
    }

    return false;
}
