// SPDX-License-Identifier: GPL-3.0-only
#include "ui/zero_map/tiles.h"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <fstream>
#include <list>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <fcntl.h>
#include <spawn.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "common/util.h"
#include "ui/zero_map/geo.h"
#include "lvgl.h"
// Only the C interface of the PNG decoder that comes with LVGL is used.
#define LODEPNG_NO_COMPILE_CPP
#include "src/libs/lodepng/lodepng.h"
extern "C" {
#include "src/libs/tjpgd/tjpgd.h"
}

extern char** environ;

namespace mz::tiles {
namespace {

// All are free to use without a key. The first two are for light use that
// names the program asking and keeps what it downloaded, and do not allow
// fetching whole regions ahead of time, so only tiles that are looked at are
// asked for.
const Layer kLayers[kLayerCount] = {
    {"street", "Street", "https://tile.openstreetmap.org/", "(c) OpenStreetMap", 19, ".png", false},
    {"topo", "Topo", "https://tile.opentopomap.org/", "(c) OpenStreetMap, SRTM, OpenTopoMap", 17, ".png", false},
    {"usgs", "US Topo", "https://basemap.nationalmap.gov/arcgis/rest/services/USGSTopo/MapServer/tile/",
     "USGS The National Map", 16, ".jpg", true},
};

constexpr size_t kPixels       = static_cast<size_t>(kTilePx) * kTilePx;
constexpr size_t kDecodedMax   = 24;     // tiles kept decoded, 128 KB each
constexpr size_t kQueueMax     = 32;
constexpr size_t kBatch        = 8;      // tiles per curl run, over one connection
constexpr uint64_t kRetryMs    = 60000;  // before a tile that failed is asked for again
constexpr uint64_t kAbsentMs   = 600000; // the same for one the server says it does not have
constexpr uint64_t kBackoffMs  = 5000;   // pause after a run that brought nothing
constexpr size_t kTileBytesMax = 2 * 1024 * 1024;

uint64_t key_of(int layer, int z, int x, int y)
{
    return (static_cast<uint64_t>(layer) << 58) | (static_cast<uint64_t>(z) << 52) | (static_cast<uint64_t>(x) << 26) |
           static_cast<uint64_t>(y);
}

std::string path_of(int layer, int z, int x, int y)
{
    return dir() + "/" + kLayers[layer].id + "/" + std::to_string(z) + "/" + std::to_string(x) + "/" +
           std::to_string(y) + kLayers[layer].ext;
}

// ---- decoded tiles (interface thread) ---------------------------------------

struct Decoded {
    uint64_t key;
    std::vector<uint16_t> px;
};
std::list<Decoded> g_decoded;  // most recently used first
std::unordered_map<uint64_t, std::list<Decoded>::iterator> g_decoded_index;

bool read_file(const std::string& path, std::vector<uint8_t>& out)
{
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    uint8_t buf[16384];
    size_t n = 0;
    while (out.size() <= kTileBytesMax && (n = fread(buf, 1, sizeof(buf), f)) > 0) out.insert(out.end(), buf, buf + n);
    fclose(f);
    return !out.empty() && out.size() <= kTileBytesMax;
}

bool decode_png(const std::vector<uint8_t>& png, std::vector<uint16_t>& px)
{
    // LVGL's copy of the decoder gives back one of its draw buffers, holding
    // red, green, blue and alpha bytes, where the original gives bare pixels.
    unsigned char* out = nullptr;
    unsigned w = 0, h = 0;
    const unsigned err = lodepng_decode32(&out, &w, &h, png.data(), png.size());
    lv_draw_buf_t* buf = reinterpret_cast<lv_draw_buf_t*>(out);
    const bool ok      = err == 0 && buf && w == kTilePx && h == kTilePx && buf->data_size >= kPixels * 4;
    if (ok) {
        px.resize(kPixels);
        for (size_t i = 0; i < kPixels; ++i) {
            const uint8_t* p = buf->data + i * 4;
            px[i] = static_cast<uint16_t>(((p[0] & 0xf8) << 8) | ((p[1] & 0xfc) << 3) | (p[2] >> 3));
        }
    }
    if (buf) lv_draw_buf_destroy(buf);
    return ok;
}

// JPEG, through the small decoder that comes with LVGL.
struct JpegJob {
    const std::vector<uint8_t>* in;
    size_t pos;
    std::vector<uint16_t>* px;
};

size_t jpeg_in(JDEC* jd, uint8_t* buf, size_t len)
{
    JpegJob* job = static_cast<JpegJob*>(jd->device);
    len          = std::min(len, job->in->size() - job->pos);
    if (buf) memcpy(buf, job->in->data() + job->pos, len);
    job->pos += len;
    return len;
}

int jpeg_out(JDEC* jd, void* bitmap, JRECT* rect)
{
    JpegJob* job     = static_cast<JpegJob*>(jd->device);
    const uint8_t* p = static_cast<const uint8_t*>(bitmap);
    for (int y = rect->top; y <= rect->bottom; ++y) {
        for (int x = rect->left; x <= rect->right; ++x, p += 3) {
            if (x >= kTilePx || y >= kTilePx) continue;
            (*job->px)[static_cast<size_t>(y) * kTilePx + x] =
                static_cast<uint16_t>(((p[0] & 0xf8) << 8) | ((p[1] & 0xfc) << 3) | (p[2] >> 3));
        }
    }
    return 1;
}

bool decode_jpeg(const std::vector<uint8_t>& jpeg, std::vector<uint16_t>& px)
{
    std::vector<uint8_t> work(16384);
    JpegJob job{&jpeg, 0, &px};
    JDEC jd{};
    if (jd_prepare(&jd, jpeg_in, work.data(), work.size(), &job) != JDR_OK) return false;
    if (jd.width != kTilePx || jd.height != kTilePx) return false;
    px.assign(kPixels, 0);
    return jd_decomp(&jd, jpeg_out, 0) == JDR_OK;
}

bool is_jpeg(const uint8_t* head, size_t n)
{
    return n >= 3 && head[0] == 0xff && head[1] == 0xd8 && head[2] == 0xff;
}

bool is_png(const uint8_t* head, size_t n)
{
    static const uint8_t magic[8] = {0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a};
    return n >= sizeof(magic) && memcmp(head, magic, sizeof(magic)) == 0;
}

// What the file is called does not decide how it is read; what is in it does.
bool decode(const std::vector<uint8_t>& file, std::vector<uint16_t>& px)
{
    if (is_png(file.data(), file.size())) return decode_png(file, px);
    if (is_jpeg(file.data(), file.size())) return decode_jpeg(file, px);
    return false;
}

// ---- settings ---------------------------------------------------------------

std::mutex g_mutex;
bool g_loaded    = false;
bool g_downloads = true;
bool g_imperial = false;
int g_layer      = -1;

std::string settings_file()
{
    return user_state_dir() + "/map";
}

void load_locked()
{
    if (g_loaded) return;
    g_loaded = true;
    std::ifstream in(settings_file());
    std::string key, value;
    while (in >> key >> value) {
        if (key == "imperial") g_imperial = value != "0";
        if (key == "downloads") g_downloads = value != "0";
        if (key == "layer") {
            for (const int i : {kStreet, kTopo}) {
                if (value == kLayers[i].id) g_layer = i;
            }
        }
    }
}

void save_locked()
{
    make_dirs(user_state_dir());
    std::ofstream out(settings_file() + ".tmp", std::ios::trunc);
    if (!out) return;
    out << "layer " << (g_layer < 0 ? "radar" : kLayers[g_layer].id) << '\n' << "downloads " << (g_downloads ? 1 : 0) << '\n';
    out << "imperial " << (g_imperial ? 1 : 0) << '\n';
    out.close();
    rename((settings_file() + ".tmp").c_str(), settings_file().c_str());
}

// ---- downloads (their own thread) -------------------------------------------

struct Request {
    uint64_t key;
    int layer, z, x, y;
};
std::deque<Request> g_queue;  // newest first: what is on screen now matters most
std::unordered_set<uint64_t> g_wanted;             // queued or being fetched
std::unordered_map<uint64_t, uint64_t> g_failed;   // until when a tile is not asked for again
std::unordered_set<uint64_t> g_absent;             // tiles a server said it does not have
bool g_worker = false;
std::atomic<uint32_t> g_revision{0};
std::atomic<bool> g_failing{false};

// What a download left behind: a tile, something else the server said
// instead (it has no such tile), or nothing (it was not reached).
enum class Got { Tile, Refused, Nothing };

Got what_came(const std::string& path)
{
    uint8_t head[8] = {};
    FILE* f         = fopen(path.c_str(), "rb");
    if (!f) return Got::Nothing;
    const size_t n = fread(head, 1, sizeof(head), f);
    fclose(f);
    if (n == 0) return Got::Nothing;
    return is_png(head, n) || is_jpeg(head, n) ? Got::Tile : Got::Refused;
}

// Runs curl with the arguments as they are, no shell in between, and waits.
void run_curl(const std::vector<std::string>& args)
{
    std::vector<char*> argv;
    for (const std::string& a : args) argv.push_back(const_cast<char*>(a.c_str()));
    argv.push_back(nullptr);
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, "/dev/null", O_WRONLY, 0);
    posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0);
    pid_t pid = 0;
    if (posix_spawnp(&pid, "curl", &actions, nullptr, argv.data(), environ) == 0) {
        int status = 0;
        while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
        }
    }
    posix_spawn_file_actions_destroy(&actions);
}

