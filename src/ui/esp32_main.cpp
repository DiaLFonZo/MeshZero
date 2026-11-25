// SPDX-License-Identifier: GPL-3.0-only
// MeshCard on the Cardputer ADV (ESP32-S3): runs the MeshCard screens as a task
// inside the Meshtastic firmware. Draws on the built-in ST7789 (240x135), reads
// the TCA8418 keyboard and uses the radio firmware's own data and mesh service
// directly (the mc_* functions, see ui/mesh.cpp and src/meshcard/LocalUiAPI.cpp).
#if defined(MESHCARD_ESP32)

#include <Arduino.h>
#include <Wire.h>
#include <driver/gpio.h>
#include <driver/spi_master.h>
#include <esp_heap_caps.h>

#include <deque>
#include <string>

#include "common/alert.h"
#include "common/util.h"
#include "lvgl.h"
#include "ui/app.h"
#include "ui/battery.h"
#include "ui/camera.h"
#include "ui/display.h"
#include "ui/fonts/fonts.h"
#include "ui/keys.h"
#include "ui/mesh.h"
#include "ui/update.h"

using namespace mz;

// Firmware hook (src/meshcard/LocalUiAPI.cpp): true once the screens may start.
extern "C" __attribute__((weak)) bool mc_platform_ready()
{
    return true;
}

namespace {

// ------------------------------------------------------------------ display

constexpr int kPinMosi = 35;
constexpr int kPinSck  = 36;
constexpr int kPinCs   = 37;
constexpr int kPinDc   = 34;
constexpr int kPinRst  = 33;
constexpr int kPinBl   = 38;
// The 135x240 panel sits in the middle of the ST7789's 240x320 memory.
constexpr int kOffX = 40;
constexpr int kOffY = 53;  // 52 left the bottom row unwritten (garbled line)
constexpr int kBufRows = 12;

spi_device_handle_t g_spi = nullptr;

void lcd_send(bool data, const void* buf, size_t len)
{
    if (len == 0) return;
    gpio_set_level(static_cast<gpio_num_t>(kPinDc), data ? 1 : 0);
    const uint8_t* p = static_cast<const uint8_t*>(buf);
    while (len > 0) {
        const size_t n = len > 4092 ? 4092 : len;
        spi_transaction_t t{};
        t.length    = n * 8;
        t.tx_buffer = p;
        spi_device_polling_transmit(g_spi, &t);
        p += n;
        len -= n;
    }
}

void lcd_cmd(uint8_t c)
{
    lcd_send(false, &c, 1);
}

void lcd_data(std::initializer_list<uint8_t> d)
{
    uint8_t buf[8];
    size_t n = 0;
    for (uint8_t b : d) buf[n++] = b;
    lcd_send(true, buf, n);
}

bool lcd_init()
{
    spi_bus_config_t bus{};
    bus.mosi_io_num     = kPinMosi;
    bus.miso_io_num     = -1;
    bus.sclk_io_num     = kPinSck;
    bus.quadwp_io_num   = -1;
    bus.quadhd_io_num   = -1;
    bus.max_transfer_sz = 4096;
    // SPI2 is the firmware's (Arduino SPI); the screen gets SPI3.
    if (spi_bus_initialize(SPI3_HOST, &bus, SPI_DMA_CH_AUTO) != ESP_OK) return false;
    spi_device_interface_config_t dev{};
    dev.clock_speed_hz = 40 * 1000 * 1000;
    dev.mode           = 0;
    dev.spics_io_num   = kPinCs;
    dev.queue_size     = 2;
    if (spi_bus_add_device(SPI3_HOST, &dev, &g_spi) != ESP_OK) return false;

    pinMode(kPinDc, OUTPUT);
    pinMode(kPinRst, OUTPUT);
    digitalWrite(kPinRst, LOW);
    delay(20);
    digitalWrite(kPinRst, HIGH);
    delay(120);

    lcd_cmd(0x01);  // software reset
    delay(150);
    lcd_cmd(0x11);  // out of sleep
    delay(120);
    lcd_cmd(0x3A);  // 16-bit colour
    lcd_data({0x55});
    lcd_cmd(0x36);  // landscape, same way round as the stock firmware
    lcd_data({0x60});
    lcd_cmd(0x21);  // inverted (this panel needs it)
    lcd_cmd(0x13);  // normal display
    // Clear the whole panel memory, so no edge row ever shows leftover noise.
    {
        uint8_t black[240 * 2] = {};  // on the stack: internal RAM, which the SPI DMA can read
        lcd_cmd(0x2A);
        lcd_data({0, 0, 0x01, 0x3F});  // 0..319
        lcd_cmd(0x2B);
        lcd_data({0, 0, 0, 0xEF});  // 0..239
        lcd_cmd(0x2C);
        for (int i = 0; i < 320; ++i) lcd_send(true, black, sizeof(black));  // 240 rows x 320 cols
    }
    lcd_cmd(0x29);  // display on
    delay(20);
    return true;
}

void lcd_flush(lv_display_t* disp, const lv_area_t* area, uint8_t* px)
{
    const int x1 = area->x1 + kOffX, x2 = area->x2 + kOffX;
    const int y1 = area->y1 + kOffY, y2 = area->y2 + kOffY;
    const size_t count = static_cast<size_t>(lv_area_get_width(area)) * lv_area_get_height(area);
    lv_draw_sw_rgb565_swap(px, count);  // the panel wants the high byte first
    lcd_cmd(0x2A);
    lcd_data({static_cast<uint8_t>(x1 >> 8), static_cast<uint8_t>(x1), static_cast<uint8_t>(x2 >> 8),
              static_cast<uint8_t>(x2)});
    lcd_cmd(0x2B);
    lcd_data({static_cast<uint8_t>(y1 >> 8), static_cast<uint8_t>(y1), static_cast<uint8_t>(y2 >> 8),
              static_cast<uint8_t>(y2)});
    lcd_cmd(0x2C);
    lcd_send(true, px, count * 2);
    lv_display_flush_ready(disp);
}

uint32_t tick_cb()
{
    return static_cast<uint32_t>(millis());
}

// Screen light: dims after 30 s without a key, goes dark after 60 s. Any key or
// a new message lights it again; the key that wakes a dark screen does nothing else.
class ScreenLight {
public:
    static constexpr uint32_t kDimMs = 30000;
    static constexpr uint32_t kOffMs = 60000;

