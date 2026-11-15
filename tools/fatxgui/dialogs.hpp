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
    std::string button; // Empty: a message with OK only
};

// One confirmation or message at a time
class Dialogs
{
  public:
    void Ask(DialogText text, std::unique_ptr<DialogAction> action);

    void Tell(const std::string &title, std::vector<std::string> lines);

    bool IsOpen() const
    {
        return m_open;
    }

    const DialogText &Text() const
    {
        return m_text;
    }

    // The button (accept), or Cancel or OK; the action may open the next dialog
    void Answer(bool accept);

    void Draw(float scale);

  private:
    bool m_open = false;
    DialogText m_text;
    std::unique_ptr<DialogAction> m_action; // Run when the button is chosen
};

// lines, then a warning about what writing to src means
std::vector<std::string> with_warning(std::vector<std::string> lines, const Source &src);

} // namespace fatxgui

#endif // FATXGUI_DIALOGS_HPP_
