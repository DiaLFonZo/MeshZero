// SPDX-License-Identifier: GPL-3.0-only
// Conversation list (home tab) and the conversation view with its composer.
#include <algorithm>
#include <ctime>

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
            ui::set_text(empty_, mesh.state() == LinkState::Syncing
                                     ? "Starting radio..."
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
                it.text += "  " + who + last.text;
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
            const meshtastic_NodeInfo* n = mesh.node(key_.id);
            if (n && n->has_user && n->user.public_key.size > 0) t += "  " LV_SYMBOL_EYE_CLOSE;
        }
        return t;
    }

    std::string hint() const override
    {
        const size_t len = input_->text().size();
        std::string h    = key_.dm ? "Enter send   Tab node info   Esc back" : "Enter send   Up/Down scroll   Esc back";
        if (len > 0) h += "   " + std::to_string(len) + "/" + std::to_string(kMaxTextLen);
        return h;
    }

    bool wants_text() const override { return true; }
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
        if (c.msgs.empty()) {
            lv_obj_t* l = ui::label(msgs_, "No messages yet",
                                    ui::c_dim());
            lv_obj_set_width(l, kContentW - 20);
            lv_obj_set_style_pad_left(l, 8, 0);
            lv_obj_set_style_pad_top(l, 8, 0);
            lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
        }
        for (size_t i = start; i < c.msgs.size(); ++i) add_row(c.msgs[i]);
        lv_obj_update_layout(msgs_);
        if (at_bottom || first) lv_obj_scroll_to_y(msgs_, LV_COORD_MAX, LV_ANIM_OFF);
        mesh.mark_read(key_);
    }

    bool on_key(const Key& key) override
    {
        switch (key.k) {
            case K::Enter: {
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
                return false;
            default:
                return input_->handle_key(key);
        }
    }

private:
    void add_row(const StoredMsg& m)
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
        if (m.emoji && m.reply_id) body = "reacted " + body;
        if (m.outgoing && m.ack == AckState::Failed) {
            body += "\nNot delivered: " + delivery_error_text(m.ack_error);
        }
        lv_obj_t* text = ui::label(row, body, m.outgoing && m.ack == AckState::Failed ? ui::c_dim() : ui::c_text());
        lv_obj_set_pos(text, kNameW + 8, 0);
        lv_obj_set_width(text, kContentW - 4 - (kNameW + 8) - meta_w);
        lv_label_set_long_mode(text, LV_LABEL_LONG_WRAP);
    }

    ChatKey key_;
    lv_obj_t* msgs_ = nullptr;
    std::unique_ptr<ui::TextField> input_;
    uint32_t sig_ = 0;
    bool built_   = false;
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