    void begin()
    {
        set(kFull);
        last_ = millis();
    }

    bool key(const Key& k)
    {
        const uint32_t now = millis();
        last_              = now;
        if (eating_) {
            if (k.release || now > eat_until_) eating_ = false;
            if (now <= eat_until_) return false;
        }
        // Dimmed counts as asleep too: dim is nearly black on the ADV, and an Esc
        // that reached Home would turn the screen off (Esc had to be pressed twice).
        if (level_ != kFull && !k.release) {
            set(kFull);
            forced_off_ = false;
            eating_     = true;
            eat_until_  = now + 1000;
            return false;
        }
        if (!forced_off_) set(kFull);
        return true;
    }

    // Esc on Home: dark now, until the next key or message.
    void turn_off()
    {
        forced_off_ = true;
        set(kOff);
    }

    // Plugged in: stays fully lit (unless turned off with Esc).
    void tick(uint32_t wake_count, bool plugged)
    {
        const uint32_t now = millis();
        // The USB "plugged" sign flickers while a computer is attached; each flicker used to
        // count as a plug-in and lit the screen after Esc (2026-11-22). A change now counts
        // only once it has held for 5 s.
        if (plugged != was_plugged_) {
            if (!plug_since_) plug_since_ = now ? now : 1;
            if (now - plug_since_ < 5000) plugged = was_plugged_;
        } else {
            plug_since_ = 0;
        }
        // A new message, or plugging in / unplugging, lights the screen again.
        if (wake_count != seen_wake_ || plugged != was_plugged_) {
            seen_wake_   = wake_count;
            was_plugged_ = plugged;
            plug_since_  = 0;
            last_        = now;
            forced_off_  = false;
        }
        if (forced_off_) return set(kOff);
        if (plugged) return set(kFull);
        const uint32_t idle = now - last_;
        set(idle >= kOffMs ? kOff : idle >= kDimMs ? kDim : kFull);
    }

private:
    static constexpr int kFull = 255, kDim = 40, kOff = 0;

    void set(int level)
    {
        if (level == level_) return;
        level_ = level;
        analogWrite(kPinBl, level);
    }

