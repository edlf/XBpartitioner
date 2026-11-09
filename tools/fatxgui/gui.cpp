// The UI: open a disk image, a partition image or an Xbox drive, see its
// partitions and run fatxtool's commands on them. Slow work runs on a worker
// thread (Job) behind a progress window; nothing else can start meanwhile.
//
// Like fatxtool, only images are ever written. Drives are read, and TRIMmed.

#define NOMINMAX
#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shobjidl.h>

#include "gui.hpp"
#include "look.hpp"

#include "imgui.h"

#include "fatx_check.hpp"
#include "fatx_recluster.hpp"
#include "fatx_resize.hpp"
#include "fatx_trim.hpp"
#include "fatx_writer.hpp"
#include "pc_image.hpp"

#include <algorithm>
#include <atomic>
#include <functional>
#include <io.h>
#include <memory>
#include <mutex>
#include <stdarg.h>
#include <stdio.h>
#include <string>
#include <thread>
#include <vector>

#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")

namespace fs = std::filesystem;

namespace
{

HWND g_hwnd = nullptr;
float g_scale = 1.0f;

// ---- Text

std::string utf8(const std::wstring &text)
{
    if (text.empty())
    {
        return std::string();
    }
    const int length = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(), nullptr, 0, nullptr, nullptr);
    std::string out(length, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(), &out[0], length, nullptr, nullptr);
    return out;
}

std::string format(const char *fmt, ...)
{
    char buffer[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    return buffer;
}

// "1823.50 MB", like fatxtool
std::string mb(uint64_t lba)
{
    const uint64_t bytes = lba * HDD_SECTOR_SIZE;
    return format("%llu.%02llu MB", bytes >> 20, ((bytes % (1 << 20)) * 100) >> 20);
}

// "931.51 GB"
std::string human(uint64_t bytes)
{
    static const char *units[] = {"bytes", "KB", "MB", "GB", "TB"};
    double value = (double)bytes;
    int unit = 0;
    while (value >= 1024.0 && unit < 4)
    {
        value /= 1024.0;
        unit++;
    }
    return unit == 0 ? format("%llu bytes", bytes) : format("%.2f %s", value, units[unit]);
}

std::string partition_name(int index)
{
    return index < 0 ? std::string("Partition") : format("Partition %d (%c)", index + 1, partition_letter(index));
}

// ---- Log

enum Tone
{
    TONE_TEXT,
    TONE_DIM,
    TONE_GOOD,
    TONE_WARN,
    TONE_ERROR,
    TONE_TITLE
};

struct LogLine
{
    Tone tone;
    std::string text;
};

std::mutex g_log_mutex;
std::vector<LogLine> g_log;
bool g_log_scroll = false;

void log_line(Tone tone, const std::string &text)
{
    std::lock_guard<std::mutex> lock(g_log_mutex);
    g_log.push_back(LogLine{tone, text});
    g_log_scroll = true;
}

// The Xbox app's text roles, in the current theme
ImVec4 tone_color(Tone tone)
{
    const Theme &theme = LookTheme();
    switch (tone)
    {
    case TONE_DIM:
        return LookColor(theme.text[COLOR_GREY]);
    case TONE_GOOD:
        return LookColor(theme.text[COLOR_GREEN]);
    case TONE_WARN:
        return LookColor(theme.text[COLOR_WARNING]);
    case TONE_ERROR:
        return LookColor(theme.text[COLOR_RED]);
    case TONE_TITLE:
        return LookColor(theme.text[COLOR_YELLOW]);
    default:
        return LookColor(theme.text[COLOR_WHITE]);
    }
}

// ---- Jobs: one at a time, on a worker thread

struct Job : public FatxProgress
{
    std::string title;
    std::atomic<int> percent{-1}; // -1 until the work reports any
    std::atomic<bool> finished{false};
    bool writes = false;          // Closing the window waits for it
    std::function<void(Job &)> work;
    std::function<void()> then;   // On the UI thread, after the work
    std::thread thread;

    void Update(int value) override
    {
        percent = value;
    }
};

std::unique_ptr<Job> g_job;
bool g_close_refused = false;

void start_job(const std::string &title, bool writes, std::function<void(Job &)> work, std::function<void()> then = nullptr)
{
    g_job = std::make_unique<Job>();
    g_job->title = title;
    g_job->writes = writes;
    g_job->work = std::move(work);
    g_job->then = std::move(then);
    Job *job = g_job.get();
    job->thread = std::thread([job]() {
        job->work(*job);
        job->finished = true;
        PostMessageW(g_hwnd, WM_NULL, 0, 0); // Wake the loop
    });
}

// Maps 0-100 onto part of an outer progress
class SubProgress : public FatxProgress
{
  public:
    SubProgress(FatxProgress &outer, int from, int to) : m_outer(outer), m_from(from), m_to(to)
    {
    }
    void Update(int value) override
    {
        m_outer.Update(m_from + (m_to - m_from) * value / 100);
    }

  private:
    FatxProgress &m_outer;
    int m_from;
    int m_to;
};

// ---- What's open

struct Part
{
    int index = -1;               // In the table, -1 for a partition image
    uint64_t start = 0;           // LBAs
    uint64_t sectors = 0;
    bool fits = true;             // Inside the image or drive
    bool fatx = false;
    bool resizing = false;        // Interrupted resize (XRSZ)
    uint64_t resize_old = 0;
    uint64_t resize_new = 0;
    bool usage_ok = false;
    FatxUsage usage = {};
    uint64_t max_sectors = 0;     // How far it can grow: the next partition or the end
};

struct Source
{
    enum Type
    {
        NONE,
        IMAGE,
        DRIVE
    } type = NONE;
    fs::path path;                // Image file, or \\.\PhysicalDriveN
    std::string name;
    uint64_t bytes = 0;
    bool has_table = false;
    std::vector<Part> parts;
    bool recluster_pending = false;
    ReclusterPlanHeader plan = {};

    // Repairs and resizes: images and drives
    bool Writable() const
    {
        return type != NONE;
    }
    // A cluster size change builds a full copy next to the image first
    bool CanRecluster() const
    {
        return type == IMAGE;
    }
};

Source g_src;
int g_selected = 0;

// Opens what's loaded (or about to be) for one job
class SourceIo
{
  public:
    bool Open(const Source &src, bool write, bool trim, std::string *error)
    {
        return m_target.Open(src.path, write, trim, error);
    }
    // The whole image or drive
    FatxDevice &Disk()
    {
        return m_target.Device();
    }
    // Where a partition's new size goes: the image (its table or its length),
    // or the drive's table and its backup
    std::unique_ptr<FatxResizeTable> Table(int index)
    {
        return m_target.Table(index);
    }
    WindowsDisk *Drive()
    {
        return m_target.Drive();
    }
    FILE *File()
    {
        return m_target.File();
    }
    uint64_t Size() const
    {
        return m_target.Size();
    }

  private:
    DiskTarget m_target; // Shared with fatxtool
};

uint32_t read_signature(FatxReader &reader)
{
    unsigned char sector[HDD_SECTOR_SIZE];
    uint32_t signature = 0;
    if (reader.Read(0, sector, sizeof(sector)))
    {
        memcpy(&signature, sector, 4);
    }
    return signature;
}

// Reads the table and what each partition holds. On the worker thread.
bool scan_source(Source *src, Job &job, std::string *error)
{
    SourceIo io;
    if (!io.Open(*src, false, false, error))
    {
        return false;
    }
    src->bytes = io.Size();
    src->parts.clear();

    XboxPartitionTable table;
    memset(&table, 0, sizeof(table));
    io.Disk().Read(0, &table, sizeof(table));
    src->has_table = XboxPartitionTableManipulator::HasMagic(table);
    if (src->has_table)
    {
        // The high bits only count from v4 on. 4K: keeps clear of the last 4K
        // block, where XBpartitioner puts the backup on 512e drives (and the
        // last sector, where it goes otherwise, is inside or past it).
        const XboxPartitionTableManipulator editor(table, src->bytes / HDD_SECTOR_SIZE, table.Version != XBOX_TABLE_VERSION_LBA48_V3, true);
        for (int i = 0; i < XboxPartitionTableEntryCount; i++)
        {
            if (editor.isPartitionEnabled(i))
            {
                Part part;
                part.index = i;
                part.start = editor.GetPartitionStart(i);
                part.sectors = editor.GetPartitionSize(i);
                src->parts.push_back(part);
            }
        }
        // Growing stops where XBpartitioner stops it: at the next partition,
        // or before the backup table at the end of the disk
        for (Part &part : src->parts)
        {
            part.max_sectors = part.sectors + editor.GetGrowthLimit(part.index);
        }
    }
    else if (src->type == Source::IMAGE)
    {
        Part part;
        part.start = 0;
        part.sectors = src->bytes / HDD_SECTOR_SIZE;
        part.max_sectors = (16ULL << 40) / HDD_SECTOR_SIZE; // The file just grows
        src->parts.push_back(part);
    }
    else
    {
        *error = "No Xbox partition table on this drive.";
        return false;
    }

    const int count = (int)src->parts.size();
    for (int i = 0; i < count; i++)
    {
        Part &part = src->parts[i];
        part.fits = (part.start + part.sectors) * HDD_SECTOR_SIZE <= src->bytes;
        if (!part.fits)
        {
            continue;
        }
        FatxOffsetReader reader(io.Disk(), part.start * HDD_SECTOR_SIZE);
        const uint32_t signature = read_signature(reader);
        part.resizing = FatxFindInterruptedResize(reader, &part.resize_old, &part.resize_new) || signature == FATX_RESIZING_SIGNATURE;
        part.fatx = signature == FATX_signature;
        if (part.fatx && !part.resizing)
        {
            SubProgress progress(job, i * 100 / count, (i + 1) * 100 / count);
            part.usage_ok = FatxReadUsage(reader, part.sectors, &part.usage, &progress);
        }
    }

    if (src->type == Source::IMAGE)
    {
        std::vector<FatxRange> ranges;
        src->recluster_pending = load_recluster_plan(recluster_plan_path(src->path), &src->plan, &ranges);
    }
    return true;
}

// ---- Dialogs (confirmations and messages)

struct Confirm
{
    bool open = false;
    std::string title;
    std::vector<std::string> lines;
    std::string button; // Empty: a message with OK only
    std::function<void()> action;
};

Confirm g_confirm;

void ask(const std::string &title, std::vector<std::string> lines, const std::string &button, std::function<void()> action)
{
    g_confirm.open = true;
    g_confirm.title = title;
    g_confirm.lines = std::move(lines);
    g_confirm.button = button;
    g_confirm.action = std::move(action);
}

void tell(const std::string &title, std::vector<std::string> lines)
{
    ask(title, std::move(lines), std::string(), nullptr);
}

// ---- Loading

void open_source(Source::Type type, const fs::path &path, const std::string &name, int keep_selected = 0)
{
    auto loaded = std::make_shared<Source>();
    loaded->type = type;
    loaded->path = path;
    loaded->name = name;
    auto error = std::make_shared<std::string>();
    auto ok = std::make_shared<bool>(false);
    start_job(
        "Reading " + name,
        false,
        [loaded, error, ok](Job &job) { *ok = scan_source(loaded.get(), job, error.get()); },
        [loaded, error, ok, keep_selected]() {
            if (!*ok)
            {
                log_line(TONE_ERROR, loaded->name + ": " + *error);
                tell("Can't open it", {*error});
                return;
            }
            g_src = *loaded;
            g_selected = keep_selected < (int)g_src.parts.size() ? keep_selected : 0;
        });
}

void reload()
{
    if (g_src.type != Source::NONE)
    {
        open_source(g_src.type, g_src.path, g_src.name, g_selected);
    }
}

void open_image(const fs::path &path)
{
    log_line(TONE_TITLE, "Opened " + utf8(path.wstring()));
    open_source(Source::IMAGE, path, utf8(path.filename().wstring()));
}

// ---- File dialogs

bool pick_file(bool save, std::wstring *path)
{
    wchar_t buffer[MAX_PATH * 4] = L"";
    if (!path->empty())
    {
        wcsncpy_s(buffer, path->c_str(), _TRUNCATE);
    }
    OPENFILENAMEW ofn = {sizeof(ofn)};
    ofn.hwndOwner = g_hwnd;
    ofn.lpstrFilter = L"Images (*.img, *.bin, *.raw)\0*.img;*.bin;*.raw\0All files\0*.*\0";
    ofn.lpstrFile = buffer;
    ofn.nMaxFile = ARRAYSIZE(buffer);
    ofn.lpstrDefExt = save ? L"img" : nullptr;
    ofn.Flags = OFN_NOCHANGEDIR | OFN_EXPLORER | (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
    const BOOL ok = save ? GetSaveFileNameW(&ofn) : GetOpenFileNameW(&ofn);
    if (ok)
    {
        *path = buffer;
    }
    return ok != FALSE;
}

bool pick_folder(std::wstring *path)
{
    IFileOpenDialog *dialog = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog))))
    {
        return false;
    }
    DWORD options = 0;
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
    bool ok = false;
    if (SUCCEEDED(dialog->Show(g_hwnd)))
    {
        IShellItem *item = nullptr;
        if (SUCCEEDED(dialog->GetResult(&item)))
        {
            PWSTR name = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &name)))
            {
                *path = name;
                CoTaskMemFree(name);
                ok = true;
            }
            item->Release();
        }
    }
    dialog->Release();
    return ok;
}

