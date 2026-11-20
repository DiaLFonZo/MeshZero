// SPDX-License-Identifier: GPL-3.0-only
#include "ui/app.h"

#include <cstdlib>
#include <ctime>

#include "common/util.h"
#include "ui/update.h"

namespace mz {

constexpr uint64_t kExitHoldMs = 2000;

namespace {
const char* const kTabNames[kTabCount] = {"Chats", "Nodes", "Map", "Radio", "Setup"};
}

Screen::Screen(App& a) : app(a), mesh(a.mesh)
{
    root = ui::box(app.content());
    lv_obj_set_size(root, kContentW, kContentH);
    lv_obj_add_flag(root, LV_OBJ_FLAG_HIDDEN);
}

Screen::~Screen()
{
    if (root) lv_obj_delete(root);
}

App::App(Mesh& m) : mesh(m) {}

App::~App()
{
    stack_.clear();
}

void App::start()
{
    lv_obj_t* scr = lv_screen_active();
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    ui::set_bg(scr, ui::c_bg());

    bar_ = ui::box(scr);
    lv_obj_set_size(bar_, kContentW, kBarH);
    ui::set_bg(bar_, ui::c_bar());

    title_ = ui::label(bar_, "", ui::c_text());
    lv_obj_set_pos(title_, 5, 2);
    lv_obj_set_width(title_, 210);
    lv_label_set_long_mode(title_, LV_LABEL_LONG_DOT);
    lv_obj_set_height(title_, kBarH - 2);

    for (int i = 0; i < kTabCount; ++i) {
        tabs_[i] = ui::label(bar_, kTabNames[i], ui::c_dim());
        lv_obj_set_style_pad_hor(tabs_[i], 4, 0);
        lv_obj_set_style_pad_top(tabs_[i], 2, 0);
        lv_obj_set_height(tabs_[i], kBarH);
    }

    status_ = ui::label(bar_, "", ui::c_dim(), ui::font_small());
    lv_obj_align(status_, LV_ALIGN_RIGHT_MID, -4, 0);

    content_ = ui::box(scr);
    lv_obj_set_pos(content_, 0, kBarH);
    lv_obj_set_size(content_, kContentW, kContentH);

    hint_ = ui::label(scr, "", ui::c_dim(), ui::font_small());
    lv_obj_set_pos(hint_, 0, 170 - kHintH);
    lv_obj_set_size(hint_, kContentW, kHintH);
    lv_obj_set_style_pad_left(hint_, 5, 0);
    lv_obj_set_style_pad_top(hint_, 1, 0);
    lv_label_set_long_mode(hint_, LV_LABEL_LONG_CLIP);
    ui::set_bg(hint_, ui::c_bar());

    toast_ = ui::box(scr);
    lv_obj_set_size(toast_, 300, LV_SIZE_CONTENT);
    lv_obj_align(toast_, LV_ALIGN_BOTTOM_MID, 0, -2);
    ui::set_bg(toast_, lv_color_hex(0x24405f));
    lv_obj_set_style_radius(toast_, 4, 0);
    lv_obj_set_style_border_width(toast_, 1, 0);
    lv_obj_set_style_border_color(toast_, ui::c_me(), 0);
    lv_obj_set_style_pad_all(toast_, 4, 0);
    toast_label_ = ui::label(toast_, "", ui::c_text());
    lv_obj_set_width(toast_label_, 290);
    lv_label_set_long_mode(toast_label_, LV_LABEL_LONG_WRAP);
    lv_obj_add_flag(toast_, LV_OBJ_FLAG_HIDDEN);

    notice_ = ui::label(scr, "", ui::c_bg());
    lv_obj_set_size(notice_, kContentW, kBarH);
    lv_obj_set_style_pad_left(notice_, 5, 0);
    lv_obj_set_style_pad_top(notice_, 2, 0);
    lv_label_set_long_mode(notice_, LV_LABEL_LONG_DOT);
    lv_obj_add_flag(notice_, LV_OBJ_FLAG_HIDDEN);

    push(make_tab(*this, kTabChats));
    apply_pending();
}

void App::push(std::unique_ptr<Screen> screen)
{
    auto* raw = screen.release();
    pending_.push_back([this, raw]() { stack_.emplace_back(raw); });
}

void App::pop(int count)
{
    pending_.push_back([this, count]() {
        for (int i = 0; i < count && stack_.size() > 1; ++i) {
            if (shown_ == stack_.back().get()) {
                shown_->on_hide();
                shown_ = nullptr;
            }
            stack_.pop_back();
        }
    });
}

void App::switch_tab(int tab)
{
    tab = (tab % kTabCount + kTabCount) % kTabCount;
    pending_.push_back([this, tab]() {
        if (shown_) {
            shown_->on_hide();
            shown_ = nullptr;
        }
        stack_.clear();
        stack_.push_back(make_tab(*this, tab));
    });
}

void App::open_chat(const ChatKey& key)
{
    push(make_chat(*this, key));
}

void App::apply_pending()
{
    // Screens created while applying may queue more work; loop until settled.
    for (int guard = 0; guard < 8 && !pending_.empty(); ++guard) {
        std::vector<std::function<void()>> work;
        work.swap(pending_);
        for (auto& fn : work) fn();
    }
    if (top() != shown_) show_top();
}

void App::show_top()
{
    if (shown_) {
        shown_->on_hide();
        lv_obj_add_flag(shown_->root, LV_OBJ_FLAG_HIDDEN);
    }
    for (auto& s : stack_) lv_obj_add_flag(s->root, LV_OBJ_FLAG_HIDDEN);
    shown_ = top();
    if (shown_) {
        lv_obj_remove_flag(shown_->root, LV_OBJ_FLAG_HIDDEN);
        shown_->on_show();
        shown_->refresh();
    }
    update_chrome();
}

void App::handle_key(const Key& pressed)
{
    Screen* s = top();
    if (!s) return;
    // Leaving the app takes holding Esc, so a stray press does not close it.
    // The wait itself is in tick(); here the hold begins and ends.
    if (pressed.release) {
        if (pressed.k == K::Esc) esc_held_ms_ = 0;
        return;
    }
    if (esc_held_ms_ != 0) {
        if (pressed.k == K::Esc && pressed.repeat) return;  // still held
        esc_held_ms_ = 0;
    }
    Key key = pressed;
    if (key.k == K::Char && !key.ctrl && !s->wants_text()) {
        switch (key.ch) {
            case 'f':
            case 'F':
                key.k = K::Up;
                break;
            case 'x':
            case 'X':
                key.k = K::Down;
                break;
            case 'z':
            case 'Z':
                key.k = K::Left;
                break;
            case 'c':
            case 'C':
                key.k = K::Right;
                break;
            default:
                break;
        }
        if (key.k != K::Char) key.ch = 0;
    }
    // A held key repeats only where it works as an arrow; in text it types once.
    if (key.held && key.k == K::Char) return;
    // A key press also clears any message on screen; it is never swallowed.
    if (toast_until_ != 0 && (key.k == K::Esc || key.k == K::Enter)) {
        lv_obj_add_flag(toast_, LV_OBJ_FLAG_HIDDEN);
        toast_until_ = 0;
    }
    // Any key acknowledges a notice so the bar underneath is usable again.
    if (notice_until_ != 0) {
        lv_obj_add_flag(notice_, LV_OBJ_FLAG_HIDDEN);
        notice_until_ = 0;
    }
    if (!s->on_key(key)) {
        if (key.k == K::Esc) {
            if (stack_.size() > 1) {
                pop();
            } else if (!key.repeat) {
                // Not on a repeat: holding Esc to go back through several
                // pages must not carry on into leaving the app.
                esc_held_ms_ = now_ms();
                notify("Keep holding Esc to exit");
            }
        } else if (s->tab() >= 0 && !key.ctrl && !key.held) {
            // Holding Left or Right moves one tab, not through all of them.
            if (key.k == K::Tab || key.k == K::Right) switch_tab(s->tab() + 1);
            if (key.k == K::Left) switch_tab(s->tab() - 1);
            if (key.is_char('?')) push(make_help(*this));
        }
    }
    apply_pending();
    if (top()) top()->refresh();
    update_chrome();
}

void App::toast(const std::string& text, bool warn)
{
    ui::set_text(toast_label_, ui::clean(utf8_truncate(text, 160), true));
    lv_obj_set_style_border_color(toast_, warn ? ui::c_warn() : ui::c_me(), 0);
    lv_obj_remove_flag(toast_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(toast_);
    toast_until_ = now_ms() + (warn ? 5000 : 3500);
}

void App::notify(const std::string& text)
{
    ui::set_text(notice_, ui::clean(utf8_truncate(text, 120), true));
    ui::set_bg(notice_, ui::c_accent());
    lv_obj_remove_flag(notice_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(notice_);
    notice_until_ = now_ms() + 4000;
}

void App::update_chrome()
{
    Screen* s = top();
    if (!s) return;
    const int tab = s->tab();
    if (tab >= 0) {
        lv_obj_add_flag(title_, LV_OBJ_FLAG_HIDDEN);
        int x = 1;
        const int unread = mesh.total_unread();
        for (int i = 0; i < kTabCount; ++i) {
            lv_obj_remove_flag(tabs_[i], LV_OBJ_FLAG_HIDDEN);
            std::string text = kTabNames[i];
            if (i == kTabChats && unread > 0) text += " " + std::to_string(unread > 99 ? 99 : unread);
            ui::set_text(tabs_[i], text);
            const bool active = i == tab;
            lv_obj_set_style_text_color(
                tabs_[i], active ? ui::c_bg() : (i == kTabChats && unread > 0 ? ui::c_accent() : ui::c_dim()), 0);
            if (active) {
                ui::set_bg(tabs_[i], ui::c_accent());
            } else {
                lv_obj_set_style_bg_opa(tabs_[i], LV_OPA_TRANSP, 0);
            }
            lv_obj_set_pos(tabs_[i], x, 0);
            lv_obj_update_layout(tabs_[i]);
            x += lv_obj_get_width(tabs_[i]) + 1;
        }
    } else {
        for (auto* t : tabs_) lv_obj_add_flag(t, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(title_, LV_OBJ_FLAG_HIDDEN);
        ui::set_text(title_, ui::clean(s->title(), true));
    }

    // Right side: link state, satellites, battery, clock.
    std::string status;
    lv_color_t color = ui::c_dim();
    switch (mesh.state()) {
        case LinkState::Ready:
            if (mesh.region_unset()) {
                status = "NO REGION";
                color  = ui::c_warn();
            }
            break;
        case LinkState::Syncing:
            status = "sync...";
            color  = ui::c_warn();
            break;
        default:
            status = mesh.offline_text();
            color  = ui::c_error();
            break;
    }
    if (status.empty()) {
        const meshtastic_NodeInfo* self = mesh.me();
        if (self && self->has_position && self->position.sats_in_view > 0) {
            status += std::string(LV_SYMBOL_GPS) + std::to_string(self->position.sats_in_view) + " ";
        }
        // The fuel gauge moves slowly; a look every 15 s is plenty. Single
        // spaces leave room for the tabs.
        const uint64_t now = now_ms();
        if (battery_read_ms_ == 0 || now - battery_read_ms_ >= 15000) {
            battery_read_ms_ = now;
            const char* dir  = getenv("MESHZERO_POWER_SUPPLY_DIR");
            battery_         = dir && dir[0] ? battery_percent(charging_, dir) : battery_percent(charging_);
        }
        if (battery_ >= 0) status += (charging_ ? LV_SYMBOL_CHARGE : "") + std::to_string(battery_) + "% ";
        const time_t t = time(nullptr);
        tm local{};
        localtime_r(&t, &local);
        char clock[8];
        strftime(clock, sizeof(clock), "%H:%M", &local);
        status += clock;
    }
    ui::set_text(status_, status);
    lv_obj_set_style_text_color(status_, color, 0);
    lv_obj_align(status_, LV_ALIGN_RIGHT_MID, -4, 0);

    ui::set_text(hint_, s->hint());
}

void App::tick()
{
    const uint64_t now = now_ms();

    if (esc_held_ms_ != 0 && now - esc_held_ms_ >= kExitHoldMs) quit();

    // Drain model events as they arrive so toasts feel immediate.
    while (!mesh.events.empty()) {
        UiEvent ev = std::move(mesh.events.front());
        mesh.events.pop_front();
        switch (ev.type) {
            case UiEvent::NewMessage:
                notify(LV_SYMBOL_ENVELOPE " " + ev.text);
                break;
            case UiEvent::Toast:
                toast(ev.text);
                break;
            case UiEvent::Trace: {
                const TraceResult& t = ev.trace;
                std::string text     = "Route to " + mesh.long_name(t.dest) + "\n\n";
                const auto path = [this](uint32_t from, const std::vector<uint32_t>& hops, const std::vector<int>& snr,
                                         uint32_t to) {
                    std::string out = mesh.short_name(from);
                    for (size_t i = 0; i <= hops.size(); ++i) {
                        char db[24] = "";
                        if (i < snr.size() && snr[i] != -128) snprintf(db, sizeof(db), " %.1fdB", snr[i] / 4.0);
                        out += std::string("\n  ") + LV_SYMBOL_DOWN + db + "\n";
                        out += i < hops.size() ? mesh.short_name(hops[i]) + "  " + mesh.long_name(hops[i])
                                               : mesh.short_name(to);
                    }
                    return out;
                };
                text += "Outbound:\n" + path(mesh.my_num, t.towards, t.snr_towards, t.dest);
                if (!t.snr_back.empty() || !t.back.empty()) {
                    text += "\n\nReturn:\n" + path(t.dest, t.back, t.snr_back, mesh.my_num);
                }
                push(make_text_view(*this, "Traceroute", text));
                break;
            }
            case UiEvent::LinkChanged:
                break;
        }
    }

    if (notice_until_ != 0 && now >= notice_until_) {
        lv_obj_add_flag(notice_, LV_OBJ_FLAG_HIDDEN);
        notice_until_ = 0;
    }
    if (toast_until_ != 0 && now >= toast_until_) {
        lv_obj_add_flag(toast_, LV_OBJ_FLAG_HIDDEN);
        toast_until_ = 0;
    }

    if (now - last_tick_ >= 250) {
        last_tick_ = now;
        if (update::take_notice()) {
            notify(LV_SYMBOL_DOWNLOAD " MeshZero " + update::available() + " is available");
        }
        // Once a session, so that a radio which won't take it can't keep restarting.
        if (mesh.ready() && mesh.config.has_lora && !coding_rate_checked_) {
            coding_rate_checked_ = true;
            repair_coding_rate(*this);
        }
        // A radio without a region may not transmit, and one on a region MeshZero
        // does not offer may transmit where it should not: ask once, right away.
        if (mesh.ready() && mesh.config.has_lora && !region_supported(mesh.config.lora.region) && !region_prompted_) {
            region_prompted_ = true;
            push(make_region_picker(*this, true));
        } else if (mesh.ready() && mesh.ask_background && !background_prompted_ && stack_.size() == 1) {
            // The update switched running in the background off for this user.
            // Offer it back, once a main page shows; Esc asks again next launch.
            background_prompted_ = true;
            push(make_confirm(*this, "Run in background",
                              std::string("Running in background currently: ") + (mesh.background == 1 ? "ON" : "OFF") +
                                  "\nMeshZero's background service can be set to stay active when you close the app. "
                                  "It is great for keeping your node always online, but will prevent any other apps "
                                  "from using your LoRa cap. Do you want the background service to remain active "
                                  "when you exit?",
                              {"Yes", "No"}, [this](int c) {
                                  mesh.set_background(c == 0);
                                  toast("Your option can be changed in SETUP any time.");
                              }));
        }
        if (Screen* s = top()) s->refresh();
        update_chrome();
    }
    apply_pending();
}

}  // namespace mz
