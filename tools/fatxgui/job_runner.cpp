#define NOMINMAX
#include "job_runner.hpp"

#include <atomic>
#include <thread>

#include "activity_log.hpp"

namespace fatxgui
{

struct Job : public FatxProgress
{
    std::string title;
    std::atomic<int> percent{-1}; // -1 until the work reports any
    std::atomic<bool> finished{false};
    bool writes = false;          // Closing the window waits for it
    std::unique_ptr<JobWork> work;
    std::thread thread;

    void Update(int value) override
    {
        percent = value;
    }
};

namespace
{

// The worker thread; wake: the window whose loop waits for it
void run_job(Job *job, HWND wake)
{
    job->work->Run(*job);
    job->finished = true;
    PostMessageW(wake, WM_NULL, 0, 0); // Wake the loop
}

} // namespace

JobRunner::JobRunner(HWND wake) : m_wake(wake), m_close_refused(false)
{
}

JobRunner::~JobRunner()
{
    if (m_job && m_job->thread.joinable())
    {
        m_job->thread.join();
    }
}

void JobRunner::Start(const std::string &title, bool writes, std::unique_ptr<JobWork> work)
{
    m_job = std::make_unique<Job>();
    m_job->title = title;
    m_job->writes = writes;
    m_job->work = std::move(work);
    m_job->thread = std::thread(run_job, m_job.get(), m_wake);
}

bool JobRunner::CollectFinished()
{
    if (!m_job || !m_job->finished)
    {
        return false;
    }
    m_job->thread.join();
    std::unique_ptr<Job> done = std::move(m_job);
    m_close_refused = false;
    done->work->OnFinished();
    return true;
}

bool JobRunner::CanClose()
{
    if (m_job && m_job->writes)
    {
        m_close_refused = true;
        return false;
    }
    return true;
}

void JobRunner::Draw(float scale, const Theme &theme)
{
    if (!m_job || CollectFinished())
    {
        return;
    }
    ImGui::OpenPopup("###job");
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(420 * scale, 0));
    const std::string id = m_job->title + "###job";
    if (!ImGui::BeginPopupModal(id.c_str(), nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove))
    {
        return;
    }
    const int percent = m_job->percent;
    if (percent < 0)
    {
        ImGui::ProgressBar(-1.0f * (float)ImGui::GetTime(), ImVec2(-1, 0), "Working...");
    }
    else
    {
        ImGui::ProgressBar(percent / 100.0f, ImVec2(-1, 0));
    }
    if (m_job->writes)
    {
        ImGui::TextColored(tone_color(theme, TONE_WARN),
                           m_close_refused ? "Writing: the window closes once this is done." : "Writing: don't switch off or unplug anything.");
    }
    ImGui::EndPopup();
}

} // namespace fatxgui
