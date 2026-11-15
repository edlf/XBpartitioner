#ifndef FATXGUI_ACTIVITY_LOG_HPP_
#define FATXGUI_ACTIVITY_LOG_HPP_

#include <mutex>
#include <string>
#include <vector>

#include "imgui.h"
#include "pc_image.hpp"
#include "theme.h"

namespace fatxgui
{

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

// The Xbox app's text roles, in a theme
ImVec4 tone_color(const Theme &theme, Tone tone);

// "free 3 clusters (96.00 KB), end the chains of 2 files"
std::string repairs_text(const FatxRepairResult &repair);

// What was done and what came of it, shown under the partitions. Lines come
// from the UI thread and from jobs on the worker thread.
class ActivityLog
{
  public:
    void Add(Tone tone, const std::string &text);

    std::vector<LogLine> Lines() const;

    // Scrolls to the end when lines were added
    void Draw(const Theme &theme);

    // ---- Results, like fatxtool prints them

    // How many files are fragmented, and the worst ones
    void Fragmentation(const FatxCheckResult &result);

    void Check(const FatxCheckResult &result);

    void Move(const FatxMoveResult &move);

    // Why a resize that only changes the size can't be done
    void ResizeRefusal(const FatxResizeResult &resize);

    void Recluster(const FatxReclusterResult &result, uint32_t new_cluster_bytes, uint64_t lba_size);

  private:
    // What it is and how full; false if there's nothing more to say
    bool CheckSummary(const FatxCheckResult &result);

    mutable std::mutex m_mutex;
    std::vector<LogLine> m_lines;
    bool m_scroll = false;
};

// copy_tree's note for each item it skips
class SkippedItemLog
{
  public:
    explicit SkippedItemLog(ActivityLog &log) : m_log(log)
    {
    }
    void operator()(const std::string &line) const
    {
        m_log.Add(TONE_WARN, "  " + line);
    }

  private:
    ActivityLog &m_log;
};

} // namespace fatxgui

#endif // FATXGUI_ACTIVITY_LOG_HPP_
