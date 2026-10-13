# Mode support audit

The installed r15 firmware has full Weight Training controls. A mode icon alone does not mean its settings or start sequence are supported. Work is ongoing; do not advertise all modes as validated.

| Mode | Settings to implement/read back | Start/session workflow | Hardware validation |
|---|---|---|---|
| Weight Training | Weight, shared Chains load/direction, signed Eccentric | Existing fresh-state guarded load/unload | Existing user testing |
| Resistance Band | Maximum force, length, length-from-ROM, curve, inverse | Read cable position, mode-specific ready/load confirmation | Pending; user prepared Band |
| Damper | Level 1–10 (wire 0–9), assist | Confirm mode-specific ready/load | Pending |
| Isokinetic | Concentric speed, eccentric type, eccentric speed/constant weight, eccentric load limit | Confirm speed units and state transitions | Pending |
| Isometric | Body weight, duration, test metric, maximum force | Position capture, test/armed state and result notifications | Pending; cannot reuse weight UI |
| Rowing | Resistance, simulated wear, Just Row/distance session | Separate screen-switch start; fitness21 is active, not fitness5 | Pending; no strength GO command |
| Custom Curves | Curve profile, ROM and force range | Vendor curve payload and acknowledgement, not base-weight alone | Protocol incomplete in current firmware |

## Sources

- Official mode descriptions: https://help.beyond-power.com/en/articles/14167613-all-modes-at-a-glance
- Official control reference: https://cortex.beyond-power.com/docs/cortex/training-control
- Local captured-protocol reference: /tmp/voltra-android/core/protocol/src/main/kotlin/com/technogizguy/voltra/controller/protocol/VoltraControlFrames.kt and /tmp/voltra-android/PROTOCOL_NOTES.md
- Vendor parameter catalog: /tmp/voltra-android/app/src/main/assets/voltra/paramInfo.csv

## Current diagnostic work

The center mode button now contains both icon and text. The mode parameter catalog issues isolated read-only queries for the selected mode, accepts integer replies of1/2/4 bytes, and records reported values/widths. Unsupported replies do not invent defaults. Session/mode changes invalidate old parameter timestamps. This is discovery for subsequent controls, not completed support.

Band's maximum resistance must be labeled Max kg or Max lbs using the device unit. Never reuse the base Weight Training register for it. Before enabling writes, verify each field's units, range and readback on the user's firmware. Confirmation must be the device reply, not a locally assigned desired value. Queued writes must carry both trainer session and expected mode to prevent stale controls writing after a mode change.

## Resistance Band visual/range reference

Official newer UI reference: https://cdn.shopify.com/s/files/1/0591/1108/0072/files/Resistance_480x480.jpg?v=1724668598 linked from https://www.beyond-power.com/blogs/news/the-autumn-voltra-i-update-launches-now . Visually inspected in browser: black background, bright turquoise highlight with dark numeral and Max lbs label. User requests circular highlight around the maximum number on the knob. Preserve mode-specific turquoise color and device kg/lbs setting. The article states15–200lb per unit,1lb steps; older August2 video states105lb maximum and is superseded by this August26 update. Do not use the old video range. Exact device kg rounding/ceiling and firmware-specific limits need live readback comparison.

## Live Band readback (2026-10-03)

The installed diagnostic successfully reads mode2 registers: 0x5362 u16 maximum=106lb, 0x53b7 u16 length=260cm, 0x53b6 u8 ROM=0, 0x5361 u8 curve=0, and 0x52e3 u8 inverse=0. The device reports unloaded (fitness4), length unit0 (cm), and Twin connection2. User confirmed 48.0 Max kg for maximum106lb, while connection2 remained reported. Band maximum must therefore not inherit the Weight Training Twin multiplier. This single observation confirms the current display, not all Band/Twin combinations.

The source now renders a filled turquoise circle with black maximum numeral and Max kg/lbs, using the dedicated Band register. Band length follows cm/in readback. Stale maximum shows --. Band controls remain read-only pending protocol validation. UI tests cover display units, return to Weight layout, no repeated redraw, and no strength-load commands from Band taps/dial. This is not a complete multi-mode release.

## Band maximum editing