void worker()
{
    for (;;) {
        std::vector<Request> batch;
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            while (!g_queue.empty() && batch.size() < kBatch) {
                batch.push_back(g_queue.front());
                g_queue.pop_front();
            }
            if (batch.empty()) {
                g_worker = false;
                return;
            }
        }
        // The servers ask for a name that says which program this is.
        // Without -f: what a server says in place of a tile it does not have is
        // kept long enough to tell that apart from not reaching it.
        std::vector<std::string> args = {"curl", "-sSL", "--connect-timeout", "10", "--max-time", "40", "-A",
                                         "MeshCard/" MESHZERO_VERSION " (+https://github.com/DiaLFonZo/MeshCard)"};
        std::vector<std::string> paths;
        for (const Request& r : batch) {
            const std::string path = path_of(r.layer, r.z, r.x, r.y);
            make_dirs(path.substr(0, path.rfind('/')));
            paths.push_back(path);
            args.push_back("-o");
            args.push_back(path + ".part");
            const Layer& l = kLayers[r.layer];
            args.push_back(l.row_first ? std::string(l.url) + std::to_string(r.z) + "/" + std::to_string(r.y) + "/" +
                                             std::to_string(r.x)
                                       : std::string(l.url) + std::to_string(r.z) + "/" + std::to_string(r.x) + "/" +
                                             std::to_string(r.y) + ".png");
        }
        run_curl(args);

        size_t got = 0, answered = 0;
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            for (size_t i = 0; i < batch.size(); ++i) {
                const std::string part = paths[i] + ".part";
                const Got came         = what_came(part);
                g_wanted.erase(batch[i].key);
                if (came != Got::Nothing) ++answered;
                if (came == Got::Tile && rename(part.c_str(), paths[i].c_str()) == 0) {
                    g_failed.erase(batch[i].key);
                    ++got;
                } else {
                    unlink(part.c_str());
                    g_failed[batch[i].key] = now_ms() + (came == Got::Refused ? kAbsentMs : kRetryMs);
                    if (came == Got::Refused) g_absent.insert(batch[i].key);
                }
            }
        }
        g_failing = answered == 0;
        ++g_revision;  // also when nothing came: what is shown about it changes
        if (answered == 0) {
            // Offline, or no curl: what is still queued would fail the same way.
            {
                std::lock_guard<std::mutex> lock(g_mutex);
                const uint64_t until = now_ms() + kRetryMs;
                for (const Request& r : g_queue) {
                    g_wanted.erase(r.key);
                    g_failed[r.key] = until;
                }
                g_queue.clear();
            }
            usleep(static_cast<useconds_t>(kBackoffMs * 1000));
        }
    }
}

