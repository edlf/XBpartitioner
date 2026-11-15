#define NOMINMAX
#include "activity_log.hpp"

#include <algorithm>

#include "look.hpp"
#include "text.hpp"

namespace fatxgui
{

namespace
{

// "a, b, c"
void append_item(std::string &text, const std::string &item)
{
    text += (text.empty() ? "" : ", ") + item;
}

} // namespace

// The Xbox app's text roles, in the current theme
ImVec4 tone_color(const Theme &theme, Tone tone)
{
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

std::string repairs_text(const FatxRepairResult &repair)
{
    std::string text;
    if (repair.clusters_freed > 0)
    {
        append_item(text, format("free %llu clusters (%s)", repair.clusters_freed, size_text(repair.clusters_freed * repair.check.cluster_bytes).c_str()));
    }
    if (repair.files_trimmed > 0)
    {
        append_item(text, format("end the chains of %u files", repair.files_trimmed));
    }
    if (repair.empty_files_cleared > 0)
    {
        append_item(text, format("clear %u empty files", repair.empty_files_cleared));
    }
    if (repair.media_fixed)
    {
        append_item(text, "set FAT entry 0 back");
    }
    return text;
}

void ActivityLog::Add(Tone tone, const std::string &text)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_lines.push_back(LogLine{tone, text});
    m_scroll = true;
}

std::vector<LogLine> ActivityLog::Lines() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_lines;
}

void ActivityLog::Draw(const Theme &theme)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_selected.resize(m_lines.size(), false);
    const ImU32 highlight = ImGui::GetColorU32(ImGuiCol_Header);
    for (size_t i = 0; i < m_lines.size(); i++)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, tone_color(theme, m_lines[i].tone));
        ImGui::TextWrapped("%s", m_lines[i].text.c_str());
        ImGui::PopStyleColor();
        if (m_selected[i])
        {
            ImGui::GetWindowDrawList()->AddRectFilled(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), highlight);
        }
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
        {
            Click(i);
        }
    }
    CopyCommands();
    if (m_scroll)
    {
        ImGui::SetScrollHereY(1.0f);
        m_scroll = false;
    }
}

// Click picks a line, Ctrl+click adds or removes one, Shift+click a range
void ActivityLog::Click(size_t line)
{
    const ImGuiIO &io = ImGui::GetIO();
    if (io.KeyShift && m_anchor < m_lines.size())
    {
        const size_t from = (m_anchor < line) ? m_anchor : line;
        const size_t to = (m_anchor < line) ? line : m_anchor;
        m_selected.assign(m_lines.size(), false);
        for (size_t i = from; i <= to; i++)
        {
            m_selected[i] = true;
        }
        return;
    }
    if (io.KeyCtrl)
    {
        m_selected[line] = !m_selected[line];
    }
    else
    {
        m_selected.assign(m_lines.size(), false);
        m_selected[line] = true;
    }
    m_anchor = line;
}

// Ctrl+C and Ctrl+A while the log has focus, and its right-click menu
void ActivityLog::CopyCommands()
{
    const bool focused = ImGui::IsWindowFocused();
    if (focused && ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_A))
    {
        m_selected.assign(m_lines.size(), true);
    }
    if (focused && ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_C))
    {
        ImGui::SetClipboardText(Text(true).c_str());
    }
    if (ImGui::BeginPopupContextWindow("log_menu"))
    {
        const bool any = std::find(m_selected.begin(), m_selected.end(), true) != m_selected.end();
        if (ImGui::MenuItem("Copy", "Ctrl+C", false, any))
        {
            ImGui::SetClipboardText(Text(true).c_str());
        }
        if (ImGui::MenuItem("Copy the whole log"))
        {
            ImGui::SetClipboardText(Text(false).c_str());
        }
        if (ImGui::MenuItem("Select all", "Ctrl+A"))
        {
            m_selected.assign(m_lines.size(), true);
        }
        ImGui::EndPopup();
    }
}

std::string ActivityLog::Text(bool selected_only) const
{
    std::string text;
    for (size_t i = 0; i < m_lines.size(); i++)
    {
        if (!selected_only || m_selected[i])
        {
            text += m_lines[i].text + "\r\n";
        }
    }
    return text;
}

