/* SPDX-License-Identifier: GPL-3.0-only */
#include "ui/fonts/fonts.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

/* Font files are looked up at run time; see mz_font() below for the chain. */

static bool g_ready = false;

static void* read_file(const char* path, size_t* size)
{
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;
    void* data = NULL;
    if (fseek(f, 0, SEEK_END) == 0) {
        const long len = ftell(f);
        if (len > 0 && fseek(f, 0, SEEK_SET) == 0) {
            data = malloc((size_t)len);
            if (data && fread(data, 1, (size_t)len, f) != (size_t)len) {
                free(data);
                data = NULL;
            }
            *size = (size_t)len;
        }
    }
    fclose(f);
    return data;
}

/* Looks in MESHZERO_FONT_DIR, the installed location and the build tree. */
static void* find_font(const char* name, size_t* size)
{
    const char* dirs[] = {getenv("MESHZERO_FONT_DIR"), "/usr/share/meshzero/fonts",
#ifdef MESHZERO_DEV_FONT_DIR
                          MESHZERO_DEV_FONT_DIR,
#endif
#ifdef MESHZERO_DEV_EMOJI_DIR
                          MESHZERO_DEV_EMOJI_DIR,
#endif
                          "/usr/share/fonts/truetype/dejavu"};
    for (size_t i = 0; i < sizeof(dirs) / sizeof(dirs[0]); ++i) {
        if (!dirs[i] || !dirs[i][0]) continue;
        char path[512];
        snprintf(path, sizeof(path), "%s/%s", dirs[i], name);
        void* data = read_file(path, size);
        if (data) return data;
    }
    return NULL;
}

typedef struct {
    const void* data;
    size_t size;
} blob_t;

/*
 * MeshCard: every text size is rendered from TrueType on demand, so the UI can
 * use bigger, bold text and scale per screen (Cardputer Zero 320x170, ADV
 * 240x135). Chain: Montserrat (Medium or Bold) -> DejaVu Sans (other scripts)
 * -> Noto Emoji -> LVGL's built-in Montserrat (UI symbols such as arrows).
 */
static blob_t g_medium, g_bold, g_dejavu, g_emoji;

#if defined(MESHCARD_ESP32)
/* Cardputer ADV: no TrueType at run time (it needs more working memory than the
 * ADV has). Montserrat is pre-drawn at the few sizes used (tools/make_adv_fonts.ps1)
 * and kept in flash. Zero size -> ADV size: 12->10, 14->11, 16->13, 20->16, 46->37. */
extern const lv_font_t mc_adv_medium_10, mc_adv_medium_11, mc_adv_medium_13, mc_adv_medium_16;
extern const lv_font_t mc_adv_bold_10, mc_adv_bold_11, mc_adv_bold_13, mc_adv_bold_16, mc_adv_bold_37;

static const lv_font_t* symbols_for(int size);

const lv_font_t* mz_font(int size, bool bold)
{
    static lv_font_t made[5][2]; /* copies, so each can fall back to the UI symbols */
    static bool ready[5][2];
    const int adv = (size * 4 + 2) / 5;
    const int i   = adv >= 30 ? 4 : adv >= 15 ? 3 : adv >= 13 ? 2 : adv >= 11 ? 1 : 0;
    const lv_font_t* med[5]  = {&mc_adv_medium_10, &mc_adv_medium_11, &mc_adv_medium_13, &mc_adv_medium_16,
                                &mc_adv_bold_37};
    const lv_font_t* bld[5]  = {&mc_adv_bold_10, &mc_adv_bold_11, &mc_adv_bold_13, &mc_adv_bold_16, &mc_adv_bold_37};
    const int sym_px[5]      = {10, 11, 13, 16, 16};
    if (!ready[i][bold]) {
        made[i][bold]          = *(bold ? bld[i] : med[i]);
        made[i][bold].fallback = symbols_for(sym_px[i]);
        ready[i][bold]         = true;
    }
    (void)g_ready;
    (void)g_medium; (void)g_bold; (void)g_dejavu; (void)g_emoji;
    (void)find_font;
    return &made[i][bold];
}
#else
static void load_files(void)
{
    if (g_ready) return;
    g_ready = true;
    /* The buffers back the fonts for the life of the process. */
    g_medium.data = find_font("Montserrat-Medium.ttf", &g_medium.size);
    g_bold.data   = find_font("Montserrat-Bold.ttf", &g_bold.size);
    if (!g_bold.data) g_bold = g_medium;
    g_dejavu.data = find_font("DejaVuSans.ttf", &g_dejavu.size);
    g_emoji.data  = find_font("NotoEmoji-Medium.ttf", &g_emoji.size);
}
#endif

static const lv_font_t* symbols_for(int size)
{
    return size >= 14 ? &lv_font_montserrat_14 : size >= 12 ? &lv_font_montserrat_12 : &lv_font_montserrat_10;
}

static lv_font_t* ttf(blob_t b, int size)
{
#if LV_USE_TINY_TTF && defined(MESHCARD_ESP32)
    /* ADV: little memory, so each size keeps only a few drawn letters (big sizes fewest). */
    if (b.data)
        return lv_tiny_ttf_create_data_ex(b.data, b.size, size, LV_FONT_KERNING_NORMAL, size >= 24 ? 12 : 24);
#elif LV_USE_TINY_TTF
    if (b.data) return lv_tiny_ttf_create_data(b.data, b.size, size);
#else
    (void)b;
    (void)size;
#endif
    return NULL;
}

#if !defined(MESHCARD_ESP32)
/* Built fonts, by size (4..64) and weight. */
static const lv_font_t* g_cache[65][2];

const lv_font_t* mz_font(int size, bool bold)
{
    load_files();
#if defined(MC_SCREEN_W) && MC_SCREEN_W < 300
    /* Cardputer ADV (240x135): everything at 4/5 of the Zero size. */
    size = (size * 4 + 2) / 5;
    if (size < 9) size = 9;
#endif
    if (size < 4) size = 4;
    if (size > 64) size = 64;
    if (g_cache[size][bold]) return g_cache[size][bold];
    const lv_font_t* symbols = symbols_for(size);
    lv_font_t* head = ttf(bold ? g_bold : g_medium, size);
    if (!head) { /* no TrueType at all: built-in only */
        g_cache[size][bold] = symbols;
        return symbols;
    }
    lv_font_t* tail = head;
    lv_font_t* extra[2] = {ttf(g_dejavu, size), ttf(g_emoji, size + 1)};
    for (int i = 0; i < 2; ++i) {
        if (extra[i]) {
            tail->fallback = extra[i];
            tail           = extra[i];
        }
    }
    tail->fallback      = symbols;
    g_cache[size][bold] = head;
    return head;
}
#endif /* !MESHCARD_ESP32 */

static int g_base = MZ_FONT_BASE;

void mz_font_set_base(int px)
{
    g_base = px;
}

const lv_font_t* mz_font_main(void)
{
    return mz_font(g_base, false);
}

const lv_font_t* mz_font_small(void)
{
    return mz_font(g_base - 2, false);
}

const lv_font_t* mz_font_bold(void)
{
    return mz_font(g_base, true);
}
