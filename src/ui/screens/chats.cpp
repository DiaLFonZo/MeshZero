// SPDX-License-Identifier: GPL-3.0-only
// Conversation list (home tab) and the conversation view with its composer.
#include <algorithm>
#include <ctime>
#include <unordered_map>

#include "common/util.h"
#include "ui/app.h"

namespace mz {
namespace {

constexpr size_t kShownMessages = 60;
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
    std::string hint() const override { return "Enter open   Tab next page   ? help   Esc exit"; }

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

        std::vector<ui::ListItem> items;
        keys_.clear();
        const auto add = [&](const Chat* c) {
            ui::ListItem it;
            it.text = mesh.chat_title(c->key);
            if (!c->msgs.empty()) {
                const StoredMsg& last = c->msgs.back();
                // In a direct chat the title already says who is talking.
                const std::string who = last.outgoing ? "me: " : c->key.dm ? "" : mesh.short_name(last.from) + ": ";
                it.text += "  " + who + (last.is_tapback() ? "reacted " : "") + last.text;
            }
            it.text = ui::clean(it.text, true);
            if (c->unread > 0) {
                it.right           = std::to_string(c->unread) + " new";
                it.has_right_color = true;
                it.right_color     = ui::c_accent();
                it.has_color       = true;
                it.color           = ui::c_text();
            } else {
                it.right     = c->last_ts ? age_str(c->last_ts, now_s()) : "";
                it.has_color = c->msgs.empty();
                it.color     = ui::c_dim();
            }
            items.push_back(std::move(it));
            keys_.push_back(c->key);
        };
        const auto header = [&](const std::string& text) {
            ui::ListItem it;
            it.text   = text;
            it.header = true;
            items.push_back(it);
            keys_.push_back(ChatKey{});
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
        return false;
    }

private:
    std::unique_ptr<ui::ListView> list_;
    lv_obj_t* empty_ = nullptr;
    std::vector<ChatKey> keys_;
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
constexpr int kReactionCols  = 6;

// Grid of emoji to answer a message with.
class ReactScreen : public Screen {
public:
    ReactScreen(App& a, const std::string& quote, std::function<void(const std::string&)> on_pick)
        : Screen(a), on_pick_(std::move(on_pick))
    {
        lv_obj_t* q = ui::label(root, ui::clean(quote, true), ui::c_dim());
        lv_obj_set_pos(q, 8, 5);
        lv_obj_set_width(q, kContentW - 16);
        lv_label_set_long_mode(q, LV_LABEL_LONG_CLIP);

        const int cell_w = 46, cell_h = 34;
        const int x0 = (kContentW - kReactionCols * cell_w) / 2, y0 = 28;
        for (int i = 0; i < kReactionCount; ++i) {
            lv_obj_t* cell = ui::box(root);
            lv_obj_set_size(cell, cell_w - 4, cell_h - 4);
            lv_obj_set_pos(cell, x0 + (i % kReactionCols) * cell_w + 2, y0 + (i / kReactionCols) * cell_h + 2);
            lv_obj_set_style_radius(cell, 4, 0);
            lv_obj_center(ui::label(cell, ui::clean(kReactions[i].emoji), ui::c_text()));
            cells_.push_back(cell);
        }
        name_ = ui::label(root, "", ui::c_text());
        lv_obj_set_width(name_, kContentW);
        lv_obj_set_style_text_align(name_, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_pos(name_, 0, y0 + ((kReactionCount + kReactionCols - 1) / kReactionCols) * cell_h + 6);
        select(0);
    }
    std::string title() const override { return LV_SYMBOL_LEFT " React"; }
    std::string hint() const override { return "Arrows choose   Enter send   Esc cancel"; }

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
                const std::string emoji = kReactions[selected_].emoji;
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
        if (index < 0 || index >= kReactionCount) return;
        lv_obj_set_style_bg_opa(cells_[static_cast<size_t>(selected_)], LV_OPA_TRANSP, 0);
        selected_ = index;
        ui::set_bg(cells_[static_cast<size_t>(selected_)], ui::c_select());
        ui::set_text(name_, kReactions[selected_].name);
    }

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
        lv_obj_set_style_pad_row(msgs_, 2, 0);
        lv_obj_set_style_pad_top(msgs_, 2, 0);
        lv_obj_set_style_pad_bottom(msgs_, 2, 0);
        lv_obj_set_scroll_dir(msgs_, LV_DIR_VER);
        lv_obj_set_scrollbar_mode(msgs_, LV_SCROLLBAR_MODE_AUTO);
        lv_obj_set_style_bg_color(msgs_, ui::c_dim(), LV_PART_SCROLLBAR);
        lv_obj_set_style_bg_opa(msgs_, LV_OPA_70, LV_PART_SCROLLBAR);
        lv_obj_set_style_width(msgs_, 2, LV_PART_SCROLLBAR);
        lv_obj_remove_flag(msgs_, LV_OBJ_FLAG_CLICKABLE);