// ---- Drives

bool is_elevated()
{
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
    {
        return false;
    }
    TOKEN_ELEVATION elevation = {};
    DWORD size = 0;
    const bool elevated = GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &size) && elevation.TokenIsElevated;
    CloseHandle(token);
    return elevated;
}

void restart_elevated()
{
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    if ((INT_PTR)ShellExecuteW(nullptr, L"runas", exe, nullptr, nullptr, SW_SHOWNORMAL) > 32)
    {
        PostMessageW(g_hwnd, WM_CLOSE, 0, 0);
    }
}

struct DriveInfo
{
    int number;
    uint64_t bytes;
    int partitions;
};

std::vector<DriveInfo> g_drives;
bool g_drives_scanned = false;
bool g_drive_window = false;

void scan_drives()
{
    auto found = std::make_shared<std::vector<DriveInfo>>();
    start_job(
        "Looking for Xbox drives",
        false,
        [found](Job &job) {
            for (int n = 0; n < 32; n++)
            {
                job.Update(n * 100 / 32);
                WindowsDisk disk;
                unsigned long error = 0;
                if (!disk.Open(format("\\\\.\\PhysicalDrive%d", n).c_str(), false, &error))
                {
                    continue;
                }
                XboxPartitionTable table;
                if (!disk.Read(0, &table, sizeof(table)) || !XboxPartitionTableManipulator::HasMagic(table))
                {
                    continue; // Only Xbox drives, so never the Windows one
                }
                const XboxPartitionTableManipulator editor(table, disk.Size() / HDD_SECTOR_SIZE, table.Version != XBOX_TABLE_VERSION_LBA48_V3, false);
                found->push_back(DriveInfo{n, disk.Size(), editor.GetActivePartitionCount()});
            }
        },
        [found]() {
            g_drives = *found;
            g_drives_scanned = true;
        });
}

// ---- Logging results, like fatxtool prints them

// How many files are fragmented, and the worst ones
void log_fragmentation(const FatxCheckResult &result)
{
    if (result.files == 0)
    {
        return;
    }
    if (result.fragmented_files == 0)
    {
        log_line(TONE_TEXT, "  Fragmentation: none, no file is fragmented.");
        return;
    }
    log_line(
        TONE_TEXT,
        format(
            "  Fragmentation: %llu of %llu files fragmented (%llu extra fragments). Most:", result.fragmented_files, result.files, result.extra_pieces));
    for (uint32_t i = 0; i < result.worst_count; i++)
    {
        log_line(TONE_DIM, format("    %u fragments: %s", result.worst[i].pieces, result.worst[i].path));
    }
}

void log_check(const FatxCheckResult &result)
{
    switch (result.status)
    {
    case FATX_CHECK_RESIZING:
        log_line(TONE_ERROR, "  A resize or cluster size change was interrupted: finish it first.");
        return;
    case FATX_CHECK_NOT_FATX:
        log_line(TONE_DIM, "  Not FATX (unformatted, or another file system): not checked.");
        return;
    case FATX_CHECK_READ_ERROR:
        log_line(TONE_ERROR, "  Read error (is the image shorter than the partition?).");
        break;
    case FATX_CHECK_NO_MEMORY:
        log_line(TONE_ERROR, "  Out of memory.");
        break;
    case FATX_CHECK_BAD_HEADER:
        break;
    default:
        log_line(TONE_TEXT,
                 format("  FATX, %u KB clusters, %u bit FAT, %llu files, %llu folders",
                        result.cluster_bytes / 1024,
                        result.layout.bytes_per_entry * 8,
                        result.files,
                        result.directories));
        log_line(TONE_TEXT,
                 format("  Clusters: %llu in use of %llu, %llu lost, %llu bad",
                        result.used_clusters,
                        result.layout.cluster_limit - 1,
                        result.lost_clusters,
                        result.bad_clusters));
        log_fragmentation(result);
        break;
    }

    for (uint32_t i = 0; i < result.message_count; i++)
    {
        log_line(result.messages[i].error ? TONE_ERROR : TONE_WARN,
                 std::string(result.messages[i].error ? "  Error: " : "  Warning: ") + result.messages[i].text);
    }
    const uint32_t total = result.errors + result.warnings;
    if (total > result.message_count)
    {
        log_line(TONE_DIM, format("  (%u more not shown)", total - result.message_count));
    }
    if (result.status == FATX_CHECK_DONE)
    {
        if (result.errors == 0)
        {
            log_line(TONE_GOOD, format("  OK: no errors, %u warnings.", result.warnings));
        }
        else
        {
            log_line(TONE_ERROR, format("  %u errors, %u warnings.", result.errors, result.warnings));
        }
    }
}