    bool eating_      = false, forced_off_ = false, was_plugged_ = false;
    int level_        = -1;
    uint32_t last_    = 0, eat_until_ = 0, plug_since_ = 0;
    uint32_t seen_wake_ = 0;
};

// ------------------------------------------------------------------ keyboard

// TCA8418 key matrix (7 rows x 8 columns), same layout the stock firmware uses
// (src/input/CardputerKeyboard.cpp): {plain, shift, fn}.
constexpr uint8_t kAddr = 0x34;
constexpr int kNumKeys  = 56;
constexpr int kFnKey = 2, kCtrlKey = 3, kShiftKey = 6, kOptKey = 7, kAltKey = 11;
constexpr int kTabKey = 1, kBspKey = 52, kEnterKey = 54, kEscKey = 0;

// 0 = no character. Fn layer is handled in code.
const char kMap[kNumKeys][2] = {
    {'`', '~'}, {0, 0},     {0, 0},     {0, 0},     {'1', '!'}, {'q', 'Q'}, {0, 0},     {0, 0},
    {'2', '@'}, {'w', 'W'}, {'a', 'A'}, {0, 0},     {'3', '#'}, {'e', 'E'}, {'s', 'S'}, {'z', 'Z'},
    {'4', '$'}, {'r', 'R'}, {'d', 'D'}, {'x', 'X'}, {'5', '%'}, {'t', 'T'}, {'f', 'F'}, {'c', 'C'},
    {'6', '^'}, {'y', 'Y'}, {'g', 'G'}, {'v', 'V'}, {'7', '&'}, {'u', 'U'}, {'h', 'H'}, {'b', 'B'},
    {'8', '*'}, {'i', 'I'}, {'j', 'J'}, {'n', 'N'}, {'9', '('}, {'o', 'O'}, {'k', 'K'}, {'m', 'M'},
    {'0', ')'}, {'p', 'P'}, {'l', 'L'}, {',', '<'}, {'_', '-'}, {'[', '{'}, {';', ':'}, {'.', '>'},
    {'=', '+'}, {']', '}'}, {'\'', '"'}, {'/', '?'}, {0, 0},    {'\\', '|'}, {0, 0},    {' ', ' '},
};

class Keyboard {
public:
    bool begin()
    {
        Wire.beginTransmission(kAddr);
        if (Wire.endTransmission() != 0) return false;
        for (uint8_t r = 0x23; r <= 0x25; ++r) write(r, 0x00);  // GPIO_DIR: inputs
        for (uint8_t r = 0x20; r <= 0x22; ++r) write(r, 0xFF);  // GPI_EM: key events
        for (uint8_t r = 0x26; r <= 0x28; ++r) write(r, 0x00);  // INT_LVL
        for (uint8_t r = 0x1A; r <= 0x1C; ++r) write(r, 0xFF);  // GPIO_INT_EN
        write(0x1D, 0x7F);                                      // KP_GPIO_1: 7 rows
        write(0x1E, 0xFF);                                      // KP_GPIO_2: 8 columns
        write(0x1F, 0x00);
        write(0x01, read(0x01) | 0x01);  // CFG: key event interrupt (status only, nobody listens)
        for (int i = 0; i < 16 && read(0x04) != 0; ++i) {
        }
        write(0x02, 0x03);
        ok_ = true;
        return true;
    }

    void poll(std::deque<Key>& out)
    {
        if (!ok_) return;
        const uint8_t count = read(0x03) & 0x0F;
        for (uint8_t i = 0; i < count; ++i) {
            const uint8_t ev = read(0x04);
            if (ev == 0) break;
            const uint8_t code = ev & 0x7F;
            const int row = (code - 1) / 10, col = (code - 1) % 10;
            if (row >= 7 || col >= 8) continue;
            event(row * 8 + col, (ev & 0x80) != 0, out);
        }
        if (count) write(0x02, 0x03);
        // Auto-repeat for a held key (arrows, backspace, letters).
        const uint32_t now = millis();
        if (held_ >= 0 && now >= next_repeat_) {
            next_repeat_ = now + 70;
            Key k        = held_key_;
            k.repeat     = true;
            out.push_back(k);
        }
    }

private:
    void event(int idx, bool down, std::deque<Key>& out)
    {
        switch (idx) {
            case kFnKey: fn_ = down; return;
            case kShiftKey: shift_ = down; return;
            case kCtrlKey: ctrl_ = down; return;
            case kOptKey:
            case kAltKey: return;
            default: break;
        }
        if (!down) {
            if (idx == held_) {
                if (held_key_.k == K::Esc) {
                    Key k     = held_key_;
                    k.release = true;
                    out.push_back(k);
                }
                held_ = -1;
            }
            return;
        }
        Key k;
        if (idx == kEscKey && !shift_) {
            k.k = K::Esc;  // ` is Esc, with or without Fn (Shift+` types ~)
        } else if (idx == kTabKey) {
            k.k = K::Tab;
        } else if (idx == kBspKey) {
            k.k = fn_ ? K::Delete : K::Backspace;
        } else if (idx == kEnterKey) {
            k.k = K::Enter;
        } else if (fn_ && idx == 46) {
            k.k = K::Up;
        } else if (fn_ && idx == 47) {
            k.k = K::Down;
        } else if (fn_ && idx == 43) {
            k.k = K::Left;
        } else if (fn_ && idx == 51) {
            k.k = K::Right;
        } else if (kMap[idx][0]) {
            char c = kMap[idx][shift_ ? 1 : 0];
            k.k    = K::Char;
            k.ch   = static_cast<uint8_t>(c);
            if (ctrl_) {
                k.ctrl = true;
                k.ch   = static_cast<uint8_t>(kMap[idx][0]);
            }
        } else {
            return;
        }
        out.push_back(k);
        held_        = idx;
        held_key_    = k;
        next_repeat_ = millis() + 450;
    }

