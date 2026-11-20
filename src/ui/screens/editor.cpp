// SPDX-License-Identifier: GPL-3.0-only
#include "ui/screens/editor.h"

#include <cstdlib>

namespace mz {

Field f_header(const std::string& label)
{
    Field f;
    f.type  = Field::Header;
    f.label = label;
    return f;
}

Field f_info(const std::string& label, std::function<std::string()> get_text)
{
    Field f;
    f.type     = Field::Info;
    f.label    = label;
    f.get_text = std::move(get_text);
    return f;
}

Field f_bool(const std::string& label, std::function<int64_t()> get, std::function<void(int64_t)> set,
             const std::string& help)
{
    Field f;
    f.type  = Field::Bool;
    f.label = label;
    f.get   = std::move(get);
    f.set   = std::move(set);
    f.help  = help;
    return f;
}

Field f_int(const std::string& label, std::function<int64_t()> get, std::function<void(int64_t)> set, int64_t min,
            int64_t max, const std::string& unit, const std::string& help)
{
    Field f;
    f.type  = Field::Int;
    f.label = label;
    f.get   = std::move(get);
    f.set   = std::move(set);
    f.min   = min;
    f.max   = max;
    f.unit  = unit;
    f.help  = help;
    return f;
}

Field f_enum(const std::string& label, std::function<int64_t()> get, std::function<void(int64_t)> set,
             std::vector<Opt> opts, const std::string& help)
{
    Field f;
    f.type  = Field::Enum;
    f.label = label;
    f.get   = std::move(get);
    f.set   = std::move(set);
    f.opts  = std::move(opts);
    f.help  = help;
    return f;
}

Field f_text(const std::string& label, std::function<std::string()> get,
             std::function<std::string(const std::string&)> set, size_t max_len, const std::string& help, bool secret)
{
    Field f;
    f.type     = Field::Text;
    f.label    = label;
    f.get_text = std::move(get);
    f.set_text = std::move(set);
    f.max_len  = max_len;
    f.help     = help;
    f.secret   = secret;
    return f;
}

Field f_action(const std::string& label, std::function<void()> run, const std::string& help, bool marks_dirty)
{
    Field f;
    f.type        = Field::Action;
    f.label       = label;
    f.run         = std::move(run);
    f.help        = help;
    f.marks_dirty = marks_dirty;
    return f;
}

namespace {

// Seven whole rows above, one line below for a note on the selected field.
constexpr int kHelpH = kContentH - 7 * ui::kRowH;

class EditorScreen : public Screen {
public:
    EditorScreen(App& a, std::string title, std::vector<Field> fields, std::function<void()> save, std::string saved_note)
        : Screen(a), title_(std::move(title)), fields_(std::move(fields)), save_(std::move(save)),
          saved_note_(std::move(saved_note))
    {
        list_ = std::make_unique<ui::ListView>(root, 0, 0, kContentW, kContentH - kHelpH);
        lv_obj_t* line = ui::box(root);
        lv_obj_set_pos(line, 0, kContentH - kHelpH);
        lv_obj_set_size(line, kContentW, 1);
        ui::set_bg(line, ui::c_bar());
        help_ = ui::label(root, "", ui::c_dim(), ui::font_small());
        lv_obj_set_pos(help_, 5, kContentH - kHelpH + 2);
        lv_obj_set_size(help_, kContentW - 10, kHelpH - 2);
        lv_label_set_long_mode(help_, LV_LABEL_LONG_WRAP);
    }

    std::string title() const override { return LV_SYMBOL_LEFT " " + title_ + (dirty_ ? "  (edited)" : ""); }
    std::string hint() const override
    {
        return dirty_ ? "Enter change   ? explain   Esc save" : "Enter change   ? explain   Esc back";
    }

