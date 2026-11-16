/* SPDX-License-Identifier: GPL-3.0-only */
#include "ui/fonts/fonts.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

/*
 * ASCII and the UI symbols come from LVGL's pre-rendered Montserrat. Anything
 * else (accents, Cyrillic, Greek, ...) is rendered on demand from TrueType
 * files: Montserrat first so text keeps one typeface, then DejaVu Sans for
 * the wider character set.
 */

static lv_font_t g_main;
static lv_font_t g_small;
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

/* Builds base -> Montserrat TTF -> DejaVu TTF, skipping whatever is missing. */
static void chain(lv_font_t* font, const lv_font_t* base, int size, const void* montserrat, size_t montserrat_size,
                  const void* dejavu, size_t dejavu_size)
{
    *font = *base;
    lv_font_t* tail = font;
#if LV_USE_TINY_TTF
    if (montserrat) {
        lv_font_t* ttf = lv_tiny_ttf_create_data(montserrat, montserrat_size, size);
        if (ttf) {
            tail->fallback = ttf;
            tail           = ttf;
        }
    }
    if (dejavu) {
        lv_font_t* ttf = lv_tiny_ttf_create_data(dejavu, dejavu_size, size);
        if (ttf) tail->fallback = ttf;
    }
#else
    (void)size;
    (void)montserrat;
    (void)montserrat_size;
    (void)dejavu;
    (void)dejavu_size;
    (void)tail;
#endif
}

static void init(void)
{
    if (g_ready) return;
    g_ready = true;
    size_t montserrat_size = 0, dejavu_size = 0;
    /* The buffers back the fonts for the life of the process. */
    const void* montserrat = find_font("Montserrat-Medium.ttf", &montserrat_size);
    const void* dejavu     = find_font("DejaVuSans.ttf", &dejavu_size);
    chain(&g_main, &lv_font_montserrat_12, 12, montserrat, montserrat_size, dejavu, dejavu_size);
    chain(&g_small, &lv_font_montserrat_10, 10, montserrat, montserrat_size, dejavu, dejavu_size);
}

const lv_font_t* mz_font_main(void)
{
    init();
    return &g_main;
}

const lv_font_t* mz_font_small(void)
{
    init();
    return &g_small;
}