// The last lines of a confirmation before writing
std::vector<std::string> write_warning(const Source &src)
{
    if (src.type == Source::DRIVE)
    {
        return {"This writes to the drive itself, not a copy: back it up first.", "Close FATXplorer or anything else that has the drive open."};
    }
    return {"Make sure nothing else has the image open."};
}

std::vector<std::string> with_warning(std::vector<std::string> lines, const Source &src)
{
    for (const std::string &line : write_warning(src))
    {
        lines.push_back(line);
    }
    return lines;
}

std::string repairs_text(const FatxRepairResult &repair)
{
    std::string text;
    auto add = [&text](const std::string &item) { text += (text.empty() ? "" : ", ") + item; };
    if (repair.clusters_freed > 0)
    {
        add(format("free %llu clusters (%s)", repair.clusters_freed, human(repair.clusters_freed * repair.check.cluster_bytes).c_str()));
    }
    if (repair.files_trimmed > 0)
    {
        add(format("end the chains of %u files", repair.files_trimmed));
    }
    if (repair.empty_files_cleared > 0)
    {
        add(format("clear %u empty files", repair.empty_files_cleared));
    }
    if (repair.media_fixed)
    {
        add("set FAT entry 0 back");
    }
    return text;
}

void check_after(FatxReader &reader, uint64_t sectors, FatxProgress &progress)
{
    log_line(TONE_TEXT, "  Checking the result:");
    auto after = std::make_unique<FatxCheckResult>();
    FatxCheck(reader, sectors * HDD_SECTOR_SIZE, after.get(), &progress);
    log_check(*after);
}

// ---- Commands

void do_check(const Part part)
{
    const Source src = g_src;
    log_line(TONE_TITLE, partition_name(part.index) + ": check");
    start_job("Checking " + partition_name(part.index), false, [src, part](Job &job) {
        SourceIo io;
        std::string error;
        if (!io.Open(src, false, false, &error))
        {
            log_line(TONE_ERROR, "  " + error);
            return;
        }
        FatxOffsetReader reader(io.Disk(), part.start * HDD_SECTOR_SIZE);
        auto result = std::make_unique<FatxCheckResult>();
        FatxCheck(reader, part.sectors * HDD_SECTOR_SIZE, result.get(), &job);
        log_check(*result);
    });
}

void do_repair(const Part part)
{
    const Source src = g_src;
    log_line(TONE_TITLE, partition_name(part.index) + ": repair");
    auto plan = std::make_shared<FatxRepairResult>();
    auto opened = std::make_shared<bool>(false);
    start_job(
        "Checking " + partition_name(part.index),
        false,
        [src, part, plan, opened](Job &job) {
            SourceIo io;
            std::string error;
            if (!io.Open(src, false, false, &error))
            {
                log_line(TONE_ERROR, "  " + error);
                return;
            }
            *opened = true;
            FatxOffsetDevice device(io.Disk(), part.start * HDD_SECTOR_SIZE);
            FatxRepair(device, part.sectors * HDD_SECTOR_SIZE, plan.get(), false, &job);
            log_check(plan->check);
        },
        [src, part, plan, opened]() {
            if (!*opened)
            {
                return;
            }
            switch (plan->status)
            {
            case FATX_REPAIR_HAS_ERRORS:
                log_line(TONE_ERROR, "  Not repaired: the safe repairs only run without errors.");
                tell("Not repaired", {"The check found errors.", "The safe repairs only run on a partition without errors."});
                return;
            case FATX_REPAIR_NOTHING_TO_DO:
                log_line(TONE_GOOD, "  Nothing to repair.");
                return;
            case FATX_REPAIR_DONE:
                break;
            default:
                return; // log_check() explained
            }
            const std::string what = repairs_text(*plan);
            log_line(TONE_TEXT, "  Would repair: " + what + ".");
            ask("Repair " + partition_name(part.index), with_warning({"It would " + what + ".", "No file's contents change."}, src), "Repair", [src, part]() {
                start_job("Repairing " + partition_name(part.index), true, [src, part](Job &job) {
                    SourceIo io;
                    std::string error;
                    if (!io.Open(src, true, false, &error))
                    {
                        log_line(TONE_ERROR, "  " + error);
                        return;
                    }
                    FatxOffsetDevice device(io.Disk(), part.start * HDD_SECTOR_SIZE);
                    auto repair = std::make_unique<FatxRepairResult>();
                    FatxRepair(device, part.sectors * HDD_SECTOR_SIZE, repair.get(), true, &job);
                    if (repair->status == FATX_REPAIR_DONE)
                    {
                        log_line(TONE_GOOD, "  Repaired: " + repairs_text(*repair) + ".");
                        check_after(device, part.sectors, job);
                    }
                    else if (repair->status == FATX_REPAIR_STOPPED_PARTWAY)
                    {
                        log_line(TONE_ERROR, "  The repair stopped partway (read or write error). Check it again.");
                    }
                    else
                    {
                        log_line(TONE_WARN, "  Not repaired: the partition changed since the check.");
                    }
                });
            });
        });
}

void log_move(const FatxMoveResult &move)
{
    if (move.repair.written)
    {
        log_line(TONE_TEXT, "  Repaired first: " + repairs_text(move.repair) + ".");
    }
    switch (move.status)
    {
    case FATX_MOVE_DONE:
        log_line(TONE_GOOD,
                 format("  Resized to %s: %llu clusters moved, the FAT and every folder renumbered.", mb(move.new_lba_size).c_str(), move.clusters_moved));
        break;
    case FATX_MOVE_SAME_FAT:
        log_line(TONE_ERROR, "  The FAT can't change by whole clusters at that size.");
        break;
    case FATX_MOVE_HAS_ERRORS:
        log_check(move.repair.check);
        log_line(TONE_ERROR, "  A resize needs a check without errors.");
        break;
    case FATX_MOVE_ENTRY_SIZE:
        log_line(TONE_ERROR, "  The FAT's entries would change between 2 and 4 bytes: not supported.");
        break;
    case FATX_MOVE_CLUSTER_SIZE:
        log_line(TONE_ERROR, format("  That size needs bigger clusters than %u KB.", move.cluster_bytes / 1024));
        break;
    case FATX_MOVE_TOO_SMALL:
        log_line(TONE_ERROR, "  No size that small works with this FAT and cluster size.");
        break;
    case FATX_MOVE_NO_ROOM:
        log_line(
            TONE_ERROR,
            format("  Not enough room: %llu clusters to move, %llu free where they can go (one more is needed).", move.clusters_to_move, move.free_clusters));
        break;
    case FATX_MOVE_STOPPED_SAFE:
        log_line(TONE_ERROR, "  Stopped while moving clusters (read or write error). It is still the old size and valid.");
        break;
    case FATX_MOVE_INTERRUPTED:
        log_line(TONE_ERROR,
                 move.table_failed ? "  The new size couldn't be written: use Finish resize."
                                   : "  Stopped while renumbering (read or write error): use Finish resize.");
        break;
    default:
        log_line(TONE_ERROR, "  Could not be checked (read error, out of memory or damaged header).");
        break;
    }
}

void do_resize(const Part part, uint64_t new_lba)
{
    const Source src = g_src;
    log_line(TONE_TITLE, partition_name(part.index) + ": resize from " + mb(part.sectors) + " to " + mb(new_lba));
    start_job(
        "Resizing " + partition_name(part.index),
        true,
        [src, part, new_lba](Job &job) {
            SourceIo io;
            std::string error;
            if (!io.Open(src, true, false, &error))
            {
                log_line(TONE_ERROR, "  " + error);
                return;
            }
            FatxOffsetDevice device(io.Disk(), part.start * HDD_SECTOR_SIZE);
            std::unique_ptr<FatxResizeTable> table_owner = io.Table(part.index);
            FatxResizeTable &table = *table_owner;

            auto resize = std::make_unique<FatxResizeResult>();
            SubProgress check_progress(job, 0, 20);
            FatxCheckResize(device, part.sectors, new_lba, resize.get(), &check_progress);
            switch (resize->status)
            {
            case FATX_RESIZE_OK:
            case FATX_RESIZE_NO_FILE_SYSTEM:
                if (!table.SetSize(new_lba))
                {
                    log_line(TONE_ERROR, "  Writing the new size failed.");
                    return;
                }
                log_line(TONE_GOOD, "  Resized: only the size changed.");
                check_after(device, new_lba, job);
                return;
            case FATX_RESIZE_OUT_OF_RANGE: {
                auto move = std::make_unique<FatxMoveResult>();
                SubProgress move_progress(job, 20, 100);
                FatxResizeWithMoves(device, part.sectors, new_lba, table, true, move.get(), &move_progress);
                log_move(*move);
                if (move->status == FATX_MOVE_DONE)
                {
                    check_after(device, move->new_lba_size, job);
                }
                return;
            }
            case FATX_RESIZE_CLUSTERS_IN_USE:
                log_line(TONE_ERROR,
                         format("  %llu clusters past the new end are in use%s.",
                                resize->clusters_in_use,
                                resize->check.lost_clusters > 0 ? " (lost ones count too: try Repair first)" : ""));
                return;
            case FATX_RESIZE_FAT_NOT_CLEAN:
                log_line(TONE_ERROR, "  The FAT entries for the new clusters aren't 0: not grown.");
                return;
            case FATX_RESIZE_HAS_ERRORS:
                log_check(resize->check);
                log_line(TONE_ERROR, "  Shrinking needs a check without errors.");
                return;
            default:
                log_line(TONE_ERROR, "  Could not be checked (read error or damaged header).");
                return;
            }
        },
        reload);
}

