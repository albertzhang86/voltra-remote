# Build Voltra Remote

The supplied firmware targets **Guition JC3636K718 / JC3636K718C, ESP32-S3, 16 MB flash, OPI PSRAM, 360×360 display**. Do not use it on an Elecrow or unrelated ESP32 board.

## Tested build environment

Apple Silicon macOS with Xcode Command Line Tools (`xcode-select --install`), Python 3.10+, Git, curl and Bash. Internet access is required for dependency setup. Native Windows and Linux firmware builds have not been validated; use the prebuilt installer if you only need to install firmware.

Pinned dependencies:

| Dependency | Version |
| --- | --- |
| Arduino CLI | 1.5.1 |
| Arduino ESP32 core | 3.3.11 |
| NimBLE-Arduino | 2.5.1 |
| LVGL, vendored | 9.1.0 |
| Arduino_GFX, vendored | 1.6.7 |

The patched ESP_SR wrapper is vendored and must be retained. Speech models are generated from the ESP32 SDK during compilation. Compiled fonts, icons and startup artwork are already included; Node.js and asset generation are not required to compile firmware.

## From a fresh source checkout

Run from the repository root:

```sh
bash tools/setup.sh
bash tools/test.sh
bash tools/build.sh
```

`setup.sh` installs project-local dependencies in ignored `.tools/` and `.arduino/` directories. Its download currently targets Apple Silicon macOS. If packaged ctags cannot run, it compiles the pinned upstream ctags source locally.

`build.sh` specifies the full board configuration and custom partition table. Outputs are in `build/VoltraKnob/`. The generic ESP_SR partition warning is expected with this custom table; `tools/firmware_parts.py` verifies the speech model exists and fits its partition before upload or packaging.

## Install a developer build

Unload the trainer first. Verify the knob's USB serial port and close serial monitors, then:

```sh
PORT=/dev/cu.usbmodemYOUR_PORT bash tools/flash.sh
```

This backs up the full device flash before writing the application and speech models. Keep the backup private. For subsequent uploads where a backup already exists:

```sh
python3 tools/upload.py --port /dev/cu.usbmodemYOUR_PORT
```

## Make a new release

Build and test first. Use a new name; packaging never overwrites an existing release:

```sh
python3 tools/package_release.py --name Voltra-Remote-2026-10-04-r17
python3 releases/Voltra-Remote-2026-10-04-r17/install.py --verify-only
```

Update release notes and version text before creating a later release. Packaging rejects private voice replay fixtures and replay-enabled firmware. Keep recordings, backups, local diagnostic logs and device configuration out of Git.
