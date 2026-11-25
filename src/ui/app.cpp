// SPDX-License-Identifier: GPL-3.0-only
#include "ui/app.h"

#include <ctime>

#include "common/util.h"
#include "ui/battery.h"
#include "ui/fonts/fonts.h"
#include "ui/update.h"

#if defined(MESHCARD_ESP32)
#include <Arduino.h>
#include <esp_heap_caps.h>
#define MC_HEAP(tag) Serial.printf("MeshCard: heap %s %u\n", tag, (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL))
#else
#define MC_HEAP(tag) ((void)0)
#endif

namespace mz {

constexpr uint64_t kExitHoldMs = 3000;
#if defined(MESHCARD_ESP32)
constexpr bool kCanQuit = false;  // Cardputer ADV: the UI runs inside the radio firmware
#else
constexpr bool kCanQuit = true;
#endif

namespace {
const char* const kTabNames[kTabCount] = {"Chats", "Nodes", "Map", "Radio", "Settings", "Home"};
const char* const kTabIcons[kTabCount] = {LV_SYMBOL_ENVELOPE, LV_SYMBOL_LIST, LV_SYMBOL_GPS,
                                          LV_SYMBOL_WIFI,     LV_SYMBOL_SETTINGS, LV_SYMBOL_HOME};
// MeshCard tab row: every tab is an icon; the open one also shows its name.
constexpr int kShownTabs[]               = {kTabHome, kTabChats, kTabNodes, kTabMap, kTabRadio, kTabSetup};
constexpr int kShownCount                = 6;
constexpr int kIconPillW                 = S(30);
constexpr int kPillGap                   = S(3);
int shown_index(int tab)
{
    for (int i = 0; i < kShownCount; ++i)
        if (kShownTabs[i] == tab) return i;
    return -1;
}
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
    MC_HEAP("start");

    // Status line: own name and unread on the left, link/GPS/clock on the right.
    bar_ = ui::box(scr);
    lv_obj_set_size(bar_, kContentW, kStatusH);
    ui::set_bg(bar_, ui::c_bg());
    who_ = ui::label(bar_, "", ui::c_text(), mz_font(12, true));
    lv_obj_align(who_, LV_ALIGN_LEFT_MID, S(6), 0);
    status_ = ui::label(bar_, "", ui::c_dim(), mz_font(12, true));
    lv_obj_align(status_, LV_ALIGN_RIGHT_MID, S(-6), 0);