The dial now writes only the Band maximum register0x5362 as uint16 lb, bounded15..200. Unit-aware steps are0.5kg or1lb. Reads initialize the editor; dedicated readback confirms writes. Pending values time out to the last reported device value. Commands require fresh unloaded Band state, matching trainer session, available units and fresh maximum; queued execution rechecks mode2. No load command is issued by this feature. Mode/session changes cancel UI edits. Band length/profile/inverse and other-mode controls remain unfinished. User confirmed the installed Band maximum dial adjustment works on the physical Voltra. Other Band settings and other modes remain pending.

## Damper controls and reference

Official visual reference inspected in browser: https://www.beyond-power.com/cdn/shop/files/training_mode_03_7c6c2dad-2657-4267-81a9-d9c8a018114d.png?v=1698994768&width=3960 linked from https://www.beyond-power.com/pages/voltra . Icon is a horizontal parachute, canopy on left, scallops and cords leading to a tow dot on right. Replaced piston artwork with vector recreation in menu and center. Green accent visually matched; not claimed pixel-identical.

User confirmed current Twin Mode display V 17 Factor, corresponding to readback0x5103=4. Factors by index0..9:5,8,11,14,17,21,30,33,41,50, from captured Android reference VoltraViewModel.damperFactorLabel. Only V=17 is hardware-confirmed so far. UI shows Roman I–X beside the factor; no kg/lb unit or Twin multiplication. Dial steps change the index by1, writing0x5103 uint8 with fresh unloaded mode4/session guards, dedicated readback, and timeout rollback. Ring represents the ten index steps. Weight modifier buttons are hidden in Damper.

Do not treat assist0x5106 as a boolean: this trainer returns8. Assist toggling is not exposed pending semantics validation. Damper tap-to-load is also not enabled by this update. User dial testing of the new Damper writer is pending.

## Damper tap-number load/unload

Captured official-app trace in /tmp/voltra-android/PROTOCOL_NOTES.md (Damper confirmation section) confirms mode4 keeps its training mode while0x3e89=5 loads and0x3e89=4 unloads. Enabled number-tap interaction in Damper using those commands. Loading requires fresh unloaded Damper state and valid factor readback, no pending edits, and fresh Twin state when active. Queued load captures expected mode and checks it again before GO. Gesture captures both trainer session and mode. Unload stays available when loaded or armed, including stale-state stop fallback. Device notifications remain the source of loaded/unloaded status; no autonomous motor test is performed during installation.

## Automatic Twin controller selection

Removed the invalid rule that the selected device must be controller whenever connection=2. The link now polls both connection0x516c and role0x5438 each second. Vendor role values are0 none,1 main,2 follower. Fresh role and active paired state are required before enabling Twin setting/load commands. No name or address is assigned a fixed controller role.

If the connected endpoint reports follower, the worker hands off to the other of the user's two saved trainers, clears queued commands and cached readings, and awaits that endpoint's fresh role/settings. It does not engage resistance or copy settings during handoff. Pending unload is serviced before handoff. One handoff is allowed until main role is confirmed (or user selects a trainer), preventing endless bouncing if both endpoints report follower. Labels follow fresh role; unknown role never labels the other endpoint Paired. The stored-role register is read live and combined with active Twin state; live hardware role semantics still need verification with both endpoints after this update.

Resistance Band now supports tap-number load/unload using the captured official-app fitness5/4 workflow. Its load gate uses confirmed Band maximum and Twin controller readiness. Band also shows an inverse-color Twin badge next to Max kg/lbs. User reported current controller126226 and follower161230; device-specific identifiers are diagnostic observations only, not firmware assignments.

## Correction: role detection experiment rolled back in source

The live role register never returned a usable value on these trainers. Requiring it introduced a Twin-control lockout. Removed that gate, its query and automatic handoff; current source restores fresh Twin-state control gating while leaving both trainers selectable. Historical automatic-handoff descriptions above document an unsuccessful experiment, not supported behavior. Correct automatic controller discovery remains unresolved. Build ready; install pending unloaded confirmation.
- Rollback installed after unloaded confirmation; not merely a source change. Live Twin operation still requires user validation; automatic controller discovery remains unresolved.

## Current Twin selection UI

