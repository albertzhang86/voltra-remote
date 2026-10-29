# Testing and verification

## Automated host checks

```sh
bash tools/test.sh
python3 tools/preview.py
```

Requires Bash and clang++ with C++17. Tests cover protocol framing, device state/load gates, remote value editing, encoder glitch filtering, voice event acceptance and action guards, audio conditioning, and bounded recording buffers. They do not send Bluetooth commands or operate hardware.

GitHub Actions runs both the host checks and the actual LVGL interaction/render harness on pushes and pull requests. Local passing results do not establish a successful hosted run.

### Critical control regressions

The LVGL harness sends pointer press/release events through real hit testing for
all four mode buttons. It covers the old-mode → idle → confirmed-mode sequence,
duplicate taps, pending controls, timeout/retry, disconnect and trainer-session
changes. Both observed unloaded states (`4` and `0x0104`) are exercised; arbitrary
high-byte variants, loaded and armed states remain blocked. The protocol suite
includes the captured CRC-valid packet carrying `0x0104`.

Weight tests distinguish the native base minimum (5 protocol lb displays as 2 kg
per trainer) from modifiers and measured force. Actual single/Twin renders and
dial steps cover the 2 ↔ 2.5 kg boundary. Telemetry conversion does not inherit
this base-setting display exception.

The consuming TV app's `KnobWorkoutTransitionTest` runs the real connector and
workout coordinator against packet transports. It covers replacing an old 50 kg
setting with a saved 5 kg prescription, full modifier confirmation before the
countdown, Single → Twin → Single transitions, restoration of a numbered direct
peer, fresh rep baselines, rep/time completion and a non-VOLTRA timer exercise.
Those replay checks do not replace physical Bluetooth or cable-motion acceptance.

### Firmware 2.0 mode and motor reports

The 2026-10-28 VTR-161230 capture in `test_motor_worker.cpp` confirms selected
mode `1` independently of motor register `0x3e89`. Native trainer-control tests
then captured loaded `1` and unloaded `0`, both with valid CRCs. Mode confirmation
alone does not grant load permission. Raw mode and motor words stay intact;
unknown flag combinations remain blocked instead of being masked into readiness.

The shared motor policy recognizes the observed `0/1` pair as well as legacy
`4/5`, loaded `21`, flagged unloaded `0x0104`, and armed `0x26/0x27`. Motor writes
use `1/0` for the new pair and retain the existing `5/4` legacy command family.
Knob tap/voice and TV bridge requests use the same one-shot worker: confirmed
mode/topology, unchanged mode-specific settings, new SETUP cable-zero readback,
one motor write, then a new device report. Unknown/extended cable, stale mode,
missing configuration, changed identity, cancellation and absent confirmation
cannot be bypassed or trigger a repeated load. Immediate unload still preempts
queued work. Only user input initiates a load.

Host tests replay the native packets through production receive code, exercise
both command families through the real worker for local and TV requests in
Single/Twin, and verify actual LVGL tap targets across supported mode screens.
TV direct and bridge tests cover command/readback and telemetry equivalence.
Hardware acceptance of the new command pair is still pending; native-state
capture and simulated write responses do not prove a successful hardware write.

Mode requests require a new mode-register revision, not a motor notification.
Reconnect clears mode freshness. Known one-byte fields `0x5102`, `0x4ee1`, and
`0x5124` are skipped in cascades; unknown-width fields stop parsing. Widths were
cross-checked against `@voltras/node-sdk` 0.15.0. Their semantics, the 230 lb
limit, and percentage-modifier handling are outside this compatibility fix.

### Acknowledged polling regressions

`tools/test.sh` also runs five Python patch-integrity tests and a compiled harness
using the pinned NimBLE 2.5.1 write implementation, GATT write/response functions
and FreeRTOS completion helpers. It reproduces an acknowledgement arriving after
TX but before procedure registration, then verifies that opt-in host-initiated
polling preserves TX → registration → acknowledgement ordering. Cases include
completion before the worker waits, initiation failures, disconnect/replacement
before dispatch, authentication retry, invalid requests, patch idempotence and
version/source guards. Control writes, long writes and no-response writes retain
their original path. Fixtures and provenance are under `test/fixtures/nimble-2.5.1`.

The production background scheduler is exercised across all four modes, fresh
and missing telemetry streams, mode changes, timer rollover, a 30-second stalled
write, 0–200 ms acknowledgement jitter and sustained 200 ms acknowledgements.
One query follows each quiet interval; mode fields and units cannot be starved
by optional wire-register diagnostics. Under tested slow acknowledgements the
largest full-mode-field refresh interval was 6.9 seconds and the largest critical
read interval was 1.8 seconds. Existing readiness limits remain unchanged;
explicit motor operations still require their stricter fresh cable readback.