    void refresh() override
    {
        std::vector<ui::ListItem> items;
        for (const Field& f : fields_) {
            ui::ListItem it;
            it.text = f.label;
            switch (f.type) {
                case Field::Header:
                    it.header = true;
                    break;
                case Field::Bool: {
                    const bool on      = f.get() != 0;
                    it.right           = on ? "on" : "off";
                    it.has_right_color = true;
                    it.right_color     = on ? ui::c_accent() : ui::c_dim();
                    break;
                }
                case Field::Int:
                    it.right           = shown(f, f.get());
                    it.has_right_color = true;
                    it.right_color     = ui::c_text();
                    break;
                case Field::Enum: {
                    const int64_t v = f.get();
                    it.right        = "(" + std::to_string(v) + ")";
                    for (const Opt& o : options(f)) {
                        if (o.value == v) it.right = o.label;
                    }
                    // Long option labels carry an explanation after two spaces.
                    const size_t cut = it.right.find("  ");
                    if (cut != std::string::npos) it.right = it.right.substr(0, cut);
                    if (f.note) it.right += f.note();
                    it.has_right_color = true;
                    it.right_color     = ui::c_text();
                    break;
                }
                case Field::Text: {
                    const std::string v = f.get_text();
                    it.right            = v.empty() ? (f.empty_text.empty() ? "(empty)" : f.empty_text) : f.secret ? "********" : ui::clean(v, true);
                    if (it.right.size() > 26) it.right = utf8_cut(it.right, 24) + "..";
                    it.has_right_color = true;
                    it.right_color     = v.empty() ? ui::c_dim() : ui::c_text();
                    break;
                }
                case Field::Action:
                    it.has_color = true;
                    it.color     = ui::c_accent();
                    it.right     = LV_SYMBOL_RIGHT;
                    break;
                case Field::Info:
                    it.has_color       = true;
                    it.color           = ui::c_dim();
                    it.right           = f.get_text ? ui::clean(f.get_text(), true) : "";
                    it.has_right_color = true;
                    it.right_color     = ui::c_text();
                    break;
            }
            items.push_back(std::move(it));
        }
        list_->set_items(items);
        const size_t i = static_cast<size_t>(list_->selected());
        ui::set_text(help_, i < fields_.size() ? help_of(fields_[i]) : "");
    }

    bool on_key(const Key& key) override
    {
        if (list_->handle_key(key)) return true;
        if (key.is_char('?')) {
            const size_t i = static_cast<size_t>(list_->selected());
            if (i < fields_.size()) explain(fields_[i]);
            return true;
        }
        if (key.k == K::Esc) {
            if (!dirty_) return false;
            app.push(make_confirm(app, title_, "Save changes?",
                                  {"Save", "Discard", "Keep editing"}, [this](int choice) {
                                      if (choice == 0) {
                                          save_and_close();
                                      } else if (choice == 1) {
                                          app.pop();
                                      }
                                  }));
            return true;
        }
        if (key.k != K::Enter && !key.is_char(' ')) return key.k == K::Left || key.k == K::Right || key.k == K::Tab;
        const size_t i = static_cast<size_t>(list_->selected());
        if (i >= fields_.size()) return true;
        Field& f = fields_[i];
        switch (f.type) {
            case Field::Bool:
                f.set(f.get() ? 0 : 1);
                dirty_ = true;
                break;
            case Field::Enum: {
                // The options are taken now, so that a pick means what was shown.
                auto opts = std::make_shared<std::vector<Opt>>(options(f));
                std::vector<std::string> labels;
                int current = -1;
                for (size_t k = 0; k < opts->size(); ++k) {
                    labels.push_back((*opts)[k].label);
                    if ((*opts)[k].value == f.get()) current = static_cast<int>(k);
                }
                app.push(make_picker(app, f.label, labels, current, [this, i, opts](int choice) {
                    if (choice >= 0 && static_cast<size_t>(choice) < opts->size()) {
                        fields_[i].set((*opts)[static_cast<size_t>(choice)].value);
                        dirty_ = true;
                    }
                }));
                break;
            }
            case Field::Int: {
                std::string help = help_of(f);
                help += (help.empty() ? "" : "\n") + std::string("Range: ") + accepted(f) +
                        (f.unit.empty() ? "" : " " + f.unit);
                // The largest interval often stands for never; say so, as it's typed as a number.
                int64_t lo = 0, hi = 0;
                range(f, lo, hi);
                if (hi >= 2147483647 && !shown_number(f, hi)) help += "\n" + std::to_string(hi) + " = " + shown(f, hi);
                // A value left as it was stays, even one outside the range now.
                const std::string initial = std::to_string(f.get());
                app.push(make_input(
                    app, f.label, initial, 12, help,
                    [this, i, initial](const std::string& text) {
                        Field& field = fields_[i];
                        if (text == initial) return true;
                        char* end         = nullptr;
                        const long long v = strtoll(text.c_str(), &end, 10);
                        if (text.empty() || (end && *end) || !takes(field, v)) {
                            app.toast("Enter " + accepted_sentence(field), true);
                            return false;
                        }
                        field.set(v);
                        dirty_ = true;
                        return true;
                    },
                    "-0123456789"));
                break;
            }
            case Field::Text:
                app.push(make_input(app, f.label, f.get_text(), f.max_len, help_of(f), [this, i](const std::string& text) {
                    const Field& field        = fields_[i];
                    const std::string warning = field.warn_text ? field.warn_text(text) : std::string();
                    if (warning.empty()) return take_text(i, text);
                    // Cancel comes first so a hurried Enter goes back to the input.
                    app.push(make_confirm(app, field.label, warning, {"Cancel", "I understand, use it"},
                                          [this, i, text](int choice) {
                                              if (choice == 1 && take_text(i, text)) app.pop();
                                          }));
                    return false;
                }));
                break;
            case Field::Action:
                if (f.run) f.run();
                if (f.marks_dirty) dirty_ = true;
                break;
            default:
                break;
        }
        return true;
    }

private:
    static std::string help_of(const Field& f) { return f.help_text ? f.help_text() : f.help; }

