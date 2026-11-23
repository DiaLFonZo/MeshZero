// SPDX-License-Identifier: GPL-3.0-only
// How other apps reach the radio, over Wi-Fi or Bluetooth: its settings page,
// and the page that shows the code a phone has to type to pair.
#include "ui/app.h"

namespace mz {
namespace {

class AppConnectionScreen : public Screen {
public:
    explicit AppConnectionScreen(App& a) : Screen(a)
    {
        list_ = std::make_unique<ui::ListView>(root, 0, 0, kContentW, kContentH);
    }
    std::string title() const override { return LV_SYMBOL_LEFT " App connection"; }
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
        const int mode = mesh.app_connection();
        row("Connection", mode == Mesh::kAppWifi ? "Wi-Fi" : mode == Mesh::kAppBluetooth ? "Bluetooth (early access)" : "off",
            true);
        pairing_row_ = forget_row_ = -1;
        if (mode == Mesh::kAppWifi) {
            const std::vector<std::string> addrs = local_addresses();
            if (addrs.empty()) row("Address", "no network", false);
            for (const auto& a : addrs) row("Address", a + ":4403", false);
            row("Status", mesh.network_conn > 0 ? std::to_string(mesh.network_conn) + " connected" : "nothing connected",
                false);
        } else if (mode == Mesh::kAppBluetooth) {
            pairing_row_ = static_cast<int>(items.size());
            row("Pairing", mesh.bluetooth_pin == 0 ? "no PIN" : "code on this screen", true);
            row("Name", mesh.bluetooth_name.empty() ? "-" : mesh.bluetooth_name, false);
            std::string status = mesh.bluetooth_why.empty() ? "starting" : "not working";
            if (mesh.bluetooth_conn > 0) {
                status = mesh.bluetooth_paired > 0 ? "paired, connected" : "connected";
            } else if (mesh.bluetooth_ok) {
                status = mesh.bluetooth_paired > 0 ? "paired, not connected" : "ready to pair";
            }
            row("Status", status, false);
            if (mesh.bluetooth_conn > 0 && !mesh.bluetooth_peer.empty()) row("Phone", mesh.bluetooth_peer, false);
            if (mesh.bluetooth_paired > 0) {
                forget_row_ = static_cast<int>(items.size());
                ui::ListItem it;
                it.text      = mesh.bluetooth_paired == 1 ? "Forget the paired phone" : "Forget the paired phones";
                it.has_color = true;
                it.color     = ui::c_accent();
                items.push_back(std::move(it));
            }
        }
        list_->set_items(items);
    }

    bool on_key(const Key& key) override
    {
        if (list_->handle_key(key)) return true;
        const int i = list_->selected();
        if (key.is_char('?')) {
            std::string text =
                "How other apps use this radio. Only one way is on at a time.\n\n"
                "Wi-Fi: for MeshMonitor, the meshtastic command or a phone app on your network, through port 4403 at "
                "the address shown. There is no password, so use it only on networks you trust.\n\n"
                "Bluetooth (early access): for the Meshtastic phone apps when there is no Wi-Fi to share. In the "
                "phone app, not the phone's own Bluetooth settings, look for the name shown here. It has seen little "
                "testing with the phone apps so far.\n\n"
                "Pairing with a code: the first time a phone connects, six digits appear on this screen to type on "
                "the phone. MeshZero has to be open for that. Afterwards the phone connects by itself, also while "
                "MeshZero is closed.\n\n"
                "No PIN: any phone nearby can connect and use the radio, change its settings included.\n\n"
                "With Run in background off, the radio starts when an app connects and stops again after it leaves.";
            if (mesh.app_connection() == Mesh::kAppBluetooth && !mesh.bluetooth_why.empty()) {
                text = "Bluetooth is not working: " + mesh.bluetooth_why + ".\n\n" + text;
            }
            app.push(make_text_view(app, "App connection", text));
            return true;
        }
        if (key.k != K::Enter) return false;
        if (mesh.app_connection() < 0 || !mesh.ready()) {
            app.toast("Radio offline", true);
            return true;
        }
        Mesh* m = &mesh;
        App* a  = &app;
        if (i == 0) {
            const bool bt = mesh.bluetooth >= 0;
            std::vector<std::string> options = {"Off", "Wi-Fi"};
            if (bt) options.push_back("Bluetooth (early access)");
            app.push(make_picker(app, "App connection", options, mesh.app_connection(), [m, a, bt](int choice) {
                if (choice < 0 || choice > (bt ? 2 : 1)) return;
                m->set_app_connection(choice);
                if (choice == Mesh::kAppBluetooth) a->toast("Look for this device in the Meshtastic phone app");
            }));
        } else if (i == forget_row_) {
            app.push(make_confirm(app, "Forget paired phones",
                                  "The phone will have to pair again with a new code. On the phone, forget this "
                                  "device too (in its Bluetooth settings), or it won't be able to connect.",
                                  {"Cancel", "Forget"}, [m, a](int choice) {
                                      if (choice != 1) return;
                                      m->forget_bluetooth_phones();
                                      a->toast("Pairing forgotten. Forget this device on the phone too.");
                                  }));
        } else if (i == pairing_row_) {
            app.push(make_picker(app, "Pairing", {"Code on this screen", "No PIN"}, mesh.bluetooth_pin == 0 ? 1 : 0,
                                 [m](int choice) {
                                     if (choice == 0 || choice == 1) m->set_bluetooth_pin(choice == 0);
                                 }));
        }
        return true;
    }

private:
    std::unique_ptr<ui::ListView> list_;
    int pairing_row_ = -1;
    int forget_row_  = -1;
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

std::unique_ptr<Screen> make_app_connection(App& app)
{
    return std::make_unique<AppConnectionScreen>(app);
}

std::unique_ptr<Screen> make_bluetooth_pairing(App& app)
{
    return std::make_unique<PairingScreen>(app);
}

}  // namespace mz