void do_resume_resize(const Part part)
{
    const Source src = g_src;
    log_line(TONE_TITLE, partition_name(part.index) + ": finish the resize");
    start_job(
        "Finishing the resize of " + partition_name(part.index),
        true,
        [src, part](Job &job) {
            SourceIo io;
            std::string error;
            if (!io.Open(src, true, false, &error))
            {
                log_line(TONE_ERROR, "  " + error);
                return;
            }
            FatxOffsetDevice device(io.Disk(), part.start * HDD_SECTOR_SIZE);
            std::unique_ptr<FatxResizeTable> table_owner = io.Table(part.index);
            FatxResizeTable &table = *table_owner;
            auto move = std::make_unique<FatxMoveResult>();
            if (FatxResumeResize(device, table, move.get(), &job))
            {
                log_line(TONE_GOOD, "  Finished.");
                check_after(device, move->new_lba_size, job);
            }
            else
            {
                log_line(TONE_ERROR, "  Could not be finished (read or write error). Try again.");
            }
        },
        reload);
}

// Step 2 of a cluster size change, also used to finish one
bool copy_reclustered(FILE *image, const Part &part, const fs::path &plan_path, const fs::path &build_path, const std::vector<FatxRange> &ranges,
                      FatxProgress &progress)
{
    FILE *build_file = nullptr;
    if (_wfopen_s(&build_file, build_path.c_str(), L"rb") != 0 || build_file == nullptr)
    {
        log_line(TONE_ERROR, "  Can't open " + utf8(build_path.wstring()) + ": the copy can't be finished.");
        return false;
    }
    FileDevice build(build_file, 0);
    FileDevice partition(image, part.start * HDD_SECTOR_SIZE);
    log_line(TONE_TEXT, "  Copying it onto the partition...");
    const bool copied = FatxCopyReclustered(build, partition, ranges, &progress);
    fclose(build_file);
    if (!copied)
    {
        log_line(TONE_ERROR, "  Stopped partway (read or write error): use Finish to copy again.");
        return false;
    }
    remove_recluster_files(plan_path, build_path);
    log_line(TONE_GOOD, "  Done.");
    check_after(partition, part.sectors, progress);
    return true;
}

void log_recluster(const FatxReclusterResult &result, uint32_t new_cluster_bytes, uint64_t lba_size)
{
    if (result.status == FATX_RECLUSTER_PLANNED || result.status == FATX_RECLUSTER_DONE || result.status == FATX_RECLUSTER_NO_ROOM)
    {
        log_line(TONE_TEXT,
                 format("  %llu files, %llu folders: %llu clusters of %u KB now (%llu in use), %llu of %u KB needed, %llu there.",
                        result.files,
                        result.folders,
                        result.check.layout.cluster_limit - 1,
                        result.old_cluster_bytes / 1024,
                        result.old_clusters_used,
                        result.clusters_needed,
                        new_cluster_bytes / 1024,
                        result.clusters_available));
    }
    switch (result.status)
    {
    case FATX_RECLUSTER_PLANNED:
        log_line(TONE_GOOD, "  It fits.");
        if (new_cluster_bytes > 64 * 1024)
        {
            log_line(TONE_WARN, "  Note: BIOSes with the LBA48 v3 patch only mount clusters up to 64 KB.");
        }
        break;
    case FATX_RECLUSTER_DONE:
        log_line(TONE_GOOD, "  Built and checked.");
        break;
    case FATX_RECLUSTER_BAD_SIZE:
        log_line(TONE_ERROR, "  The cluster size must be a power of two from 16 to 1024 KB.");
        break;
    case FATX_RECLUSTER_TOO_SMALL:
        log_line(TONE_ERROR, format("  XBpartitioner uses %u KB clusters at least for this size.", ClusterSizeKbFor(lba_size)));
        break;
    case FATX_RECLUSTER_NOT_FATX:
        log_line(TONE_ERROR, "  Not FATX.");
        break;
    case FATX_RECLUSTER_RESIZING:
        log_line(TONE_ERROR, "  A resize was interrupted: finish it first.");
        break;
    case FATX_RECLUSTER_HAS_ERRORS:
        log_check(result.check);
        log_line(TONE_ERROR, "  Changing the cluster size needs a check without errors.");
        break;
    case FATX_RECLUSTER_NO_ROOM:
        log_line(TONE_ERROR, "  Not enough room: each file takes whole clusters, and bigger ones waste more.");
        break;
    case FATX_RECLUSTER_WRITE_ERROR:
        log_line(TONE_ERROR, "  Couldn't write the temporary file (disk full?). The image wasn't changed.");
        break;
    case FATX_RECLUSTER_BUILD_FAILED:
        log_line(TONE_ERROR, "  The new file system didn't check clean (a bug). The image wasn't changed.");
        break;
    default:
        log_line(TONE_ERROR, "  Read error. The image wasn't changed.");
        break;
    }
}

// The cluster size it has now defragments: every file is rewritten unfragmented
void do_recluster(const Part part, uint32_t new_cluster_bytes)
{
    const Source src = g_src;
    const bool defrag = new_cluster_bytes == part.usage.cluster_bytes;
    log_line(TONE_TITLE, partition_name(part.index) + (defrag ? ": defragment" : format(": change to %u KB clusters", new_cluster_bytes / 1024)));
    auto planned = std::make_shared<bool>(false);
    start_job(
        "Checking " + partition_name(part.index),
        false,
        [src, part, new_cluster_bytes, planned, defrag](Job &job) {
            SourceIo io;
            std::string error;
            if (!io.Open(src, false, false, &error))
            {
                log_line(TONE_ERROR, "  " + error);
                return;
            }
            FatxOffsetDevice device(io.Disk(), part.start * HDD_SECTOR_SIZE);
            auto result = std::make_unique<FatxReclusterResult>();
            FatxBuildReclustered(device, part.sectors, new_cluster_bytes, nullptr, result.get(), &job);
            if (defrag && result->check.status == FATX_CHECK_DONE)
            {
                log_fragmentation(result->check);
            }
            // No file fragmented: nothing to rebuild
            if (defrag && result->status == FATX_RECLUSTER_PLANNED && result->check.fragmented_files == 0)
            {
                log_line(TONE_GOOD, "  Nothing to defragment.");
                return;
            }
            log_recluster(*result, new_cluster_bytes, part.sectors);
            *planned = result->status == FATX_RECLUSTER_PLANNED;
        },
        [src, part, new_cluster_bytes, planned, defrag]() {
            if (!*planned)
            {
                return;
            }
            const fs::path build_path = recluster_build_path(src.path);
            const std::string first = defrag
                                          ? format("Every file of %s is rewritten unfragmented and kept,", partition_name(part.index).c_str())
                                          : format("%s gets %u KB clusters. Every file is kept,", partition_name(part.index).c_str(), new_cluster_bytes / 1024);
            ask(defrag ? "Defragment" : "Change the cluster size",
                {first,
                 "but every byte of the partition is rewritten.",
                 "The new file system is built in " + utf8(build_path.filename().wstring()) + " next to",
                 "the image first: that needs free space for the files on it."},
                defrag ? "Defragment" : "Change it",
                [src, part, new_cluster_bytes, defrag]() {
                    start_job((defrag ? "Defragmenting " : "Changing the cluster size of ") + partition_name(part.index),
                              true,
                              [src, part, new_cluster_bytes](Job &job) {
                                  const fs::path plan_path = recluster_plan_path(src.path);
                                  const fs::path build_path = recluster_build_path(src.path);
                                  SourceIo io;
                                  std::string error;
                                  if (!io.Open(src, true, false, &error))
                                  {
                                      log_line(TONE_ERROR, "  " + error);
                                      return;
                                  }
                                  FILE *build_file = nullptr;
                                  if (_wfopen_s(&build_file, build_path.c_str(), L"w+b") != 0 || build_file == nullptr)
                                  {
                                      log_line(TONE_ERROR, "  Can't create " + utf8(build_path.wstring()));
                                      return;
                                  }
                                  make_sparse(build_file);
                                  FatxOffsetDevice device(io.Disk(), part.start * HDD_SECTOR_SIZE);
                                  std::vector<FatxRange> ranges;
                                  bool built = false;
                                  {
                                      FileDevice build(build_file, 0);
                                      FatxRangeRecorder recorder(build);
                                      auto result = std::make_unique<FatxReclusterResult>();
                                      SubProgress build_progress(job, 0, 60);
                                      FatxBuildReclustered(device, part.sectors, new_cluster_bytes, &recorder, result.get(), &build_progress);
                                      log_recluster(*result, new_cluster_bytes, part.sectors);
                                      built = result->status == FATX_RECLUSTER_DONE;
                                      if (built)
                                      {
                                          ranges = recorder.Ranges();
                                      }
                                  }
                                  fclose(build_file);
                                  if (!built)
                                  {
                                      remove_recluster_files(plan_path, build_path);
                                      return;
                                  }

                                  ReclusterPlanHeader plan = {};
                                  memcpy(plan.magic, RECLUSTER_MAGIC, sizeof(RECLUSTER_MAGIC));
                                  plan.partition_offset = part.start * HDD_SECTOR_SIZE;
                                  plan.lba_size = part.sectors;
                                  plan.new_cluster_bytes = new_cluster_bytes;
                                  plan.range_count = (uint32_t)ranges.size();
                                  if (!save_recluster_plan(plan_path, plan, ranges))
                                  {
                                      log_line(TONE_ERROR, "  Couldn't write the plan file. The image wasn't changed.");
                                      remove_recluster_files(plan_path, build_path);
                                      return;
                                  }
                                  SubProgress copy_progress(job, 60, 100);
                                  copy_reclustered(io.File(), part, plan_path, build_path, ranges, copy_progress);
                              },
                              reload);
                });
        });
}