After user confirmation that the selected VTR-161230 is the current controller, the selector now labels the active fresh Twin connection Control and the other trainer Paired, disabling ordinary selection of Paired. Switch controller explicitly unlocks the choices while unloaded, allowing the user to select a new controller after changing roles on the trainers. This is a session-level selection policy; it does not detect physical controller roles from advertisements or prove that an arbitrary selected connection is the actual controller. No identity is fixed in firmware. Automatic role discovery remains unresolved. This UI update is installed; it preserves the working rollback connection/control path.

## Requested: retract cable across training modes (not implemented)

User clarified cable rollback means retracting the cable into the trainer, not resetting its saved length. Vendor parameter catalog includes BP_RollBack / BP_WIre_RollBack at decimal20184 (0x4ED8), uint8, generic0..255 range. This does not establish a trigger value, completion state, stop behavior, supported modes or Twin routing. Local protocol captures and SDK contain no verified retraction sequence; published Cortex training-control docs expose cable-zero checking and length adjustment, which are different operations. Do not substitute load/unload or guess a register write. Asked where a working retract action exists on the user's app/device to establish a reference. Firmware unchanged pending this evidence.

## Isokinetic control implementation

User confirmed0.1m/s on the trainer; live mode7, fitness4 and isolated0x5350 uint32 value100 match. Eccentric readbacks:0x5410=0(speed),0x5411=0(Auto),0x5412=5lb,0x5413=5lb. Four selector tiles now expose main Speed, Return type, contextual Ecc. speed/constant resistance, and maximum eccentric Limit. Main and eccentric speeds display m/s and step100mm/s; speed bounds100..2000mm/s, with eccentric0=Auto. Weight fields follow device kg/lb readback using the existing display conversion, bounds5..100lb constant and5..200lb limit. No speculative Twin speed/force scaling is applied.

Writes use captured official-app parameter widths:0x5350 uint32,0x5410 uint8,0x5411/12/13 uint16. Fresh unloaded mode7/fitness4, matching session, and parameter readback are required; eccentric field writes also require the corresponding fresh return type. Pending edits await isolated readback and restore confirmed values on timeout. Tap main speed uses the existing guarded load/unload path, including expected-mode/session checks and no optimistic loaded status. All physical motor testing remains user-driven. Old reference firmware also reports readiness260, which is not promoted to a supported motor state in this update; current hardware reports4.

The weight-mode selector now refuses entry when loaded, armed, loading, busy, or with pending edits. An already-open menu closes when loaded/armed state arrives. Its center icon/text spacing is reduced. Twin discovery and connection policy are unchanged. Retract-cable functionality remains unimplemented and is separate from this update.

## Five-mode picker, unsupported states and Cardio

- Supported picker: Weight Training, Resistance Band, Damper, Isokinetic, Cardio. Custom Curves and Isometric removed from selection. Unmapped workout/fitness states and idle open the picker when fresh and not loaded/armed/loading; returning to a mapped state closes an automatic fallback menu. Swipe right from fallback opens trainer selection. Command guards still reject mode changes in unknown fitness states. Device settings screens that retain a mapped workout and fitness state cannot yet be distinguished by this rule; screen-ID logging added to establish those mappings instead of guessing IDs.
- Isokinetic eccentric Auto stays on its small tile; large readout/unit blank. Return type words use full-alphabet20px font rather than numeric-only48px font.
- Cardio UI matches user-described Row/Ski radio choices, Just Row/Ski versus Distance, and50/100/500/1000/2000/5000m presets. Row resistance and wear display wire index+1; Ski uses its separate resistance register and hides wear. Tap settings to edit with dial. Dial on setup cycles distance presets. Starting or changing activity/settings requires fresh unloaded mode3 state and confirmed parameters. Activity0/1 at0x54F5; resistance0x53A7 and wear0x53AE; Ski resistance0x5522.
- Start uses captured0x5165 screen action03 for free workout and06..0B for distances (payload[action,3E,00,01]); does not reuse weight-mode GO. Queued start captures selected activity and session. Running state21 is recognized as loaded; Stop uses captured fitness4 finish path. UI waits for running state, offers Cancel start, and reports retry on timeout. No distance/pace telemetry is claimed or fabricated. Workout metrics and icon asset corrections remain later work.
- References: local PROTOCOL_NOTES.md April24 Row captures and June8 stock Android bundle; official https://cortex.beyond-power.com/docs/cortex/training-control confirms Row/Ski family and these presets. Cardio physical start and Ski read/write behavior still require user verification.
