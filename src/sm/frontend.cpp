#include "frontend.h"
#include <utility>
namespace sm {
const char *screen_name(Screen s) {
    switch (s) {
    case Screen::Title:
        return "title";
    case Screen::NewGame:
        return "new_game";
    case Screen::Loading:
        return "loading";
    case Screen::Level:
        return "level_bootstrap";
    case Screen::Pause:
        return "pause";
    case Screen::Options:
        return "options";
    case Screen::Display:
        return "display";
    case Screen::Controls:
        return "controls";
    case Screen::Notice:
        return "notice";
    }
    return "unknown";
}
Frontend::Frontend(hig::MenuData title) : title_(std::move(title)) {
    // Progress persistence is not implemented. Never present a fixture or PC
    // display preferences as a loadable game save in the real frontend.
    for (auto &item : title_.items)
        if (item.action == 99)
            item.enabled = false;
    hig::navigate_menu(title_, 0);
}
void Frontend::go(Screen s) {
    screen_ = s;
    selected_ = 0;
}
bool Frontend::level_visible() const {
    return has_level_ && (screen_ == Screen::Level || screen_ == Screen::Pause ||
                          ((screen_ == Screen::Options || screen_ == Screen::Display ||
                            screen_ == Screen::Controls) &&
                           options_parent_ == Screen::Pause));
}
void Frontend::inspect_level() {
    has_level_ = true;
    go(Screen::Level);
}
void Frontend::loaded(bool success, std::string error) {
    if (!loading())
        return;
    if (success) {
        has_level_ = true;
        go(Screen::Level);
    } else {
        message_ = std::move(error);
        go(Screen::Notice);
    }
}
void Frontend::choose(unsigned row) {
    if (screen_ == Screen::Title) {
        if (row >= title_.items.size() || !title_.items[row].enabled)
            return;
        title_.selection = row;
    } else
        selected_ = row;
    pulse(hig::Cross);
}
void Frontend::pulse(uint32_t p) {
    using namespace hig;
    if (screen_ == Screen::Loading)
        return;
    if (screen_ == Screen::Level) {
        if (p & (Start | Circle))
            go(Screen::Pause);
        return;
    }
    if (p & Circle) {
        if (screen_ == Screen::Display || screen_ == Screen::Controls)
            go(Screen::Options);
        else if (screen_ == Screen::Options)
            go(options_parent_);
        else if (screen_ == Screen::Pause)
            go(Screen::Level);
        else if (screen_ != Screen::Title)
            go(Screen::Title);
        return;
    }
    if (screen_ == Screen::Title) {
        auto action = advance_menu(title_, p);
        if (action == 98)
            go(Screen::NewGame);
        else if (action == 79) {
            options_parent_ = Screen::Title;
            go(Screen::Options);
        } else if (action == 94) {
            message_ = "Multiplayer gameplay is not implemented yet.";
            go(Screen::Notice);
        }
        return;
    }
    if (screen_ == Screen::Pause && (p & Start)) {
        go(Screen::Level);
        return;
    }
    unsigned count = screen_ == Screen::Pause || screen_ == Screen::Options ? 3
                     : screen_ == Screen::NewGame                           ? 2
                                                                            : 1;
    if (p & Cross) {
        if (selected_ >= count)
            return;
        switch (screen_) {
        case Screen::NewGame:
            go(selected_ == 0 ? Screen::Loading : Screen::Title);
            break;
        case Screen::Pause:
            if (selected_ == 0)
                go(Screen::Level);
            else if (selected_ == 1) {
                options_parent_ = Screen::Pause;
                go(Screen::Options);
            } else {
                has_level_ = false;
                go(Screen::Title);
            }
            break;
        case Screen::Options:
            go(selected_ == 0   ? Screen::Display
               : selected_ == 1 ? Screen::Controls
                                : options_parent_);
            break;
        case Screen::Controls:
            go(Screen::Options);
            break;
        case Screen::Notice:
            go(Screen::Title);
            break;
        default:
            break;
        }
    } else if (p & Up)
        selected_ = selected_ ? selected_ - 1 : count - 1;
    else if (p & Down)
        selected_ = (selected_ + 1) % count;
}
void InputLatch::sample(const hig::PadSample &s) {
    observed_ = hig::advance_pad(observed_, s);
    pressed_ |= observed_.pressed;
    released_ |= observed_.released;
    directions_ |= observed_.left_directions_pressed;
}
hig::PadState InputLatch::consume() {
    auto next = hig::advance_pad(consumed_, observed_.raw);
    next.pressed |= pressed_;
    next.released |= released_;
    next.left_directions_pressed |= directions_;
    consumed_ = next;
    pressed_ = released_ = directions_ = 0;
    return next;
}
void InputLatch::clear() {
    observed_ = {};
    consumed_ = {};
    pressed_ = released_ = directions_ = 0;
}
uint32_t MenuRepeat::tick(const hig::PadState &s) {
    uint32_t directions = (s.raw.buttons | s.left_directions) & (hig::Up | hig::Down);
    uint32_t pulse = s.pressed | s.left_directions_pressed;
    if (directions != held_) {
        held_ = directions;
        age_ = 0;
    } else if (directions) {
        // PC menu policy at 30 Hz: 400 ms initial delay, then 133 ms repeat.
        if (++age_ >= 12 && (age_ - 12) % 4 == 0)
            pulse |= directions;
    } else
        age_ = 0;
    return pulse;
}
} // namespace sm