Authenticated TV loading has separate preparation and GO admission gates.
Snapshot mask bit `0x1000` advertises that a load request may perform SETUP and
readback even when its background cable cache is absent or stale. Existing
`0x4000` still means a fresh verified cable-zero reading. Before GO, the worker
requires newer cable and saved-zero replies, refreshed unchanged settings, and
the same trainer session, training mode, and Twin peer. Neither admission bit
alone authorizes the motor write. The host suite exercises this production
commit gate for all four modes, single/Twin, both verified unloaded statuses,
missing/stale/extended cable, changed settings, and revoked endpoint/session.
These tests do not establish that physical Twin readbacks arrive successfully.

`test_motor_worker.py` compiles the production `applyControllerFrame` and
`performMotor` functions unchanged against a host BLE/time harness. It replays
the CRC-valid ordinary and SETUP replies captured from VTR-161230 on 2026-10-17
(`test_motor_worker.cpp`), both reporting position 2 cm and saved zero 0 cm while
the user reported the cable fully returned. The decoder preserves those values;
the existing strict `< 2 cm` gate rejects them without sending GO. A separately
marked synthetic zero reply permits exactly one GO followed by loaded-state
confirmation. Missing readback and missing confirmation cannot trigger retries,
guided loading, or calibration writes. These checks cover single and Twin
topologies; they do not establish why the physical trainer reports 2 cm at rest
or authorize changing its zero calibration or load policy.

These tests eliminate a reproducible local race and check request scheduling.
They do not establish the cause or resolution of every physical ATT timeout.
VTR-161230 advertises acknowledged writes only (`write=1 noResponse=0 mtu=247`),
so its hardware validation must show `pollTransport=host` before testing cable
pulls. Capture slow/failed writes and disconnect reasons during that acceptance
test; an unloaded capture alone is insufficient.

## r17 verification

Local test and compile output are supplied under `release-evidence/`. The firmware was compiled on Apple Silicon macOS and installed with flash hash verification. Voice initialization reported two registered commands, no audio/init errors, and active control enabled. The user confirmed the screen responsiveness fix. Recent idle UI-loop gaps were 7–8 ms while the recognizer kept pace with incoming audio.

Recorded replay and a small live trial informed the selected Load pronunciation. The live trial recorded three Load detections; earlier Release trials were accepted by the user. This is not a measured general recognition accuracy or false-trigger rate. No automated physical load/unload was performed to verify voice actuation; actual motor response through voice remains unverified in this release.

## Hardware checks for a new board/trainer

With the cable clear, low resistance and the trainer's own controls reachable, check display/touch, dial direction, trainer selection, displayed settings/units, and supported mode behavior. Confirm physical unload remains available. Inspect the actual trainer state after a voice request; speech recognition is not an emergency-stop system.

Repeat relevant checks for different firmware versions and Twin roles. When testing an existing Twin, select its actual controller. Do not infer controller identity from connection order.

The native LVGL UI harness (`python3 tools/preview.py`) also passed locally, including tap-number load, loaded weight edits, mode-menu navigation, connection session and trainer availability checks. It renders simulated screens and does not operate a physical trainer.

## Native Twin regression checks

Host tests distinguish request admission from native topology readiness. All four
supported modes can admit an explicit local or TV Create/Exit request while
unloaded, loaded (`5`/`21`) or in known armed states (`0x26`/`0x27`). Native
idle/pair writes remain blocked until a one-STOP preparation receives a newer,
same-identity unloaded report. Tests cover failed writes, stale or changed state,
cancellation, exact TV handoff authorization and occupied saved slots. Unknown
fitness values never gain permission through those preparation gates.

The actual LVGL pointer fixture exercises Create/Exit in each supported mode and
known loaded/armed state, duplicate-tap blocking while pending, and completion
with unloaded `4` or `0x0104`. Nearby/remembered peers, unavailable and rejected
actions, and discovery on explicit selector opening are also covered. A trusted
TV connection or pairing window must not be interrupted by opening selection.

Consuming-library tests verify that cancelling or timing out an accepted TV Twin
request revokes the command session before closing the link and never resends the
topology command. App tests cover delayed edit/Twin/load readiness after either
an automatic or local unload, countdown activation, and compatible loaded sets
without redundant STOP/GO. These tests do not prove physical motor responses.

Packet-assembly tests replay captured telemetry, endpoint and settings layouts
split at 20-byte boundaries, with synthetic device identities. They also cover
coalesced packets, CRC/header corruption, bounded noise, connection resets,
independent controller/peer buffers, partial-frame expiration and timer rollover.

Mixed-handoff tests distinguish the short offer admission deadline from an
accepted transaction's fixed 40-second deadline. They cover a delayed heartbeat
during the peer handshake, rejection of a new request using an expired offer,
explicit revocation, changed peer/TV/trainer session, and uptime wraparound.
Refreshing an offer cannot extend an accepted transaction or revive a revoked one.

For hardware acceptance, first confirm Create/Exit from independent unloaded
trainers and verify that trainer screens, knob and TV agree. Then, using the
user's chosen comfortable settings with cables clear, request Create from a
loaded single and Exit from a loaded pair without a separate manual unload.
Verify automatic unloading, confirmed topology, previous-mode restoration and
that manual actions leave resistance unloaded. Cover both TV and knob initiation,
including a mixed connection with the knob trainer as controller.

