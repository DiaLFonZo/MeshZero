// SPDX-License-Identifier: GPL-3.0-only
// Conversation list (home tab) and the conversation view with its composer.
#include <algorithm>
#include <ctime>
#include <unordered_map>

#ifdef MESHCARD_ESP32
#include <esp_heap_caps.h>
#endif
#include "common/util.h"
#include "ui/fonts/fonts.h"
#include "ui/app.h"

namespace mz {
namespace {

#ifdef MESHCARD_ESP32
// The ADV has ~22 KB free and its screen shows ~4 messages: only 6 are built at a time,
// older / newer ones are built when the arrows reach them. All stay saved. (10 left only
// ~12 KB free with a chat open, 2026-11-19.)
constexpr size_t kShownMessages = 6;
#else
constexpr size_t kShownMessages = 60;
#endif
constexpr int kInputH           = ui::kRowH + 1;
constexpr int kNameW            = 42;  // fits a four-letter short name

std::string time_str(uint32_t ts)
{
    if (ts == 0) return "";
    const time_t t = ts;
    tm local{};
    localtime_r(&t, &local);
    char buf[16];
    const uint32_t now = now_s();
    strftime(buf, sizeof(buf), now > ts && now - ts > 20 * 3600 ? "%d/%m" : "%H:%M", &local);
    return buf;
}

// ------------------------------------------------------------ chat list

class ChatsScreen : public Screen {
public:
    explicit ChatsScreen(App& a) : Screen(a)
    {
        list_ = std::make_unique<ui::ListView>(root, 0, 0, kContentW, kContentH);
        empty_ = ui::label(root, "", ui::c_dim());
        lv_obj_set_width(empty_, kContentW - 24);
        lv_label_set_long_mode(empty_, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_align(empty_, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(empty_, LV_ALIGN_CENTER, 0, 0);
    }
    int tab() const override { return kTabChats; }
    std::string hint() const override { return "Enter open   Del archive   Tab next page   ? help   Hold Esc exit"; }

    void refresh() override
    {
        if (!mesh.ever_ready()) {
            lv_obj_add_flag(list_->obj(), LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(empty_, LV_OBJ_FLAG_HIDDEN);
            const std::string note = mesh.radio_note();
            ui::set_text(empty_, mesh.state() == LinkState::Syncing || note == "starting" || note == "stopped"
                                     ? "Starting radio..."
                                     : mesh.cap_missing()
                                           ? "No LoRa cap\n\nPower off, attach the cap, power on."
                                           : "Radio offline\n\nCheck that the LoRa cap is attached.");
            return;
        }
        lv_obj_add_flag(empty_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(list_->obj(), LV_OBJ_FLAG_HIDDEN);

        std::vector<const Chat*> channels;
        std::vector<const Chat*> dms;
        for (const auto& [key, c] : mesh.chats) {
            if (key.dm) {
                if (!c.msgs.empty()) dms.push_back(&c);
            } else if (mesh.channel_enabled(static_cast<int>(key.id))) {
                channels.push_back(&c);
            }
        }
        std::sort(dms.begin(), dms.end(), [](const Chat* a, const Chat* b) { return a->last_ts > b->last_ts; });

        ui::ListItems items;
        keys_.clear();
        is_chat_.clear();
        const auto add = [&](const Chat* c) {
            ui::ListItem it;
            it.text = mesh.chat_title(c->key);
            if (!c->msgs.empty()) {
                const StoredMsg& last = c->msgs.back();
                // In a direct chat the title already says who is talking.
                const std::string who = last.outgoing ? "me: " : c->key.dm ? "" : mesh.short_name(last.from) + ": ";
                it.sub = ui::clean(who + (last.is_tapback() ? "reacted " : "") + last.text, true);
            }
            it.text = ui::clean(it.text, true);
            if (c->unread > 0) {
                it.right           = std::to_string(c->unread > 99 ? 99 : c->unread);
                it.right_badge     = true;
                it.has_color       = true;
                it.color           = ui::c_text();
            } else {
                it.right     = c->last_ts ? age_str(c->last_ts, now_s()) : "";
                it.has_color = c->msgs.empty();
                it.color     = ui::c_dim();
            }
            items.push_back(std::move(it));
            keys_.push_back(c->key);
            is_chat_.push_back(true);
        };
        const auto header = [&](const std::string& text) {
            ui::ListItem it;
            it.text   = text;
            it.header = true;
            items.push_back(it);
            keys_.push_back(ChatKey{});
            is_chat_.push_back(false);
        };
        header("Channels");
        for (const Chat* c : channels) add(c);
        header("Direct messages");
        if (dms.empty()) {
            ui::ListItem it;
            it.text      = "No direct messages";
            it.has_color = true;
            it.color     = ui::c_dim();
            it.header    = false;
            items.push_back(it);
            keys_.push_back(ChatKey{true, 0});
            is_chat_.push_back(false);
        }
        for (const Chat* c : dms) add(c);
        list_->set_items(items);
    }

    bool on_key(const Key& key) override
    {
        if (list_->handle_key(key)) return true;
        if (key.k == K::Enter && mesh.ever_ready()) {
            const size_t i = static_cast<size_t>(list_->selected());
            if (i < keys_.size()) {
                if (keys_[i].dm && keys_[i].id == 0) {
                    app.switch_tab(kTabNodes);
                } else {
                    app.open_chat(keys_[i]);
                }
            }
            return true;
        }
        // Archive one chat: its messages are hidden, not deleted; the same key shows them again.
        if ((key.k == K::Backspace || key.k == K::Delete) && mesh.ever_ready()) {
            const size_t i = static_cast<size_t>(list_->selected());
            if (i >= keys_.size() || i >= is_chat_.size() || !is_chat_[i]) return true;  // a header or a note
            const ChatKey k     = keys_[i];
            const std::string t = ui::clean(mesh.chat_title(k), true);
            Mesh* m             = &mesh;
            App* a              = &app;
            if (mesh.chat_archived(k)) {
                app.push(make_confirm(app, "Archive messages",
                                      t + "\n\nHide its messages again, or show the archived ones?",
                                      {"Cancel", "Archive", "Show archived"}, [a, m, k](int c) {
                                          if (c == 1) {
                                              m->archive_chat(k);
                                              a->toast("Messages archived");
                                          } else if (c == 2) {
                                              m->unarchive_chat(k);
                                              a->toast("Archived messages shown");
                                          }
                                      }));
            } else {
                app.push(make_confirm(app, "Archive messages",
                                      t + "\n\nHide its messages? Nothing is deleted: Del on this chat shows them again.",
                                      {"Cancel", "Archive"}, [a, m, k](int c) {
                                          if (c == 1) {
                                              m->archive_chat(k);
                                              a->toast("Messages archived");
                                          }
                                      }));
            }
            return true;
        }
        return false;
    }

private:
    std::unique_ptr<ui::ListView> list_;
    lv_obj_t* empty_ = nullptr;
    std::vector<ChatKey> keys_;
    std::vector<bool> is_chat_;  // per row: a real chat (not a header or a note)
};

// -------------------------------------------------------------- reactions

struct Reaction {
    const char* emoji;
    const char* name;
};
// What the other clients offer first, so they all have a picture for these.
constexpr Reaction kReactions[] = {
    {"\xF0\x9F\x91\x8D", "Thumbs up"},      {"\xE2\x9D\xA4\xEF\xB8\x8F", "Heart"},     {"\xF0\x9F\x98\x82", "Laughing"},
    {"\xF0\x9F\x98\xAE", "Surprised"},      {"\xF0\x9F\x98\xA2", "Sad"},                {"\xF0\x9F\x99\x8F", "Thanks"},
    {"\xF0\x9F\x91\x8E", "Thumbs down"},    {"\xE2\x80\xBC\xEF\xB8\x8F", "Important"}, {"\xE2\x9D\x93", "Question"},
    {"\xF0\x9F\x91\x8B", "Wave"},           {"\xF0\x9F\x94\xA5", "Fire"},               {"\xF0\x9F\x92\xA9", "Poop"},
};
constexpr int kReactionCount = static_cast<int>(sizeof(kReactions) / sizeof(kReactions[0]));
// More of them for writing into a message; the reactions come first.
constexpr Reaction kMoreEmoji[] = {
    {"\xF0\x9F\x98\x80", "Grin"},        {"\xF0\x9F\x98\x8A", "Smile"},   {"\xF0\x9F\x98\x89", "Wink"},
    {"\xF0\x9F\x98\x8E", "Cool"},        {"\xF0\x9F\xA4\x94", "Thinking"}, {"\xF0\x9F\x98\x85", "Phew"},
    {"\xF0\x9F\x91\x8C", "OK"},          {"\xF0\x9F\x8E\x89", "Party"},   {"\xE2\x9C\x85", "Done"},
    {"\xE2\x9D\x8C", "No"},              {"\xE2\x98\x80", "Sun"},         {"\xF0\x9F\x93\xA1", "Antenna"},
};
constexpr int kReactionCols = 6;

// Grid of emoji: to answer a message with (the message is quoted on top), or
// to put into the one being written.
class EmojiScreen : public Screen {
public:
    EmojiScreen(App& a, std::string title, const std::string& quote, bool more,
                std::function<void(const std::string&)> on_pick)
        : Screen(a), title_(std::move(title)), on_pick_(std::move(on_pick))
    {
        for (const Reaction& r : kReactions) emoji_.push_back(r);
        if (more) {
            for (const Reaction& r : kMoreEmoji) emoji_.push_back(r);
        }
        int y0 = 4;
        if (!quote.empty()) {
            lv_obj_t* q = ui::label(root, ui::clean(quote, true), ui::c_dim());
            lv_obj_set_pos(q, 8, 5);
            lv_obj_set_width(q, kContentW - 16);
            lv_obj_set_height(q, ui::kRowH);
            lv_label_set_long_mode(q, LV_LABEL_LONG_CLIP);
            y0 = 28;
        }
        const int rows   = (count() + kReactionCols - 1) / kReactionCols;
        const int cell_w = 46, cell_h = rows > 2 ? 27 : 34;
        const int x0 = (kContentW - kReactionCols * cell_w) / 2;
        for (int i = 0; i < count(); ++i) {
            lv_obj_t* cell = ui::box(root);
            lv_obj_set_size(cell, cell_w - 4, cell_h - 4);
            lv_obj_set_pos(cell, x0 + (i % kReactionCols) * cell_w + 2, y0 + (i / kReactionCols) * cell_h + 2);
            lv_obj_set_style_radius(cell, 4, 0);
            lv_obj_center(ui::label(cell, ui::clean(emoji_[static_cast<size_t>(i)].emoji), ui::c_text()));
            cells_.push_back(cell);
        }
        name_ = ui::label(root, "", ui::c_text());
        lv_obj_set_width(name_, kContentW);
        lv_obj_set_style_text_align(name_, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_pos(name_, 0, y0 + rows * cell_h + 5);
        select(0);
    }
    std::string title() const override { return title_; }
    std::string hint() const override { return "Arrows choose   Enter pick   Esc cancel"; }

    bool on_key(const Key& key) override
    {
        switch (key.k) {
            case K::Left:
                select(selected_ - 1);
                return true;
            case K::Right:
            case K::Tab:
                select(selected_ + 1);
                return true;
            case K::Up:
                select(selected_ - kReactionCols);
                return true;
            case K::Down:
                select(selected_ + kReactionCols);
                return true;
            case K::Enter: {
                const auto cb           = on_pick_;
                const std::string emoji = emoji_[static_cast<size_t>(selected_)].emoji;
                app.pop();
                if (cb) cb(emoji);
                return true;
            }
            default:
                return false;
        }
    }

private:
    void select(int index)
    {
        if (index < 0 || index >= count()) return;
        lv_obj_set_style_bg_opa(cells_[static_cast<size_t>(selected_)], LV_OPA_TRANSP, 0);
        selected_ = index;
        ui::set_bg(cells_[static_cast<size_t>(selected_)], ui::c_select());
        ui::set_text(name_, emoji_[static_cast<size_t>(selected_)].name);
    }
    int count() const { return static_cast<int>(emoji_.size()); }

    std::string title_;
    std::vector<Reaction> emoji_;
    std::function<void(const std::string&)> on_pick_;
    std::vector<lv_obj_t*> cells_;
    lv_obj_t* name_ = nullptr;
    int selected_   = 0;
};

// ----------------------------------------------------------- conversation

class ChatScreen : public Screen {
public:
    ChatScreen(App& a, const ChatKey& key) : Screen(a), key_(key)
    {
        msgs_ = lv_obj_create(root);
        lv_obj_remove_style_all(msgs_);
        lv_obj_set_size(msgs_, kContentW, kContentH - kInputH - 1);
        lv_obj_set_flex_flow(msgs_, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(msgs_, 4, 0);
        lv_obj_set_style_pad_top(msgs_, 2, 0);
        lv_obj_set_style_pad_bottom(msgs_, 2, 0);
        lv_obj_set_scroll_dir(msgs_, LV_DIR_VER);
        lv_obj_set_scrollbar_mode(msgs_, LV_SCROLLBAR_MODE_AUTO);
        lv_obj_set_style_bg_color(msgs_, ui::c_dim(), LV_PART_SCROLLBAR);
        lv_obj_set_style_bg_opa(msgs_, LV_OPA_70, LV_PART_SCROLLBAR);
        lv_obj_set_style_width(msgs_, 2, LV_PART_SCROLLBAR);
        lv_obj_remove_flag(msgs_, LV_OBJ_FLAG_CLICKABLE);

        input_ = std::make_unique<ui::TextField>(root, 3, kContentH - kInputH, kContentW - 6, "Message");
        input_->set_max_bytes(kMaxTextLen);

        // Says which message is being replied to; hidden the rest of the time.
        reply_bar_ = ui::label(root, "", ui::c_accent(), ui::font_small());
        lv_obj_set_pos(reply_bar_, 0, kContentH - kInputH - ui::kRowH);
        lv_obj_set_size(reply_bar_, kContentW, ui::kRowH);
        lv_obj_set_style_pad_left(reply_bar_, 4, 0);
        lv_obj_set_style_pad_top(reply_bar_, 2, 0);
        lv_label_set_long_mode(reply_bar_, LV_LABEL_LONG_CLIP);
        ui::set_bg(reply_bar_, ui::c_bar());
        lv_obj_add_flag(reply_bar_, LV_OBJ_FLAG_HIDDEN);
    }

    std::string title() const override
    {
        std::string t = LV_SYMBOL_LEFT " " + mesh.chat_title(key_);
        if (key_.dm) {
            if (mesh.has_key(key_.id)) {
                t += "  " LV_SYMBOL_EYE_CLOSE;
            } else if (mesh.awaiting_key(key_.id)) {
                t += "  " LV_SYMBOL_REFRESH;
            }
        }
        return t;
    }

    std::string hint() const override
    {
        const size_t len = input_->text().size();
        if (selecting_) return "Enter react, reply...   Up/Down choose   Esc cancel";
        std::string h    = reply_id_ ? "Enter send reply   Esc cancel reply" : "Enter send   Up select   Tab emoji   Esc back";
        if (key_.dm && mesh.awaiting_key(key_.id)) h = "Waiting for pubkey   Enter send   Esc back";
        if (len > 0) h += "   " + std::to_string(len) + "/" + std::to_string(kMaxTextLen);
        return h;
    }

    // While a message is being picked the arrow keys work without Fn.
    bool wants_text() const override { return !selecting_; }
    void on_show() override { mesh.set_active_chat(&key_); }
    void on_hide() override { mesh.set_active_chat(nullptr); }

    void refresh() override
    {
        const Chat& c = mesh.chat(key_);
        // Only a window of messages is built: the newest ones, or older ones once the
        // arrows go up past the top. The rest stay saved and are built when reached.
        if (back_ > c.msgs.size()) back_ = c.msgs.size();
        const size_t end       = c.msgs.size() - back_;
        const size_t sig_start = end > kShownMessages ? end - kShownMessages : 0;
        // Cheap fingerprint of what is on screen: rebuild only when it changes.
        uint32_t sig = static_cast<uint32_t>(c.msgs.size()) * 2654435761u + static_cast<uint32_t>(back_) * 977u;
        for (size_t i = sig_start; i < end; ++i) {
            sig = sig * 31 + c.msgs[i].id + static_cast<uint32_t>(c.msgs[i].ack) * 7;
        }
        sig ^= mesh.node(key_.id) ? 1u : 0u;
        if (sig == sig_ && built_) return;
        const bool first = !built_;
        sig_             = sig;
        built_           = true;

        lv_obj_update_layout(msgs_);
        const bool at_bottom = lv_obj_get_scroll_bottom(msgs_) <= 4;
        lv_obj_clean(msgs_);
        rows_.clear();

        size_t shown = kShownMessages;
#ifdef MESHCARD_ESP32
        // Each message on screen costs memory the ADV barely has. Show only what fits
        // while keeping 12 KB for everything else (radio, Bluetooth, SD card): building
        // more ran it out of memory and crashed it (2026-11-19). Shared looks brought a
        // message down from ~1.5 KB; 700 bytes is the safe estimate.
        {
            const size_t free_now = heap_caps_get_free_size(MALLOC_CAP_8BIT);
            const size_t budget   = free_now > 12 * 1024 ? (free_now - 12 * 1024) / 700 : 0;
            shown                 = std::max<size_t>(3, std::min(shown, budget));
        }
#endif
        const size_t start = end > shown ? end - shown : 0;
        if (start > 0) {
            lv_obj_t* l = ui::label(msgs_, LV_SYMBOL_UP " Older messages: Up", ui::c_dim(), ui::font_small());
            lv_obj_set_style_pad_left(l, 8, 0);
        }
        if (c.msgs.empty()) {
            lv_obj_t* l = ui::label(msgs_, "No messages yet",
                                    ui::c_dim());
            lv_obj_set_width(l, kContentW - 20);
            lv_obj_set_style_pad_left(l, 8, 0);
            lv_obj_set_style_pad_top(l, 8, 0);
            lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
        }
        // A reaction is drawn under the message it belongs to. One whose message
        // is not on screen, or that could not be sent, gets a line of its own.
        std::unordered_map<uint32_t, size_t> by_id;
        for (size_t i = start; i < end; ++i) {
            if (!c.msgs[i].is_tapback() && c.msgs[i].id != 0) by_id[c.msgs[i].id] = i;
        }
        std::unordered_map<size_t, std::vector<const StoredMsg*>> reactions;
        std::vector<bool> attached(c.msgs.size(), false);
        for (size_t i = start; i < end; ++i) {
            const StoredMsg& m = c.msgs[i];
            if (!m.is_tapback() || (m.outgoing && m.ack == AckState::Failed)) continue;
            const auto target = by_id.find(m.reply_id);
            if (target == by_id.end()) continue;
            reactions[target->second].push_back(&m);
            attached[i] = true;
        }
        for (size_t i = start; i < end; ++i) {
            if (attached[i]) continue;
            const auto r = reactions.find(i);
            add_row(c, c.msgs[i], r == reactions.end() ? nullptr : &r->second);
        }
        if (back_ > 0) {
            lv_obj_t* l = ui::label(msgs_, LV_SYMBOL_DOWN " Newer messages: Down", ui::c_dim(), ui::font_small());
            lv_obj_set_style_pad_left(l, 8, 0);
        }
        start_ = start;
        end_   = end;
        lv_obj_update_layout(msgs_);
#ifdef MESHCARD_ESP32
        printf("MeshCard: chat shows %u-%u of %u messages, free %u\n", (unsigned)start, (unsigned)end,
               (unsigned)c.msgs.size(), (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT));
#endif
        if (selecting_) {
            // Stay on the message that was picked, wherever it is now.
            selecting_ = false;
            for (size_t i = 0; i < rows_.size(); ++i) {
                if (rows_[i].id == sel_id_ && rows_[i].from == sel_from_) select_row(static_cast<int>(i));
            }
        }
        if (!selecting_ && (at_bottom || first)) lv_obj_scroll_to_y(msgs_, LV_COORD_MAX, LV_ANIM_OFF);
        mesh.mark_read(key_);
    }

    bool on_key(const Key& key) override
    {
        switch (key.k) {
            case K::Enter: {
                if (selecting_) {
                    actions();
                    return true;
                }
                const std::string text = trim(input_->text());
                if (!text.empty() && mesh.send_text(key_, text, reply_id_)) {
                    back_ = 0;  // show the newest, with the one just sent
                    set_reply(0, "");
                    input_->set_text("");
                    refresh();
                    lv_obj_scroll_to_y(msgs_, LV_COORD_MAX, LV_ANIM_OFF);
                }
                return true;
            }
            case K::Up:
            case K::Down:
                // Up picks the newest message, and from there the arrows move
                // from message to message. Down past the newest lets go again.
                if (selecting_ || key.k == K::Up) {
                    move_selection(key.k == K::Up ? -1 : 1);
                    return true;
                }
                [[fallthrough]];
            case K::PageUp:
            case K::PageDown: {
                const int step = key.k == K::Up ? -34 : key.k == K::Down ? 34 : key.k == K::PageUp ? -100 : 100;
                lv_obj_update_layout(msgs_);
                // At the top or bottom of what is built: build the next older / newer ones,
                // keeping the message that was at that edge in view.
                if ((step < 0 && lv_obj_get_scroll_y(msgs_) <= 0 && start_ > 0) ||
                    (step > 0 && lv_obj_get_scroll_bottom(msgs_) <= 0 && back_ > 0)) {
                    const Row edge = rows_.empty() ? Row{} : (step < 0 ? rows_.front() : rows_.back());
                    shift_window(step < 0 ? -1 : 1);
                    for (const Row& r : rows_) {
                        if (r.id == edge.id && r.from == edge.from) lv_obj_scroll_to_view(lv_obj_get_parent(r.obj), LV_ANIM_OFF);
                    }
                    return true;
                }
                int y           = lv_obj_get_scroll_y(msgs_) + step;
                const int max_y = lv_obj_get_scroll_y(msgs_) + lv_obj_get_scroll_bottom(msgs_);
                if (y > max_y) y = max_y;
                if (y < 0) y = 0;
                lv_obj_scroll_to_y(msgs_, y, LV_ANIM_OFF);
                return true;
            }
            case K::Tab:
                if (selecting_) end_selection();
                app.push(std::make_unique<EmojiScreen>(app, LV_SYMBOL_LEFT " Emoji", "", true,
                                                       [this](const std::string& emoji) {
                                                           // The marker that asks for the colour picture has no
                                                           // shape of its own here, so it stays out of the field.
                                                           if (!input_->insert(ui::clean(emoji, true))) {
                                                               app.toast("The message is full", true);
                                                           }
                                                       }));
                return true;
            case K::Esc:
                if (selecting_) {
                    end_selection();
                    return true;
                }
                if (reply_id_ == 0) return false;
                set_reply(0, "");
                return true;
            default:
                if (selecting_) end_selection();
                return input_->handle_key(key);
        }
    }

private:
    struct Row {
        lv_obj_t* obj = nullptr;
        uint32_t id   = 0;
        uint32_t from = 0;
        std::string quote;  // "who: text", to show what is being answered
    };

    // The message a row stands for, wherever it is by now. Null when gone.
    const StoredMsg* message(const Row& r)
    {
        const Chat& c = mesh.chat(key_);
        for (auto m = c.msgs.rbegin(); m != c.msgs.rend(); ++m) {
            if (m->id == r.id && m->from == r.from && !m->is_tapback()) return &*m;
        }
        return nullptr;
    }

    // "who: text" of the message with this packet id, or empty when it is not in this chat.
    std::string quote_of(const Chat& c, uint32_t id) const
    {
        for (auto m = c.msgs.rbegin(); m != c.msgs.rend(); ++m) {
            if (m->id == id && !m->is_tapback()) return (m->outgoing ? "me" : mesh.short_name(m->from)) + ": " + m->text;
        }
        return {};
    }

    // Starts (or with id 0 ends) answering one particular message.
    void set_reply(uint32_t id, const std::string& quote)
    {
        reply_id_ = id;
        lv_obj_set_height(msgs_, kContentH - kInputH - 1 - (id ? ui::kRowH : 0));
        if (id) {
            ui::set_text(reply_bar_, ui::clean(LV_SYMBOL_RIGHT " Reply to " + quote, true));
            lv_obj_remove_flag(reply_bar_, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(reply_bar_, LV_OBJ_FLAG_HIDDEN);
        }
        lv_obj_update_layout(msgs_);
        lv_obj_scroll_to_y(msgs_, LV_COORD_MAX, LV_ANIM_OFF);
    }

    void select_row(int index)
    {
        if (selecting_) lv_obj_set_style_border_width(rows_[static_cast<size_t>(sel_)].obj, 0, 0);
        const Row& r = rows_[static_cast<size_t>(index)];
        selecting_   = true;
        sel_         = index;
        sel_id_      = r.id;
        sel_from_    = r.from;
        lv_obj_set_style_border_width(r.obj, 2, 0);
        lv_obj_scroll_to_view(lv_obj_get_parent(r.obj), LV_ANIM_OFF);
    }

    void end_selection()
    {
        if (selecting_ && static_cast<size_t>(sel_) < rows_.size()) {
            lv_obj_set_style_border_width(rows_[static_cast<size_t>(sel_)].obj, 0, 0);
        }
        selecting_ = false;
    }

    // Moves the built window of messages older (-1) or newer (+1) by half its size.
    // Returns false when there is nothing further that way.
    bool shift_window(int dir)
    {
        const size_t half = std::max<size_t>(1, (end_ - start_) / 2);
        if (dir < 0) {
            if (start_ == 0) return false;
            back_ += std::min(half, start_);
        } else {
            if (back_ == 0) return false;
            back_ -= std::min(half, back_);
        }
        refresh();
        return true;
    }

    void move_selection(int delta)
    {
        if (rows_.empty()) return;
        int last = static_cast<int>(rows_.size()) - 1;
        if (!selecting_) {
            select_row(last);
            return;
        }
        // Past the top or bottom of what is built: build the next older / newer ones.
        // refresh() keeps the picked message picked.
        if ((sel_ + delta < 0 && shift_window(-1)) || (sel_ + delta > last && shift_window(1))) {
            last = static_cast<int>(rows_.size()) - 1;
            if (!selecting_) return;
        }
        if (sel_ + delta > last) {
            end_selection();
            lv_obj_scroll_to_y(msgs_, LV_COORD_MAX, LV_ANIM_OFF);
        } else if (sel_ + delta >= 0) {
            select_row(sel_ + delta);
        }
    }

    // What can be done with the picked message.
    void actions()
    {
        const Row r = rows_[static_cast<size_t>(sel_)];
        end_selection();
        const StoredMsg* m = message(r);
        if (!m) return;
        enum Action { React, Reply, Details, Resend, NodeInfo };
        std::vector<Action> what;
        std::vector<std::string> labels;
        const auto offer = [&](Action a, const char* label) {
            what.push_back(a);
            labels.push_back(label);
        };
        // Other nodes know a message by its packet id; without one there is nothing to point at.
        if (r.id != 0) {
            offer(React, "React");
            offer(Reply, "Reply");
        }
        offer(Details, "Details");
        if (m->outgoing && m->ack == AckState::Failed) offer(Resend, "Send again");
        if (!m->outgoing) offer(NodeInfo, "About the sender");
        app.push(make_picker(app, LV_SYMBOL_LEFT " " + ui::clean(utf8_truncate(r.quote, 40), true), labels, -1,
                             [this, r, what](int choice) {
                                 if (choice < 0 || static_cast<size_t>(choice) >= what.size()) return;
                                 const StoredMsg* msg = message(r);
                                 if (!msg) return;
                                 switch (what[static_cast<size_t>(choice)]) {
                                     case React:
                                         react(r);
                                         break;
                                     case Reply:
                                         set_reply(r.id, r.quote);
                                         break;
                                     case Details:
                                         app.push(make_text_view(app, LV_SYMBOL_LEFT " Message", details(*msg)));
                                         break;
                                     case Resend:
                                         mesh.send_text(key_, msg->text, msg->reply_id);
                                         break;
                                     case NodeInfo:
                                         app.push(make_node_detail(app, msg->from));
                                         break;
                                 }
                             }));
    }

    // Offers the emoji for the picked message and sends the chosen one.
    void react(const Row& r)
    {
        App* a            = &app;
        const ChatKey key = key_;
        const uint32_t id = r.id;
        app.push(std::make_unique<EmojiScreen>(
            app, LV_SYMBOL_LEFT " React", r.quote, false,
            [a, key, id](const std::string& emoji) { a->mesh.send_tapback(key, id, emoji); }));
    }

    // Everything known about one message, for the details page.
    std::string details(const StoredMsg& m)
    {
        const Chat& c = mesh.chat(key_);
        std::string out;
        const auto line = [&out](const std::string& name, const std::string& value) { out += name + ": " + value + "\n"; };
        const uint32_t sender = m.outgoing ? mesh.my_num : m.from;
        line("From", mesh.long_name(sender) + " (" + mesh.short_name(sender) + ")");
        line("To", key_.dm ? mesh.long_name(m.outgoing ? m.to : mesh.my_num) : mesh.chat_title(key_));
        if (m.ts) {
            const time_t t = m.ts;
            tm local{};
            localtime_r(&t, &local);
            char when[32];
            strftime(when, sizeof(when), "%Y-%m-%d %H:%M:%S", &local);
            line(m.outgoing ? "Sent" : "Received", when);
        }
        line("Encryption", m.pki ? "this node's key and theirs (private)" : key_.dm ? "channel key (no key for this node yet)"
                                                                                    : "channel key");
        if (m.outgoing) {
            switch (m.ack) {
                case AckState::Pending:
                    line("Status", "waiting for an acknowledgement");
                    break;
                case AckState::Acked:
                    line("Status", "delivered");
                    break;
                case AckState::Implicit:
                    line("Status", key_.dm ? "relayed, not confirmed by the recipient" : "relayed by another node");
                    break;
                case AckState::Failed:
                    line("Status", "not delivered: " + delivery_error_text(m.ack_error));
                    break;
                case AckState::None:
                    line("Status", "unknown");
                    break;
            }
        } else {
            line("Hops", m.hops < 0 ? "unknown" : m.hops == 0 ? "none, heard directly" : std::to_string(m.hops));
            // Signal figures describe the last hop, so they only say something about the sender when heard directly.
            if (m.snr != 0 || m.rssi != 0) {
                char sig[48];
                snprintf(sig, sizeof(sig), "SNR %.2f dB, RSSI %d dBm%s", static_cast<double>(m.snr), m.rssi,
                         m.hops > 0 ? " (last hop)" : "");
                line("Signal", sig);
            }
        }
        if (m.reply_id) {
            const std::string quote = quote_of(c, m.reply_id);
            line("Reply to", quote.empty() ? "a message that is not in this chat" : quote);
        }
        std::string reacted;
        for (const StoredMsg& t : c.msgs) {
            if (!t.is_tapback() || t.reply_id != m.id || m.id == 0) continue;
            reacted += (reacted.empty() ? "" : ", ") + t.text + " " + (t.outgoing ? "me" : mesh.short_name(t.from));
        }
        if (!reacted.empty()) line("Reactions", reacted);
        char id[16];
        snprintf(id, sizeof(id), "%08x", m.id);
        line("Packet id", id);
        line("Length", std::to_string(m.text.size()) + " bytes");
        return out;
    }

    // "(thumbs up) ANNA, me   (heart) 3": who reacted with what, or how many did.
    std::string reaction_text(const std::vector<const StoredMsg*>& taps, bool& mine) const
    {
        struct Group {
            std::string emoji;
            std::vector<uint32_t> from;
        };
        std::vector<Group> groups;
        for (const StoredMsg* t : taps) {
            const std::string emoji = ui::clean(t->text, true);
            auto g = std::find_if(groups.begin(), groups.end(), [&](const Group& x) { return x.emoji == emoji; });
            if (g == groups.end()) g = groups.insert(groups.end(), Group{emoji, {}});
            const uint32_t who = t->outgoing ? mesh.my_num : t->from;
            if (std::find(g->from.begin(), g->from.end(), who) == g->from.end()) g->from.push_back(who);
            mine = mine || t->outgoing;
        }
        std::string out;
        for (const Group& g : groups) {
            if (!out.empty()) out += "   ";
            out += g.emoji;
            if (g.from.size() > 2) {
                out += " " + std::to_string(g.from.size());
                continue;
            }
            for (size_t i = 0; i < g.from.size(); ++i) {
                out += (i ? ", " : " ") + (g.from[i] == mesh.my_num ? "me" : ui::clean(mesh.short_name(g.from[i]), true));
            }
        }
        return out;
    }

    // MeshCard: one chat bubble per message. Theirs on the left (dark card,
    // name in their colour), mine on the right (green tint).
    // Looks shared by every message on screen. Set once, so each message costs only its
    // own few objects and text, not a private copy of ~15 style values per object (the
    // ADV has little memory: with private copies only ~7 messages fitted, 2026-11-19).
    struct RowStyles {
        lv_style_t row, bubble, bubble_in, bubble_out, head, text, text_failed, small;
    };
    static const RowStyles& row_styles()
    {
        static RowStyles s;
        static bool ready = false;
        if (ready) return s;
        ready                = true;
        const int bubble_max = kContentW * 4 / 5;
        const int inner_max  = bubble_max - 12;
        lv_style_init(&s.row);
        lv_style_set_width(&s.row, kContentW - 4);
        lv_style_set_height(&s.row, LV_SIZE_CONTENT);
        lv_style_set_pad_left(&s.row, 3);
        lv_style_set_pad_right(&s.row, 3);
        lv_style_set_layout(&s.row, LV_LAYOUT_FLEX);
        lv_style_set_flex_flow(&s.row, LV_FLEX_FLOW_ROW);

        lv_style_init(&s.bubble);
        lv_style_set_width(&s.bubble, LV_SIZE_CONTENT);
        lv_style_set_height(&s.bubble, LV_SIZE_CONTENT);
        lv_style_set_max_width(&s.bubble, bubble_max);
        lv_style_set_radius(&s.bubble, 8);
        lv_style_set_pad_left(&s.bubble, 6);
        lv_style_set_pad_right(&s.bubble, 6);
        lv_style_set_pad_top(&s.bubble, 3);
        lv_style_set_pad_bottom(&s.bubble, 3);
        lv_style_set_border_color(&s.bubble, ui::c_accent());
        lv_style_set_border_width(&s.bubble, 0);
        lv_style_set_bg_opa(&s.bubble, LV_OPA_COVER);
        lv_style_set_layout(&s.bubble, LV_LAYOUT_FLEX);
        lv_style_set_flex_flow(&s.bubble, LV_FLEX_FLOW_COLUMN);
        lv_style_init(&s.bubble_in);
        lv_style_set_bg_color(&s.bubble_in, ui::c_bar());
        lv_style_set_flex_cross_place(&s.bubble_in, LV_FLEX_ALIGN_START);
        lv_style_init(&s.bubble_out);
        lv_style_set_bg_color(&s.bubble_out, lv_color_hex(0x1f4d33));
        lv_style_set_flex_cross_place(&s.bubble_out, LV_FLEX_ALIGN_END);

        lv_style_init(&s.head);
        lv_style_set_text_font(&s.head, mz_font(12, false));
        lv_style_set_text_color(&s.head, ui::c_dim());
        lv_style_set_width(&s.head, LV_SIZE_CONTENT);
        lv_style_set_max_width(&s.head, inner_max);

        lv_style_init(&s.text);
        lv_style_set_text_font(&s.text, ui::font());
        lv_style_set_text_color(&s.text, ui::c_text());
        lv_style_set_width(&s.text, LV_SIZE_CONTENT);
        lv_style_set_max_width(&s.text, inner_max);
        lv_style_init(&s.text_failed);
        lv_style_set_text_color(&s.text_failed, ui::c_dim());

        lv_style_init(&s.small);
        lv_style_set_text_font(&s.small, ui::font_small());
        lv_style_set_text_color(&s.small, ui::c_dim());
        lv_style_set_width(&s.small, LV_SIZE_CONTENT);
        lv_style_set_max_width(&s.small, inner_max);
        lv_style_set_height(&s.small, lv_font_get_line_height(ui::font_small()));
        return s;
    }

    // A plain object or label carrying only shared looks (no private style copy).
    static lv_obj_t* styled(lv_obj_t* parent, bool is_label, const lv_style_t* a, const lv_style_t* b = nullptr)
    {
        lv_obj_t* o = is_label ? lv_label_create(parent) : lv_obj_create(parent);
        lv_obj_remove_style_all(o);
        lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_style(o, const_cast<lv_style_t*>(a), 0);
        if (b) lv_obj_add_style(o, const_cast<lv_style_t*>(b), 0);
        return o;
    }

    // "#" starts a colour in a coloured label: keep it out of names.
    static std::string no_hash(std::string s)
    {
        std::replace(s.begin(), s.end(), '#', ' ');
        return s;
    }

    static std::string hex_of(lv_color_t c)
    {
        char buf[8];
        snprintf(buf, sizeof(buf), "%02x%02x%02x", c.red, c.green, c.blue);
        return buf;
    }

    void add_row(const Chat& c, const StoredMsg& m, const std::vector<const StoredMsg*>* taps)
    {
        const RowStyles& st = row_styles();

        lv_obj_t* row = styled(msgs_, false, &st.row);
        lv_obj_set_flex_align(row, m.outgoing ? LV_FLEX_ALIGN_END : LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_START);

        lv_obj_t* bubble = styled(row, false, &st.bubble, m.outgoing ? &st.bubble_out : &st.bubble_in);

        // Head line: "NAME  08:02" for theirs, "08:02 (tick)" for mine. One coloured
        // label instead of a box with two labels.
        const std::string who = m.outgoing ? "me" : mesh.short_name(m.from);
        std::string head_text;
        if (!m.outgoing) {
            head_text = "#" + hex_of(ui::node_color(m.from)) + " " + no_hash(ui::clean(who, true)) + "#  ";
        }
        std::string meta = time_str(m.ts);
        lv_color_t meta_color = ui::c_dim();
        if (m.outgoing) {
            switch (m.ack) {
                case AckState::Pending:
                    meta += " " LV_SYMBOL_REFRESH;
                    break;
                case AckState::Acked:
                    meta += " " LV_SYMBOL_OK;
                    meta_color = ui::c_accent();
                    break;
                case AckState::Implicit:
                    meta += " " LV_SYMBOL_OK;
                    break;
                case AckState::Failed:
                    meta += " " LV_SYMBOL_CLOSE;
                    meta_color = ui::c_error();
                    break;
                case AckState::None:
                    break;
            }
        }
        if (meta_color.red == ui::c_dim().red && meta_color.green == ui::c_dim().green &&
            meta_color.blue == ui::c_dim().blue) {
            head_text += meta;
        } else {
            head_text += "#" + hex_of(meta_color) + " " + meta + "#";
        }
        lv_obj_t* head = styled(bubble, true, &st.head);
        lv_label_set_recolor(head, true);
        lv_label_set_long_mode(head, LV_LABEL_LONG_CLIP);
        lv_label_set_text(head, head_text.c_str());

        // A reply shows what it answers above the text.
        if (m.reply_id != 0 && !m.is_tapback()) {
            const std::string quote = quote_of(c, m.reply_id);
            lv_obj_t* q = styled(bubble, true, &st.small);
            lv_label_set_long_mode(q, LV_LABEL_LONG_DOT);
            lv_label_set_text(q, ui::clean(LV_SYMBOL_RIGHT " " + (quote.empty() ? std::string("reply") : quote), true).c_str());
        }

        std::string body = ui::clean(m.text);
        if (m.is_tapback()) body = "reacted " + body;
        const bool failed = m.outgoing && m.ack == AckState::Failed;
        if (failed) body += "\nNot delivered: " + delivery_error_text(m.ack_error);
        lv_obj_t* text = styled(bubble, true, &st.text, failed ? &st.text_failed : nullptr);
        lv_label_set_long_mode(text, LV_LABEL_LONG_WRAP);
        lv_label_set_text(text, body.c_str());

        if (taps && !taps->empty()) {
            bool mine                 = false;
            const std::string summary = reaction_text(*taps, mine);
            lv_obj_t* r = styled(bubble, true, &st.small);
            if (mine) lv_obj_set_style_text_color(r, ui::c_accent(), 0);
            lv_label_set_long_mode(r, LV_LABEL_LONG_DOT);
            lv_label_set_text(r, summary.c_str());
        }
        // Reactions themselves cannot be reacted to.
        if (!m.is_tapback()) rows_.push_back(Row{bubble, m.id, m.from, who + ": " + m.text});
    }

    ChatKey key_;
    lv_obj_t* msgs_ = nullptr;
    lv_obj_t* reply_bar_ = nullptr;
    uint32_t reply_id_   = 0;  // the message being answered, 0 when writing a new one
    std::unique_ptr<ui::TextField> input_;
    uint32_t sig_ = 0;
    bool built_   = false;
    std::vector<Row> rows_;   // the messages on screen, oldest first
    size_t back_  = 0;        // newest messages left out below the window (0 = showing the newest)
    size_t start_ = 0;        // the built window: c.msgs[start_, end_)
    size_t end_   = 0;
    bool selecting_    = false;  // a message is picked, to react to it
    int sel_           = 0;
    uint32_t sel_id_   = 0;
    uint32_t sel_from_ = 0;
};

}  // namespace

std::unique_ptr<Screen> make_chats(App& app)
{
    return std::make_unique<ChatsScreen>(app);
}

std::unique_ptr<Screen> make_chat(App& app, const ChatKey& key)
{
    return std::make_unique<ChatScreen>(app, key);
}

}  // namespace mz
