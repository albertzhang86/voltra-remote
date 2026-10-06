# Voltra Remote r17 — voice control and smoother screen updates

Tag: `r17`  
Release date: 2026-10-04  
Suggested GitHub status: **Pre-release**

For Guition JC3636K718 / JC3636K718C ESP32-S3 knobs with 16 MB flash and a 360×360 display.

## Changes since r16

- Offline, wake-free voice control: “Load weight” requests loading; “Release” or “Release weight” requests unloading. No phone or cloud speech service is required.
- Load uses ready/unloaded state, confirmed settings, current mode and connection checks. Release uses the priority unload path, including during a pending load. Commands are explicit actions, not toggles.
- Faster screen updates, partial redraws and higher UI scheduling priority. Voice feedback no longer covers controls, and bottom voice text is removed.
- Refined mode-selector layout, centered Damper value, loaded cable-page guard, and cleaner trainer selection with nearby-device availability.
- Preserves the corrected directional encoder filtering and confirmed-value handling.

## Supported modes

Weight Training, Resistance Band, Damper and Isokinetic. Unsupported modes route to the mode selector while unloaded.

## Important limitations

Voice can miss speech or trigger incorrectly. Keep physical unloading available; disconnecting or powering off the knob does not unload the trainer. Voice recognition received limited local tests; this release does not establish reliability during exhaustion or loud music, and physical voice-driven motor response has not been verified by the agent.

Weight Training/modifiers currently display kg; automatic kg/lb matching there is incomplete. Band/Isokinetic weight fields and cable length follow their reported units. Twin controller-role detection is not reliable: configure Twin Mode first and select the actual controller. No 250 lb/accessory-limit discovery or motorized cable retraction.

Mode icons are reconstructions, and Inter approximates the manufacturer font. Included startup artwork remains the owner's original logo. This is an independent community project, not an official Beyond Power product.

## Verification

Host protocol/control/encoder/voice/audio tests, native LVGL UI interaction checks and ESP32-S3 compilation pass; see `TESTING.md` and `release-evidence/`. Firmware installed locally with hash verification; the user confirmed the screen lag fix. Installer archives include per-firmware checksums and an archive SHA-256. Windows/Linux installation remains hardware-untested.

## Downloads

- `Voltra-Remote-2026-10-04-r17.zip`: ready-to-flash installer, firmware, speech models, full source, build/test instructions and licenses.
- `Voltra-Remote-2026-10-04-r17-source.zip`: source checkout suitable for a GitHub repository, without toolchain caches, recordings or device backups.
- Matching `.sha256` files verify each ZIP.

Extract the installer ZIP and follow `START-HERE.txt`. No compilation is needed to install the prebuilt firmware. Existing users should unload before updating; the installer backs up flash and clears saved pairings.