    // Shows what a field does. The note under the list follows when it says
    // what the values mean, as it does for numbers and text; on the other
    // fields it is a short form of the explanation.
    void explain(const Field& f)
    {
        std::string text       = f.about;
        const std::string note = f.type == Field::Int || f.type == Field::Text ? help_of(f) : std::string();
        if (!note.empty()) text += (text.empty() ? "" : "\n\n") + note;
        if (!text.empty()) app.push(make_text_view(app, f.label, text));
    }

    static std::vector<Opt> options(const Field& f) { return f.opts_of ? f.opts_of() : f.opts; }

    // The bounds an Int field takes now.
    static void range(const Field& f, int64_t& lo, int64_t& hi)
    {
        lo = f.min;
        hi = f.max;
        if (f.bounds) f.bounds(lo, hi);
    }

    static bool takes(const Field& f, int64_t v)
    {
        int64_t lo = 0, hi = 0;
        range(f, lo, hi);
        return (f.zero_ok && v == 0) || (v >= lo && v <= hi);
    }

    // The bounds to tell the user, with 0 joined to a range next to it, and
    // whether 0 is a value of its own outside them.
    static bool told_range(const Field& f, int64_t& lo, int64_t& hi)
    {
        range(f, lo, hi);
        if (!f.zero_ok) return false;
        if (lo == 1) lo = 0;
        if (hi == -1) hi = 0;
        return lo > 0 || hi < 0;
    }

    // Two bounds, written so that a minus sign isn't taken for the dash between them.
    static std::string span(int64_t lo, int64_t hi, const char* dash)
    {
        if (lo == hi) return std::to_string(lo);
        return std::to_string(lo) + (lo < 0 ? " to " : dash) + std::to_string(hi);
    }

    // What an Int field takes, for its input: "0, or 3600-86400".
    static std::string accepted(const Field& f)
    {
        int64_t lo = 0, hi = 0;
        const bool zero_apart = told_range(f, lo, hi);
        return (zero_apart ? "0, or " : "") + span(lo, hi, "-");
    }

    // The same, for a message: "0, or a number from 3600 to 86400".
    static std::string accepted_sentence(const Field& f)
    {
        int64_t lo = 0, hi = 0;
        const bool zero_apart     = told_range(f, lo, hi);
        const std::string numbers = lo == hi ? std::to_string(lo)
                                             : "a number from " + std::to_string(lo) + " to " + std::to_string(hi);
        return (zero_apart ? "0, or " : "") + numbers;
    }

    // The radio firmware keeps "never" in its intervals, all in seconds, as the
    // largest value it allows (CLIENT_HIDDEN, or device metrics by default).
    static bool never(const Field& f, int64_t v) { return f.unit == "s" && v >= 2147483647; }

    // How an Int field's value reads: a word the field gives it, never, or the number.
    static std::string shown(const Field& f, int64_t v)
    {
        if (f.value_text) {
            const std::string text = f.value_text(v);
            if (!text.empty()) return text;
        }
        if (never(f, v)) return "never";
        return std::to_string(v) + (f.unit.empty() ? "" : " " + f.unit);
    }

    static bool shown_number(const Field& f, int64_t v)
    {
        return (!f.value_text || f.value_text(v).empty()) && !never(f, v);
    }

    // Gives a Text field the value typed in; false, with the reason shown, if it refuses it.
    bool take_text(size_t i, const std::string& text)
    {
        const std::string error = fields_[i].set_text(text);
        if (!error.empty()) {
            app.toast(error, true);
            return false;
        }
        dirty_ = true;
        return true;
    }

    static std::string utf8_cut(const std::string& s, size_t n)
    {
        if (s.size() <= n) return s;
        while (n > 0 && (static_cast<uint8_t>(s[n]) & 0xC0) == 0x80) --n;
        return s.substr(0, n);
    }

    void save_and_close()
    {
        if (!mesh.ready()) {
            app.toast("Radio offline. Not saved.", true);
            return;
        }
        if (save_) save_();
        dirty_ = false;
        app.toast(saved_note_.empty() ? "Saved" : saved_note_);
        app.pop();
    }

    std::string title_;
    std::vector<Field> fields_;
    std::function<void()> save_;
    std::string saved_note_;
    std::unique_ptr<ui::ListView> list_;
    lv_obj_t* help_ = nullptr;
    bool dirty_     = false;
};

}  // namespace

std::unique_ptr<Screen> make_editor(App& app, const std::string& title, std::vector<Field> fields,
                                    std::function<void()> save, const std::string& saved_note)
{
    return std::make_unique<EditorScreen>(app, title, std::move(fields), std::move(save), saved_note);
}

}  // namespace mz
