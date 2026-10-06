# Testing and verification

## Automated host checks

```sh
bash tools/test.sh
```

Requires Bash and clang++ with C++17. Tests cover protocol framing, device state/load gates, remote value editing, encoder glitch filtering, voice event acceptance and action guards, audio conditioning, and bounded recording buffers. They do not send Bluetooth commands or operate hardware.

GitHub Actions runs these checks on pushes and pull requests. The workflow is included but has not been run on GitHub before publication.

## r17 verification

Local test and compile output are supplied under `release-evidence/`. The firmware was compiled on Apple Silicon macOS and installed with flash hash verification. Voice initialization reported two registered commands, no audio/init errors, and active control enabled. The user confirmed the screen responsiveness fix. Recent idle UI-loop gaps were 7–8 ms while the recognizer kept pace with incoming audio.

Recorded replay and a small live trial informed the selected Load pronunciation. The live trial recorded three Load detections; earlier Release trials were accepted by the user. This is not a measured general recognition accuracy or false-trigger rate. No automated physical load/unload was performed to verify voice actuation; actual motor response through voice remains unverified in this release.

## Hardware checks for a new board/trainer

With the cable clear, low resistance and the trainer's own controls reachable, check display/touch, dial direction, trainer selection, displayed settings/units, and supported mode behavior. Confirm physical unload remains available. Inspect the actual trainer state after a voice request; speech recognition is not an emergency-stop system.

Repeat relevant checks for different firmware versions and Twin roles. Set Twin Mode on the trainers first and select the actual controller. Do not infer controller identity from connection order.

The native LVGL UI harness (`python3 tools/preview.py`) also passed locally, including tap-number load, loaded weight edits, mode-menu navigation, connection session and trainer availability checks. It renders simulated screens and does not operate a physical trainer.

## Installer verification without hardware

```sh
python3 install.py --verify-only
```

Run inside the extracted installer bundle. This verifies every firmware SHA-256 without accessing USB. The installer includes Windows/macOS launchers, but Windows/Linux flashing has not been hardware-tested.

Private microphone recordings and local diagnostic logs are intentionally excluded from this release.
