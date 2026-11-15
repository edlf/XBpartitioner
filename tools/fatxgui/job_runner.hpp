#ifndef FATXGUI_JOB_RUNNER_HPP_
#define FATXGUI_JOB_RUNNER_HPP_

// Slow work runs on a worker thread, one job at a time, behind a progress window

#include <windows.h>

#include <memory>
#include <string>

#include "fatx_check.hpp"
#include "theme.h"

namespace fatxgui
{

// Progress that can also say what's being done, shown under the bar
class JobProgress : public FatxProgress
{
  public:
    virtual void Stage(const std::string &stage) = 0;
};

// Says what's being done, if progress is a job's (or part of one)
void show_stage(FatxProgress &progress, const std::string &stage);

// A job's work: Run on the worker thread, reporting its progress, then
// OnFinished on the UI thread (which may start the next job)
class JobWork
{
  public:
    virtual ~JobWork() = default;
    virtual void Run(FatxProgress &progress) = 0;
    virtual void OnFinished()
    {
    }
};

struct Job; // job_runner.cpp

// Runs one job at a time, and shows its progress meanwhile
class JobRunner
{
  public:
    explicit JobRunner(HWND wake);
    ~JobRunner();
    JobRunner(const JobRunner &) = delete;
    JobRunner &operator=(const JobRunner &) = delete;

    void Start(const std::string &title, bool writes, std::unique_ptr<JobWork> work);

    bool Busy() const
    {
        return m_job != nullptr;
    }

    // On the UI thread, once the work is done: its OnFinished, which may
    // start the next job. False if none had finished.
    bool CollectFinished();

    // False while a job writes; the progress window then says so
    bool CanClose();

    // The progress window, while a job runs
    void Draw(float scale, const Theme &theme);

  private:
    const HWND m_wake;
    std::unique_ptr<Job> m_job;
    bool m_close_refused;
};

// Maps 0-100 onto part of an outer progress
class SubProgress : public JobProgress
{
  public:
    SubProgress(FatxProgress &outer, int from, int to) : m_outer(outer), m_from(from), m_to(to)
    {
    }
    void Update(int value) override
    {
        m_outer.Update(m_from + (m_to - m_from) * value / 100);
    }
    void Stage(const std::string &stage) override
    {
        show_stage(m_outer, stage);
    }

  private:
    FatxProgress &m_outer;
    int m_from;
    int m_to;
};

} // namespace fatxgui

#endif // FATXGUI_JOB_RUNNER_HPP_
