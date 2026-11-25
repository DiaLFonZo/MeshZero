// SPDX-License-Identifier: GPL-3.0-only
// Application shell: top bar, hint bar, toast and a stack of screens.
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "lvgl.h"
#include "ui/display.h"
#include "ui/keys.h"
#include "ui/mesh.h"
#include "ui/ui.h"

namespace mz {

// MeshCard layout (Cardputer Zero 320x170): status line, tab row (or the
// page title on sub-pages), then the page. No hint bar: '?' shows the keys.
constexpr int kStatusH  = S(16);
constexpr int kTabRowH  = S(21);
constexpr int kBarH     = kStatusH + kTabRowH;
constexpr int kHintH    = 0;
constexpr int kContentW = kScreenW;
constexpr int kContentH = kScreenH - kBarH - kHintH;

enum Tab { kTabChats = 0, kTabNodes, kTabMap, kTabRadio, kTabSetup, kTabHome, kTabCount };

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
    // Goes up by one for every new-message popup, so the screen can wake for it.
    uint32_t wake_count() const { return wake_count_; }
    // Self-test (ESP32 test builds): which tab is on top and how many pages deep.
    int top_tab() const { return stack_.empty() ? -1 : stack_.back()->tab(); }
    size_t depth() const { return stack_.size(); }
    // True once after a short Esc press on Home: the screen goes dark right away.
    bool take_screen_off_request()
    {
        const bool r   = screen_off_req_;
        screen_off_req_ = false;
        return r;
    }
    lv_obj_t* content() const { return content_; }

    Mesh& mesh;

private:
    void apply_pending();
    void update_chrome();
    void show_top();
    Screen* top() const { return stack_.empty() ? nullptr : stack_.back().get(); }

    lv_obj_t* bar_      = nullptr;
    lv_obj_t* tabrow_   = nullptr;
    lv_obj_t* who_      = nullptr;  // own short name + unread count
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
    lv_obj_t* exit_pop_   = nullptr;  // MeshCard: centre popup while Esc is held
    lv_obj_t* exit_bar_   = nullptr;
    uint64_t esc_held_ms_ = 0;  // since when Esc is held on a home page, 0 if it is not
    bool screen_off_req_  = false;
    // MeshCard: centre popup for a new message; stays until a key is pressed.
    void show_message(const UiEvent& ev);
    lv_obj_t* msg_pop_  = nullptr;
    lv_obj_t* msg_head_ = nullptr;
    lv_obj_t* msg_text_ = nullptr;
    ChatKey msg_chat_;
    int msg_count_ = 0;  // messages since the popup was last closed
    uint32_t wake_count_ = 0;
    bool quit_          = false;
    bool region_prompted_ = false;
};

// ---- screen factories (src/ui/screens) ------------------------------------
std::unique_ptr<Screen> make_tab(App& app, int tab);
std::unique_ptr<Screen> make_chats(App& app);
std::unique_ptr<Screen> make_home(App& app);
std::unique_ptr<Screen> make_nodes(App& app);
std::unique_ptr<Screen> make_map(App& app);
std::unique_ptr<Screen> make_radio(App& app);
std::unique_ptr<Screen> make_setup(App& app);
std::unique_ptr<Screen> make_chat(App& app, const ChatKey& key);
std::unique_ptr<Screen> make_node_detail(App& app, uint32_t num);
std::unique_ptr<Screen> make_packet_log(App& app);
std::unique_ptr<Screen> make_region_picker(App& app, bool first_run);
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
