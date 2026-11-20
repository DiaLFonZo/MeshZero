// SPDX-License-Identifier: GPL-3.0-only
// Application shell: top bar, hint bar, toast and a stack of screens.
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "lvgl.h"
#include "ui/keys.h"
#include "ui/mesh.h"
#include "ui/ui.h"

namespace mz {

constexpr int kBarH     = 18;
constexpr int kHintH    = 13;
constexpr int kContentW = 320;
constexpr int kContentH = 170 - kBarH - kHintH;

enum Tab { kTabChats = 0, kTabNodes, kTabMap, kTabRadio, kTabSetup, kTabCount };

class App;

class Screen {
public:
    explicit Screen(App& app);
    virtual ~Screen();
    Screen(const Screen&)            = delete;
    Screen& operator=(const Screen&) = delete;

    virtual std::string title() const { return ""; }
    virtual int tab() const { return -1; }  // >= 0: one of the home tabs
    virtual std::string hint() const { return ""; }
    virtual void on_show() {}
    virtual void on_hide() {}
    virtual void refresh() {}  // a few times per second while visible
    virtual bool on_key(const Key&) { return false; }
    // True while the screen is taking typed text. Elsewhere the keys that carry
    // the arrow legends (F, X, Z, C) work as arrows without holding Fn.
    virtual bool wants_text() const { return false; }

    lv_obj_t* root = nullptr;

protected:
    App& app;
    Mesh& mesh;
};

class App {
public:
    explicit App(Mesh& mesh);
    ~App();

    void start();
    void handle_key(const Key& key);
    void tick();

    // Stack changes are applied after the current key/tick has been handled,
    // so a screen can safely pop itself.
    void push(std::unique_ptr<Screen> screen);
    void pop(int count = 1);
    void switch_tab(int tab);
    void open_chat(const ChatKey& key);

    void toast(const std::string& text, bool warn = false);
    // One-line notice in the top bar; does not cover the page.
    void notify(const std::string& text);
    void quit() { quit_ = true; }
    bool quit_requested() const { return quit_; }
    lv_obj_t* content() const { return content_; }

    Mesh& mesh;

private:
    void apply_pending();
    void update_chrome();
    void show_top();
    Screen* top() const { return stack_.empty() ? nullptr : stack_.back().get(); }

    lv_obj_t* bar_      = nullptr;
    lv_obj_t* title_    = nullptr;
    lv_obj_t* tabs_[kTabCount]{};
    lv_obj_t* status_   = nullptr;
    lv_obj_t* content_  = nullptr;
    lv_obj_t* hint_     = nullptr;
    lv_obj_t* toast_    = nullptr;
    lv_obj_t* toast_label_ = nullptr;
    uint64_t toast_until_  = 0;
    lv_obj_t* notice_      = nullptr;
    uint64_t notice_until_ = 0;

    std::vector<std::unique_ptr<Screen>> stack_;
    std::vector<std::function<void()>> pending_;
    Screen* shown_      = nullptr;
    uint64_t last_tick_ = 0;
    uint64_t esc_held_ms_ = 0;  // since when Esc is held on a home page, 0 if it is not
    bool quit_          = false;
    bool region_prompted_ = false;
    bool background_prompted_ = false;
    int battery_ = -1;              // percent, -1 without a battery
    bool charging_ = false;
    uint64_t battery_read_ms_ = 0;  // when it was last read, 0 if never
};

// ---- screen factories (src/ui/screens) ------------------------------------
std::unique_ptr<Screen> make_tab(App& app, int tab);
std::unique_ptr<Screen> make_chats(App& app);
std::unique_ptr<Screen> make_nodes(App& app);
std::unique_ptr<Screen> make_map(App& app);
std::unique_ptr<Screen> make_radio(App& app);
std::unique_ptr<Screen> make_setup(App& app);
std::unique_ptr<Screen> make_chat(App& app, const ChatKey& key);
std::unique_ptr<Screen> make_node_detail(App& app, uint32_t num);
std::unique_ptr<Screen> make_packet_log(App& app);
std::unique_ptr<Screen> make_region_picker(App& app, bool first_run);
// Whether MeshZero offers a region: the cap and the radio firmware can use it.
bool region_supported(int region);
std::unique_ptr<Screen> make_channels(App& app);
std::unique_ptr<Screen> make_help(App& app);
// Scrollable text ending in the credit line and a QR code for the link.
std::unique_ptr<Screen> make_about_view(App& app, const std::string& title, const std::string& text,
                                        const std::string& link);

// ---- generic dialogs (dialogs.cpp) ----------------------------------------
std::unique_ptr<Screen> make_text_view(App& app, const std::string& title, const std::string& text);
std::unique_ptr<Screen> make_confirm(App& app, const std::string& title, const std::string& message,
                                     const std::vector<std::string>& options, std::function<void(int)> on_choice);
std::unique_ptr<Screen> make_picker(App& app, const std::string& title, const std::vector<std::string>& options,
                                    int current, std::function<void(int)> on_pick);
std::unique_ptr<Screen> make_input(App& app, const std::string& title, const std::string& initial, size_t max_bytes,
                                   const std::string& help, std::function<bool(const std::string&)> on_accept,
                                   const char* accept_chars = nullptr);
std::unique_ptr<Screen> make_qr(App& app, const std::string& title, const std::string& data);
// Camera view that looks for a QR code (scan.cpp). on_code gets the text of
// each code seen and returns true once it has taken over, false to keep looking.
std::unique_ptr<Screen> make_scanner(App& app, const std::string& title,
                                     std::function<bool(const std::string&)> on_code);

}  // namespace mz
