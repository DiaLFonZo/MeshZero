// SPDX-License-Identifier: GPL-3.0-only
// meshcard-alertd: rings the speaker when a message arrives, whether MeshCard
// is open or not. It is one more client of meshzerod (which shares the radio
// with any number of clients). The ring repeats until any key is pressed.
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>

#include <arpa/inet.h>
#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include "common/alert.h"
#include "common/proto.h"

using namespace mz;

namespace {

constexpr const char* kKeyboard = "/dev/input/by-path/platform-3f804000.i2c-event";
constexpr int kRingEveryMs     = 5000;
constexpr int kGiveUpMs        = 3 * 60 * 1000;  // stop by itself after 3 minutes

volatile sig_atomic_t g_stop = 0;

uint64_t now_ms()
{
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1000 + static_cast<uint64_t>(ts.tv_nsec) / 1000000;
}

int connect_api(uint16_t port)
{
    const int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    sockaddr_in a{};
    a.sin_family      = AF_INET;
    a.sin_port        = htons(port);
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (connect(fd, reinterpret_cast<sockaddr*>(&a), sizeof(a)) != 0) {
        close(fd);
        return -1;
    }
    return fd;
}

bool send_all(int fd, const std::string& data)
{
    size_t done = 0;
    while (done < data.size()) {
        const ssize_t n = write(fd, data.data() + done, data.size() - done);
        if (n <= 0) return false;
        done += static_cast<size_t>(n);
    }
    return true;
}

struct Alarm {
    bool active       = false;
    uint64_t started  = 0;
    uint64_t next     = 0;
    int kbd           = -1;
    AlertConf conf;

    void start()
    {
        conf = alert_conf_load();
        if (!conf.on) return;
        const uint64_t t = now_ms();
        if (!active) {
            started = t;
            // Only keys pressed from now on stop the ring.
            if (kbd < 0) kbd = open(kKeyboard, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
            input_event ev{};
            while (kbd >= 0 && read(kbd, &ev, sizeof(ev)) == sizeof(ev)) {
            }
        }
        active = true;
        next   = t;  // ring now, also for a second message during a ring
    }

    void stop()
    {
        active = false;
        if (kbd >= 0) close(kbd);
        kbd = -1;
    }

    // Called often: rings when due, stops on a key press.
    void tick()
    {
        if (!active) return;
        input_event ev{};
        while (kbd >= 0 && read(kbd, &ev, sizeof(ev)) == sizeof(ev)) {
            if (ev.type == EV_KEY && ev.value == 1) {
                stop();
                return;
            }
        }
        const uint64_t t = now_ms();
        if (t - started > static_cast<uint64_t>(kGiveUpMs)) {
            stop();
            return;
        }
        if (t >= next) {
            alert_ring(conf);
            next = t + kRingEveryMs;
            if (!conf.repeat) stop();
        }
    }
};

// Plugged in: the screen stays on, also with MeshCard closed (the Zero's launcher would turn
// it off on its own timer). MeshCard open does this itself, and Esc there can turn it off,
// so nothing is done while it runs. On battery the launcher decides, as before.
struct KeepLit {
    uint64_t next = 0;
    int on_level  = -1;  // the last level seen lit, put back as it was

    static bool read_int(const std::string& path, int& v)
    {
        FILE* f = fopen(path.c_str(), "r");
        if (!f) return false;
        const bool ok = fscanf(f, "%d", &v) == 1;
        fclose(f);
        return ok;
    }

    static std::string read_word(const std::string& path)
    {
        char buf[32] = "";
        FILE* f = fopen(path.c_str(), "r");
        if (!f) return "";
        if (!fgets(buf, sizeof(buf), f)) buf[0] = 0;
        fclose(f);
        std::string s(buf);
        while (!s.empty() && (s.back() == '\n' || s.back() == ' ')) s.pop_back();
        return s;
    }

    static bool plugged()
    {
        DIR* d = opendir("/sys/class/power_supply");
        if (!d) return false;
        bool yes = false;
        while (dirent* e = readdir(d)) {
            if (e->d_name[0] == '.') continue;
            const std::string dir = std::string("/sys/class/power_supply/") + e->d_name;
            if (read_word(dir + "/type") != "Battery") continue;
            const std::string s = read_word(dir + "/status");
            yes = s == "Charging" || s == "Full" || s == "Not charging";  // same rule as MeshCard
            break;
        }
        closedir(d);
        return yes;
    }

