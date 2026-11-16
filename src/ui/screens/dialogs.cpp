// SPDX-License-Identifier: GPL-3.0-only
// Small reusable screens: scrollable text, confirmation, option picker,
// single-line input and QR code.
#include "ui/app.h"

namespace mz {
namespace {

class TextViewScreen : public Screen {
public:
    TextViewScreen(App& a, std::string title, const std::string& text) : Screen(a), title_(std::move(title))
    {
        lv_obj_add_flag(root, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_scroll_dir(root, LV_DIR_VER);
        lv_obj_set_scrollbar_mode(root, LV_SCROLLBAR_MODE_AUTO);
        lv_obj_set_style_bg_color(root, ui::c_dim(), LV_PART_SCROLLBAR);
        lv_obj_set_style_bg_opa(root, LV_OPA_70, LV_PART_SCROLLBAR);
        lv_obj_set_style_width(root, 2, LV_PART_SCROLLBAR);
        lv_obj_t* l = ui::label(root, ui::clean(text), ui::c_text());
        lv_obj_set_pos(l, 6, 3);
        lv_obj_set_width(l, kContentW - 14);
        lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    }
    std::string title() const override { return title_; }
    std::string hint() const override { return "Up/Down scroll   Esc back"; }
    bool on_key(const Key& key) override
    {
        const int step = key.k == K::Up ? -34 : key.k == K::Down ? 34 : key.k == K::PageUp ? -110 : key.k == K::PageDown ? 110 : 0;
        if (step == 0) {
            if (key.k == K::Enter) {
                app.pop();
                return true;
            }
            return false;
        }
        lv_obj_update_layout(root);
        int y = lv_obj_get_scroll_y(root) + step;
        const int max_y = lv_obj_get_scroll_y(root) + lv_obj_get_scroll_bottom(root);
        if (y > max_y) y = max_y;
        if (y < 0) y = 0;
        lv_obj_scroll_to_y(root, y, LV_ANIM_OFF);
        return true;
    }

private:
    std::string title_;
};

class ConfirmScreen : public Screen {
public:
    ConfirmScreen(App& a, std::string title, const std::string& message, std::vector<std::string> options,
                  std::function<void(int)> on_choice)
        : Screen(a), title_(std::move(title)), options_(std::move(options)), on_choice_(std::move(on_choice))
    {
        lv_obj_t* l = ui::label(root, ui::clean(message), ui::c_text());
        lv_obj_set_pos(l, 8, 6);
        lv_obj_set_width(l, kContentW - 16);
        lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
        const int list_h = static_cast<int>(options_.size()) * ui::kRowH;
        list_            = std::make_unique<ui::ListView>(root, 0, kContentH - list_h - 2, kContentW, list_h);
        std::vector<ui::ListItem> items;
        for (const auto& o : options_) {
            ui::ListItem it;
            it.text = o;
            items.push_back(it);
        }
        list_->set_items(items);
    }
    std::string title() const override { return title_; }
    std::string hint() const override { return "Up/Down choose   Enter confirm   Esc cancel"; }
    bool on_key(const Key& key) override
    {
        if (list_->handle_key(key)) return true;
        if (key.k == K::Enter) {
            const int choice = list_->selected();
            auto cb          = on_choice_;
            app.pop();
            if (cb) cb(choice);
            return true;
        }
        return false;
    }

private:
    std::string title_;
    std::vector<std::string> options_;
    std::function<void(int)> on_choice_;
    std::unique_ptr<ui::ListView> list_;
};

class PickerScreen : public Screen {
public:
    PickerScreen(App& a, std::string title, std::vector<std::string> options, int current, std::function<void(int)> on_pick)
        : Screen(a), title_(std::move(title)), on_pick_(std::move(on_pick))
    {
        list_ = std::make_unique<ui::ListView>(root, 0, 0, kContentW, kContentH);
        std::vector<ui::ListItem> items;
        for (size_t i = 0; i < options.size(); ++i) {
            ui::ListItem it;
            it.text = options[i];
            if (static_cast<int>(i) == current) {
                it.right           = LV_SYMBOL_OK;
                it.has_right_color = true;
                it.right_color     = ui::c_accent();
            }
            items.push_back(it);
        }
        list_->set_items(items);
        if (current >= 0) list_->select(current);
    }
    std::string title() const override { return title_; }
    std::string hint() const override { return "Up/Down choose   Enter select   Esc cancel"; }
    bool on_key(const Key& key) override
    {
        if (list_->handle_key(key)) return true;
        if (key.k == K::Enter && list_->count() > 0) {
            const int choice = list_->selected();
            auto cb          = on_pick_;
            app.pop();
            if (cb) cb(choice);
            return true;
        }
        return false;
    }

private:
    std::string title_;
    std::function<void(int)> on_pick_;
    std::unique_ptr<ui::ListView> list_;
};

class InputScreen : public Screen {
public:
    InputScreen(App& a, std::string title, const std::string& initial, size_t max_bytes, const std::string& help,
                std::function<bool(const std::string&)> on_accept, const char* accept_chars)
        : Screen(a), title_(std::move(title)), on_accept_(std::move(on_accept))
    {
        field_ = std::make_unique<ui::TextField>(root, 6, 10, kContentW - 12);
        field_->set_max_bytes(max_bytes);
        if (accept_chars) field_->set_accept(accept_chars);
        field_->set_text(initial);
        lv_obj_t* l = ui::label(root, ui::clean(help), ui::c_dim());
        lv_obj_set_pos(l, 8, 36);
        lv_obj_set_width(l, kContentW - 16);
        lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    }
    std::string title() const override { return title_; }
    std::string hint() const override { return "Enter save   Esc cancel"; }
    bool wants_text() const override { return true; }
    bool on_key(const Key& key) override
    {
        if (key.k == K::Enter) {
            // The callback may reject the value (and explain why with a toast).
            if (!on_accept_ || on_accept_(field_->text())) app.pop();
            return true;
        }
        if (key.k == K::Esc || key.k == K::Tab) return false;
        field_->handle_key(key);
        return true;
    }

private:
    std::string title_;
    std::function<bool(const std::string&)> on_accept_;
    std::unique_ptr<ui::TextField> field_;
};

class QrScreen : public Screen {
public:
    QrScreen(App& a, std::string title, const std::string& data) : Screen(a), title_(std::move(title))
    {
        const int size = kContentH - 6;
        lv_obj_t* qr   = lv_qrcode_create(root);
        lv_qrcode_set_size(qr, size);
        lv_qrcode_set_dark_color(qr, lv_color_black());
        lv_qrcode_set_light_color(qr, lv_color_white());
        lv_obj_set_pos(qr, 6, 3);
        lv_obj_set_style_border_color(qr, lv_color_white(), 0);
        lv_obj_set_style_border_width(qr, 3, 0);
        const bool ok = lv_qrcode_update(qr, data.c_str(), static_cast<uint32_t>(data.size())) == LV_RESULT_OK;
        lv_obj_t* l   = ui::label(root, ok ? data : "Too long for a QR code\n\n" + data, ui::c_dim(),
                                  ui::font_small());
        lv_obj_set_pos(l, size + 16, 3);
        lv_obj_set_width(l, kContentW - size - 20);
        lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
        if (!ok) lv_obj_add_flag(qr, LV_OBJ_FLAG_HIDDEN);
    }
    std::string title() const override { return title_; }
    std::string hint() const override { return "Esc back"; }

private:
    std::string title_;
};

}  // namespace

std::unique_ptr<Screen> make_text_view(App& app, const std::string& title, const std::string& text)
{
    return std::make_unique<TextViewScreen>(app, title, text);
}

std::unique_ptr<Screen> make_confirm(App& app, const std::string& title, const std::string& message,
                                     const std::vector<std::string>& options, std::function<void(int)> on_choice)
{
    return std::make_unique<ConfirmScreen>(app, title, message, options, std::move(on_choice));
}

std::unique_ptr<Screen> make_picker(App& app, const std::string& title, const std::vector<std::string>& options, int current,
                                    std::function<void(int)> on_pick)
{
    return std::make_unique<PickerScreen>(app, title, options, current, std::move(on_pick));
}

std::unique_ptr<Screen> make_input(App& app, const std::string& title, const std::string& initial, size_t max_bytes,
                                   const std::string& help, std::function<bool(const std::string&)> on_accept,
                                   const char* accept_chars)
{
    return std::make_unique<InputScreen>(app, title, initial, max_bytes, help, std::move(on_accept), accept_chars);
}

std::unique_ptr<Screen> make_qr(App& app, const std::string& title, const std::string& data)
{
    return std::make_unique<QrScreen>(app, title, data);
}

}  // namespace mz
