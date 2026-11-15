// A new partition image

#define NOMINMAX
#include "commands.hpp"

#include "partition_work.hpp"
#include "text.hpp"

#include <io.h>

namespace fatxgui
{

namespace
{

const uint32_t new_image_clusters[] = {0, 16, 32, 64, 128, 256, 512, 1024}; // KB, 0 = XBpartitioner's choice

// A new partition image, formatted and filled from a folder; then it's opened
class MakeImageWork : public JobWork
{
  public:
    MakeImageWork(Commands &commands, const NewImage &spec)
        : m_commands(commands), m_log(commands.Log()), m_path(spec.path),
          m_size(((uint64_t)(spec.size_mb * 1048576.0)) & ~(uint64_t)(FATX_HEADER_BYTES - 1)), m_cluster_bytes(new_image_clusters[spec.cluster] * 1024),
          m_from(spec.from), m_made(false)
    {
    }

    const fs::path &Path() const
    {
        return m_path;
    }

    void Run(FatxProgress &) override
    {
        FILE *file = nullptr;
        if (_wfopen_s(&file, m_path.c_str(), L"w+b") != 0 || file == nullptr)
        {
            m_log.Add(TONE_ERROR, "  Can't create it.");
            return;
        }
        // Sparse if the file system allows; unused clusters are never written
        if (_chsize_s(_fileno(file), (__int64)m_size) != 0)
        {
            m_log.Add(TONE_ERROR, "  Can't make it that big.");
            fclose(file);
            return;
        }
        FileDevice device(file, 0);
        FatxWriter writer(device, m_size, m_cluster_bytes);
        int skipped = 0;
        const bool ok = Fill(writer, &skipped);
        fclose(file);
        if (!ok)
        {
            m_log.Add(TONE_ERROR, "  Failed.");
            return;
        }
        m_log.Add(TONE_GOOD,
                  format("  Made: %s, %u KB clusters, %u bit FAT%s.",
                         size_text(m_size).c_str(),
                         writer.ClusterBytes() / 1024,
                         writer.Layout().bytes_per_entry * 8,
                         skipped > 0 ? format(", %d items skipped", skipped).c_str() : ""));
        m_made = true;
    }

    void OnFinished() override
    {
        if (m_made)
        {
            m_commands.OpenImage(m_path);
        }
    }

  private:
    // Formats it and copies the folder in; false if something failed
    bool Fill(FatxWriter &writer, int *skipped) const
    {
        bool ok = writer.Format();
        if (ok && !m_from.empty())
        {
            ok = copy_tree(writer, fs::path(m_from), writer.RootCluster(), skipped, SkippedItemLog(m_log));
        }
        return writer.Flush() && ok;
    }

    Commands &m_commands;
    ActivityLog &m_log;
    const fs::path m_path;
    const uint64_t m_size;
    const uint32_t m_cluster_bytes;
    const std::wstring m_from;
    bool m_made;
};

} // namespace

void Commands::Make(const NewImage &spec)
{
    auto work = std::make_unique<MakeImageWork>(*this, spec);
    m_log.Add(TONE_TITLE, "New image " + utf8(work->Path().wstring()));
    const std::string title = "Making " + utf8(work->Path().filename().wstring()); // Before work is moved
    m_jobs.Start(title, true, std::move(work));
}

} // namespace fatxgui
