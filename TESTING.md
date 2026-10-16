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

## Native Twin regression checks

Host tests cover admission from all four supported unloaded modes, loaded/armed
rejection, peer identity and freshness, and acknowledgment plus matching endpoint
confirmation. The native UI fixture covers nearby and remembered peers, disabled
actions, rejected requests, and discovery on explicit selector opening. A trusted
TV connection or pairing window must not be interrupted by opening selection.

Packet-assembly tests replay captured telemetry, endpoint and settings layouts
split at 20-byte boundaries, with synthetic device identities. They also cover
coalesced packets, CRC/header corruption, bounded noise, connection resets,
independent controller/peer buffers, partial-frame expiration and timer rollover.

Mixed-handoff tests distinguish the short offer admission deadline from an
accepted transaction's fixed 40-second deadline. They cover a delayed heartbeat
during the peer handshake, rejection of a new request using an expired offer,
explicit revocation, changed peer/TV/trainer session, and uptime wraparound.
Refreshing an offer cannot extend an accepted transaction or revive a revoked one.

For hardware acceptance, start with both trainers independent and unloaded. Select
the intended controller, open trainer selection, create Twin, and confirm the
trainer screens and knob agree. Exit Twin and confirm independent state, then
repeat from each supported mode. Check that the previous mode returns after a
confirmed change, and that an unavailable or externally connected peer produces a
clear failure without a success state. Repeat after reconnecting and with a saved
TV bond but no TV connected. A physical Create/Exit cycle with Weight Training
restored after both transitions passed on October 15. Other modes, role reversals,
reconnect and unavailable-peer scenarios still need hardware coverage; host tests
and a firmware build alone do not verify those Bluetooth transactions.

On October 16 the mixed TV-button flow also passed: the TV released its direct
VTR-126226 connection, the knob paired it with VTR-161230 as controller, and Weight
Training returned automatically. Native creation plus restoration took 12.83
seconds; exit plus mode restoration took 1.51 seconds. Both trainers stayed
unloaded, configured settings were unchanged, and the TV restored its original
direct peer connection and device numbers. The TV's uppercase offered MAC and
knob's lowercase endpoint MAC now confirm without an additional timeout.
The user subsequently confirmed on October 16 that knob-button Create Twin and
Exit Twin also work. This is user-reported acceptance; the recorded diagnostics
above cover TV-button initiation. Automatic exercise topology changes remain
unverified on hardware.

## Installer verification without hardware

```sh
python3 install.py --verify-only
```

Run inside the extracted installer bundle. This verifies every firmware SHA-256 without accessing USB. The installer includes Windows/macOS launchers, but Windows/Linux flashing has not been hardware-tested.

Private microphone recordings and local diagnostic logs are intentionally excluded from this release.