For planned workouts, check two compatible consecutive sets, Twin, then single,
with mode-specific presets. A single Start should apply each preset, complete its
countdown and confirm loaded before timing/reps begin. Compatible loaded sets
must avoid redundant STOP/GO. Pause before countdown completion and confirm no
later queued load; explicit Resume must prepare again. Reconnect alone must not
resume or load. A missing/externally owned peer or unconfirmed state must give a
clear failure without a success claim or automatic motor retry.

Repeat relevant checks across supported modes, role reversals and reconnects.
A physical Create/Exit cycle with Weight Training restored after both transitions
passed on October 15. That earlier unloaded test does not verify the new automatic
unload or planned-set loading paths; host tests and a firmware build do not replace
physical acceptance.

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

2026-10-28 hardware acceptance: user confirmed knob taps load and unload
VTR-161230 in Weight Training after both updates. USB capture confirms
`0 → 1 → 0`, successful one-shot load, touch STOP writes, and live moving-force
rendering with motor `1`. TV reconnected and reports both knob and trainer ready.
Direct-TV motor control, TV-initiated motor commands, Twin and other modes
remain covered by regression tests but await Firmware 2.0 hardware acceptance.

2026-10-28 device limits and units: installed firmware reads VTR-161230's
`0x5314` allowed-force ceiling as 229 protocol pounds, `0x506e` as kg, and
`0x506f` as cm. Repeated USB reports confirm the values with Weight Training
selected and resistance unloaded. The updated physical Google TV reconnects
to the trusted knob and shows both knob and trainer ready. Captured v3 bridge
snapshots carry the same device-sourced 229-pound ceiling and kg/cm units.
No resistance settings or motor commands were changed during this check.

Host tests and actual LVGL previews cover kg/lb, cm/in, unknown units, dynamic
limits, out-of-range rejection and the separate Isokinetic bounds. Android
tests cover both direct and knob parsing/control paths, including v3 snapshots
with v2 commands. Physical lb/in switching and direct-TV range/unit verification
remain pending; the kg/cm knob-to-TV check does not establish those results.

Saved-peer Twin regression: with the TV radio active and advertisements expired,
an explicit Create request can check the exact saved peer. The trainer is never
marked nearby from its saved identity. Tests cover both controller slots, all
four modes, Firmware 2.0 unloaded/loaded words, legacy states, missing peers,
competing advertisements, stale identities and concurrent work. Native topology
authorization still requires fresh independent/unloaded participant readbacks.
Real LVGL pointer tests verify an enabled action, one pending request and blocked
duplicate taps, without disconnecting the TV or starting scanning.

2026-10-28 saved-peer Twin hardware acceptance: after installing the initial
saved-peer fix, the user selected the controller and confirmed Create Twin
connects. Subsequent USB and physical-TV snapshots confirmed a paired endpoint
with the expected controller and peer. Later ownership/mode recheck refinements
were included in the subsequent setup-progress installation described below;
they were not part of that initial hardware acceptance.

Firmware 2.0 Twin rep verification: the user performed three Weight Training
reps and reported a final count of 3 on VOLTRA. Captured B4/source-0x12 packets
report set counter 2 and completed counts 1, 2, 3 at knob uptimes 282532,
284512 and 286312 ms. The knob forwarded exactly 1, 2, 3 to the physical TV,
whose Just Train dashboard visibly showed 3 after unloading. The knob itself
does not expose a rep counter. No rep-offset change was needed.

The previous count of 4 persisted until the first completed rep; no zero-count
packet was captured between sets. Replaying this 4-to-1-to-2-to-3 sequence in
the Android controller preserves all three reps and retains 3 on unload.
The three device packets are retained as protocol fixtures without device IDs.
This verifies Weight Training over the knob route only; other modes and direct
TV rep counting were not physically retested in this run.

Setup progress regression: actual LVGL preview tests keep Creating/Exiting Twin
visible across Ready, Scanning, Connecting, Handshaking, trainer-session changes
and temporary native mode 0. Dial input is discarded during the overlay; returning
to normal state hides it. Bridge tests cover expired and replayed display messages,
session/epoch mismatch, cancellation, unknown activities and millis wraparound.
Display heartbeats carry no motor-command ID and do not authorize control.

2026-10-28 setup-progress deployment: flashed the app partition only, verified
the written hash, and retained saved devices and TV trust. The USB RTS reset
left the flasher stub running; an esptool watchdog reset booted the application.
USB logs then confirmed the normal UI loop and trusted TV authentication. The
physical TV received v3 snapshots advertising setup-display support and later
reported the selected trainer ready. The user confirmed that Creating Twin
remains visible through the reconnect steps and Twin connects. Exiting Twin and
an automatic workout transition still need visual hardware acceptance; the host
and LVGL tests cover these paths. No load commands were sent during installation.