void request(int layer, int z, int x, int y)
{
    const uint64_t key = key_of(layer, z, x, y);
    std::lock_guard<std::mutex> lock(g_mutex);
    load_locked();
    if (!g_downloads || g_wanted.count(key)) return;
    if (const auto it = g_failed.find(key); it != g_failed.end() && now_ms() < it->second) return;
    g_wanted.insert(key);
    g_queue.push_front({key, layer, z, x, y});
    while (g_queue.size() > kQueueMax) {
        g_wanted.erase(g_queue.back().key);
        g_queue.pop_back();
    }
    if (!g_worker) {
        g_worker = true;
        std::thread(worker).detach();
    }
}

}  // namespace

const Layer& layer(int index)
{
    return kLayers[index];
}

std::string dir()
{
    if (const char* env = getenv("MESHZERO_MAP_DIR"); env && env[0]) return env;
    return user_state_dir() + "/maps";
}

const uint16_t* get(int layer, int zoom, int x, int y, bool want)
{
    if (layer < 0 || layer >= kLayerCount || zoom < 0 || zoom > kLayers[layer].max_zoom) return nullptr;
    const uint64_t key = key_of(layer, zoom, x, y);
    if (const auto it = g_decoded_index.find(key); it != g_decoded_index.end()) {
        g_decoded.splice(g_decoded.begin(), g_decoded, it->second);
        return it->second->px.data();
    }
    std::vector<uint8_t> png;
    if (!read_file(path_of(layer, zoom, x, y), png)) {
        if (want) request(layer, zoom, x, y);
        return nullptr;
    }
    Decoded d;
    d.key = key;
    // A file that is no map tile stays as it is; it just is not shown.
    if (!decode(png, d.px)) return nullptr;
    g_decoded.push_front(std::move(d));
    g_decoded_index[key] = g_decoded.begin();
    while (g_decoded.size() > kDecodedMax) {
        g_decoded_index.erase(g_decoded.back().key);
        g_decoded.pop_back();
    }
    return g_decoded.front().px.data();
}

bool absent(int layer, int zoom, int x, int y)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_absent.count(key_of(layer, zoom, x, y)) != 0;
}

uint32_t revision()
{
    return g_revision;
}

int pending()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    return static_cast<int>(g_wanted.size());
}

bool failing()
{
    return g_failing;
}

bool imperial()
{
    const std::lock_guard<std::mutex> lock(g_mutex);
    load_locked();
    return g_imperial;
}

void set_imperial(bool on)
{
    const std::lock_guard<std::mutex> lock(g_mutex);
    load_locked();
    g_imperial = on;
    save_locked();
}

bool downloads()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    load_locked();
    return g_downloads;
}

void set_downloads(bool on)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    load_locked();
    g_downloads = on;
    g_failing   = false;
    g_failed.clear();
    if (!on) {
        for (const Request& r : g_queue) g_wanted.erase(r.key);
        g_queue.clear();
    }
    save_locked();
}

int saved_layer()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    load_locked();
    return g_layer;
}

void save_layer(int index)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    load_locked();
    if (index == g_layer || index > kTopo) return;
    g_layer = index < 0 ? -1 : index;
    save_locked();
}

}  // namespace mz::tiles
