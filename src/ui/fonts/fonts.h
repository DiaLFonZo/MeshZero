/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Body and small fonts. Both fall back to LVGL's built-in font for symbols. */
const lv_font_t* mz_font_main(void);
const lv_font_t* mz_font_small(void);

#ifdef __cplusplus
}
#endif