void do_resume_recluster()
{
    const Source src = g_src;
    const Part *found = nullptr;
    for (const Part &part : g_src.parts)
    {
        if (part.start * HDD_SECTOR_SIZE == g_src.plan.partition_offset && part.sectors == g_src.plan.lba_size)
        {
            found = &part;
        }
    }
    if (found == nullptr)
    {
        tell("Can't finish it", {"The unfinished cluster size change was for a partition", "that isn't in this image's table anymore."});
        return;
    }
    const Part part = *found;
    log_line(TONE_TITLE, partition_name(part.index) + ": finish the cluster size change");
    start_job(
        "Finishing the cluster size change",
        true,
        [src, part](Job &job) {
            const fs::path plan_path = recluster_plan_path(src.path);
            const fs::path build_path = recluster_build_path(src.path);
            ReclusterPlanHeader plan;
            std::vector<FatxRange> ranges;
            if (!load_recluster_plan(plan_path, &plan, &ranges))
            {
                log_line(TONE_ERROR, "  The plan file can't be read.");
                return;
            }
            SourceIo io;
            std::string error;
            if (!io.Open(src, true, false, &error))
            {
                log_line(TONE_ERROR, "  " + error);
                return;
            }
            copy_reclustered(io.File(), part, plan_path, build_path, ranges, job);
        },
        reload);
}

// TRIM: what's free on every partition, then the command
struct TrimPlan
{
    bool ready = false;
    std::vector<FatxRange> ranges; // Disk offsets
    uint64_t bytes = 0;
    bool identified = false;
    bool supported = false;
    uint32_t max_blocks = 0;
    std::string model;
};

TrimPlan g_trim;

void do_find_trim()
{
    const Source src = g_src;
    auto plan = std::make_shared<TrimPlan>();
    log_line(TONE_TITLE, "TRIM: finding the free space");
    start_job(
        "Finding free space",
        false,
        [src, plan](Job &job) {
            SourceIo io;
            std::string error;
            if (!io.Open(src, false, true, &error))
            {
                log_line(TONE_ERROR, "  " + error);
                return;
            }
            const int count = (int)src.parts.size();
            for (int i = 0; i < count; i++)
            {
                const Part &part = src.parts[i];
                job.Update(i * 100 / count);
                if (!part.fits)
                {
                    log_line(TONE_DIM, "  " + partition_name(part.index) + ": ends past the end of the disk, skipped.");
                    continue;
                }
                FatxOffsetReader reader(io.Disk(), part.start * HDD_SECTOR_SIZE);
                auto check = std::make_unique<FatxCheckResult>();
                std::vector<FatxRange> free_ranges;
                if (!FatxFreeRanges(reader, part.sectors * HDD_SECTOR_SIZE, &free_ranges, check.get()))
                {
                    const char *why = check->status == FATX_CHECK_NOT_FATX   ? "not FATX"
                                      : check->status == FATX_CHECK_RESIZING ? "an interrupted resize (finish it first)"
                                      : check->status == FATX_CHECK_DONE     ? "errors (Check shows them)"
                                                                             : "could not be checked";
                    log_line(TONE_DIM, "  " + partition_name(part.index) + ": " + why + ", skipped.");
                    continue;
                }
                uint64_t free_bytes = 0;
                for (FatxRange range : free_ranges)
                {
                    free_bytes += range.length;
                    range.offset += part.start * HDD_SECTOR_SIZE;
                    plan->ranges.push_back(range);
                }
                plan->bytes += free_bytes;
                log_line(TONE_TEXT,
                         format("  %s: %s free, in %u ranges.", partition_name(part.index).c_str(), human(free_bytes).c_str(), (unsigned)free_ranges.size()));
            }

            uint16_t identify[256];
            plan->identified = io.Drive()->Identify(identify);
            if (plan->identified)
            {
                plan->supported = (identify[169] & 0x0001) != 0;
                plan->max_blocks = identify[105];
                plan->model = identify_string(&identify[27], 20);
                log_line(TONE_TEXT, "  Drive: " + plan->model + (plan->supported ? ", supports TRIM." : ", no TRIM support."));
            }
            else
            {
                log_line(TONE_WARN, "  ATA commands don't get through to this drive (USB adapters often block them): it can't be trimmed from here.");
            }
            log_line(TONE_TEXT, format("  To trim: %s in %u ranges.", human(plan->bytes).c_str(), (unsigned)plan->ranges.size()));
            plan->ready = true;
        },
        [plan]() { g_trim = *plan; });
}

void do_trim()
{
    const Source src = g_src;
    const TrimPlan plan = g_trim;
    log_line(TONE_TITLE, "TRIM");
    start_job("Trimming", true, [src, plan](Job &job) {
        SourceIo io;
        std::string error;
        if (!io.Open(src, false, true, &error))
        {
            log_line(TONE_ERROR, "  " + error);
            return;
        }
        if (io.Drive()->Trim(plan.ranges, plan.max_blocks, &job))
        {
            log_line(TONE_GOOD, "  Trimmed " + human(plan.bytes) + ".");
        }
        else
        {
            log_line(TONE_ERROR, "  The drive refused the TRIM command (or the connection didn't pass it on).");
        }
    });
    g_trim = TrimPlan();
}

// ---- New image

struct NewImage
{
    bool open = false;
    std::wstring path;
    double size_mb = 1024.0;
    int cluster = 0; // Index into new_image_clusters
    std::wstring from;
};

NewImage g_new;
const uint32_t new_image_clusters[] = {0, 16, 32, 64, 128, 256, 512, 1024}; // KB, 0 = XBpartitioner's choice

void do_make(const NewImage &spec)
{
    const fs::path path = spec.path;
    const uint64_t size = ((uint64_t)(spec.size_mb * 1048576.0)) & ~(uint64_t)(FATX_HEADER_BYTES - 1);
    const uint32_t cluster_bytes = new_image_clusters[spec.cluster] * 1024;
    const std::wstring from = spec.from;
    log_line(TONE_TITLE, "New image " + utf8(path.wstring()));
    auto made = std::make_shared<bool>(false);
    start_job(
        "Making " + utf8(path.filename().wstring()),
        true,
        [path, size, cluster_bytes, from, made](Job &) {
            FILE *file = nullptr;
            if (_wfopen_s(&file, path.c_str(), L"w+b") != 0 || file == nullptr)
            {
                log_line(TONE_ERROR, "  Can't create it.");
                return;
            }
            // Sparse if the file system allows; unused clusters are never written
            if (_chsize_s(_fileno(file), (__int64)size) != 0)
            {
                log_line(TONE_ERROR, "  Can't make it that big.");
                fclose(file);
                return;
            }
            FileDevice device(file, 0);
            FatxWriter writer(device, size, cluster_bytes);
            bool ok = writer.Format();
            int skipped = 0;
            if (ok && !from.empty())
            {
                ok = copy_tree(writer, fs::path(from), writer.RootCluster(), &skipped, [](const std::string &line) { log_line(TONE_WARN, "  " + line); });
            }
            ok = writer.Flush() && ok;
            fclose(file);
            if (!ok)
            {
                log_line(TONE_ERROR, "  Failed.");
                return;
            }
            log_line(TONE_GOOD,
                     format("  Made: %s, %u KB clusters, %u bit FAT%s.",
                            human(size).c_str(),
                            writer.ClusterBytes() / 1024,
                            writer.Layout().bytes_per_entry * 8,
                            skipped > 0 ? format(", %d items skipped", skipped).c_str() : ""));
            *made = true;
        },
        [path, made]() {
            if (*made)
            {
                open_image(path);
            }
        });
}

// ---- Drawing

