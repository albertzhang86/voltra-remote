# Voltra Remote — Guition JC3636K718

Standalone Bluetooth remote for up to two Beyond Power VOLTRA I trainers using the Guition 360 × 360 ESP32-S3 knob. It controls one connection at a time. Adapted from [omarshahine/voltra-knob](https://github.com/omarshahine/voltra-knob).

## Supported modes

| Mode | Knob controls |
| --- | --- |
| Weight Training | Weight, Chains, Inverse Chains, Eccentric added weight; tap main Weight number to load/unload |
| Resistance Band | Maximum weight; tap maximum number to load/unload |
| Damper | Factor with Roman numeral level; tap factor to load/unload |
| Isokinetic | Speed, Return type, eccentric speed/weight and limit; tap main value to load/unload |

Cardio, Custom Curves and Isometric Test are excluded. When the trainer reports an unsupported mode or an unmapped fitness state while unloaded, the knob shows mode selection. Mode changes are blocked while loaded or armed. Some device settings screens reuse the same state as a training screen and cannot yet be distinguished.

## Controls

- Tap the trainer name to return to selection. Names are discovered through Bluetooth and saved in stable slots. Startup requires explicitly selecting a trainer.
- Turn the dial to adjust the highlighted setting. The number grows briefly and keeps its mode color. Device confirmation updates the value; a timeout restores the last confirmed value.
- Swipe left for mode selection. Rotate to highlight, then tap the center icon/text to confirm, or tap an outer icon. Confirmation returns to controls. Swipe right to cancel.
- Weight Training uses four icon/value controls. Selecting Chains or Inverse Chains reads the trainer's starting value and selects its direction, without writing a fixed preset. Eccentric is added weight, not a percentage. Weight can change while loaded; modifier edits require unloading.
- Band uses a filled circle and inverse-color maximum number. Damper shows the device's factor and Roman numeral level. Isokinetic's Auto eccentric speed leaves the large number blank and shows Auto on its tile.
- Swipe right from controls for cable-length adjustment while unloaded. Pull and hold the cable for the trainer to save. The white length display follows its cm/in setting and returns after confirmation. This is length adjustment, not motorized cable retraction.
- The main value and physical ring dim while unloaded. A dot marks the unloaded value. The screen perimeter uses inward-facing clock ticks.

## Units and ranges

The protocol uses integer pounds. Weight Training and its modifiers currently display kg on the observed device scale of 2.2 lb/kg, rounded to 0.5 kg. Band and Isokinetic weight fields follow the reported kg/lb setting; cable length follows cm/in. Weight Training automatic kg/lb matching is not complete.

Protocol limits used here: Weight 5–200 lb; Chains 0–100 lb; Eccentric −195 to +195 lb added weight; Band maximum 15–200 lb. Damper has ten factors: 5, 8, 11, 14, 17, 21, 30, 33, 41, 50. Isokinetic speed is 0.1–2.0 m/s; eccentric speed additionally permits Auto, eccentric weight 5–100 lb, and limit 5–200 lb. Dynamic accessory limits and 250 lb operation are not implemented. Trainer confirmation remains authoritative.

## First connection and Twin Mode

Close Beyond+ or disconnect other Bluetooth clients, select the trainer on the knob, and accept its connection prompt if shown. The knob reads existing settings without automatically loading. It needs only USB power afterward; no computer or phone bridge is required. Disconnecting the knob does not unload a trainer.

Establish Twin Mode on the trainers before connecting the knob. Select the actual controller. The selector's Control/Paired labels reflect the selected Twin session; reliable physical controller-role detection is not available. If the physical controller changes, unload and use Switch controller. A connection to the follower may read values but cannot control it. The firmware does not hard-code either trainer's identity or automatically hand off connections.

Twin Weight Training values account for the combined display. Band maximum and Damper factors use their own mode-specific readbacks. Hardware testing across every Twin role and firmware version is incomplete.

## Build and verification

The local project toolchain is under `.tools/` and `.arduino/` (ignored by Git): Arduino CLI 1.5.1, ESP32 Arduino 3.3.11, NimBLE-Arduino 2.5.1. Vendored LVGL 9.1.0 and Arduino_GFX 1.6.7 are in `firmware/libraries/`.

```sh
tools/test.sh
tools/build.sh
python3 tools/preview.py  # optional: render actual LVGL UI on macOS
```

`tools/setup.sh` installs the pinned local dependencies on a fresh Mac. Arduino's packaged ctags is Intel-only; setup builds its native source on Apple Silicon if necessary.

## USB programming

Manufacturer instructions: disable **HID**, exit Settings to save, then restart the knob. If USB serial still does not enumerate, enter BOOT/download mode. Verify the specific `/dev/cu.usbmodem...` port before proceeding.

```sh
PORT=/dev/cu.usbmodemYOUR_PORT tools/flash.sh
```

The script reads the entire 16 MB flash into `backups/` and records its SHA-256 before uploading. Do not erase the backup; it includes the original app and its configuration. Backups are ignored by Git. Flashing replaces the factory application. To restore, use the same local esptool with `write-flash 0 path/to/backup.bin` in download mode.

Firmware artifacts are in `build/VoltraKnob/`. The build is not equivalent to a hardware acceptance test: verify display, touch, dial direction, pairing with each trainer, and each setting against the trainer screen. Initial motor testing needs a clear cable and low resistance with the trainer's own controls reachable.

## Hardware

| Function | Pins |
| --- | --- |
| ST77916 QSPI | CS 12, clock 11, data 13/14/15/16, reset 17 |
| Backlight | 21 |
| CST816 touch | SDA 9, SCL 10, reset 8, interrupt 7 |
| Knob direction pulses | counterclockwise 2, clockwise 1 |
| Audio mute | 46, low |

The 13-pixel GRB WS2812 ring uses GPIO0 after boot. Screen initialization comes from the user-supplied `JC3636K718_knob_EN.zip` manufacturer demo. The independent directional-pulse encoder behavior is also documented by [the working Guition ESPHome port](https://github.com/MichalZaniewicz/esphome-guition-jc3636k718c-va). The direction signals are sampled every 0.5 ms and must remain stable for 1 ms; this rejects the opposite-pin release spikes captured on the physical knob. This is a board-specific port, not an Elecrow firmware image.

## Current validation

- Host tests cover protocol frames and CRCs, parameter decoding, kilogram rounding, pending confirmation/timeout, limits, freshness, and switch/load gates.
- `tools/preview.py` renders the firmware's actual LVGL layout with simulated data; those images do not show a live trainer.
- Weight Training, Band, Damper and Isokinetic have received user hardware checks during development; not every setting/unit/Twin combination is verified.

## Display appearance

Black background, charcoal controls, Inter font, and mode accents: lime for Weight, turquoise for Band, mint for Damper, yellow for Isokinetic. Inter is a visual match, not a verified identification of the manufacturer font. The included original Polar Bear Strength logo fades in at startup.

The four mode symbols are reconstructed from published Beyond Power references; editable SVGs and provenance are in `assets/modes/`. The same antialiased asset is used throughout the UI. These are not manufacturer-supplied vector files. The 13-pixel ring follows live mode color and loaded brightness, and turns off while disconnected/idle.

## Shareable release

The prebuilt ZIP contains a Python installer, firmware with SHA-256 checks, source and licenses. See `START-HERE.txt` and `RELEASE-NOTES.txt` inside it. Windows/Linux installation has not been hardware-tested. The installer backs up the full flash, then clears saved pairings before installation; the release contains no owner's trainer identities or flash backups.

## Voice control

Release r17 enables local voice control: say “Load weight” to request loading, or “Release” / “Release weight” to request unloading. No wake phrase or touch is required. Load requires the selected trainer’s control page, fresh ready/unloaded state, confirmed settings and no pending edit/action. Release uses the priority unload path and remains available during a pending load or stale status. Commands are tied to the current connection session; duplicate Load cannot toggle into Unload. Voice control runs without bottom-screen labels; the trainer’s reported state drives the normal loaded/unloaded display. Recognition can miss or falsely detect speech; keep the physical unloading control available. The speech model and pronunciation settings are unchanged from the accepted recognition test.

Microphone audio normally stays on-device without recording. Running `tools/capture_voice.py --port PORT --output build/voice-diagnostic` arms the explicit eight-second recording tap and disables voice actuation until reboot. Audio is retrieved locally over USB, with no cloud service. Upload using `tools/upload.py` so the speech-model partition is included. See [release notes](RELEASE_NOTES.md), [build instructions](BUILDING.md) and [test coverage](TESTING.md).
