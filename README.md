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

- Tap the trainer name to return to selection. Scroll through saved trainers and nearby discoveries; selecting a new trainer saves it in a free slot. The two saved slots are never replaced automatically. Startup requires explicitly selecting a trainer.
- Swipe right on trainer selection for **TV link**, **Manage devices**, and **Find nearby trainers**. Swipe left to return to the trainer list.
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

Disconnect the trainer from Cortex, Beyond+ and any direct TV connection before
selecting it on the knob. Each trainer accepts one controlling client at a time;
closing another app's window may leave its Bluetooth connection active. An occupied
connection can cause a timeout, but a timeout alone does not prove the cause.
When using the TV, select **Via knob** so it connects to the knob's bridge instead
of competing for the trainer. Accept the trainer's connection prompt if shown.
The knob reads existing settings without automatically loading. It needs only USB
power afterward; no computer or phone bridge is required. Disconnecting the knob
does not unload a trainer.

Trainer discovery pauses before TV advertising starts and stays
paused while listening for a trusted TV or while the TV link is active. Saved trainers remain
selectable as **Saved · Reconnect**; their connection and fresh endpoint state still
have to be verified before controls are enabled. A saved identity is not labeled
nearby without a recent advertisement or a fresh active connection. When no TV is
connected or pairing, tapping the trainer name to open selection starts a
30-second discovery window. To search again, swipe right and use **Find nearby
trainers**. An active TV connection must first be disconnected.
This avoids a reproduced ESP32 Bluetooth scheduler assertion (`sch_arb.c:517`)
when the TV connects during scanning. A
[related Espressif report](https://github.com/espressif/esp-idf/issues/18063)
describes the same assertion with concurrent peripheral connections and scanning;
the local crash log established this knob's failure. After the fix, the physical
Google TV completed PIN pairing and authenticated, encrypted bridge telemetry.

Select the intended controller, unload both machines, then open trainer selection
and choose **Create Twin**. Pairing is enabled when exactly two distinct trainers
are nearby and one is connected. A remembered peer keeps the action visible when
unavailable, with a disabled button and a status message. A new peer can be saved
in a free slot; occupied saved slots are never replaced.

The knob temporarily connects to the second trainer, verifies both identities and
unloaded states, enters idle on both, and sends the controller's address to the
peer. Weight Training, Resistance Band, Damper and Isokinetic can all start this
flow while unloaded. Success requires acknowledgment plus matching native endpoint
identities. **Exit Twin** confirms independent status and keeps the selected
controller connected. The previous controller mode is restored only after the
topology change is confirmed; a failed restoration prompts choosing a mode.
Neither action loads/unloads a trainer or replays after a disconnect.

Bluetooth replies may span several notifications after a Twin change. Controller
and temporary-peer packets are assembled separately, checked against both packet
checksums, and discarded if incomplete for a second or if their connection changes.
Only complete packets update trainer state or confirm a command.

Existing Twin controllers are recognized from the endpoint message, with both
batteries and the actual peer identity. Stale or unknown topology blocks controls.
The selector cannot switch to a paired peer as if it were independently available.
There are no hard-coded hardware identities or automatic controller handoffs.

Twin Weight Training values account for the combined display. Band maximum and Damper factors use their own mode-specific readbacks. Hardware testing across every Twin role and firmware version is incomplete.

## Build and verification

The local project toolchain is under `.tools/` and `.arduino/` (ignored by Git): Arduino CLI 1.5.1, ESP32 Arduino 3.3.11, NimBLE-Arduino 2.5.1. Vendored LVGL 9.1.0 and Arduino_GFX 1.6.7 are in `firmware/libraries/`.

```sh
tools/test.sh
tools/build.sh
python3 tools/preview.py  # optional: render actual LVGL UI on macOS
```

`tools/setup.sh` installs the pinned local dependencies on a fresh Mac. Arduino's packaged ctags is Intel-only; setup builds its native source on Apple Silicon if necessary.

## TV Bluetooth bridge (development)

Swipe right on trainer selection and open the large **TV link** button on the
connections page. **TV link**, **Manage devices**, and **Find nearby trainers**
use matching 240 × 56 px buttons, 16 px labels, and outline icons with clearance
from the circular display edge. The page is viewable while loaded; starting pairing remains
disabled with an unload prompt until the trainer allows it. Choose
**Pair TV**, then **VOLTRA → Via knob → Find knob** in Super AI Gym. Enter the
knob's displayed six-digit code in Android's Bluetooth pairing prompt. The bridge
requires authenticated encryption; new pairing is available for two minutes.
After pairing, use **Reconnect knob** on the TV without opening a knob menu or
entering another code, including after reboot. The knob advertises for saved,
authenticated Secure Connections identities; new TVs require **Pair new TV**.
**Disconnect TV** drops the current session while preserving the saved bond.
The TV does not automatically reconnect after an explicit disconnect.

Trusted TV advertising pauses trainer discovery to avoid the reproduced Bluetooth
scheduler crash. Saved trainer identities remain selectable after reboot; fresh
connection/readback is still required before control. Opening trainer selection
from controls starts discovery when no TV is connected or pairing; **Find nearby
trainers** on the connections page starts another window. TV listening resumes
after 30 seconds. Build/setup apply `tools/patch_nimble.py` to pinned NimBLE 2.5.1,
adding an application repeat-pairing callback. Replacement of a saved bond is
rejected outside an explicit pairing window, before the old bond is deleted.

To reset a device connection, swipe right on trainer selection, choose **Manage devices**,
choose the saved trainer, then confirm **Forget**. The knob closes that trainer
connection and removes its saved entry without changing trainer settings or Twin
mode. Forgetting is blocked while loaded, armed, stale while connected, or busy.
**TV link → Forget TVs** separately removes authenticated TV bonds and disconnects
the TV; saved trainers and their bonds remain. Both actions have a cancelable
confirmation. Also forget the knob on the TV before pairing again after a TV reset.

Use **TV link → Find trainers** to discover a forgotten trainer again. A just-forgotten
advertisement is ignored during the current search so it does not immediately
repopulate the saved list; explicit discovery or reboot starts a fresh search.
Forgetting never sends load, unload, or Twin commands.

The TV follows the knob's selected trainer and can request the four mode/settings
changes; the existing knob command gates and trainer confirmation remain authoritative.
Local draft edits block TV changes. The bridge cannot load/unload, select a trainer,
or start training. Bridge v2 supports Create/Exit Twin with verified roles and both
batteries; its pair/exit request requires unloaded trainers and confirms idle before
changing topology. Twin live force/reps are withheld
until validated from hardware, rather than doubling a single cable reading.
Expired commands or commands for an old trainer/TV connection are rejected.
The primary number shows the configured setting at rest, including loaded without
cable movement. Fresh independent B4 force replaces it during loaded movement;
stopping or losing fresh motion returns to the setting. Explicitly unloaded state
dims the configured number and adds a dot in all four modes. Damper uses Factor
at rest; Isokinetic uses speed in m/s. Editing and modifier pages keep their own
values. Measured force uses SI kg conversion to one decimal; settings retain the
trainer's observed half-kg display grid.

Movement requires force and signed cable velocity from the same B4 sample, at
least 10 mm/s in either direction, less than 500 ms old. The common independent
B4 fields are force at payload +0 (u16, tenths lb), cable length at +2 (u16, mm),
and velocity at +4 (i16, mm/s), as decoded by Cortex. Residual cable tension or
absolute cable extension alone never proves movement. Session/load/mode/topology
changes and legacy force-only packets clear motion evidence. Native Twin motion
is not inferred from one trainer.

Separately, independent target resistance uses the trainer's reported target while
loaded, and zero only when fresh fitness state 4 explicitly reports unloaded.
Unknown/armed load states, stale or missing target readings, unsupported modes,
and Twin topology report unavailable. Target readings expire after two seconds
and are cleared across mode, load-state, topology, and connection changes. Neither
measured cable tension, the runtime wire-weight register (`0x3e83`, signed whole
lb), nor configured set weight (`0x3e86`) substitutes for a missing target.
Measured force and four-per-second runtime queries remain separate diagnostics.
Bridge v2 byte 133 bit 6 identifies target resistance (or explicitly unloaded zero)
at byte 22 in tenths of a pound. Bit 7 instead identifies fresh measured force
during confirmed loaded movement; bits 4, 6 and 7 are mutually exclusive. Moving
force takes priority at offset 22; target resistance is sent otherwise. Packet
length is unchanged, so older readers treat bit-7 values as measured force rather
than target resistance. Legacy bit 4 is no longer emitted.

The runtime register's readback and whole-lb field agree with a physical unloaded
capture (1 lb while the base setting was 6 lb). Moving-cable validation is pending.
The captured trainer reports the newer `0xb4` runtime stream. Its independent
source variants 0/1/2 are decoded according to Cortex's field contract: the first
payload word is force in tenths of a pound; the optional trailing word is target
force. Real 21/25-byte capture fixtures verify this distinction (1.0–1.2 lb cable
tension versus 6 lb target). An absent target or its `0xffff` sentinel clears the
target cache. Matching target resistance to the trainer's dynamic on-screen value
still requires physical validation. Native Twin variants and
unidentified `0xaa94` batches are not interpreted. The existing telemetry rate was
already 10 Hz, so this change does not alter the rate or training settings. Bounded USB diagnostics enqueue
one raw notification per family and print from the worker without blocking BLE.
Bridge v2 also reports the knob's display unit in optional flags. Weight Training
reports kg, matching the knob's half-kg setpoint grid (59 raw lb → 27 kg); Band,
Damper and Isokinetic follow the unit read from VOLTRA. The active-weight field uses
tenths of a pound on the wire, independent of the display preference. USB diagnostics
include unit, measured force, target, runtime weight, and sample ages.

Protocol definitions are in `src/tv/bridge_protocol.h` and the public `android-voltra`
library's `docs/knob-bridge.md`. Host tests and compilation pass. The development
firmware has been flashed and real Google TV PIN pairing, authenticated bridge
telemetry and simultaneous trainer connection pass. The TV receives trainer
battery, mode and set weight. TV-issued setting changes and native Twin through
the bridge still need physical validation. This firmware has not been packaged
as a release.

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

**Unreleased mitigation (2026-10-12):** voice-triggered loading and unloading are temporarily disabled in the current source after a report of unintended unloading. Touch control remains active. Published r17 still enables voice actuation; the cause is under investigation. Recognition continues only for diagnostics, with no bottom-screen text.

Release r17 enables local voice control: say “Load weight” to request loading, or “Release” / “Release weight” to request unloading. No wake phrase or touch is required. Load requires the selected trainer’s control page, fresh ready/unloaded state, confirmed settings and no pending edit/action. Release uses the priority unload path and remains available during a pending load or stale status. Commands are tied to the current connection session; duplicate Load cannot toggle into Unload. Voice control runs without bottom-screen labels; the trainer’s reported state drives the normal loaded/unloaded display. Recognition can miss or falsely detect speech; keep the physical unloading control available. The speech model and pronunciation settings are unchanged from the accepted recognition test.

Microphone audio normally stays on-device without recording. Running `tools/capture_voice.py --port PORT --output build/voice-diagnostic` arms the explicit eight-second recording tap and disables voice actuation until reboot. Audio is retrieved locally over USB, with no cloud service. Upload using `tools/upload.py` so the speech-model partition is included. See [release notes](RELEASE_NOTES.md), [build instructions](BUILDING.md) and [test coverage](TESTING.md).

### Knob battery indicator

A battery icon and estimated percentage stay at the top of every page. When a USB host is detected, a lightning bolt inside the battery replaces the percentage on both the knob and TV. Detection uses the ESP32-S3 USB controller's SOF activity and does not require an open serial monitor. It means USB power is connected, not that battery charging or charge completion was measured. This board exposes no charge-status input or fuel gauge, so charge-only adapters and wireless chargers cannot be detected reliably.

The percentage uses the knob’s GPIO6 voltage divider, averaged and smoothed independently of the trainer battery. `--%` means voltage is unavailable, invalid, or elevated by external power without a detected USB host. Unknown voltage never implies charging. This is a voltage estimate (3.2–4.2 V), not a fuel gauge; power and load affect accuracy. Pin/divider reference: [Guition board configuration](https://github.com/MichalZaniewicz/esphome-guition-jc3636k718c-va/blob/main/base/core.yaml).

Native Twin protocol/gate tests, bridge tests and rendered UI checks pass, and the
ESP32 firmware compiles. The matching direct TV connector has passed an unloaded
physical pair/exit/reconnect cycle. Knob/TV pairing and a simultaneous trainer
connection also pass on hardware. The October 15 knob build passed a physical
Create Twin / Exit Twin cycle, restoring Weight Training after both transitions.
Other training modes and role reversals remain outside that physical check.

The October 13 v2 firmware was flashed with a full device backup. Hardware boot
verification caught and fixed the GATT-service/scanning startup order; the corrected
build boots and discovers both trainers. A second reproduced crash during TV
connection was fixed by pausing trainer discovery before advertising and throughout
the TV link. Authenticated TV-to-knob pairing and telemetry now pass, including
connecting the selected trainer while the TV stays connected. The TV displayed
trainer battery, mode and set weight without a restart during the check.