// The disk map's colors, like on the Xbox
ImU32 part_color(int index, bool selected)
{
    const Theme &theme = LookTheme();
    if (selected)
    {
        return LookColorU32(theme.map_selected);
    }
    return LookColorU32(index >= 0 && index < XBOX_STANDARD_PARTITION_COUNT ? theme.map_standard : theme.map_extended);
}

std::string theme_name(int index)
{
    return utf8(LookThemeAt(index).name);
}

// Like Start > Theme on the Xbox; the choice is remembered
void draw_theme_picker()
{
    const float width = 150 * g_scale;
    ImGui::SameLine(std::max(ImGui::GetCursorPosX(), ImGui::GetWindowContentRegionMax().x - width));
    ImGui::SetNextItemWidth(width);
    if (ImGui::BeginCombo("##theme", theme_name(LookThemeIndex()).c_str()))
    {
        for (int i = 0; i < LookThemeCount(); i++)
        {
            if (ImGui::Selectable(theme_name(i).c_str(), i == LookThemeIndex()))
            {
                LookSetTheme(i);
            }
        }
        ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("Theme");
    }
}

void draw_toolbar()
{
    if (ImGui::Button("Open image..."))
    {
        std::wstring path;
        if (pick_file(false, &path))
        {
            open_image(path);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Open drive..."))
    {
        g_drive_window = true;
        if (is_elevated() && !g_drives_scanned)
        {
            scan_drives();
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("New image..."))
    {
        g_new = NewImage();
        g_new.open = true;
    }
    if (g_src.type != Source::NONE)
    {
        ImGui::SameLine();
        if (ImGui::Button("Reload"))
        {
            reload();
        }
        ImGui::SameLine();
        ImGui::TextDisabled("%s", g_src.type == Source::DRIVE ? "Xbox drive" : (g_src.has_table ? "Disk image" : "Partition image"));
        ImGui::SameLine();
        ImGui::TextUnformatted(g_src.name.c_str());
    }
    draw_theme_picker();
}

// Set from the right-click menu: the partition whose resize window is open
int g_resize_window = -1;

void draw_part_menu_items(int i);

// A bar with the partitions along the disk; click to select, right-click
// for the partition's menu
void draw_map()
{
    const float height = 34.0f * g_scale;
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    ImDrawList *draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(origin, ImVec2(origin.x + width, origin.y + height), LookColorU32(LookTheme().map_free), 4.0f);

    const double total = (double)(g_src.bytes / HDD_SECTOR_SIZE);
    for (int i = 0; i < (int)g_src.parts.size(); i++)
    {
        const Part &part = g_src.parts[i];
        if (total <= 0)
        {
            break;
        }
        float x0 = origin.x + (float)(part.start / total * width);
        float x1 = origin.x + (float)((part.start + part.sectors) / total * width);
        x1 = std::max(x1, x0 + 3.0f);
        x1 = std::min(x1, origin.x + width);
        draw->AddRectFilled(ImVec2(x0, origin.y + 2), ImVec2(x1 - 1, origin.y + height - 2), part_color(part.index, i == g_selected), 3.0f);
        const std::string label = part.index < 0 ? std::string("FATX") : std::string(1, partition_letter(part.index));
        const ImVec2 size = ImGui::CalcTextSize(label.c_str());
        if (x1 - x0 > size.x + 6)
        {
            // Dark on the bright selected color, light on the others
            const D3DCOLOR text = i == g_selected ? LookTheme().background : LookTheme().text[COLOR_WHITE];
            draw->AddText(ImVec2((x0 + x1 - size.x) / 2, origin.y + (height - size.y) / 2), LookColorU32(text), label.c_str());
        }
        if (ImGui::IsMouseHoveringRect(ImVec2(x0, origin.y), ImVec2(x1, origin.y + height)))
        {
            ImGui::SetTooltip("%s\n%s", partition_name(part.index).c_str(), human(part.sectors * HDD_SECTOR_SIZE).c_str());
            if (ImGui::IsMouseClicked(0))
            {
                g_selected = i;
            }
            if (ImGui::IsMouseClicked(1))
            {
                g_selected = i;
                ImGui::OpenPopup("map_menu");
            }
        }
    }
    ImGui::Dummy(ImVec2(width, height));
    if (ImGui::BeginPopup("map_menu"))
    {
        draw_part_menu_items(g_selected);
        ImGui::EndPopup();
    }
}

std::string part_state(const Part &part)
{
    if (!part.fits)
    {
        return "past the end";
    }
    if (part.resizing)
    {
        return "interrupted resize";
    }
    if (!part.fatx)
    {
        return "not FATX";
    }
    if (!part.usage_ok)
    {
        return "FATX (unreadable FAT)";
    }
    return format("FATX, %u KB clusters", part.usage.cluster_bytes / 1024);
}

void draw_table()
{
    const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp;
    if (!ImGui::BeginTable("parts", 5, flags))
    {
        return;
    }
    ImGui::TableSetupColumn("Partition", ImGuiTableColumnFlags_WidthStretch, 1.4f);
    ImGui::TableSetupColumn("Start", ImGuiTableColumnFlags_WidthStretch, 1.0f);
    ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthStretch, 1.0f);
    ImGui::TableSetupColumn("File system", ImGuiTableColumnFlags_WidthStretch, 1.6f);
    ImGui::TableSetupColumn("Used", ImGuiTableColumnFlags_WidthStretch, 1.0f);
    ImGui::TableHeadersRow();
    for (int i = 0; i < (int)g_src.parts.size(); i++)
    {
        const Part &part = g_src.parts[i];
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::PushID(i);
        if (ImGui::Selectable(partition_name(part.index).c_str(), i == g_selected, ImGuiSelectableFlags_SpanAllColumns))
        {
            g_selected = i;
        }
        // Right-click: the partition's menu (and it becomes the selected one)
        if (ImGui::BeginPopupContextItem("part_menu"))
        {
            g_selected = i;
            draw_part_menu_items(i);
            ImGui::EndPopup();
        }
        ImGui::PopID();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(human(part.start * HDD_SECTOR_SIZE).c_str());
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(human(part.sectors * HDD_SECTOR_SIZE).c_str());
        ImGui::TableNextColumn();
        const std::string state = part_state(part);
        if (part.resizing || !part.fits)
        {
            ImGui::TextColored(tone_color(TONE_ERROR), "%s", state.c_str());
        }
        else
        {
            ImGui::TextUnformatted(state.c_str());
        }
        ImGui::TableNextColumn();
        if (part.usage_ok)
        {
            ImGui::TextUnformatted(human(part.usage.used_clusters * part.usage.cluster_bytes).c_str());
        }
    }
    ImGui::EndTable();
}

// The selected partition's resize controls
struct ResizeEdit
{
    int part = -1;     // Which partition the value is for
    uint64_t start = 0;
    double mb = 0;
};

ResizeEdit g_resize;
int g_recluster_choice = 0;

// True when Resize was pressed (the confirmation is up)
bool draw_resize(const Part &part)
{
    if (g_resize.part != g_selected || g_resize.start != part.start)
    {
        g_resize.part = g_selected;
        g_resize.start = part.start;
        g_resize.mb = (double)(part.sectors * HDD_SECTOR_SIZE) / 1048576.0;
    }

    ImGui::SetNextItemWidth(160 * g_scale);
    ImGui::InputDouble("New size (MB)", &g_resize.mb, 1.0, 1024.0, "%.2f");
    const double max_mb = (double)(part.max_sectors * HDD_SECTOR_SIZE) / 1048576.0;
    if (part.index >= 0)
    {
        ImGui::SameLine();
        ImGui::TextDisabled("up to %s", mb(part.max_sectors).c_str());
    }
    g_resize.mb = std::max(0.0, std::min(g_resize.mb, max_mb));
    const uint64_t new_lba = ((uint64_t)(g_resize.mb * 2048.0)) & ~(uint64_t)(LBA_ALIGNMENT - 1);

    if (new_lba == part.sectors)
    {
        ImGui::TextDisabled("Type a new size: the status shows whether the data can be kept.");
        return false;
    }
    const FatxResizeKind kind = FatxClassifyResize(part.sectors, part.usage, new_lba);
    if (kind == FATX_RESIZE_KIND_IN_PLACE)
    {
        ImGui::TextColored(tone_color(TONE_GOOD), "Keeps the data: only the size changes.");
    }
    else if (kind == FATX_RESIZE_KIND_MOVES)
    {
        ImGui::TextColored(tone_color(TONE_GOOD), "Keeps the data: the FAT changes size, clusters move and are renumbered.");
    }
    else
    {
        ImGui::TextColored(tone_color(TONE_ERROR), "The data can't be kept at %s.", mb(new_lba).c_str());
        const uint64_t below = FatxKeepingSizeBelow(part.sectors, part.usage, new_lba);
        uint64_t above = FatxKeepingSizeAbove(part.sectors, part.usage, new_lba);
        if (above > part.max_sectors)
        {
            above = 0;
        }
        if (below != 0 || above != 0)
        {
            ImGui::TextUnformatted("Nearest sizes that keep it:");
            for (uint64_t size : {below, above})
            {
                if (size != 0)
                {
                    ImGui::SameLine();
                    if (ImGui::SmallButton(mb(size).c_str()))
                    {
                        g_resize.mb = (double)(size * HDD_SECTOR_SIZE) / 1048576.0;
                    }
                }
            }
        }
        return false;
    }

    if (ImGui::Button("Resize..."))
    {
        const Part copy = part;
        ask("Resize " + partition_name(part.index),
            with_warning({"From " + mb(part.sectors) + " to " + mb(new_lba) + ".",
                          kind == FATX_RESIZE_KIND_MOVES ? "Clusters move and the FAT is renumbered." : "Only the size is written.",
                          "If it stops partway, Finish resize completes it."},
                         g_src),
            "Resize",
            [copy, new_lba]() { do_resize(copy, new_lba); });
        return true;
    }
    return false;
}

// What can be done to a partition, as its right-click menu offers it
bool can_check(const Part &part)
{
    return part.fits && part.fatx && !part.resizing;
}

bool can_resize(const Part &part)
{
    return g_src.Writable() && can_check(part) && part.usage_ok;
}

void draw_part_menu_items(int i)
{
    if (i < 0 || i >= (int)g_src.parts.size())
    {
        return;
    }
    const Part part = g_src.parts[i];
    ImGui::TextDisabled("%s", partition_name(part.index).c_str());
    ImGui::Separator();
    if (ImGui::MenuItem("Check", nullptr, false, can_check(part)))
    {
        do_check(part);
    }
    if (g_src.Writable() && ImGui::MenuItem("Repair...", nullptr, false, can_check(part)))
    {
        do_repair(part);
    }
    if (g_src.Writable() && ImGui::MenuItem("Resize...", nullptr, false, can_resize(part)))
    {
        g_resize_window = i;
    }
    if (g_src.CanRecluster() && ImGui::MenuItem("Defragment...", nullptr, false, can_resize(part) && !g_src.recluster_pending))
    {
        do_recluster(part, part.usage.cluster_bytes);
    }
    if (part.resizing && g_src.Writable() && ImGui::MenuItem("Finish resize"))
    {
        do_resume_resize(part);
    }
}

// The resize controls in a window, from the right-click menu
void draw_resize_window()
{
    if (g_resize_window < 0)
    {
        return;
    }
    if (g_resize_window >= (int)g_src.parts.size() || !can_resize(g_src.parts[g_resize_window]))
    {
        g_resize_window = -1; // Reloaded meanwhile
        return;
    }
    const Part part = g_src.parts[g_resize_window];
    g_selected = g_resize_window;

    const std::string id = "Resize " + partition_name(part.index) + "###resize";
    ImGui::OpenPopup(id.c_str());
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(560 * g_scale, 0));
    bool open = true;
    if (!ImGui::BeginPopupModal(id.c_str(), &open, ImGuiWindowFlags_NoResize))
    {
        g_resize_window = -1;
        return;
    }
    ImGui::TextDisabled("Now %s, %s used, %u KB clusters.",
                        mb(part.sectors).c_str(),
                        human(part.usage.used_clusters * part.usage.cluster_bytes).c_str(),
                        part.usage.cluster_bytes / 1024);
    ImGui::Spacing();
    const bool asked = draw_resize(part);
    ImGui::Spacing();
    if (ImGui::Button("Close"))
    {
        open = false;
    }
    if (asked || !open)
    {
        g_resize_window = -1;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void draw_recluster(const Part &part)
{
    static const uint32_t sizes[] = {16, 32, 64, 128, 256, 512, 1024};
    static const char *labels[] = {"16 KB", "32 KB", "64 KB", "128 KB", "256 KB", "512 KB", "1024 KB"};
    ImGui::SetNextItemWidth(160 * g_scale);
    ImGui::Combo("Cluster size", &g_recluster_choice, labels, IM_ARRAYSIZE(labels));
    ImGui::SameLine();
    const bool same = sizes[g_recluster_choice] * 1024 == part.usage.cluster_bytes;
    ImGui::BeginDisabled(same || g_src.recluster_pending);
    if (ImGui::Button("Change..."))
    {
        do_recluster(part, sizes[g_recluster_choice] * 1024);
    }
    ImGui::EndDisabled();
    if (same)
    {
        ImGui::SameLine();
        ImGui::TextDisabled("(current)");
    }

    // Rebuilt at the size it has: no file fragmented
    ImGui::BeginDisabled(g_src.recluster_pending);
    if (ImGui::Button("Defragment..."))
    {
        do_recluster(part, part.usage.cluster_bytes);
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextDisabled("Check shows how fragmented it is.");
}

void draw_selected()
{
    if (g_selected < 0 || g_selected >= (int)g_src.parts.size())
    {
        return;
    }
    const Part part = g_src.parts[g_selected];

    ImGui::TextColored(tone_color(TONE_TITLE), "%s", partition_name(part.index).c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("%s at %s", human(part.sectors * HDD_SECTOR_SIZE).c_str(), human(part.start * HDD_SECTOR_SIZE).c_str());
    if (part.usage_ok)
    {
        const uint64_t total = part.sectors * HDD_SECTOR_SIZE;
        const uint64_t used = part.usage.used_clusters * part.usage.cluster_bytes;
        ImGui::ProgressBar(total ? (float)((double)used / (double)total) : 0.0f, ImVec2(260 * g_scale, 0), (human(used) + " used").c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("%u KB clusters", part.usage.cluster_bytes / 1024);
    }

    if (!part.fits)
    {
        ImGui::TextColored(tone_color(TONE_ERROR), "It ends past the end of the %s.", g_src.type == Source::DRIVE ? "drive" : "image");
        return;
    }
    if (part.resizing)
    {
        if (part.resize_new != 0)
        {
            ImGui::TextColored(tone_color(TONE_ERROR), "A resize from %s to %s was interrupted.", mb(part.resize_old).c_str(), mb(part.resize_new).c_str());
        }
        else
        {
            ImGui::TextColored(tone_color(TONE_ERROR), "A resize or cluster size change was interrupted.");
        }
        ImGui::TextUnformatted("It can't be used until it's finished.");
        if (ImGui::Button("Finish resize"))
        {
            do_resume_resize(part);
        }
        return;
    }

    ImGui::Spacing();
    ImGui::BeginDisabled(!part.fatx);
    if (ImGui::Button("Check"))
    {
        do_check(part);
    }
    if (g_src.Writable())
    {
        ImGui::SameLine();
        if (ImGui::Button("Repair..."))
        {
            do_repair(part);
        }
    }
    ImGui::EndDisabled();

    if (!g_src.Writable())
    {
        return;
    }
    if (!part.fatx || !part.usage_ok)
    {
        ImGui::TextDisabled("Resizing and cluster size changes need a FATX file system.");
        return;
    }
    ImGui::Spacing();
    ImGui::SeparatorText("Resize, keeping the data");
    draw_resize(part);
    ImGui::Spacing();
    ImGui::SeparatorText("Cluster size and defragmenting, keeping the data");
    if (g_src.CanRecluster())
    {
        draw_recluster(part);
    }
    else
    {
        ImGui::TextDisabled("Need an image of the drive: the new file system is built as a full copy first.");
    }
}

void draw_banners()
{
    if (g_src.recluster_pending)
    {
        ImGui::TextColored(
            tone_color(TONE_ERROR), "A change to %u KB clusters didn't finish: the partition is unusable until it does.", g_src.plan.new_cluster_bytes / 1024);
        ImGui::SameLine();
        if (ImGui::Button("Finish it"))
        {
            do_resume_recluster();
        }
    }
    if (g_src.type == Source::DRIVE)
    {
        if (g_trim.ready)
        {
            const bool can = g_trim.identified && g_trim.supported && !g_trim.ranges.empty();
            ImGui::Text("Free to trim: %s in %u ranges.", human(g_trim.bytes).c_str(), (unsigned)g_trim.ranges.size());
            ImGui::SameLine();
            ImGui::BeginDisabled(!can);
            if (ImGui::Button("TRIM now..."))
            {
                ask("TRIM",
                    {"The drive is told " + human(g_trim.bytes) + " of free clusters is unused.",
                     "Only free clusters are trimmed, never file data or the FAT.",
                     "Make sure nothing writes to the drive meanwhile."},
                    "TRIM",
                    do_trim);
            }
            ImGui::EndDisabled();
        }
        else if (ImGui::Button("Find free space to TRIM"))
        {
            do_find_trim();
        }
    }
}

void draw_log()
{
    std::lock_guard<std::mutex> lock(g_log_mutex);
    for (const LogLine &line : g_log)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, tone_color(line.tone));
        ImGui::TextWrapped("%s", line.text.c_str());
        ImGui::PopStyleColor();
    }
    if (g_log_scroll)
    {
        ImGui::SetScrollHereY(1.0f);
        g_log_scroll = false;
    }
}

void draw_drive_window()
{
    if (!g_drive_window)
    {
        return;
    }
    ImGui::OpenPopup("Open a drive");
    ImGui::SetNextWindowSize(ImVec2(480 * g_scale, 0));
    if (!ImGui::BeginPopupModal("Open a drive", &g_drive_window, ImGuiWindowFlags_NoResize))
    {
        return;
    }
    if (!is_elevated())
    {
        ImGui::TextWrapped("Reading drives needs administrator rights.");
        if (ImGui::Button("Restart as administrator"))
        {
            restart_elevated();
        }
    }
    else
    {
        ImGui::TextWrapped("Drives with an Xbox partition table, connected directly or by USB. Repairs and resizes are "
                           "written straight to the drive, so back it up first. Some USB adapters don't pass TRIM on.");
        ImGui::Spacing();
        if (g_drives.empty())
        {
            ImGui::TextDisabled(g_drives_scanned ? "None found." : "Looking...");
        }
        for (const DriveInfo &drive : g_drives)
        {
            const std::string label = format("PhysicalDrive%d: %s, %d partitions", drive.number, human(drive.bytes).c_str(), drive.partitions);
            if (ImGui::Selectable(label.c_str()))
            {
                const std::string path = format("\\\\.\\PhysicalDrive%d", drive.number);
                g_trim = TrimPlan();
                log_line(TONE_TITLE, "Opened " + path);
                open_source(Source::DRIVE, fs::path(path), format("PhysicalDrive%d", drive.number));
                g_drive_window = false;
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::Spacing();
        if (ImGui::Button("Look again"))
        {
            g_drives.clear();
            g_drives_scanned = false;
            scan_drives();
        }
    }
    ImGui::EndPopup();
}

void draw_new_image()
{
    if (!g_new.open)
    {
        return;
    }
    ImGui::OpenPopup("New image");
    ImGui::SetNextWindowSize(ImVec2(560 * g_scale, 0));
    if (!ImGui::BeginPopupModal("New image", &g_new.open, ImGuiWindowFlags_NoResize))
    {
        return;
    }
    ImGui::TextWrapped("A formatted FATX partition image, empty or filled from a folder.");
    ImGui::Spacing();
    if (ImGui::Button("File..."))
    {
        pick_file(true, &g_new.path);
    }
    ImGui::SameLine();
    ImGui::TextUnformatted(g_new.path.empty() ? "(none)" : utf8(g_new.path).c_str());

    ImGui::SetNextItemWidth(160 * g_scale);
    ImGui::InputDouble("Size (MB)", &g_new.size_mb, 64.0, 1024.0, "%.0f");
    g_new.size_mb = std::max(1.0, g_new.size_mb);
    static const char *cluster_labels[] = {"Automatic", "16 KB", "32 KB", "64 KB", "128 KB", "256 KB", "512 KB", "1024 KB"};
    ImGui::SetNextItemWidth(160 * g_scale);
    ImGui::Combo("Clusters", &g_new.cluster, cluster_labels, IM_ARRAYSIZE(cluster_labels));

    if (ImGui::Button("Fill from folder..."))
    {
        pick_folder(&g_new.from);
    }
    ImGui::SameLine();
    ImGui::TextUnformatted(g_new.from.empty() ? "(empty)" : utf8(g_new.from).c_str());
    if (!g_new.from.empty())
    {
        ImGui::SameLine();
        if (ImGui::SmallButton("Clear"))
        {
            g_new.from.clear();
        }
    }

    ImGui::Spacing();
    ImGui::BeginDisabled(g_new.path.empty());
    if (ImGui::Button("Create"))
    {
        const NewImage spec = g_new;
        g_new.open = false;
        ImGui::CloseCurrentPopup();
        do_make(spec);
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel"))
    {
        g_new.open = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void draw_confirm()
{
    if (!g_confirm.open)
    {
        return;
    }
    const std::string id = g_confirm.title + "###confirm";
    ImGui::OpenPopup(id.c_str());
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal(id.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        return;
    }
    for (const std::string &line : g_confirm.lines)
    {
        ImGui::TextUnformatted(line.c_str());
    }
    ImGui::Spacing();
    bool close = false;
    std::function<void()> action;
    if (g_confirm.button.empty())
    {
        close = ImGui::Button("OK", ImVec2(100 * g_scale, 0));
    }
    else
    {
        if (ImGui::Button(g_confirm.button.c_str(), ImVec2(120 * g_scale, 0)))
        {
            close = true;
            action = g_confirm.action;
        }
        ImGui::SameLine();
        close = ImGui::Button("Cancel", ImVec2(100 * g_scale, 0)) || close;
    }
    if (close)
    {
        g_confirm.open = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
    if (action)
    {
        action(); // May open the next dialog
    }
}

void draw_job()
{
    if (!g_job)
    {
        return;
    }
    if (g_job->finished)
    {
        g_job->thread.join();
        std::unique_ptr<Job> done = std::move(g_job);
        g_close_refused = false;
        if (done->then)
        {
            done->then(); // May start the next job
        }
        return;
    }

    ImGui::OpenPopup("###job");
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(420 * g_scale, 0));
    const std::string id = g_job->title + "###job";
    if (ImGui::BeginPopupModal(id.c_str(), nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove))
    {
        const int percent = g_job->percent;
        if (percent < 0)
        {
            ImGui::ProgressBar(-1.0f * (float)ImGui::GetTime(), ImVec2(-1, 0), "Working...");
        }
        else
        {
            ImGui::ProgressBar(percent / 100.0f, ImVec2(-1, 0));
        }
        if (g_job->writes)
        {
            ImGui::TextColored(tone_color(TONE_WARN),
                               g_close_refused ? "Writing: the window closes once this is done." : "Writing: don't switch off or unplug anything.");
        }
        ImGui::EndPopup();
    }
}

void setup_style()
{
    ImGuiStyle &style = ImGui::GetStyle();
    ImGui::StyleColorsDark();
    style.WindowRounding = 6.0f;
    style.FrameRounding = 4.0f;
    style.PopupRounding = 4.0f;
    style.GrabRounding = 4.0f;
    style.WindowPadding = ImVec2(14, 12);
    style.FramePadding = ImVec2(8, 5);
    style.ItemSpacing = ImVec2(8, 7);
    style.ScaleAllSizes(g_scale);

    ImGuiIO &io = ImGui::GetIO();
    wchar_t fonts[MAX_PATH];
    GetWindowsDirectoryW(fonts, MAX_PATH);
    const std::string font = utf8(fonts) + "\\Fonts\\segoeui.ttf";
    if (GetFileAttributesA(font.c_str()) != INVALID_FILE_ATTRIBUTES)
    {
        io.Fonts->AddFontFromFileTTF(font.c_str(), 17.0f * g_scale);
    }
    else
    {
        ImFontConfig config;
        config.SizePixels = 13.0f * g_scale;
        io.Fonts->AddFontDefault(&config);
    }
}

} // namespace

void GuiInit(HWND hwnd, float dpi_scale, ID3D11Device *device, ID3D11DeviceContext *context)
{
    g_hwnd = hwnd;
    g_scale = dpi_scale > 0 ? dpi_scale : 1.0f;
    setup_style();
    LookInit(hwnd, device, context); // The theme's colors, over the sizes
    log_line(TONE_DIM, "Open a disk image, a partition image or an Xbox drive. Images are changed in place: work on a copy.");

    // An image on the command line opens at once
    int argc = 0;
    LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv != nullptr)
    {
        if (argc >= 2)
        {
            open_image(argv[1]);
        }
        LocalFree(argv);
    }
}

void GuiFrame()
{
    LookDrawBackground();

    const ImGuiViewport *viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::Begin(
        "main", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);

    draw_toolbar();
    ImGui::Separator();

    const float log_height = ImGui::GetContentRegionAvail().y * 0.32f;
    // See-through, so the background shows; the table and the log get panels
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
    ImGui::BeginChild("top", ImVec2(0, -log_height - ImGui::GetStyle().ItemSpacing.y));
    ImGui::PopStyleColor();
    if (g_src.type == Source::NONE)
    {
        ImGui::Spacing();
        ImGui::TextDisabled("Nothing open. Drop an image on the window, or use the buttons above.");
    }
    else
    {
        draw_map();
        draw_table();
        ImGui::Spacing();
        draw_banners();
        ImGui::Separator();
        draw_selected();
    }
    ImGui::EndChild();

    ImGui::BeginChild("log", ImVec2(0, 0), ImGuiChildFlags_Borders);
    draw_log();
    ImGui::EndChild();

    // One modal at a time: ImGui would swap them every frame
    if (!g_job)
    {
        draw_drive_window();
        draw_new_image();
        draw_resize_window();
        draw_confirm();
    }
    draw_job();

    ImGui::End();
}

void GuiShutdown()
{
    LookShutdown();
    if (g_job && g_job->thread.joinable())
    {
        g_job->thread.join();
    }
    g_job.reset();
}

bool GuiCanClose()
{
    if (g_job && g_job->writes)
    {
        g_close_refused = true;
        return false;
    }
    return true;
}

void GuiDropFile(const wchar_t *path)
{
    if (g_job)
    {
        return;
    }
    open_image(path);
}
