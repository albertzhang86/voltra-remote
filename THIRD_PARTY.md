# Third-party components

Vendored under `firmware/libraries/` so the firmware builds reproducibly. Each keeps its own license; see the upstream projects.

| Component | Version | Upstream | License |
| --- | --- | --- | --- |
| LVGL | 9.1.0 | https://github.com/lvgl/lvgl | MIT (`firmware/libraries/lvgl/LICENCE.txt`) |
| GFX Library for Arduino (Arduino_GFX) | 1.6.7 | https://github.com/moononournation/Arduino_GFX | See upstream |
| Adafruit BusIO | 1.16.2 | https://github.com/adafruit/Adafruit_BusIO | MIT (`firmware/libraries/Adafruit_BusIO/LICENSE`) |
| Adafruit CST8XX Library | 1.1.1 | https://github.com/adafruit/Adafruit_CST8XX | MIT |
| PCF8574 library | 2.3.7 | https://github.com/xreef/PCF8574_library | MIT (`firmware/libraries/PCF8574_library-master/LICENSE`) |
| NimBLE-Arduino | 2.5.1 | https://github.com/h2zero/NimBLE-Arduino | Apache-2.0 (installed via arduino-cli, not vendored) |

Protocol data under `third_party/` is decoded from `@voltras/node-sdk` 0.14.0 by Henry Jewkes, MIT, license included as `third_party/voltra-node-sdk-LICENSE`.

The 150 px numeral font (`firmware/VoltraKnob/src/ui/font_weight_150.c`) was generated from Montserrat Medium (SIL Open Font License 1.1) with `lv_font_conv`.

The upstream project targeted Elecrow; this port uses the Guition mapping described below.

## Guition adaptation

The application/protocol starting point is Omar Shahine's [voltra-knob](https://github.com/omarshahine/voltra-knob), MIT (root `LICENSE`). The original Elecrow hardware driver and screen implementation have been replaced.

`src/board/guition_init.h` transcribes the ST77916 register initialization from the user-supplied Guition `JC3636K718_knob_EN.zip`, `ST77916_LVGL_DEMO/scr_st77916.h`. Pin assignments are from its `pincfg.h`. Directional-pulse encoder behavior was cross-checked against [MichalZaniewicz/esphome-guition-jc3636k718c-va](https://github.com/MichalZaniewicz/esphome-guition-jc3636k718c-va). No ESPHome code is linked into this firmware.

The active UI uses Inter (SIL Open Font License 1.1), sourced from Google Fonts. The font and license are in `third_party/fonts/`; LVGL subsets are in `src/ui/font_inter_*.c`. Inter is a visual approximation, not a verified identification of the VOLTRA device font.

The startup logo uses the user’s original Polar Bear Strength artwork, copied to `assets/polar-bear-strength-original.png`, converted to a white alpha mask by `tools/build_logo.cjs`.

Mode symbol designs reference Beyond Power’s official VOLTRA imagery. The reconstructed SVGs and source URLs are documented in `assets/modes/README.md`; these assets are not manufacturer-supplied vectors. Reference photographs are excluded from the shareable package.

The local `firmware/libraries/ESP_SR` copy comes from Espressif Arduino-ESP32 3.3.11 (original per-file license headers retained). Its command wrapper is modified to clear MultiNet state on each command-listening session in the detection task and expose bounded diagnostic counters, accept explicit MultiNet7 phonemes, enable single-channel noise suppression, and disable WakeNet when starting directly in command mode. The reset follows Espressif's ESP-Skainet English speech-command example. Speech models are supplied by the installed Espressif SDK and used locally.
