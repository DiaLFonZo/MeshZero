// SPDX-License-Identifier: GPL-3.0-only
// Bluetooth for the phone apps: its settings page and the page that shows the
// code a phone has to type to pair.
#include "ui/app.h"

namespace mz {
namespace {

class BluetoothScreen : public Screen {
public:
    explicit BluetoothScreen(App& a) : Screen(a)
    {
        list_ = std::make_unique<ui::ListView>(root, 0, 0, kContentW, kContentH);
    }
    std::string title() const override { return LV_SYMBOL_LEFT " Bluetooth (early access)"; }
    std::string hint() const override { return "Enter change   ? explain   Esc back"; }

    void refresh() override
    {
        std::vector<ui::ListItem> items;
        const auto row = [&](const std::string& name, const std::string& value, bool setting) {
            ui::ListItem it;
            it.text  = name;
            it.right = ui::clean(value, true);
            if (!setting) {
                it.has_color = true;
                it.color     = ui::c_dim();
            }
            it.has_right_color = true;
            it.right_color     = setting ? ui::c_accent() : ui::c_text();
            items.push_back(std::move(it));
        };
        const bool on = mesh.bluetooth == 1;
        row("Bluetooth", mesh.bluetooth < 0 ? "not available" : on ? "on" : "off", true);
        row("Pairing", mesh.bluetooth_pin == 0 ? "no PIN" : "code on this screen", true);
        row("Name", mesh.bluetooth_name.empty() ? "-" : mesh.bluetooth_name, false);
        std::string status = "off";
        if (on) status = mesh.bluetooth_ok ? "phones can find it" : mesh.bluetooth_why.empty() ? "starting" : "not working";
        row("Status", status, false);
        list_->set_items(items);
    }

    bool on_key(const Key& key) override
    {
        if (list_->handle_key(key)) return true;
        const int i = list_->selected();
        if (key.is_char('?')) {
            std::string text =
                "Lets the Meshtastic phone apps use this radio over Bluetooth, as they would a radio with Bluetooth "
                "of its own. In the phone app, look for the name shown here.\n\n"
                "Pairing with a code: the first time a phone connects, six digits appear on this screen to type on "
                "the phone. MeshZero has to be open for that. Afterwards the phone connects by itself, also while "
                "MeshZero is closed.\n\n"
                "No PIN: any phone nearby can connect and use the radio, change its settings included.\n\n"
                "With Run in background off, the radio starts when a phone connects and stops again after it "
                "leaves.\n\n"
                "This is early access: it has seen little testing with the phone apps so far.";
            if (!mesh.bluetooth_why.empty()) text = "Not working: " + mesh.bluetooth_why + ".\n\n" + text;
            app.push(make_text_view(app, "Bluetooth", text));
            return true;
        }
        if (key.k != K::Enter) return false;
        if (mesh.bluetooth < 0 || !mesh.ready()) {
            app.toast(mesh.bluetooth == -1 ? "This build has no Bluetooth support" : "Radio offline", true);
            return true;
        }
        Mesh* m = &mesh;
        if (i == 0) {
            mesh.set_bluetooth(mesh.bluetooth != 1);
        } else if (i == 1) {
            app.push(make_picker(app, "Pairing", {"Code on this screen", "No PIN"}, mesh.bluetooth_pin == 0 ? 1 : 0,
                                 [m](int choice) {
                                     if (choice == 0 || choice == 1) m->set_bluetooth_pin(choice == 0);
                                 }));
        }
        return true;
    }

private:
    std::unique_ptr<ui::ListView> list_;
};

// Shown while a phone is pairing; goes away by itself once that is over.
class PairingScreen : public Screen {
public:
    explicit PairingScreen(App& a) : Screen(a)
    {
        lv_obj_t* top = ui::label(root, "A phone is pairing over Bluetooth.\nType this code on the phone:", ui::c_text());
        lv_obj_set_width(top, kContentW - 24);
        lv_obj_set_style_text_align(top, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(top, LV_ALIGN_TOP_MID, 0, 18);

        lv_obj_t* box = ui::box(root);
        lv_obj_set_size(box, 150, 34);
        lv_obj_align(box, LV_ALIGN_TOP_MID, 0, 62);
        lv_obj_set_style_radius(box, 4, 0);
        ui::set_bg(box, ui::c_select());
        code_ = ui::label(box, "", ui::c_accent());
        lv_obj_set_style_text_letter_space(code_, 4, 0);
        lv_obj_center(code_);
    }
    std::string title() const override { return LV_SYMBOL_BLUETOOTH " Bluetooth pairing"; }
    std::string hint() const override { return "Esc hide"; }

    void refresh() override
    {
        const std::string& key = mesh.bluetooth_key;
        if (key.empty()) {
            if (!leaving_) {
                leaving_ = true;
                app.pop();
            }
            return;
        }
        ui::set_text(code_, key.size() == 6 ? key.substr(0, 3) + " " + key.substr(3) : key);
        lv_obj_center(code_);
    }

private:
    lv_obj_t* code_ = nullptr;
    bool leaving_   = false;
};

}  // namespace

std::unique_ptr<Screen> make_bluetooth(App& app)
{
    return std::make_unique<BluetoothScreen>(app);
}

std::unique_ptr<Screen> make_bluetooth_pairing(App& app)
{
    return std::make_unique<PairingScreen>(app);
}

}  // namespace mz
