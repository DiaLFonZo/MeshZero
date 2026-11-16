// SPDX-License-Identifier: GPL-3.0-only
#include "daemon/hw.h"

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include <arpa/inet.h>
#include <fcntl.h>
#include <linux/i2c-dev.h>
#include <netinet/in.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

namespace mz {
namespace {

// PI4IOE5V6408 register map (datasheet).
constexpr uint8_t kRegDeviceId  = 0x01;
constexpr uint8_t kRegDirection = 0x03;  // 1 = output
constexpr uint8_t kRegOutput    = 0x05;
constexpr uint8_t kRegHighZ     = 0x07;  // 1 = output disabled
constexpr uint8_t kRegPullEn    = 0x0B;
constexpr uint8_t kRegPullSel   = 0x0D;
constexpr uint8_t kRegInput     = 0x0F;
constexpr uint8_t kAntSwitchBit = 0x01;  // P0 -> SX_ANT_SW

std::string env_or(const char* name, const char* fallback)
{
    const char* v = getenv(name);
    return v && v[0] ? v : fallback;
}

std::string rail_path()
{
    return env_or("MESHZERO_5V_PATH", "/sys/class/leds/ext_5v_out/brightness");
}

std::string i2c_path()
{
    return env_or("MESHZERO_I2C_DEV", "/dev/i2c-1");
}

int expander_addr()
{
    return static_cast<int>(strtol(env_or("MESHZERO_EXPANDER_ADDR", "0x43").c_str(), nullptr, 0));
}

bool read_file(const std::string& path, std::string& out)
{
    const int fd = open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) return false;
    char buf[64]    = {};
    const ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) return false;
    out.assign(buf, static_cast<size_t>(n));
    while (!out.empty() && (out.back() == '\n' || out.back() == '\r')) out.pop_back();
    return true;
}

bool write_file(const std::string& path, const char* value)
{
    const int fd = open(path.c_str(), O_WRONLY | O_CLOEXEC);
    if (fd < 0) return false;
    const ssize_t n = write(fd, value, strlen(value));
    close(fd);
    return n == static_cast<ssize_t>(strlen(value));
}

class Expander {
public:
    bool open_bus()
    {
        fd_ = open(i2c_path().c_str(), O_RDWR | O_CLOEXEC);
        if (fd_ < 0) return false;
        // The kernel may already have a driver bound to this address; FORCE lets
        // us talk to it anyway, which is what we want for a single shared pin.
        if (ioctl(fd_, I2C_SLAVE_FORCE, expander_addr()) < 0) {
            close(fd_);
            fd_ = -1;
            return false;
        }
        return true;
    }
    ~Expander()
    {
        if (fd_ >= 0) close(fd_);
    }
    bool read_reg(uint8_t reg, uint8_t& value) const
    {
        if (write(fd_, &reg, 1) != 1) return false;
        return read(fd_, &value, 1) == 1;
    }
    bool write_reg(uint8_t reg, uint8_t value) const
    {
        const uint8_t buf[2] = {reg, value};
        return write(fd_, buf, 2) == 2;
    }
    bool update(uint8_t reg, uint8_t mask, bool set) const
    {
        uint8_t value = 0;
        if (!read_reg(reg, value)) return false;
        const uint8_t next = set ? static_cast<uint8_t>(value | mask) : static_cast<uint8_t>(value & ~mask);
        return next == value || write_reg(reg, next);
    }

private:
    int fd_ = -1;
};

bool antenna_switch(bool on)
{
    Expander ex;
    if (!ex.open_bus()) {
        fprintf(stderr, "meshzero-hw: cannot open %s: %s\n", i2c_path().c_str(), strerror(errno));
        return false;
    }
    uint8_t id = 0;
    bool ok    = false;
    // The expander needs a moment after the 5 V rail comes up.
    for (int attempt = 0; attempt < 20 && !ok; ++attempt) {
        ok = ex.read_reg(kRegDeviceId, id);
        if (!ok) usleep(50000);
    }
    if (!ok) {
        fprintf(stderr, "meshzero-hw: no IO expander at 0x%02x on %s (is the LoRa cap attached?)\n", expander_addr(),
                i2c_path().c_str());
        return false;
    }
    if (on) {
        // Latch the level before enabling the driver so the pin never glitches low.
        ok = ex.update(kRegOutput, kAntSwitchBit, true) && ex.update(kRegPullEn, kAntSwitchBit, false) &&
             ex.update(kRegDirection, kAntSwitchBit, true) && ex.update(kRegHighZ, kAntSwitchBit, false);
    } else {
        ok = ex.update(kRegOutput, kAntSwitchBit, false);
    }
    if (!ok) fprintf(stderr, "meshzero-hw: IO expander write failed: %s\n", strerror(errno));
    return ok;
}

bool tcp_port_open(int port)
{
    const int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return false;
    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(static_cast<uint16_t>(port));
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    const bool ok        = connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0;
    close(fd);
    return ok;
}

bool exists(const std::string& path)
{
    struct stat st {};
    return stat(path.c_str(), &st) == 0;
}

}  // namespace

