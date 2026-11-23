// SPDX-License-Identifier: GPL-3.0-only
// Map tiles: 256 pixel squares of a street or topographic map, kept on disk
// as <folder>/<layer>/<zoom>/<x>/<y>.png (.jpg for a layer that comes as JPEG),
// the layout map tile tools write.
// Tiles that are there are used as they are, so a folder filled beforehand
// works with no network at all. A missing tile is downloaded in the
// background with the system's curl, when downloads are on, and kept.
#pragma once

#include <cstdint>
#include <string>

namespace mz::tiles {

struct Layer {
    const char* id;      // folder name, and how the choice is saved
    const char* name;    // shown
    const char* url;     // tile address up to the zoom level
    const char* credit;  // who the map is by; has to stay on screen
    int max_zoom;
    const char* ext;     // of the files, with the dot
    bool row_first;      // the address ends in zoom/y/x, not zoom/x/y.png
};

// Topo covers the world with heights in metres. US Topo has them in feet and
// covers the United States only.
enum { kStreet = 0, kTopo, kUsTopo, kLayerCount };
const Layer& layer(int index);

// The folder the layers are in: MESHZERO_MAP_DIR, or "maps" in the state folder.
std::string dir();

// The pixels of a tile as 256 rows of 256 RGB565 values, or null when it is
// not on disk. With `want`, a missing tile is asked for. The pixels stay valid
// until the next call. Interface thread only.
const uint16_t* get(int layer, int zoom, int x, int y, bool want);

// True when the layer's server has said, since the start, that it has no such tile.
bool absent(int layer, int zoom, int x, int y);

// Goes up each time downloaded tiles have arrived.
uint32_t revision();
// Tiles waiting to be downloaded.
int pending();
// True while downloads are being tried and the servers cannot be reached.
bool failing();

bool downloads();
void set_downloads(bool on);

// What the map was last left on: kStreet, kTopo, or -1 for the radar.
int saved_layer();
void save_layer(int index);

}  // namespace mz::tiles
