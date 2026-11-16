// The selected partition: what it is, and what can be done to it

#define NOMINMAX
#include "panels.hpp"

#include <algorithm>

#include "text.hpp"

namespace fatxgui
{

void Panels::Selected()
{
    if (m_workspace.selected < 0 || m_workspace.selected >= (int)Src().parts.size())
    {
        return;
    }
    const Part part = Src().parts[m_workspace.selected];
    PartitionSummary(part);
    if (!part.fits)
    {
        ImGui::TextColored(tone_color(Colors(), TONE_ERROR), "It ends past the end of the %s.", Src().type == Source::DRIVE ? "drive" : "image");
        return;
    }
    if (InterruptedResize(part))
    {
        return;
    }
    Actions(part);
}

void Panels::PartitionSummary(const Part &part)
{
    ImGui::TextColored(tone_color(Colors(), TONE_TITLE), "%s", partition_name(part.index).c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("%s at %s", size_text(part.sectors * HDD_SECTOR_SIZE).c_str(), size_text(part.start * HDD_SECTOR_SIZE).c_str());
    if (part.usage_ok)
    {
        const uint64_t total = part.sectors * HDD_SECTOR_SIZE;
        const uint64_t used = part.usage.used_clusters * part.usage.cluster_bytes;
        ImGui::ProgressBar(total ? (float)((double)used / (double)total) : 0.0f, ImVec2(260 * m_scale, 0), (size_text(used) + " used").c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("%u KB clusters", part.usage.cluster_bytes / 1024);
    }
}

bool Panels::InterruptedResize(const Part &part)
{
    if (!part.resizing)
    {
        return false;
    }
    if (part.resize_new != 0)
    {
        ImGui::TextColored(tone_color(Colors(), TONE_ERROR), "A resize from %s to %s was interrupted.", megabytes_text(part.resize_old).c_str(), megabytes_text(part.resize_new).c_str());
    }
    else
    {
        ImGui::TextColored(tone_color(Colors(), TONE_ERROR), "A resize or cluster size change was interrupted.");
    }
    ImGui::TextUnformatted("It can't be used until it's finished.");
    if (!Src().CanResize())
    {
        ImGui::TextDisabled("Finish it with the tool that started it: this drive has no partition table.");
    }
    else if (ImGui::Button("Finish resize"))
    {
        m_commands.ResumeResize(part);
    }
    return true;
}

// Check and repair, then resizing and cluster sizes
void Panels::Actions(const Part &part)
{
    ImGui::Spacing();
    ImGui::BeginDisabled(!part.fatx);
    if (ImGui::Button("Check"))
    {
        m_commands.Check(part);
    }
    ImGui::SameLine();
    if (ImGui::Button("Browse files"))
    {
        m_browser.Open(Src(), part);
    }
    if (Src().Writable())
    {
        ImGui::SameLine();
        if (ImGui::Button("Repair..."))
        {
            m_commands.Repair(part);
        }
    }
    ImGui::EndDisabled();

    if (!Src().Writable())
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
    if (Src().CanResize())
    {
        ResizeControls(part);
    }
    else
    {
        ImGui::TextDisabled(Src().stock_layout ? "The stock partitions have fixed places, and there's no partition table to hold a new size."
                                               : "The FATX volume fills the drive, and there's no partition table to hold a new size.");
    }
    ImGui::Spacing();
    ImGui::SeparatorText("Cluster size and defragmenting, keeping the data");
    if (Src().CanRecluster())
    {
        ReclusterControls(part);
    }
    else
    {
        ImGui::TextDisabled("Need an image of the drive: the new file system is built as a full copy first.");
    }
}

bool Panels::ResizeControls(const Part &part)
{
    if (m_resize.part != m_workspace.selected || m_resize.start != part.start)
    {
        m_resize.part = m_workspace.selected;
        m_resize.start = part.start;
        m_resize.mb = (double)(part.sectors * HDD_SECTOR_SIZE) / 1048576.0;
    }

    ImGui::SetNextItemWidth(160 * m_scale);
    ImGui::InputDouble("New size (MB)", &m_resize.mb, 1.0, 1024.0, "%.2f");
    const double max_mb = (double)(part.max_sectors * HDD_SECTOR_SIZE) / 1048576.0;
    if (part.index >= 0)
    {
        ImGui::SameLine();
        ImGui::TextDisabled("up to %s", megabytes_text(part.max_sectors).c_str());
    }
    m_resize.mb = std::max(0.0, std::min(m_resize.mb, max_mb));
    const uint64_t new_lba = ((uint64_t)(m_resize.mb * 2048.0)) & ~(uint64_t)(LBA_ALIGNMENT - 1);

    if (new_lba == part.sectors)
    {
        ImGui::TextDisabled("Type a new size: the status shows whether the data can be kept.");
        return false;
    }
    const FatxResizeKind kind = FatxClassifyResize(part.sectors, part.usage, new_lba);
    if (kind != FATX_RESIZE_KIND_IN_PLACE && kind != FATX_RESIZE_KIND_MOVES)
    {
        ImGui::TextColored(tone_color(Colors(), TONE_ERROR), "The data can't be kept at %s.", megabytes_text(new_lba).c_str());
        KeepingSizes(part, new_lba);
        return false;
    }
    ImGui::TextColored(tone_color(Colors(), TONE_GOOD),
                       kind == FATX_RESIZE_KIND_IN_PLACE ? "Keeps the data: only the size changes."
                                                         : "Keeps the data: the FAT changes size, clusters move and are renumbered.");
    if (!ImGui::Button("Resize..."))
    {
        return false;
    }
    m_commands.AskResize(part, new_lba, kind);
    return true;
}

// The nearest sizes that keep the data, as buttons that pick them
void Panels::KeepingSizes(const Part &part, uint64_t new_lba)
{
    const uint64_t below = FatxKeepingSizeBelow(part.sectors, part.usage, new_lba);
    uint64_t above = FatxKeepingSizeAbove(part.sectors, part.usage, new_lba);
    if (above > part.max_sectors)
    {
        above = 0;
    }
    if (below == 0 && above == 0)
    {
        return;
    }
    ImGui::TextUnformatted("Nearest sizes that keep it:");
    for (uint64_t size : {below, above})
    {
        if (size == 0)
        {
            continue;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton(megabytes_text(size).c_str()))
        {
            m_resize.mb = (double)(size * HDD_SECTOR_SIZE) / 1048576.0;
        }
    }
}

void Panels::ReclusterControls(const Part &part)
{
    static const uint32_t sizes[] = {16, 32, 64, 128, 256, 512, 1024};
    static const char *labels[] = {"16 KB", "32 KB", "64 KB", "128 KB", "256 KB", "512 KB", "1024 KB"};
    ImGui::SetNextItemWidth(160 * m_scale);
    ImGui::Combo("Cluster size", &m_recluster_choice, labels, IM_ARRAYSIZE(labels));
    ImGui::SameLine();
    const bool same = sizes[m_recluster_choice] * 1024 == part.usage.cluster_bytes;
    ImGui::BeginDisabled(same || Src().recluster_pending);
    if (ImGui::Button("Change..."))
    {
        m_commands.Recluster(part, sizes[m_recluster_choice] * 1024);
    }
    ImGui::EndDisabled();
    if (same)
    {
        ImGui::SameLine();
        ImGui::TextDisabled("(current)");
    }

    // Rebuilt at the size it has: no file fragmented
    ImGui::BeginDisabled(Src().recluster_pending);
    if (ImGui::Button("Defragment..."))
    {
        m_commands.Recluster(part, part.usage.cluster_bytes);
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextDisabled("Check shows how fragmented it is.");
}

} // namespace fatxgui