void ActivityLog::Fragmentation(const FatxCheckResult &result)
{
    if (result.files == 0)
    {
        return;
    }
    if (result.fragmented_files == 0)
    {
        Add(TONE_TEXT, "  Fragmentation: none, no file is fragmented.");
        return;
    }
    Add(TONE_TEXT,
        format("  Fragmentation: %llu of %llu files fragmented (%llu extra fragments). Most:", result.fragmented_files, result.files, result.extra_pieces));
    for (uint32_t i = 0; i < result.worst_count; i++)
    {
        Add(TONE_DIM, format("    %u fragments: %s", result.worst[i].pieces, result.worst[i].path));
    }
}

void ActivityLog::Check(const FatxCheckResult &result)
{
    if (!CheckSummary(result))
    {
        return;
    }
    for (uint32_t i = 0; i < result.message_count; i++)
    {
        Add(result.messages[i].error ? TONE_ERROR : TONE_WARN, std::string(result.messages[i].error ? "  Error: " : "  Warning: ") + result.messages[i].text);
    }
    const uint32_t total = result.errors + result.warnings;
    if (total > result.message_count)
    {
        Add(TONE_DIM, format("  (%u more not shown)", total - result.message_count));
    }
    if (result.status != FATX_CHECK_DONE)
    {
        return;
    }
    if (result.errors == 0)
    {
        Add(TONE_GOOD, format("  OK: no errors, %u warnings.", result.warnings));
    }
    else
    {
        Add(TONE_ERROR, format("  %u errors, %u warnings.", result.errors, result.warnings));
    }
}

void ActivityLog::Move(const FatxMoveResult &move)
{
    if (move.repair.written)
    {
        Add(TONE_TEXT, "  Repaired first: " + repairs_text(move.repair) + ".");
    }
    switch (move.status)
    {
    case FATX_MOVE_DONE:
        Add(TONE_GOOD, format("  Resized to %s: %llu clusters moved, the FAT and every folder renumbered.", megabytes_text(move.new_lba_size).c_str(), move.clusters_moved));
        break;
    case FATX_MOVE_SAME_FAT:
        Add(TONE_ERROR, "  The FAT can't change by whole clusters at that size.");
        break;
    case FATX_MOVE_HAS_ERRORS:
        Check(move.repair.check);
        Add(TONE_ERROR, "  A resize needs a check without errors.");
        break;
    case FATX_MOVE_ENTRY_SIZE:
        Add(TONE_ERROR, "  The FAT's entries would change between 2 and 4 bytes: not supported.");
        break;
    case FATX_MOVE_CLUSTER_SIZE:
        Add(TONE_ERROR, format("  That size needs bigger clusters than %u KB.", move.cluster_bytes / 1024));
        break;
    case FATX_MOVE_TOO_SMALL:
        Add(TONE_ERROR, "  No size that small works with this FAT and cluster size.");
        break;
    case FATX_MOVE_NO_ROOM:
        Add(TONE_ERROR,
            format("  Not enough room: %llu clusters to move, %llu free where they can go (one more is needed).", move.clusters_to_move, move.free_clusters));
        break;
    case FATX_MOVE_STOPPED_SAFE:
        Add(TONE_ERROR, "  Stopped while moving clusters (read or write error). It is still the old size and valid.");
        break;
    case FATX_MOVE_INTERRUPTED:
        Add(TONE_ERROR,
            move.table_failed ? "  The new size couldn't be written: use Finish resize." : "  Stopped while renumbering (read or write error): use Finish resize.");
        break;
    default:
        Add(TONE_ERROR, "  Could not be checked (read error, out of memory or damaged header).");
        break;
    }
}