        input_ = std::make_unique<ui::TextField>(root, 0, kContentH - kInputH, kContentW, "Message");
        input_->set_max_bytes(kMaxTextLen);
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
        if (selecting_) return "Enter react   Up/Down choose   Esc cancel";
        std::string h    = key_.dm ? "Enter send   Up react   Tab info   Esc back" : "Enter send   Up react   Esc back";
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
        // Cheap fingerprint of what is on screen: rebuild only when it changes.
        uint32_t sig       = static_cast<uint32_t>(c.msgs.size()) * 2654435761u;
        const size_t start = c.msgs.size() > kShownMessages ? c.msgs.size() - kShownMessages : 0;
        for (size_t i = start; i < c.msgs.size(); ++i) {
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
        for (size_t i = start; i < c.msgs.size(); ++i) {
            if (!c.msgs[i].is_tapback() && c.msgs[i].id != 0) by_id[c.msgs[i].id] = i;
        }
        std::unordered_map<size_t, std::vector<const StoredMsg*>> reactions;
        std::vector<bool> attached(c.msgs.size(), false);
        for (size_t i = start; i < c.msgs.size(); ++i) {
            const StoredMsg& m = c.msgs[i];
            if (!m.is_tapback() || (m.outgoing && m.ack == AckState::Failed)) continue;
            const auto target = by_id.find(m.reply_id);
            if (target == by_id.end()) continue;
            reactions[target->second].push_back(&m);
            attached[i] = true;
        }
        for (size_t i = start; i < c.msgs.size(); ++i) {
            if (attached[i]) continue;
            const auto r = reactions.find(i);
            add_row(c.msgs[i], r == reactions.end() ? nullptr : &r->second);
        }
        lv_obj_update_layout(msgs_);
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
                    react();
                    return true;
                }
                const std::string text = trim(input_->text());
                if (!text.empty() && mesh.send_text(key_, text)) {
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
                int y           = lv_obj_get_scroll_y(msgs_) + step;
                const int max_y = lv_obj_get_scroll_y(msgs_) + lv_obj_get_scroll_bottom(msgs_);
                if (y > max_y) y = max_y;
                if (y < 0) y = 0;
                lv_obj_scroll_to_y(msgs_, y, LV_ANIM_OFF);
                return true;
            }
            case K::Tab:
                if (key_.dm) app.push(make_node_detail(app, key_.id));
                return true;
            case K::Esc:
                if (!selecting_) return false;
                end_selection();
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
        std::string quote;
    };

    void select_row(int index)
    {
        if (selecting_) lv_obj_set_style_bg_opa(rows_[static_cast<size_t>(sel_)].obj, LV_OPA_TRANSP, 0);
        const Row& r = rows_[static_cast<size_t>(index)];
        selecting_   = true;
        sel_         = index;
        sel_id_      = r.id;
        sel_from_    = r.from;
        ui::set_bg(r.obj, ui::c_select());
        lv_obj_scroll_to_view(r.obj, LV_ANIM_OFF);
    }

