#ifndef FATXGUI_DIALOGS_HPP_
#define FATXGUI_DIALOGS_HPP_

// Confirmations and messages, one at a time

#include <memory>
#include <string>
#include <vector>

#include "workspace.hpp"

namespace fatxgui
{

// What a confirmation's button does
class DialogAction
{
  public:
    virtual ~DialogAction() = default;
    virtual void Run() = 0;
};

struct DialogText
{
    std::string title;
    std::vector<std::string> lines;
    std::string button;        // Empty: a message with OK only
    std::string second_button; // Choose() only
};

// One confirmation or message at a time
class Dialogs
{
  public:
    void Ask(DialogText text, std::unique_ptr<DialogAction> action);
    // Two answers and Cancel
    void Choose(DialogText text, std::unique_ptr<DialogAction> first, std::unique_ptr<DialogAction> second);

    void Tell(const std::string &title, std::vector<std::string> lines);

    bool IsOpen() const
    {
        return m_open;
    }

    const DialogText &Text() const
    {
        return m_text;
    }

    // 1 or 2 for a button, 0 for Cancel or OK; the action may open the next dialog
    void Answer(int button);

    void Draw(float scale);

  private:
    bool m_open = false;
    DialogText m_text;
    std::unique_ptr<DialogAction> m_action;        // Run when the button is chosen
    std::unique_ptr<DialogAction> m_second_action; // Run for the second button
};

// lines, then a warning about what writing to src means
std::vector<std::string> with_warning(std::vector<std::string> lines, const Source &src);

} // namespace fatxgui

#endif // FATXGUI_DIALOGS_HPP_