    // Tab row: four pills, or the page title on sub-pages.
    tabrow_ = ui::box(scr);
    lv_obj_set_pos(tabrow_, 0, kStatusH);
    lv_obj_set_size(tabrow_, kContentW, kTabRowH);
    for (int i = 0; i < kTabCount; ++i) {
        tabs_[i] = ui::label(tabrow_, kTabIcons[i], ui::c_text(), mz_font(14, true));
        lv_obj_set_height(tabs_[i], kTabRowH - S(3));
        lv_obj_set_style_text_align(tabs_[i], LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_pad_top(tabs_[i], 1, 0);
        lv_obj_set_style_radius(tabs_[i], S(6), 0);
        lv_label_set_long_mode(tabs_[i], LV_LABEL_LONG_CLIP);
        if (shown_index(i) < 0) lv_obj_add_flag(tabs_[i], LV_OBJ_FLAG_HIDDEN);
    }
    title_ = ui::label(tabrow_, "", ui::c_text(), mz_font(16, true));
    lv_obj_set_pos(title_, 2, 1);
    lv_obj_set_size(title_, kContentW - 4, kTabRowH - S(3));
    lv_obj_set_style_pad_left(title_, S(6), 0);
    lv_obj_set_style_radius(title_, S(6), 0);
    ui::set_bg(title_, ui::c_bar());
    lv_label_set_long_mode(title_, LV_LABEL_LONG_DOT);
    MC_HEAP("after bars");

    content_ = ui::box(scr);
    lv_obj_set_pos(content_, 0, kBarH);
    lv_obj_set_size(content_, kContentW, kContentH);

    hint_ = ui::label(scr, "", ui::c_dim(), ui::font_small());
    lv_obj_add_flag(hint_, LV_OBJ_FLAG_HIDDEN);  // MeshCard: keys are under '?'

    // MeshCard: short messages ("Saved"...) are a centre popup like the exit one.
    toast_ = ui::box(scr);
    lv_obj_set_size(toast_, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_min_width(toast_, S(160), 0);
    lv_obj_set_style_radius(toast_, S(10), 0);
    lv_obj_set_style_border_width(toast_, 2, 0);
    lv_obj_set_style_border_color(toast_, ui::c_accent(), 0);
    ui::set_bg(toast_, ui::c_bar());
    lv_obj_set_style_pad_hor(toast_, S(14), 0);
    lv_obj_set_style_pad_ver(toast_, S(12), 0);
    toast_label_ = ui::label(toast_, "", ui::c_text(), mz_font(16, true));
    lv_obj_set_style_max_width(toast_label_, kContentW - S(60), 0);
    lv_obj_set_style_text_align(toast_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(toast_label_, LV_LABEL_LONG_WRAP);
    lv_obj_set_align(toast_label_, LV_ALIGN_CENTER);
    lv_obj_center(toast_);
    lv_obj_add_flag(toast_, LV_OBJ_FLAG_HIDDEN);

    notice_ = ui::label(scr, "", ui::c_bg(), ui::font_bold());
    lv_obj_set_size(notice_, kContentW, kStatusH + 2);
    lv_obj_set_style_pad_left(notice_, 5, 0);
    lv_obj_set_style_pad_top(notice_, 0, 0);
    lv_label_set_long_mode(notice_, LV_LABEL_LONG_DOT);
    ui::set_bg(notice_, ui::c_accent());
    lv_obj_add_flag(notice_, LV_OBJ_FLAG_HIDDEN);

    // Centre popup shown while Esc is held: "Hold Esc 3 s to exit" + progress bar.
    exit_pop_ = ui::box(scr);
    lv_obj_set_size(exit_pop_, S(210), S(58));
    lv_obj_center(exit_pop_);
    lv_obj_set_style_radius(exit_pop_, S(10), 0);
    lv_obj_set_style_border_color(exit_pop_, ui::c_accent(), 0);
    lv_obj_set_style_border_width(exit_pop_, 2, 0);
    ui::set_bg(exit_pop_, ui::c_bar());
    lv_obj_t* exit_text = ui::label(exit_pop_, "Hold Esc 3 s to exit", ui::c_text(), mz_font(16, true));
    lv_obj_align(exit_text, LV_ALIGN_TOP_MID, 0, S(8));
    exit_bar_ = lv_bar_create(exit_pop_);
    lv_obj_remove_style_all(exit_bar_);
    lv_obj_set_size(exit_bar_, S(170), S(8));
    lv_obj_align(exit_bar_, LV_ALIGN_BOTTOM_MID, 0, S(-10));
    lv_obj_set_style_radius(exit_bar_, 4, 0);
    lv_obj_set_style_radius(exit_bar_, 4, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(exit_bar_, ui::c_bg(), 0);
    lv_obj_set_style_bg_opa(exit_bar_, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(exit_bar_, ui::c_accent(), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(exit_bar_, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_bar_set_range(exit_bar_, 0, static_cast<int32_t>(kExitHoldMs));
    lv_obj_add_flag(exit_pop_, LV_OBJ_FLAG_HIDDEN);

    // Centre popup for a new message, same look as the exit popup.
    msg_pop_ = ui::box(scr);
    lv_obj_set_size(msg_pop_, kContentW - S(28), S(100));
    lv_obj_align(msg_pop_, LV_ALIGN_CENTER, 0, S(8));
    lv_obj_set_style_radius(msg_pop_, S(10), 0);
    lv_obj_set_style_border_color(msg_pop_, ui::c_accent(), 0);
    lv_obj_set_style_border_width(msg_pop_, 2, 0);
    ui::set_bg(msg_pop_, ui::c_bar());
    msg_head_ = ui::label(msg_pop_, "", ui::c_accent(), mz_font(14, true));
    lv_obj_set_pos(msg_head_, S(10), S(6));
    lv_obj_set_size(msg_head_, kContentW - S(52), lv_font_get_line_height(mz_font(14, true)));
    lv_label_set_long_mode(msg_head_, LV_LABEL_LONG_DOT);
    msg_text_ = ui::label(msg_pop_, "", ui::c_text(), mz_font(14, false));
    lv_obj_set_pos(msg_text_, S(10), S(26));
    lv_obj_set_size(msg_text_, kContentW - S(52), 2 * lv_font_get_line_height(mz_font(14, false)));
    lv_label_set_long_mode(msg_text_, LV_LABEL_LONG_DOT);
    lv_obj_t* msg_hint = ui::label(msg_pop_, "Enter: open     Esc: close", ui::c_dim(), mz_font(12, false));
    lv_obj_align(msg_hint, LV_ALIGN_BOTTOM_MID, 0, S(-5));
    lv_obj_add_flag(msg_pop_, LV_OBJ_FLAG_HIDDEN);
    MC_HEAP("after frame");

    push(make_tab(*this, kTabHome));
    MC_HEAP("after home");
    apply_pending();
    MC_HEAP("after pending");
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
#if defined(MESHCARD_ESP32)
    Serial.printf("MeshCard: screen tab=%d depth=%u free %u largest %u\n", shown_ ? shown_->tab() : -1,
                  (unsigned)stack_.size(), (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                  (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
#endif
}

void App::handle_key(const Key& pressed)
{
    Screen* s = top();
    if (!s) return;
    // Leaving the app takes holding Esc, so a stray press does not close it.
    // The wait itself is in tick(); here the hold begins and ends.
    if (pressed.release) {
        if (pressed.k == K::Esc && esc_held_ms_ != 0) {
            // Let go before the exit hold finished: a short press on Home turns the screen off.
            esc_held_ms_    = 0;
            screen_off_req_ = true;
            lv_obj_add_flag(exit_pop_, LV_OBJ_FLAG_HIDDEN);
        }
        return;
    }
    if (esc_held_ms_ != 0) {
        if (pressed.k == K::Esc && pressed.repeat) return;  // still held
        esc_held_ms_ = 0;
    }
    // A new-message popup takes the next key: Enter opens the chat, any
    // other key just closes it.
    if (msg_count_ > 0) {
        if (pressed.repeat) return;
        lv_obj_add_flag(msg_pop_, LV_OBJ_FLAG_HIDDEN);
        msg_count_ = 0;
        if (pressed.k == K::Enter) {
            switch_tab(kTabChats);
            open_chat(msg_chat_);
        }
        apply_pending();
        if (top()) top()->refresh();
        update_chrome();
        return;
    }
    Key key = pressed;
    if (key.k == K::Char && !key.ctrl && !s->wants_text()) {
        switch (key.ch) {
#if defined(MESHCARD_ESP32)
            // Cardputer ADV: the arrow legends are on ; . , /
            case ';':
                key.k = K::Up;
                break;
            case '.':
                key.k = K::Down;
                break;
            case ',':
                key.k = K::Left;
                break;
            case '/':
                key.k = K::Right;
                break;
#endif
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
#if defined(MESHCARD_ESP32)
    // Cardputer ADV (no PSRAM): Enter is what opens a page. When memory is already low,
    // building one more could run it out: say so instead, and let Esc free a page first.
    if (key.k == K::Enter && !s->wants_text() &&
        (heap_caps_get_free_size(MALLOC_CAP_INTERNAL) < 10 * 1024 ||
         heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) < 5 * 1024)) {
        toast("Memory low: press Esc to go back first", true);
        update_chrome();
        return;
    }
#endif
    if (!s->on_key(key)) {
        if (key.k == K::Esc) {
            if (stack_.size() > 1) {
                pop();
            } else if (s->tab() != kTabHome) {
                // MeshCard: Esc on any other tab goes Home first.
                if (!key.repeat) switch_tab(kTabHome);
            } else if (!key.repeat && !kCanQuit) {
                // Cardputer ADV: MeshCard is the whole firmware, there is nothing to exit to,
                // so Esc on Home just turns the screen off.
                screen_off_req_ = true;
            } else if (!key.repeat) {
                // Not on a repeat: holding Esc to go back through several
                // pages must not carry on into leaving the app.
                esc_held_ms_ = now_ms();
                lv_bar_set_value(exit_bar_, 0, LV_ANIM_OFF);  // popup appears in tick() after 0.5 s
            }
        } else if (s->tab() >= 0 && !key.ctrl) {
            const int at = shown_index(s->tab());
            if (key.k == K::Tab || key.k == K::Right) switch_tab(kShownTabs[(at + 1) % kShownCount]);
            if (key.k == K::Left) switch_tab(kShownTabs[(at + kShownCount - 1) % kShownCount]);
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
    lv_obj_set_style_border_color(toast_, warn ? ui::c_warn() : ui::c_accent(), 0);
    lv_obj_remove_flag(toast_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_update_layout(toast_);
    lv_obj_center(toast_);
    lv_obj_move_foreground(toast_);
    toast_until_ = now_ms() + (warn ? 5000 : 3500);
}

void App::show_message(const UiEvent& ev)
{
    msg_chat_ = ev.chat;
    ++msg_count_;
    ++wake_count_;
    std::string head = LV_SYMBOL_ENVELOPE "  ";
    head += ev.chat.dm ? mesh.long_name(ev.from) : mesh.short_name(ev.from) + "  in " + mesh.chat_title(ev.chat);
    if (msg_count_ > 1) head += "   (" + std::to_string(msg_count_) + " new)";
    ui::set_text(msg_head_, ui::clean(head, true));
    ui::set_text(msg_text_, ui::clean(utf8_truncate(ev.body, 160), true));
    esc_held_ms_ = 0;  // a message cuts short a hold on Esc
    lv_obj_add_flag(exit_pop_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(msg_pop_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(msg_pop_);
}

void App::notify(const std::string& text)
{
    ui::set_text(notice_, ui::clean(utf8_truncate(text, 120), true));
    lv_obj_remove_flag(notice_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(notice_);
    notice_until_ = now_ms() + 4000;
}

void App::update_chrome()
{
    Screen* s = top();
    if (!s) return;
    const int tab = s->tab();
    const int unread = mesh.total_unread();
    if (tab >= 0) {
        lv_obj_add_flag(title_, LV_OBJ_FLAG_HIDDEN);
        // Icon pills, the open tab wide with its name; laid out left to right.
        const int wide_w = kContentW - 4 - (kShownCount - 1) * (kIconPillW + kPillGap);
        int x            = 2;
        for (int at = 0; at < kShownCount; ++at) {
            const int i       = kShownTabs[at];
            const bool active = i == tab;
            lv_obj_remove_flag(tabs_[i], LV_OBJ_FLAG_HIDDEN);
            const int w = active ? wide_w : kIconPillW;
            lv_obj_set_pos(tabs_[i], x, 1);
            lv_obj_set_width(tabs_[i], w);
            x += w + kPillGap;
            std::string text = kTabIcons[i];
            if (active) text += std::string("  ") + kTabNames[i];
            ui::set_text(tabs_[i], text);
            lv_obj_set_style_text_color(tabs_[i], active ? ui::c_bg() : ui::c_text(), 0);
            ui::set_bg(tabs_[i], active ? ui::c_accent() : ui::c_bar());
        }
    } else {
        for (auto* t : tabs_) lv_obj_add_flag(t, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(title_, LV_OBJ_FLAG_HIDDEN);
        ui::set_text(title_, ui::clean(s->title(), true));
    }
    {
        std::string who = mesh.short_name(mesh.my_num);
        if (unread > 0) who += "   " LV_SYMBOL_ENVELOPE " " + std::to_string(unread > 99 ? 99 : unread);
        ui::set_text(who_, ui::clean(who, true));
        lv_obj_set_style_text_color(who_, unread > 0 ? ui::c_accent() : ui::c_text(), 0);
    }

    // Right side: link state, satellites, clock.
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
            status += std::string(LV_SYMBOL_GPS) + std::to_string(self->position.sats_in_view) + "  ";
        }
        const time_t t = time(nullptr);
        tm local{};
        localtime_r(&t, &local);
        char clock[8];
        strftime(clock, sizeof(clock), "%H:%M", &local);
        status += clock;
    }
    // Battery first: "<icon>83% ↓3h20m" (arrow = time left, never a colon).
    if (const std::string batt = battery_text(battery_info()); !batt.empty()) status = batt + "  " + status;
    ui::set_text(status_, status);
    lv_obj_set_style_text_color(status_, color, 0);
    lv_obj_align(status_, LV_ALIGN_RIGHT_MID, S(-6), 0);
}

void App::tick()
{
    const uint64_t now = now_ms();

    if (esc_held_ms_ != 0) {
        const uint64_t held = now - esc_held_ms_;
        // The popup only shows after half a second, so a quick press (screen off) stays clean.
        if (held >= 500 && lv_obj_has_flag(exit_pop_, LV_OBJ_FLAG_HIDDEN)) {
            lv_obj_remove_flag(exit_pop_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_move_foreground(exit_pop_);
        }
        // The bar starts empty when the popup appears and fills over the rest of the hold.
        const uint64_t shown = held < 500 ? 0 : held - 500;
        const uint64_t span  = kExitHoldMs - 500;
        lv_bar_set_value(exit_bar_, static_cast<int32_t>((shown > span ? span : shown) * kExitHoldMs / span), LV_ANIM_OFF);
        if (held >= kExitHoldMs) quit();
    } else if (!lv_obj_has_flag(exit_pop_, LV_OBJ_FLAG_HIDDEN)) {
        lv_obj_add_flag(exit_pop_, LV_OBJ_FLAG_HIDDEN);  // let go early: stay in the app
    }

    // Drain model events as they arrive so toasts feel immediate.
    while (!mesh.events.empty()) {
        UiEvent ev = std::move(mesh.events.front());
        mesh.events.pop_front();
        switch (ev.type) {
            case UiEvent::NewMessage:
                if (ev.tapback) {
                    notify(LV_SYMBOL_ENVELOPE " " + ev.text);  // reactions stay a small notice
                } else {
                    show_message(ev);
                }
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
        // A radio without a region may not transmit: ask once, right away.
        if (mesh.ready() && mesh.region_unset() && !region_prompted_) {
            region_prompted_ = true;
            push(make_region_picker(*this, true));
        }
        if (Screen* s = top()) s->refresh();
        update_chrome();
    }
    apply_pending();
}

}  // namespace mz