Cap hw_cap_probe(bool patient)
{
    if (env_or("MESHZERO_CAP_DETECT", "1") == "0") return Cap::Unknown;
    // The expander is powered from the 5 V rail, so that has to be on first.
    const std::string rail = rail_path();
    bool rail_ok           = true;
    if (exists(rail)) {
        std::string current;
        if (!read_file(rail, current)) {
            rail_ok = false;
        } else if (current == "0") {
            rail_ok = write_file(rail, "1");
            if (rail_ok) usleep(200000);
        }
    }
    Expander ex;
    if (!ex.open_bus()) return Cap::Unknown;
    uint8_t id = 0;
    for (int attempt = 0; attempt < (patient ? 10 : 2); ++attempt) {
        if (ex.read_reg(kRegDeviceId, id)) return Cap::Present;
        usleep(patient ? 50000 : 5000);
    }
    return rail_ok ? Cap::Absent : Cap::Unknown;
}

const char* hw_cap_name(Cap cap)
{
    return cap == Cap::Present ? "attached" : cap == Cap::Absent ? "not attached" : "unknown";
}

bool hw_power(bool on)
{
    const std::string rail = rail_path();
    if (!on) {
        // Leave the rail alone when switching off: other accessories may share it.
        if (hw_cap_probe(false) != Cap::Absent) antenna_switch(false);
        return true;
    }
    if (exists(rail)) {
        std::string current;
        const bool already_on = read_file(rail, current) && current != "0";
        if (!already_on) {
            if (!write_file(rail, "1")) {
                fprintf(stderr,
                        "meshzero-hw: cannot switch on the cap's 5V rail (%s: %s). This user needs write access "
                        "through group 'gpio'; reinstalling the package or rebooting applies it.\n",
                        rail.c_str(), strerror(errno));
                return false;
            }
            usleep(200000);
        }
    } else {
        fprintf(stderr, "meshzero-hw: %s not present, assuming the cap is always powered\n", rail.c_str());
    }
    // Without the switch the radio still starts, only with a poor RF path, so
    // let the radio daemon decide whether a cap is really there.
    if (!antenna_switch(true)) {
        fprintf(stderr, "meshzero-hw: continuing without the antenna switch; expect weak signals\n");
    }
    return true;
}

int hw_status()
{
    std::string value;
    const std::string rail = rail_path();
    if (read_file(rail, value)) {
        printf("5V rail      : %s (%s)\n", value == "0" ? "off" : "on", rail.c_str());
    } else {
        printf("5V rail      : not available (%s)\n", rail.c_str());
    }
    Expander ex;
    if (!ex.open_bus()) {
        printf("IO expander  : cannot open %s: %s\n", i2c_path().c_str(), strerror(errno));
        return 1;
    }
    uint8_t id = 0, dir = 0, out = 0, hiz = 0, pull_en = 0, pull_sel = 0, in = 0;
    if (!ex.read_reg(kRegDeviceId, id)) {
        printf("IO expander  : no response at 0x%02x\n", expander_addr());
        return 1;
    }
    ex.read_reg(kRegDirection, dir);
    ex.read_reg(kRegOutput, out);
    ex.read_reg(kRegHighZ, hiz);
    ex.read_reg(kRegPullEn, pull_en);
    ex.read_reg(kRegPullSel, pull_sel);
    ex.read_reg(kRegInput, in);
    printf("IO expander  : id=0x%02x dir=0x%02x out=0x%02x hiz=0x%02x pullen=0x%02x pullsel=0x%02x in=0x%02x\n", id, dir,
           out, hiz, pull_en, pull_sel, in);
    const bool driven = (dir & kAntSwitchBit) && (out & kAntSwitchBit) && !(hiz & kAntSwitchBit);
    printf("Antenna sw   : %s\n", driven ? "enabled" : "disabled");
    return driven ? 0 : 1;
}

int hw_doctor(int radio_port, int api_port)
{
    int problems     = 0;
    const auto check = [&problems](bool ok, const char* what, const std::string& hint) {
        printf("[%s] %s%s%s\n", ok ? " ok " : "FAIL", what, ok ? "" : " - ", ok ? "" : hint.c_str());
        if (!ok) ++problems;
    };
    // Present and usable by whoever runs this (the services run as 'meshzero').
    const auto device = [&check](const std::string& path, int mode, const char* what, const char* missing,
                                 const char* group) {
        if (!exists(path)) {
            check(false, what, path + " does not exist: " + missing);
        } else if (access(path.c_str(), mode) != 0) {
            check(false, what, "no permission for " + path + " (this user needs group '" + group + "')");
        } else {
            check(true, what, "");
        }
    };
    device(env_or("MESHZERO_SPI_DEV", "/dev/spidev0.1"), R_OK | W_OK, "SPI device for the SX1262",
           "enable SPI (dtparam=spi=on)", "spi");
    device("/dev/gpiochip0", R_OK | W_OK, "GPIO character device", "kernel GPIO support is missing", "gpio");
    device(i2c_path(), R_OK | W_OK, "I2C bus for the IO expander", "enable I2C (dtparam=i2c_arm=on)", "i2c");
    device(env_or("MESHZERO_GPS_DEV", "/dev/ttyS0"), R_OK | W_OK, "GNSS serial port",
           "enable the UART (enable_uart=1) and remove the serial console", "dialout");
    device(rail_path(), W_OK, "5V rail control for the cap", "the ext_5v_out control is not present on this image", "gpio");
    check(hw_status() == 0, "LoRa cap antenna switch",
          "power off, attach the cap, power on");
    check(tcp_port_open(radio_port), "Radio daemon API (local)",
          "see 'journalctl -u meshzero -n 40'");
    check(tcp_port_open(api_port), "MeshZero API for clients", "see 'journalctl -u meshzero -n 40'");
    printf("%s\n", problems == 0 ? "All checks passed." : "Some checks failed; see above.");
    return problems == 0 ? 0 : 1;
}

}  // namespace mz