    static bool meshcard_running()
    {
        DIR* d = opendir("/proc");
        if (!d) return false;
        bool yes = false;
        while (dirent* e = readdir(d)) {
            if (yes) break;
            if (e->d_name[0] < '0' || e->d_name[0] > '9') continue;
            yes = read_word(std::string("/proc/") + e->d_name + "/comm") == "meshcard";
        }
        closedir(d);
        return yes;
    }

    void tick()
    {
        const uint64_t t = now_ms();
        if (t < next) return;
        next = t + 2000;
        const std::string bl = "/sys/class/backlight/backlight/brightness";
        int level = -1;
        if (!read_int(bl, level)) return;
        if (level > 0) {
            on_level = level;
            return;
        }
        if (!plugged() || meshcard_running()) return;
        int max = 100;
        read_int("/sys/class/backlight/backlight/max_brightness", max);
        const int want = on_level > 0 ? on_level : max;
        if (FILE* f = fopen(bl.c_str(), "w")) {
            fprintf(f, "%d\n", want);
            fclose(f);
        }
    }
};

}  // namespace

int main(int argc, char** argv)
{
    uint16_t port = kDefaultApiPort;
    if (argc > 2 && !strcmp(argv[1], "--port")) port = static_cast<uint16_t>(atoi(argv[2]));
    if (argc > 1 && !strcmp(argv[1], "--test")) {  // ring once with the saved settings
        AlertConf c = alert_conf_load();
        alert_ring(c);
        sleep(3);
        return 0;
    }
    signal(SIGTERM, [](int) { g_stop = 1; });
    signal(SIGINT, [](int) { g_stop = 1; });
    signal(SIGPIPE, SIG_IGN);

    Alarm alarm;
    KeepLit keep_lit;
    while (!g_stop) {
        const int fd = connect_api(port);
        if (fd < 0) {
            for (int i = 0; i < 30 && !g_stop; ++i) {
                alarm.tick();
            keep_lit.tick();
                usleep(100000);
            }
            continue;
        }
        fprintf(stderr, "meshcard-alertd: connected to %u\n", port);
        // Ask for the node's own number; live packets follow.
        meshtastic_ToRadio req   = meshtastic_ToRadio_init_zero;
        req.which_payload_variant = meshtastic_ToRadio_want_config_id_tag;
        req.want_config_id        = static_cast<uint32_t>(time(nullptr)) | 1u;
        if (!send_all(fd, frame_wrap(pb_encode_msg(meshtastic_ToRadio_fields, req)))) {
            close(fd);
            continue;
        }
        uint32_t my_num = 0;
        bool synced     = false;
        FrameParser parser;
        while (!g_stop) {
            pollfd p{fd, POLLIN, 0};
            const int r = poll(&p, 1, 100);
            alarm.tick();
            keep_lit.tick();
            if (r < 0 && errno != EINTR) break;
            if (r <= 0) continue;
            uint8_t buf[1024];
            const ssize_t n = read(fd, buf, sizeof(buf));
            if (n <= 0) break;
            parser.feed(buf, static_cast<size_t>(n), [&](const std::string& payload) {
                meshtastic_FromRadio fr = meshtastic_FromRadio_init_zero;
                if (!pb_decode_msg(meshtastic_FromRadio_fields, payload, fr)) return;
                if (fr.which_payload_variant == meshtastic_FromRadio_my_info_tag) my_num = fr.my_info.my_node_num;
                if (fr.which_payload_variant == meshtastic_FromRadio_config_complete_id_tag) synced = true;
                if (fr.which_payload_variant != meshtastic_FromRadio_packet_tag || !synced) return;
                const meshtastic_MeshPacket& pk = fr.packet;
                if (pk.which_payload_variant != meshtastic_MeshPacket_decoded_tag) return;
                if (pk.decoded.portnum != meshtastic_PortNum_TEXT_MESSAGE_APP &&
                    pk.decoded.portnum != meshtastic_PortNum_RANGE_TEST_APP)  // "seq N": a message too
                    return;
                if (pk.from == my_num || pk.decoded.emoji != 0) return;  // own echo, reactions
                const bool dm = pk.to == my_num;
                // That chat is open on screen: you are reading it, no ring.
                if (alert_chat_is_open(dm, dm ? pk.from : pk.channel)) return;
                const AlertConf c = alert_conf_load();
                if (c.on && (dm || c.all)) alarm.start();
            });
        }
        close(fd);
        fprintf(stderr, "meshcard-alertd: disconnected\n");
    }
    alarm.stop();
    return 0;
}
