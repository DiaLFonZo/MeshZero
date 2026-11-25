# MeshCard changes to MeshZero

This folder is a copy of [MeshZero](https://github.com/s0liton/MeshZero) by
Pedro Santos ([s0liton](https://github.com/s0liton)), GPL-3.0-only (see `LICENSE`),
modified for MeshCard. This notice is required by GPL-3.0 section 5a.
MeshZero's README, changelog and notices are kept as written by its author.

- Base: commit `22ec6f9` (17 Nov 2026)
- Changed by: MeshCard (DiaLFonZo, with Claude), 18–20 Nov 2026 and later
- Purpose: the MeshCard screens (home screen, alerts, battery, ADV build on
  the ESP32-S3, new fonts) on top of MeshZero's app for the Cardputer Zero.

The exact changes are the difference between this folder and the base commit
above; the git history of this repository has them in detail.

## Files modified (21)

- `CMakeLists.txt`
- `src/common/history.cpp`
- `src/common/util.cpp`
- `src/ui/app.cpp`, `src/ui/app.h`
- `src/ui/display.h`
- `src/ui/fonts/fonts.c`, `src/ui/fonts/fonts.h`
- `src/ui/main.cpp`
- `src/ui/mesh.cpp`, `src/ui/mesh.h`
- `src/ui/screens/chats.cpp`, `dialogs.cpp`, `editor.cpp`, `map.cpp`, `nodes.cpp`, `radio.cpp`, `setup.cpp`
- `src/ui/ui.cpp`, `src/ui/ui.h`
- `tools/uitest.sh`

## Files added (26)

- `library.json`
- `src/alert/main.cpp`, `src/common/alert.cpp`, `src/common/alert.h`
- `src/ui/battery.cpp`, `src/ui/battery.h`
- `src/ui/esp32_main.cpp`
- `src/ui/screens/home.cpp`
- `src/ui/fonts/embedded_fonts.c`
- `src/ui/fonts/adv/` — 9 pre-rendered fonts (`mc_adv_bold_10/11/13/16/37.c`,
  `mc_adv_medium_10/11/13/16.c`), made from Montserrat with lv_font_conv
- `tools/embed_fonts.py`, `tools/make_adv_fonts.ps1`
- `assets/fonts/Montserrat-Bold.ttf`, `Montserrat-Medium.ttf`, `Montserrat-SemiBold.ttf`,
  `Montserrat-OFL.txt` — [Montserrat](https://github.com/JulietaUla/Montserrat), SIL Open Font License 1.1
- `assets/meshcard-logo.png`, `assets/meshcard-icon-80.png` — MeshCard logo and icon

No upstream file was removed.