    void write(uint8_t reg, uint8_t v)
    {
        Wire.beginTransmission(kAddr);
        Wire.write(reg);
        Wire.write(v);
        Wire.endTransmission();
    }

    uint8_t read(uint8_t reg)
    {
        Wire.beginTransmission(kAddr);
        Wire.write(reg);
        if (Wire.endTransmission(false) != 0) return 0;
        if (Wire.requestFrom(kAddr, static_cast<uint8_t>(1)) != 1) return 0;
        return static_cast<uint8_t>(Wire.read());
    }

    bool ok_ = false, fn_ = false, shift_ = false, ctrl_ = false;
    int held_ = -1;
    Key held_key_;
    uint32_t next_repeat_ = 0;
};

// ------------------------------------------------------------- self-test
// The build script writes ui/selftest_on.h each build: "#define MC_SELFTEST 1" for
// a test build, empty otherwise. Included (not just tested) so the build notices
// when its content changes and recompiles this file.
#if __has_include("ui/selftest_on.h")
#include "ui/selftest_on.h"
#endif
#if defined(MC_SELFTEST)
// Test builds only: walks every tab, opens every row (Enter), scrolls the page
// it opened, then backs out with Esc, so the USB log shows memory on every page
// without anyone pressing keys. Esc never applies a choice, and nothing is
// pressed inside an opened page except Up/Down, so no setting changes.
class SelfTest {
public:
    void tick(App& app, std::deque<Key>& keys)
    {
        const uint32_t now = millis();
        if (now < next_ || done_) return;
        next_ = now + 350;
        if (now < 25000) return;  // let the radio and Bluetooth settle first
        // Home is left out: its rows only jump to the other tabs, tested here anyway.
        static const int order[] = {kTabSetup, kTabRadio, kTabChats, kTabNodes, kTabMap};
        if (tab_i_ >= 5) {  // one round done: start again (runs until the next flash)
            Serial.printf("MeshCard: SELFTEST ROUND %d DONE\n", ++round_);
            tab_i_ = 0;
            row_   = 0;
            step_  = 0;
            return;
        }
        const int want = order[tab_i_];
        const auto press = [&](K k) {
            Key key;
            key.k = k;
            keys.push_back(key);
        };
        switch (step_) {
            case 0:  // get to the tab, one page deep
                if (app.depth() > 1) {
                    press(K::Esc);
                } else if (app.top_tab() != want) {
                    press(K::Tab);  // Right pans the map; Tab always changes tab
                } else {
                    Serial.printf("MeshCard: SELFTEST tab %d row %d\n", want, row_);
                    // Back after an Enter that switched tab: go down to where we were.
                    for (int i = 0; i < row_; ++i) press(K::Down);
                    step_ = 1;
                }
                return;
            case 1:  // open the current row
                if (row_ >= 40) {
                    ++tab_i_;
                    row_  = 0;
                    step_ = 0;
                    return;
                }
                press(K::Enter);
                step_ = 2;
                return;
            case 2:  // something opened: scroll it down and back up
                if (app.depth() > 1) {
                    for (int i = 0; i < 12; ++i) press(K::Down);
                    for (int i = 0; i < 12; ++i) press(K::Up);
                    step_ = 3;
                } else {
                    step_ = 4;
                }
                return;
            case 3:  // back out
                if (app.depth() > 1) {
                    press(K::Esc);
                    return;
                }
                step_ = 4;
                return;
            case 4:  // next row (if Enter switched tab, step 0 brings us back)
                if (app.top_tab() != want) {
                    step_ = 0;
                    ++row_;
                    return;
                }
                press(K::Down);
                ++row_;
                step_ = 1;
                return;
        }
    }

private:
    uint32_t next_ = 0;
    int tab_i_     = 0;
    int step_      = 0;
    int row_       = 0;
    int round_     = 0;
    bool done_     = false;
};
#endif

// ------------------------------------------------------------------ task

void ui_task(void*)
{
    lv_init();
    lv_tick_set_cb(tick_cb);

    if (!lcd_init()) {
        Serial.println("MeshCard: screen setup failed");
        vTaskDelete(nullptr);
        return;
    }
    const size_t buf_bytes = kScreenW * kBufRows * 2;
    void* buf              = heap_caps_malloc(buf_bytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    lv_display_t* disp     = lv_display_create(kScreenW, kScreenH);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(disp, buf, nullptr, buf_bytes, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(disp, lcd_flush);

    ScreenLight light;
    light.begin();

    // Splash while the radio and Bluetooth start (the screens wait for them: memory).
    {
        lv_obj_t* scr = lv_screen_active();
        lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
        lv_obj_t* name = lv_label_create(scr);
        lv_label_set_text(name, "MeshCard");
        lv_obj_set_style_text_color(name, lv_color_white(), 0);
        lv_obj_set_style_text_font(name, mz_font(20, true), 0);
        lv_obj_align(name, LV_ALIGN_CENTER, 0, -10);
        lv_obj_t* sub = lv_label_create(scr);
        lv_label_set_text(sub, "starting...");
        lv_obj_set_style_text_color(sub, lv_color_hex(0x8a8a8a), 0);
        lv_obj_set_style_text_font(sub, mz_font(12, false), 0);
        lv_obj_align(sub, LV_ALIGN_CENTER, 0, 14);
        while (!mc_platform_ready()) {
            lv_timer_handler();
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        lv_obj_delete(sub);
        lv_obj_delete(name);
    }
    Keyboard keyboard;
    if (!keyboard.begin()) Serial.println("MeshCard: keyboard not found");

    static Mesh mesh;
    static App app(mesh);
    Serial.printf("MeshCard: free heap before screens %u\n", (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    app.start();

    std::deque<Key> keys;
    uint32_t next_heap_log = 0;
#if defined(MC_SELFTEST)
    SelfTest selftest;
#endif
    for (;;) {
#if defined(MC_SELFTEST)
        selftest.tick(app, keys);
#endif
        if (millis() >= next_heap_log) {  // memory check, readable on the USB log
            Serial.printf("MeshCard: free heap %u, lowest %u, largest block %u\n",
                          (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                          (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
                          (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
            next_heap_log = millis() + 30000;
        }
        mesh.poll();
        keyboard.poll(keys);
        while (!keys.empty()) {
            const Key k = keys.front();
            keys.pop_front();
            alert_key();  // any key stops the alarm, as on the Zero
            if (light.key(k)) app.handle_key(k);
        }
        alert_tick();
        app.tick();
        if (app.take_screen_off_request()) light.turn_off();
        light.tick(app.wake_count(), battery_info().charging);
        const uint32_t idle = lv_timer_handler();
        vTaskDelay(pdMS_TO_TICKS(idle < 5 ? 5 : idle > 20 ? 20 : idle));
    }
}

}  // namespace

// Called once by the firmware (meshcardSetup in src/meshcard/LocalUiAPI.cpp).
void meshcard_ui_start()
{
    xTaskCreatePinnedToCore(ui_task, "meshcard", 16384, nullptr, 1, nullptr, 0);
}

// ---------------------------------------------------------------- stand-ins
// Linux-only parts of MeshCard that the ADV build leaves out.

namespace mz {

bool Camera::available()
{
    return false;
}

std::unique_ptr<Screen> make_scanner(App&, const std::string&, std::function<bool(const std::string&)>)
{
    return nullptr;
}

namespace update {
void start() {}
std::string available()
{
    return {};
}
bool take_notice()
{
    return false;
}
bool checked()
{
    return false;
}
bool enabled()
{
    return false;
}
void set_enabled(bool) {}
}  // namespace update

}  // namespace mz

#endif  // MESHCARD_ESP32
