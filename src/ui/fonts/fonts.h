/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

/* MeshCard body text size in pixels (Zero 320x170: 14). */
#ifndef MZ_FONT_BASE
#define MZ_FONT_BASE 14
#endif

/* Any size, medium or bold; symbols fall back to LVGL's built-in font. */
const lv_font_t* mz_font(int size, bool bold);
void mz_font_set_base(int px);
const lv_font_t* mz_font_main(void);  /* body */
const lv_font_t* mz_font_small(void); /* body - 2 */
const lv_font_t* mz_font_bold(void);  /* body, bold */

#ifdef __cplusplus
}
#endif