void ActivityLog::ResizeRefusal(const FatxResizeResult &resize)
{
    switch (resize.status)
    {
    case FATX_RESIZE_CLUSTERS_IN_USE:
        Add(TONE_ERROR,
            format("  %llu clusters past the new end are in use%s.",
                   resize.clusters_in_use,
                   resize.check.lost_clusters > 0 ? " (lost ones count too: try Repair first)" : ""));
        break;
    case FATX_RESIZE_FAT_NOT_CLEAN:
        Add(TONE_ERROR, "  The FAT entries for the new clusters aren't 0: not grown.");
        break;
    case FATX_RESIZE_HAS_ERRORS:
        Check(resize.check);
        Add(TONE_ERROR, "  Shrinking needs a check without errors.");
        break;
    default:
        Add(TONE_ERROR, "  Could not be checked (read error or damaged header).");
        break;
    }
}

void ActivityLog::Recluster(const FatxReclusterResult &result, uint32_t new_cluster_bytes, uint64_t lba_size)
{
    if (result.status == FATX_RECLUSTER_PLANNED || result.status == FATX_RECLUSTER_DONE || result.status == FATX_RECLUSTER_NO_ROOM)
    {
        Add(TONE_TEXT,
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
        Add(TONE_GOOD, "  It fits.");
        if (new_cluster_bytes > 64 * 1024)
        {
            Add(TONE_WARN, "  Note: BIOSes with the LBA48 v3 patch only mount clusters up to 64 KB.");
        }
        break;
    case FATX_RECLUSTER_DONE:
        Add(TONE_GOOD, "  Built and checked.");
        break;
    case FATX_RECLUSTER_BAD_SIZE:
        Add(TONE_ERROR, "  The cluster size must be a power of two from 16 to 1024 KB.");
        break;
    case FATX_RECLUSTER_TOO_SMALL:
        Add(TONE_ERROR, format("  XBpartitioner uses %u KB clusters at least for this size.", ClusterSizeKbFor(lba_size)));
        break;
    case FATX_RECLUSTER_NOT_FATX:
        Add(TONE_ERROR, "  Not FATX.");
        break;
    case FATX_RECLUSTER_RESIZING:
        Add(TONE_ERROR, "  A resize was interrupted: finish it first.");
        break;
    case FATX_RECLUSTER_HAS_ERRORS:
        Check(result.check);
        Add(TONE_ERROR, "  Changing the cluster size needs a check without errors.");
        break;
    case FATX_RECLUSTER_NO_ROOM:
        Add(TONE_ERROR, "  Not enough room: each file takes whole clusters, and bigger ones waste more.");
        break;
    case FATX_RECLUSTER_WRITE_ERROR:
        Add(TONE_ERROR, "  Couldn't write the temporary file (disk full?). The image wasn't changed.");
        break;
    case FATX_RECLUSTER_BUILD_FAILED:
        Add(TONE_ERROR, "  The new file system didn't check clean (a bug). The image wasn't changed.");
        break;
    default:
        Add(TONE_ERROR, "  Read error. The image wasn't changed.");
        break;
    }
}

bool ActivityLog::CheckSummary(const FatxCheckResult &result)
{
    switch (result.status)
    {
    case FATX_CHECK_RESIZING:
        Add(TONE_ERROR, "  A resize or cluster size change was interrupted: finish it first.");
        return false;
    case FATX_CHECK_NOT_FATX:
        Add(TONE_DIM, "  Not FATX (unformatted, or another file system): not checked.");
        return false;
    case FATX_CHECK_READ_ERROR:
        Add(TONE_ERROR, "  Read error (is the image shorter than the partition?).");
        return true;
    case FATX_CHECK_NO_MEMORY:
        Add(TONE_ERROR, "  Out of memory.");
        return true;
    case FATX_CHECK_BAD_HEADER:
        return true;
    default:
        Add(TONE_TEXT,
            format("  FATX, %u KB clusters, %u bit FAT, %llu files, %llu folders",
                   result.cluster_bytes / 1024,
                   result.layout.bytes_per_entry * 8,
                   result.files,
                   result.directories));
        Add(TONE_TEXT,
            format("  Clusters: %llu in use of %llu, %llu lost, %llu bad",
                   result.used_clusters,
                   result.layout.cluster_limit - 1,
                   result.lost_clusters,
                   result.bad_clusters));
        Fragmentation(result);
        return true;
    }
}

} // namespace fatxgui
