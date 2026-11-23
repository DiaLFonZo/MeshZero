// SPDX-License-Identifier: GPL-3.0-only
#include "ui/sound.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include "common/util.h"

namespace mz::sound {
namespace {

constexpr int kRate = 22050;

struct Note {
    double hz;  // 0 for a pause
    int ms;
};

bool g_enabled   = true;
pid_t g_player   = -1;
Kind g_playing   = Kind::Message;
bool g_written[2] = {false, false};
uint64_t g_last_ms = 0;  // when the last sound started

// A busy channel should not turn into a row of beeps.
constexpr uint64_t kMessageGapMs = 2000;

// The desktop's sound server first, as the speaker is its to share; plain ALSA
// where there is none. MESHZERO_SOUND_CMD names another player to run with the
// file, or nothing at all for silence.
const char* const kPlayers = "pw-play \"$1\" 2>/dev/null || paplay \"$1\" 2>/dev/null || aplay -q \"$1\" 2>/dev/null";

void put_le(std::string& out, uint32_t value, int bytes)
{
    for (int i = 0; i < bytes; ++i) out.push_back(static_cast<char>((value >> (8 * i)) & 0xff));
}

// 16-bit mono WAV of the notes, each faded in and out so it does not click.
std::string render(const std::vector<Note>& notes)
{
    std::vector<int16_t> pcm;
    for (const Note& n : notes) {
        const int count = kRate * n.ms / 1000;
        const int fade  = kRate * 6 / 1000;
        for (int i = 0; i < count; ++i) {
            double gain = 0.45;
            if (i < fade) gain *= static_cast<double>(i) / fade;
            if (count - i < fade) gain *= static_cast<double>(count - i) / fade;
            const double v = n.hz > 0 ? std::sin(2.0 * M_PI * n.hz * i / kRate) * gain : 0.0;
            pcm.push_back(static_cast<int16_t>(v * 32767));
        }
    }
    const uint32_t bytes = static_cast<uint32_t>(pcm.size() * 2);
    std::string out      = "RIFF";
    put_le(out, 36 + bytes, 4);
    out += "WAVEfmt ";
    put_le(out, 16, 4);
    put_le(out, 1, 2);          // PCM
    put_le(out, 1, 2);          // mono
    put_le(out, kRate, 4);
    put_le(out, kRate * 2, 4);  // bytes per second
    put_le(out, 2, 2);          // bytes per sample
    put_le(out, 16, 2);         // bits per sample
    out += "data";
    put_le(out, bytes, 4);
    for (const int16_t s : pcm) put_le(out, static_cast<uint16_t>(s), 2);
    return out;
}

std::string file_of(Kind kind)
{
    const char* dir = getenv("XDG_RUNTIME_DIR");
    return std::string(dir && dir[0] ? dir : "/tmp") + "/meshzero-" + std::to_string(getuid()) +
           (kind == Kind::Alert ? "-alert.wav" : "-message.wav");
}

bool write_file(Kind kind, const std::string& path)
{
    const size_t slot = kind == Kind::Alert ? 1 : 0;
    if (g_written[slot]) return true;
    std::vector<Note> notes;
    if (kind == Kind::Alert) {
        for (int i = 0; i < 3; ++i) {
            notes.push_back({1568, 140});
            notes.push_back({1175, 140});
            notes.push_back({0, 90});
        }
    } else {
        notes = {{988, 70}, {0, 25}, {1319, 110}};
    }
    const std::string wav = render(notes);
    const std::string tmp = path + ".tmp";
    FILE* f               = fopen(tmp.c_str(), "wb");
    if (!f) return false;
    const bool ok = fwrite(wav.data(), 1, wav.size(), f) == wav.size();
    fclose(f);
    if (!ok || rename(tmp.c_str(), path.c_str()) != 0) {
        unlink(tmp.c_str());
        return false;
    }
    g_written[slot] = true;
    return true;
}

}  // namespace

void set_enabled(bool on)
{
    g_enabled = on;
}

void play(Kind kind)
{
    const char* custom = getenv("MESHZERO_SOUND_CMD");
    if (custom ? custom[0] == '\0' : !g_enabled) return;
    const uint64_t now = now_ms();
    if (kind == Kind::Message && g_last_ms != 0 && now - g_last_ms < kMessageGapMs) return;

    if (g_player > 0) {
        if (waitpid(g_player, nullptr, WNOHANG) == 0) {
            if (kind != Kind::Alert || g_playing == Kind::Alert) return;
            kill(-g_player, SIGTERM);
            waitpid(g_player, nullptr, 0);
        }
        g_player = -1;
    }

    const std::string path = file_of(kind);
    if (!write_file(kind, path)) return;
    const std::string cmd = custom ? std::string(custom) + " \"$1\"" : kPlayers;

    const pid_t pid = fork();
    if (pid < 0) return;
    if (pid == 0) {
        // Its own process group, so that stopping it takes the player along.
        setpgid(0, 0);
        const int null = open("/dev/null", O_RDWR);
        if (null >= 0) {
            dup2(null, 0);
            dup2(null, 1);
            dup2(null, 2);
        }
        for (int fd = 3; fd < 256; ++fd) close(fd);
        signal(SIGPIPE, SIG_DFL);
        execl("/bin/sh", "sh", "-c", cmd.c_str(), "sh", path.c_str(), static_cast<char*>(nullptr));
        _exit(127);
    }
    setpgid(pid, pid);
    g_last_ms = now;
    g_player  = pid;
    g_playing = kind;
}

}  // namespace mz::sound
