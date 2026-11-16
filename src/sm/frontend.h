#pragma once
#include "hig/higmenu.h"
#include <string>

namespace sm {
// Native PC navigation around the recovered title data. The title's order,
// action IDs and navigation are retail; PC subpages are explicitly owned UI.
enum class Screen { Title, NewGame, Loading, Level, Pause, Options, Display, Controls, Notice };
const char *screen_name(Screen);
class Frontend {
  public:
    explicit Frontend(hig::MenuData title);
    void pulse(uint32_t buttons);
    void choose(unsigned row); // mouse selects, then follows the same activation path
    void loaded(bool success, std::string error = {});
    void inspect_level();
    Screen screen() const { return screen_; }
    const hig::MenuData &title() const { return title_; }
    unsigned selection() const { return screen_ == Screen::Title ? title_.selection : selected_; }
    const std::string &message() const { return message_; }
    bool loading() const { return screen_ == Screen::Loading; }
    bool level_visible() const;
    bool level_active() const { return screen_ == Screen::Level; }

  private:
    void go(Screen screen);
    hig::MenuData title_;
    Screen screen_ = Screen::Title, options_parent_ = Screen::Title;
    unsigned selected_ = 0;
    bool has_level_ = false;
    std::string message_;
};

// Capture input at presentation rate, consume once per simulation tick. A
// short press/release between ticks must survive, and catch-up ticks must not
// replay an edge. Native PC sampling policy, not a claim about PS2 polling.
class InputLatch {
  public:
    void sample(const hig::PadSample &);
    hig::PadState consume();
    void clear();

  private:
    hig::PadState observed_, consumed_;
    uint32_t pressed_ = 0, released_ = 0, directions_ = 0;
};
class MenuRepeat {
  public:
    uint32_t tick(const hig::PadState &);
    void clear() { held_ = age_ = 0; }

  private:
    uint32_t held_ = 0, age_ = 0;
};
} // namespace sm