    void end_selection()
    {
        if (selecting_ && static_cast<size_t>(sel_) < rows_.size()) {
            lv_obj_set_style_bg_opa(rows_[static_cast<size_t>(sel_)].obj, LV_OPA_TRANSP, 0);
        }
        selecting_ = false;
    }

    void move_selection(int delta)
    {
        if (rows_.empty()) return;
        const int last = static_cast<int>(rows_.size()) - 1;
        if (!selecting_) {
            select_row(last);
        } else if (sel_ + delta > last) {
            end_selection();
            lv_obj_scroll_to_y(msgs_, LV_COORD_MAX, LV_ANIM_OFF);
        } else if (sel_ + delta >= 0) {
            select_row(sel_ + delta);
        }
    }

    // Offers the emoji for the picked message and sends the chosen one.
    void react()
    {
        const Row r = rows_[static_cast<size_t>(sel_)];
        end_selection();
        if (r.id == 0) {
            app.toast("This message cannot be reacted to", true);
            return;
        }
        App* a            = &app;
        const ChatKey key = key_;
        const uint32_t id = r.id;
        app.push(std::make_unique<ReactScreen>(
            app, r.quote, [a, key, id](const std::string& emoji) { a->mesh.send_tapback(key, id, emoji); }));
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

    void add_row(const StoredMsg& m, const std::vector<const StoredMsg*>* taps)
    {
        lv_obj_t* row = ui::box(msgs_);
        lv_obj_set_width(row, kContentW - 4);
        lv_obj_set_height(row, LV_SIZE_CONTENT);

        const std::string who = m.outgoing ? "me" : mesh.short_name(m.from);
        lv_obj_t* name = ui::label(row, ui::clean(who, true), m.outgoing ? ui::c_me() : ui::node_color(m.from));
        lv_obj_set_pos(name, 4, 0);
        lv_obj_set_width(name, kNameW);
        lv_label_set_long_mode(name, LV_LABEL_LONG_CLIP);

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
        lv_obj_t* meta_l = ui::label(row, meta, meta_color, ui::font_small());
        lv_obj_align(meta_l, LV_ALIGN_TOP_RIGHT, -3, 2);
        lv_obj_update_layout(meta_l);
        const int meta_w = lv_obj_get_width(meta_l) + 6;

        std::string body = ui::clean(m.text);
        if (m.is_tapback()) body = "reacted " + body;
        if (m.outgoing && m.ack == AckState::Failed) {
            body += "\nNot delivered: " + delivery_error_text(m.ack_error);
        }
        lv_obj_t* text = ui::label(row, body, m.outgoing && m.ack == AckState::Failed ? ui::c_dim() : ui::c_text());
        lv_obj_set_pos(text, kNameW + 8, 0);
        lv_obj_set_width(text, kContentW - 4 - (kNameW + 8) - meta_w);
        lv_label_set_long_mode(text, LV_LABEL_LONG_WRAP);

        if (taps && !taps->empty()) {
            bool mine             = false;
            const std::string summary = reaction_text(*taps, mine);
            lv_obj_update_layout(text);
            lv_obj_t* r = ui::label(row, summary, mine ? ui::c_accent() : ui::c_dim());
            lv_obj_set_pos(r, kNameW + 8, lv_obj_get_height(text));
            lv_obj_set_width(r, kContentW - 4 - (kNameW + 8) - 4);
            lv_label_set_long_mode(r, LV_LABEL_LONG_CLIP);
        }
        // Reactions themselves cannot be reacted to.
        if (!m.is_tapback()) rows_.push_back(Row{row, m.id, m.from, who + ": " + m.text});
    }

    ChatKey key_;
    lv_obj_t* msgs_ = nullptr;
    std::unique_ptr<ui::TextField> input_;
    uint32_t sig_ = 0;
    bool built_   = false;
    std::vector<Row> rows_;   // the messages on screen, oldest first
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
