// SPDX-License-Identifier: GPL-3.0-only
// MeshCard home page: big clock and date, this node, and the mesh at a glance.
#include <ctime>

#include "common/util.h"
#include "ui/app.h"
#include "ui/fonts/fonts.h"

namespace mz {
namespace {

constexpr uint32_t kOnlineWindowS = 2 * 3600;  // same as the Nodes tab

class HomeScreen : public Screen {
public:
    explicit HomeScreen(App& a) : Screen(a)
    {
        // Left: clock, date, own node.
        clock_ = ui::label(root, "", ui::c_text(), mz_font(46, true));
        lv_obj_set_pos(clock_, S(10), S(4));
        date_ = ui::label(root, "", ui::c_dim(), mz_font(14, false));
        lv_obj_set_pos(date_, S(12), S(58));
        me_ = ui::label(root, "", ui::c_accent(), mz_font(14, true));
        lv_obj_set_pos(me_, S(12), S(80));
        lv_obj_set_size(me_, kLeftW - S(14), lv_font_get_line_height(mz_font(14, true)));  // one line only
        lv_label_set_long_mode(me_, LV_LABEL_LONG_DOT);
        radio_ = ui::label(root, "", ui::c_dim(), mz_font(12, false));
        lv_obj_set_pos(radio_, S(12), S(100));
        lv_obj_set_size(radio_, kLeftW - S(14), lv_font_get_line_height(mz_font(12, false)));
        lv_label_set_long_mode(radio_, LV_LABEL_LONG_DOT);

        // Right: three tiles (unread, nodes online, last heard).
        const int x = kLeftW, w = kContentW - kLeftW - 4;
        const int h = (kContentH - S(6) - 2 * S(4)) / 3;
        for (int i = 0; i < 3; ++i) {
            Tile& t = tiles_[i];
            t.box   = ui::box(root);
            lv_obj_set_pos(t.box, x, S(3) + i * (h + S(4)));
            lv_obj_set_size(t.box, w, h);
            lv_obj_set_style_radius(t.box, S(8), 0);
            ui::set_bg(t.box, ui::c_bar());
            t.value = ui::label(t.box, "", ui::c_text(), mz_font(20, true));
            lv_obj_align(t.value, LV_ALIGN_LEFT_MID, S(8), 0);
            t.name = ui::label(t.box, "", ui::c_dim(), mz_font(12, false));
            lv_obj_align(t.name, LV_ALIGN_RIGHT_MID, S(-8), 0);
        }
    }

    int tab() const override { return kTabHome; }

    void refresh() override
    {
        const time_t now = time(nullptr);
        tm local{};
        localtime_r(&now, &local);
        char buf[48];
        strftime(buf, sizeof(buf), "%H:%M", &local);
        ui::set_text(clock_, buf);
        strftime(buf, sizeof(buf), "%a %e %b", &local);
        ui::set_text(date_, buf);

        ui::set_text(me_, ui::clean(mesh.short_name(mesh.my_num) + "  " + mesh.long_name(mesh.my_num), true));
        std::string radio = mesh.ready() ? mesh.preset_name() : mesh.offline_text();
        ui::set_text(radio_, ui::clean(radio, true));
        lv_obj_set_style_text_color(radio_, mesh.ready() ? ui::c_dim() : ui::c_error(), 0);

        const int unread = mesh.total_unread();
        int online = 0, total = 0;
        uint32_t last = 0;
        const uint32_t now_u = static_cast<uint32_t>(now);
        for (const auto& [num, n] : mesh.nodes) {
            ++total;
            if (num == mesh.my_num) continue;
            if (n.last_heard && now_u - n.last_heard < kOnlineWindowS) ++online;
            if (n.last_heard > last) last = n.last_heard;
        }
        set_tile(0, LV_SYMBOL_ENVELOPE " " + std::to_string(unread), "new",
                 unread > 0);
        set_tile(1, std::to_string(online), kSmall ? "online" : "nodes online", false);
        set_tile(2, last ? age_str(last, now_u) : "-", kSmall ? "heard" : "last heard", false);
    }

    // A page to look at: no selection. Enter goes to the chats.
    bool on_key(const Key& key) override
    {
        if (key.k == K::Enter) {
            app.switch_tab(kTabChats);
            return true;
        }
        return false;
    }

private:
    static constexpr int kLeftW = S(168);

    struct Tile {
        lv_obj_t* box   = nullptr;
        lv_obj_t* value = nullptr;
        lv_obj_t* name  = nullptr;
    };

    void set_tile(int i, const std::string& value, const std::string& name, bool hot)
    {
        Tile& t = tiles_[i];
        ui::set_text(t.value, value);
        ui::set_text(t.name, name);
        // A tile with news turns green, like the selected rows.
        ui::set_bg(t.box, hot ? ui::c_accent() : ui::c_bar());
        lv_obj_set_style_text_color(t.value, hot ? ui::c_bg() : ui::c_text(), 0);
        lv_obj_set_style_text_color(t.name, hot ? ui::c_bg() : ui::c_dim(), 0);
    }

    lv_obj_t* clock_ = nullptr;
    lv_obj_t* date_  = nullptr;
    lv_obj_t* me_    = nullptr;
    lv_obj_t* radio_ = nullptr;
    Tile tiles_[3];
};

}  // namespace

std::unique_ptr<Screen> make_home(App& app)
{
    return std::make_unique<HomeScreen>(app);
}

}  // namespace mz
