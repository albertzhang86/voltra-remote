# Validation status

- ESP32-S3 firmware compiled successfully with ESP32 Arduino 3.3.11 and NimBLE 2.5.1. The build contains the kg/two-trainer implementation.
- Host suites: 867 protocol checks, 64 inherited dial/button checks, 2,701 Guition remote checks; zero failures.
- Actual LVGL screen code rendered natively at 360×360; trainer selector, unloaded weight page, and loaded page reviewed as images.
- Actual touchscreen handler tests passed: short tap never loads, one-second hold sends exactly one load, next press unloads even before load acknowledgement, trainer-session change cancels the hold, and loaded press unloads.
- Full 16 MB original flash backed up to `backups/guition-original.bin` with a SHA-256 sidecar.
- Firmware uploaded to `/dev/cu.usbmodem83201`; esptool verified the written hash.
- Serial heartbeat confirms firmware runs without a boot loop and discovers two trainer addresses (ending `60:a1:e6` and `92:7e:e2`). Voltra 2 reached Ready and returned validated weight/mode/load-state notifications (14 lb, idle, unloaded).
- User confirmed the SELECT TRAINER screen renders on the knob. Touch selection switched from Voltra 1 to Voltra 2. Dial behavior remains pending user verification.
- Both trainers reached Ready and returned their own settings: Voltra 1 reported 74 lb (33.6 kg), Voltra 2 reported 14 lb (6.4 kg). User-driven selection between the slots was observed. Changed-setting confirmation remains pending.
- Motor engagement/unload behavior: not tested on hardware. No motor commands have been sent by this development session.

No simulated screen or host test result should be interpreted as live trainer verification.

## Detected-name update

- Replaced numbered trainer labels with persisted Bluetooth-advertised names; names refresh when a nonempty changed name is discovered. Address suffixes distinguish duplicate names. Long names use ellipsis within the round display.
- Firmware rebuilt and uploaded with hash verification. Actual LVGL screens rendered and visually checked; existing touchscreen interaction tests passed.

## Mode theme, font, and startup update

- Mode-dependent accents: weight training lime, resistance band turquoise, damper mint, custom curves green, isokinetic yellow, isometric cyan. Idle, rowing (unverified color), unknown modes, and stale state use neutral gray. Colors are visually sampled approximations of published VOLTRA imagery.
- Accent is recalculated from the selected trainer’s live reported mode; arc, selected tab, loaded value/state, selected trainer, and eligible load button follow it. Unload stays red.
- Inter replaces the prior font; exact manufacturer font has not been identified.
- Original Polar Bear Strength logo fades in at startup; BLE discovery proceeds while controls remain covered.
- Host suites and touchscreen interaction checks pass; native previews generated for all supported palette modes and neutral fallbacks.
- Unit parameter diagnostic reads added; automatic display-unit mapping remains pending live verification. Current weight display still uses kg.

- Theme/font/logo firmware uploaded successfully and flash hash verified. Serial connection returned Ready, idle, unloaded. Unit diagnostic returned raw 1 for the trainer the user confirmed uses kg; lb mapping still requires verification.

## Physical LED ring

- Added GPIO0 WS2812 driver for all 13 GRB pixels, using ESP32-S3 RMT with DMA and an explicit 300 µs reset interval. Board wiring is from the Guition reference configuration.
- Ring uses the same mode palette as the screen, capped at approximately 16% brightness. It turns off during startup, idle, stale/disconnected state, and unverified/unknown modes. It only transmits on color changes.
- Native UI checks assert ring color for all six mapped modes plus idle/rowing fallback. Physical color appearance requires visual confirmation on hardware.

- LED-ring firmware compiled and uploaded; esptool verified its flash hash. Hardware appearance of the illuminated colors is pending user confirmation.

## Unresponsive-control investigation

- User reported both dial and touch unresponsive on the weight page. Heartbeat continued and trainer reported idle mode 0 / fitness state 0; no evidence of a full processor freeze.
- Replaced the LED driver's unlimited completion wait with a 20 ms bound; failure disables the ring rather than blocking controls. Removed repetitive unit frame logging from the BLE state mutex.
- Diagnostic firmware compiled, uploaded, and hash verified. Touch polling reports no I2C errors; one trainer-selection touch was observed after reboot. Continued interactive confirmation pending.
- Native LVGL pointer hit-testing now verifies tapping the trainer header opens selection while idle and tapping the other trainer switches selection. These checks pass along with prior control tests.

- Live diagnostic captured 9 touch presses and 5 dial events, with zero I2C errors. Several touches landed near/below the header edge (y=65–71).
- Expanded the trainer header touch target by 18 pixels without changing its visual layout; idle now explicitly displays IDLE and the navigation hint. Native LVGL hit-testing includes y=71 and passes. Firmware rebuilt and flashed with verified hash. User confirmation of the original symptom’s resolution remains pending.

## Larger setting tabs

- Weight, Chains, and Ecc buttons increased from 86×34 to 96×48 pixels (58% more area), with 16 px Inter labels replacing 14 px. Moved row upward to preserve separation from Load/Unload. Actual native LVGL rendering reviewed; interaction tests passed.

## USB logging freeze fix

- Found blocking USB CDC output: the configured Arduino core can wait 20 × 100 ms per write under host backpressure. Repeated scan logging also ran under the shared BLE-state mutex.
- Set USB transmit timeout to zero, increased its buffer to 2048 bytes, skip heartbeat output unless space is available, and log discovery only once per observed trainer.
- Firmware built and uploaded with verified hash. Closed the host log reader for 35 seconds: UI loop count continued increasing, captured touch and dial input, and the maximum loop gap remained 99 ms (including startup). No multi-second logging pauses observed.
- User explicitly confirmed the screen responds after this fix.

## Tap-number controls and mode navigation (supersedes hold-to-load)

- User explicitly requested single-tap load/unload on the weight numeral; removed the separate load button. A tap captures the current session and rechecks eligibility on click. Unload remains available on press, and its release cannot reload the trainer.
- Weight edits allowed in confirmed loaded state (fitness 5), while stale, unknown and armed states remain gated; Chains/Ecc edits retain the unloaded restriction.
- Half-kg display rounding matches the user's reported 61 lb / 27.5 kg case. Native regression covers adjacent dial steps.
- Dedicated Mode button is persistent. Mode menu includes Weight Training, Resistance Band, Damper, Custom Curves, Isokinetic, Isometric, and Idle. Commands remain gated to fresh unloaded state and confirmation; non-weight mode parameter controls remain on the trainer.
- 867 protocol, 64 inherited control, and 2735 remote checks passed. Native LVGL pointer tests cover tap-number load, loaded weight edits, mode-menu availability/restrictions, and idle trainer navigation. Main and mode-menu screens visually reviewed.
- Live device read reported 61 lb, Weight Training, loaded. Waiting for user to unload before flashing. No motor/weight commands sent by the development session.

- User unloaded the trainer; live status confirmed fitness 4. Updated firmware flashed with hash verification, reboot heartbeat and touch input observed. Hardware verification of the revised motor/weight interactions remains pending user testing. Shareable r2 archive rebuilt and firmware checksums verified.

## Remaining kg discrepancy — candidate fix, not flashed

- User reports knob 37.0 kg vs trainer 37.5 kg; live BLE base weight is 82 lb. Earlier verified pair was 61 lb vs trainer 27.5 kg.
- These two points fit a 2.2 lb/kg display conversion followed by half-kg rounding. This is an empirical inference, not confirmed manufacturer documentation. Requested a third discriminating observation: old knob 42.0 kg (93 lb) versus trainer display.
- Candidate source uses an integer half-kg grid in both display and encoder conversion, avoiding duplicated displayed dial steps. Exhaustive half-kg round-trip tests pass. 3271 remote checks plus protocol/UI suites pass.
- Candidate is NOT installed or included in r2; trainer remained loaded during observation. Awaiting the requested display comparison and unloading before any next upload.

## Device-scale correction installed (r3)

- Third observation: user confirmed old knob 42.0 kg versus trainer 42.5 kg; live raw weight was 93 lb. Together with 61→27.5 and 82→37.5, this supports the 2.2 lb/kg half-kg display scale. It is device-display compatibility, not exact SI conversion; full-range hardware validation remains pending.
- Display and dial now share this scale; integer half-step conversion avoids floating-point rounding and duplicate visible steps. Regression covers all representable half-kg steps plus three live observations. 3274 remote checks, 867 protocol checks, 64 inherited checks, and native UI interaction tests passed.
- User unloaded; live firmware confirmed fitness 4 at 93 lb. Flashed corrected firmware with hash verification and regenerated the r3 share package with verified checksums. No motor or weight commands were sent by the development session.

## Visual refinements (r4)

- Weight font increased from 48 to 54 px (12.5%), with its position adjusted to preserve spacing. Removed pending/amber color and opacity changes during dialing; footer still reports confirmation.
- Replaced the screen arc with a full circular scale of inward-pointing ticks (minute marks with longer hour marks), preserving the colored value portion.
- Physical mode LEDs now use half brightness unloaded and the existing capped brightness loaded/armed/load-requested, matching numeral state. Idle/disconnected remains off.
- Native UI tests including loaded/unloaded ring color checks passed. Screen rendering visually reviewed. Firmware compiled, flashed after a confirmed unloaded heartbeat, and hash verified. r4 sharing package regenerated and checksums verified. Physical appearance remains for user confirmation.

## Swipe / radial mode selection (r5)

- Replaced the Mode button with a non-button current-mode glyph. Swipe left opens eight icon-only mode choices in a circle (Weight, Band, Damper, Curves, Isokinetic, Isometric, Rowing, Idle). Dial rotation highlights only; center tap confirms, outer-icon tap selects directly. Swipe right returns.
- Native LVGL pointer tests cover actual left/right gestures, browsing without weight/mode commands, center confirmation, blocked mode changes while loaded, trainer navigation, and weight tap controls. Swiping from the loaded weight numeral sends no unload command; both load and unload are committed on a completed tap rather than press-down.
- Main screen and radial menu visually reviewed. Firmware compiled and flashed after an unloaded heartbeat; upload hash verified. r5 archive regenerated and checksums verified. Physical swipe behavior awaits user testing.

## Mode confirmation and idle icon (r6)

- Confirming the current mode returns to the Weight control page, including while loaded, without sending a mode command. A different mode still requires fresh unloaded state.
- Center confirmation text is tappable, and the center icon has a larger touch target. Accepted mode changes return to the control page.
- Removed the misleading idle/power glyph from the main screen and radial selector; seven training modes remain.
- Firmware compiled; 867 protocol, 64 inherited, 3274 remote checks and native UI interaction tests passed. Tests cover current-mode return while loaded, alternate-mode loaded restriction, and confirmation through the text target. Radial and idle screen renders visually reviewed.
- Installation pending unloading: live device reported fitness 5 at 97 lb.

- User confirmed both trainers unloaded. r6 firmware flashed successfully with written-data hash verification and reboot. Live heartbeat and touch input observed with zero input errors; Bluetooth handshake resumed. r6 share archive firmware checksums verified. Physical mode-selection behavior awaits user confirmation.

## Icon/value modifier controls (r7)

- Four 116x44 touch targets in two rows: Weight/current-mode glyph, Chains, Inverse Chains, Eccentric. Each shows its small current value; the selected parameter retains a full title above the large numeral. Removed the redundant standalone mode glyph below the controls.
- Modifier glyphs drawn from the shapes in Beyond Power's official inverse-chains guide reference (20250529-160747.jpg); these are code-drawn approximations, not vendor source assets.
- Added separate inverse-chain frames 0..100 lb from the existing SDK table, one-byte 0x53b0 status parsing, settings polling, worker/session gating and confirmation. No motor or setting commands are sent during development tests.
- 1376 protocol checks, 64 inherited checks, 3274 remote checks pass. Native LVGL pointer tests select and adjust each modifier separately and confirm loaded modifier edits are blocked. Actual screen render reviewed.
- Build and hardware installation in progress.

- r7 installed after fresh unloaded status; upload hash verified. Reboot heartbeat is healthy (max gap 111 ms, zero touch errors). Knob is at trainer selection; live inverse-chain reading/write verification awaits user selection/testing. Sharing archive checksums verified. No load or setting commands sent during verification.

## Dial emphasis and cable adjustment (r8, pending hardware validation)

- Dialing briefly increases the numeral from 54 to 60 px for 500 ms after the last effective turn; existing mode color and load-state opacity stay unchanged.
- Swipe right opens cable length; swipe left or Back exits. Entry requires fresh unloaded state. Pull-and-hold uses the official-app screen-entry payload 0x5165=00 10 00 01 documented in the Android protocol capture notes, not a direct cable-offset write.
- Polls live position 0x3e82, saved offset 0x506a, screen ID 0x5011, and independent length-unit register 0x506f. Auto-return requires a changed saved offset or the trainer returning from its adjustment screen to the original screen. Raw movement and routine polls alone cannot confirm.
- Length mapping 1=cm/0=in awaits live verification against user's confirmed cm setting. Unknown unit values display no inferred unit. No cable motor commands have been sent by development tools.
- 1391 protocol, 64 inherited, 3274 remote checks passed. Native UI tests cover number growth/reset, cable navigation, cm/in rendering, no weight edits on the cable page, changed/unchanged length confirmation, and loaded entry rejection. Visual renders reviewed.

- Final single firmware build passed; uploaded after a fresh fitness=4 heartbeat and verified flash hash. Reboot heartbeat is healthy with max gap 111 ms and no input errors. Knob awaits trainer selection, so unit-register mapping and physical cable adjustment/confirmation are still awaiting user verification. r8 share archive checksums verified.

## Cable units and white numeral (r9)

- User confirmed Voltra shows cm while r8 showed inches. Live register 0x506f is 0 with cm selected, confirming the previous mapping was reversed. Corrected to 0=cm, 1=in; cm uses the native value and inches divide by 2.54. Unknown values remain unlabelled.
- Cable length numeral is now pure white, independent of mode color. Updated existing UI fixtures to match observed unit mapping.

- r9 native UI tests and build passed. Flashed after unloaded status with hash verification. Reconnected live with lengthUnit=0 (user-confirmed cm), cable=5, offset=5; touch/dial input observed with no input errors. White numeral render reviewed; release archive checksums verified.

## Full-screen swipes and simplified control page (r10)

- Replaced LVGL velocity/object-based gestures with raw touch displacement recognition: 36px horizontal movement, dominant over vertical movement, independent of starting object or swipe speed. The entire recognized touch is consumed before LVGL dispatch; navigation is handled on the UI loop so release cannot trigger load/unload or a button.
- Removed the repeated Weight/Chains/Eccentric setting title above the number; active icon/value button remains highlighted.

- Native UI regression passed, including 16 slow 48px swipes across four screen regions with unchanged load/unload/setting command counts. Firmware compiled, flashed with hash verification after fresh unloaded status, and rebooted normally (112 ms max loop gap, zero input errors). Both trainers discovered. Physical swipe feel awaits user confirmation. r10 sharing package verified.

## Modifier icons, larger numeral, reduced redraws (r11)

- Rechecked the full-resolution official modifier UI image: inverse chains is an open-link chain cutout inside a filled circle. Replaced the incorrect circular-arrow drawing. Chains now uses two open links with a central stroke; eccentric uses three progressively wider chevrons. Weight uses filled dumbbell plates based on the on-device mode reference. These remain code-drawn reproductions, not original vendor vector assets.
- Removed positive plus and percentage adornments from eccentric in the large numeral and button. Negative signs remain meaningful and are preserved; no protocol values or edit increments changed.
- Raised numeral from y129 to y106 and increased base font 54 to72; dialing uses80 at y100.
- Follow-up swipe refinement: threshold28px; touch timer10ms. Avoid repainting unchanged text, colors, opacity, fonts, tabs and clock ticks, freeing the UI thread for input reads.

- Native UI tests passed: plain eccentric text, 72/80px dial emphasis, 16 slow 32px swipes across four regions without load/unload/setting side effects, and zero display flushes on unchanged loaded/unloaded screens. Full refresh occurs after visual changes to preserve static icons/labels; final screen renders reviewed. Final build passed, flashed after fresh unloaded status, hash verified. Reboot healthy; idle trainer-selection input polling measured 500 reads per 5 seconds with zero errors. Hardware swipe feel still awaits user confirmation. r11 sharing archive checksums verified.

## Correct shared Chains mapping (pending release)

- User observed Chains=0, Inverse Chains=1.0kg. Live registers report shared chains=2lb, inverse flag=1. The previous SDK-table interpretation of 0x53b0 as a separate load was incorrect; Android decoder and setter identify it as an enable flag.
- Corrected attribution: 0x3e87 is shared load; 0x53b0 chooses standard/inverse. Restricted generated inverse-direction frames to 0 and1. Inactive chain direction displays zero. A chain edit writes direction then shared load with session/load/stop rechecks; acknowledgement requires both matching.
- Eccentric 0x3e88 is signed added pounds per corrected SDK documentation and Android code. Now uses kg display/dial conversion, including symmetric negative rounding, without positive plus or percentage decoration.
- Raised kg label from y193 to181. Tap-to-activate defaults remain pending user clarification; current edits are not yet installed.

## Device-provided modifier values (r12)

- User clarified that tapped values must come from the Voltra. Added a selection/read command which never writes a numerical load: chain taps write only the validated 0/1 direction flag, then query settings; Weight/Eccentric taps query settings. The UI waits for a post-request read and chain direction confirmation before dial edits. No guessed minimum or remembered local preset is substituted, including if the device returns zero.
- Combined with corrected chain attribution and signed eccentric kg conversion from the preceding pending change. Unit text is raised12px.
- 896 protocol,64 inherited,3478 remote checks pass; native UI suite checks tap selection preserves both numeric loads and sends no setting edits, alongside existing loaded/stale/session/swipe behavior.

- Firmware build and native screen render passed. r12 sharing archive generated with verified firmware checksums. Not installed: latest live state fitness=5 (loaded), awaiting user unload confirmation. No resistance or modifier commands were sent by development tools.

- User observed installed knob eccentric15 versus Voltra7kg. Live confirmed raw eccentric15 and fitness4, corroborating the pounds-to-device-kg correction. Installed prepared r12 with written-data hash verification after the fresh unloaded heartbeat. No resistance commands sent by development tools.

## Twin Mode investigation (after r12; diagnostic build)

- User reports knob displays approximately half the controlling Voltra value while Twin Mode is active. Existing UI reads the single-trainer 0x3e86 register and has no Twin conversion. No multiplier assumed yet.
- Added read-only polling and decoding of catalog fields COWORKER_CONN_ST (0x516c, uint8), LAST_COWORKER_ROLE (0x5438, uint8), and COWORKER_FORCE_MAIN/SUB/SUM (0x53b3/4/5, uint16). Saved role alone must never be interpreted as an active connection. Force units and connection enum remain unverified. No Twin writes added.
- 902 protocol, 64 inherited, 3478 remote checks passed. Diagnostic firmware compiled and flashed after a fresh fitness=4 unloaded heartbeat; flash hash verified. Boot healthy with max gap109ms and no input errors. Awaiting trainer selection to capture actual Twin values and compare device screens. Shareable r12 archive remains unchanged.

## Twin weight, paired selection, and icon (r13)

- Live isolated read of COWORKER_CONN_ST 0x516c returned 2 while the user-confirmed controlling trainer was in Twin Mode. Older device firmware did not return the newer saved-role and force-sum fields. Production polls only the supported connection register every second; unsupported probes and raw diagnostic logging are not run.
- User confirmed 163 kg on controller versus 81.5 kg on knob, with live 0x3e86=179. Combined weight must sum two rounded single-unit values (81.5+81.5=163), not convert twice the raw integer (which would incorrectly produce162.5). Only base weight/main weight button use the confirmed combined display; modifier scaling is unchanged pending corresponding device evidence. Encoder and BLE commands remain per unit, so one 0.5kg single-unit step displays1kg combined.
- Active connection state2 shows a two-device icon beside kg. Within this two-trainer installation, the non-selected trainer is marked Paired and blocked both in the UI handler and link selection API. Inactive state restores selection. Pair identification requires connecting to the controller first after boot; firmware does not infer live pairing from saved names or persisted roles. Startup selection remains unchanged. This is a verified two-trainer behavior, not a general multi-pair discovery implementation.
- Stale Twin readings hide combined weight and block editing/loading; follower remains blocked until the active connection reports Twin ended or disconnects. Existing unloaded switching guards remain.
- 924 protocol,64 inherited,3485 remote checks passed. Native LVGL tests validate163.0 display, Twin icon, Paired label and disabled state, rejection of synthetic follower taps, and restoration of81.5/single selection when Twin ends. Actual renders reviewed; no new motor commands sent by development tools. Firmware build passed; installation pending below.

- r13 installed after a fresh unloaded (fitness4) heartbeat; flash hash verified. Reconnected to selected controller after restart: connection2,179lb,fitness4, touch/dial input observed with zero errors and150ms maximum loop gap. Combined UI mapping for this value is163.0kg. Release ZIP and all packaged firmware checksums verified.

## Twin modifiers and official-reference badge (r14)

- User confirmed modifier config values were still half in Twin. Applied the same combined, device-rounded kg display to Chains, Inverse Chains, and signed Eccentric on both tiles and large selected numeral. Per-unit editor values and BLE payloads are unchanged. Stale Twin reads now hide all affected numbers.
- Replaced the invented two-device icon after inspecting Beyond Power's official Twin Mode Introduction video, https://www.youtube.com/watch?v=-MsEybQnnSY at0:14 and0:24, linked by their official help article https://help.beyond-power.com/en/articles/10124771-how-do-i-pair-two-voltra-i-devices-in-twin-mode. Recreated the filled circular badge containing two diagonally overlapping horizontal links as LVGL vector geometry. Badge uses the active mode accent and dims with the unloaded weight; no gray placeholder.
- 924 protocol,64 inherited,3485 remote checks passed. Native UI regression covers combined modifier tiles and selected numerals, inactive chain direction0, no writes on display changes, and a1kg combined chain dial step sending11lb per unit rather than a doubled command. Visual preview inspected. Firmware builds successfully.

- r14 uploaded after fresh fitness4 confirmation; flash data hash verified. Shareable r14 ZIP built and packaged firmware checksums verified.

## Initial idle mode selection and center labels (r15)

- First fresh connected state in each trainer session opens mode selection automatically if the trainer reports mode0 (idle). Unknown or connecting state waits; active modes open the existing controls. This is once per session, so returning/swiping away does not create a reopening loop. Session change closes an old mode menu and resets the initial-state check.
- Center confirm button now displays the highlighted mode name, in its mode accent, updated by rotary browsing or icon selection. All seven mode names are provided; long names use two lines. Outer icons and confirmation behavior remain.
- 924 protocol,64 inherited,3485 remote checks pass. Native LVGL regression covers idle selection, delayed ready state, center label changes while turning, confirmation returning to controls, no repeated reopening, and active-mode connection. Render reviewed; firmware build passed. Live trainer reports fitness5, so flash is deferred until unloading is confirmed.

- r15 installed after user confirmed both Voltras unloaded. Upload succeeded and flash hash verified. Reboot heartbeat healthy: trainer selection screen,107ms maximum loop gap,1286 input reads and0 input errors. Live idle-mode transition awaits trainer selection; covered by native UI regression.

## Mode diagnostics and Band maximum display (unreleased)

- Installed read-only mode diagnostics and center icon+text after the user prepared an unloaded trainer. USB upload hash verified; Band registers now return actual maximum106lb and length260cm with successful 1/2-byte responses. Full register observations and unverified mode workflows are recorded in MODE-SUPPORT.md.
- Prepared a dedicated Band maximum circle with dark numerals, Max kg/lbs following device units, and cm/in band length. Reads maximum from0x5362 rather than normal weight. Unavailable readback displays --. Band interaction remains read-only; no Weight Training load sequence is reused.
-1158 protocol,64 inherited,3485 model checks pass. Native LVGL tests pass for unit labels, dedicated maximum, no steady redraw, read-only interactions, stale values, and restoring the Weight layout. ESP32 firmware compilation succeeded (1,238,814 bytes).
- Circle update is built but not flashed or published. Awaiting the user's on-device Max number to validate kg/Twin presentation; diagnostic still reports Twin connection2 despite earlier request to disable Twin. Do not claim all modes are implemented or validated. r15 distribution remains unchanged.

## Band maximum comparison and installation

- User confirmed 48.0 Max kg while the Band maximum register returned106lb and Twin connection remained2. Removed the Weight Training Twin multiplier from Band maximum presentation for both kg and lbs. Regression now covers this exact combination, including106lb in imperial units.
- All1158 protocol,64 inherited and3485 remote checks pass; native UI tests and firmware build pass. Visual render inspected with48.0 Max kg in the circle.
- Fresh live fitness4 confirmed immediately before USB update. Installed successfully; written-data hash verified. Rebooted to trainer selection, max loop gap107ms,792 input reads,0 errors. User must reselect trainer to compare the live display. Band settings remain read-only; no all-mode support claim. Share package r15 unchanged.

## Band maximum dial fix

- Added the dedicated0x5362 uint16 write with15..200lb bounds. UI uses device units for0.5kg/1lb steps and a separate Band editor. No automatic load command. Mode/session change cancels edits; execution rechecks fresh unloaded Band state. Device readback confirms writes; five-second UI timeout restores confirmed value.
-2091 protocol,64 inherited and3491 model checks pass. Native UI tests pass for kg/lb steps, no optimistic confirmation, loaded-state rejection, timeout, and cancelling a pending edit after mode change. Build passed (1,240,110 bytes).
- Fresh live mode2 fitness4 verified before flashing (latest maximum133lb). USB install completed with verified data hash. Physical dial-to-device acceptance remains to be checked by user. Existing share package remains r15 pending multi-mode completion.

- User confirmed “works now” after testing the installed Band maximum dial fix. Physical knob-to-Voltra maximum adjustment is confirmed working. This does not validate all range endpoints, loaded-state editing, or other modes.

## Damper factor controls and corrected parachute icon

- User confirmed Twin Mode V 17 Factor with live mode4, fitness4 and0x5103=4. New UI uses Roman I–X plus factor table rather than kg/lb, and never doubles factors in Twin. Dial writes index0..9 to0x5103 as one byte. Fresh mode/session/readback guards, timeout rollback and loaded-state exclusion match Band handling.
- Recreated official sideways parachute icon from visually inspected Beyond Power product-page image; green accent updated. Host previews of Damper controls and selected mode menu visually inspected.
-2144 protocol,64 inherited,3500 remote checks pass. Native UI verifies V17 in Twin, VI21 dial target/readback, no optimistic device confirmation, loaded rejection, timeout, stale data, both bounds, mode-change cancellation and no steady redraw. Firmware build succeeded (1,246,526 bytes).
- Fresh unloaded state confirmed before flashing. USB upload completed with verified data hash. Physical Damper adjustment and other factor labels await user testing. Assist is not exposed because observed register value8 contradicts reference boolean semantics. No change to Damper motor-start workflow in this update.

## Damper tap-number load/unload correction

- Number tap now routes load/unload for Damper as well as Weight Training. Damper readiness uses its factor readback rather than Weight Training modifier values. Captured app trace confirms the existing setup-read and fitness5/4 load/stop commands are valid in Damper. No training-mode write is sent by a tap.
- Gesture captures session and mode; queued loads capture expected mode and recheck immediately before GO. Pending adjustment prevents load; a canceled swipe or repeated click without a new press cannot load again. Unload remains reachable while loaded/armed or state is stale.
-2144 protocol,64 inherited,3504 model checks and native UI suite pass. Added Damper press-release-click load, device-loaded unload, duplicate-click rejection, canceled gesture and mode-change tests. Firmware build passed (1,246,622 bytes).
- Before upload, live trainer mode4 fitness4 confirmed unloaded. Physical tap-to-load/unload testing remains user-driven; no resistance is engaged by development verification.
- USB installation completed; written data hash verified and reset completed successfully.

## Damper Twin badge restoration

- Restored the existing interlocking-link Twin badge next to Factor whenever fresh active Twin state is reported. Positioned it outside the wider Factor label. Uses the Damper accent and same unloaded dimming as the number; factor values remain unscaled.
- Native UI regression passes: Twin badge visible in Damper, no label overlap, hidden in single or stale Twin state, restored after fresh state, and no steady redraw. Firmware builds and preview visually inspected.
- Fresh mode4 fitness4 Twin connection2 confirmed before flashing.
- USB installation succeeded; written data hash verified and reset completed. Damper Twin badge correction installed.

## Twin role reversal and Resistance Band load correction (prepared)

- User identifies126226 as current Twin controller while old firmware remains connected to161230 and incorrectly labels126226 Paired. Live selected0/connection2 confirms connection state alone does not establish controller identity.
- Added live role polling0x5438 alongside connection0x516c. Active Twin commands require fresh main-role evidence. Follower detection triggers BLE handoff to the other saved trainer without transferring queued writes; controller settings are read afresh. Role polling continues after connection so later reversals are handled. Conflicting follower roles cannot cause endless handoff loops. Pending stop is serviced first. No device name/address is hardcoded as controller.
- Band tap load/unload enabled using protocol-captured fitness5/4 sequence; includes expected-mode and trainer-session guards. Twin badge appears inside Band Max display with inverse colors.
-2144 protocol,64 inherited,3513 model checks and native UI tests pass. Regression covers reversed selector roles, unknown-role controls blocked, both handoff directions, stale role rejection, Band tap load/unload and existing Damper/Weight behavior. Arduino build succeeds (1,247,554 bytes). Band Twin visual inspected.
- Not installed yet: awaiting confirmation both physical trainers are unloaded. Hardware readback of role register and automatic handoff needs verification after installation; software tests alone do not establish live role semantics.
- User confirmed both trainers unloaded. Automatic Twin role polling/handoff and Band tap load/unload firmware installed successfully; written-data hash verified. Reboot healthy at selector (109ms maximum loop gap,1284 input reads,0 errors). Awaiting selection of161230 to verify follower-role readback and automatic connection to126226. Installation is confirmed; hardware handoff is not yet confirmed.

## Connection diagnosis after Twin update

- First live test:126226 failed at BLE connection (before authorization), while161230 connected but no role value was decoded. User subsequently confirmed disconnecting a phone/tablet from126226; competing connection was a plausible cause, not yet confirmed resolved.
- Installed additional bounded raw role-response logging and NimBLE connection error/address-type logging. Advertisement discoveries now refresh saved address type. Diagnostics remain nonblocking and send no motor commands. Build and USB upload succeeded with verified hash.
- Awaiting selection of161230 with phone/tablet disconnected to verify actual role reply and automatic handoff. Do not describe automatic handoff as hardware-validated yet.
- User corrected an earlier answer:126226 is NOT connected. Latest diagnostics still show selected0/161230, Ready, Twin connection2, role=-1, with no post-update attempt to126226 recorded. Both the role-response semantics and controller connection remain unresolved. Asked what occurs when selecting126226 to distinguish selection/UI failure from BLE connection failure. Do not mark handoff verified or this issue resolved.
- Subsequent user selection correctly targeted126226 (80:65:99:92:7e:e2, public address type0). Repeated NimBLE connect failures return13; local ble_hs.h defines13 as BLE_HS_ETIMEOUT. Failure is before service discovery/authentication, not a training command rejection. Asked user to restart unloaded126226 and restore it as Twin controller with Beyond+ closed; firmware already retries automatically. No motor commands issued.
- User confirms126226 connects when Twin Mode is off. Live verification: selected1,Ready,connection0,mode1,fitness4,weight194lb. Thus saved address and authorization work standalone; failure is specific to observed Twin setup. Role0x5438 remains unavailable even standalone. Automatic handoff is implemented but NOT a verified solution on this firmware. Next requested comparison: knob powered off, Twin restored with126226 controlling, official Beyond+ connection/control test. This separates device/Twin exposure from knob interoperability without further speculative firmware changes.

## Confirmed Twin control regression; rollback prepared

- User reports official Beyond+ works in Twin, while knob no longer usable with either Twin endpoint. Comparison against r15 confirms newly added twinControlReady required LAST_COWORKER_ROLE=1; live role remains unavailable (-1), so every Twin setting/load command was rejected. This is a definite introduced regression. It is distinct from observed BLE timeout13 before authorization; initial BLE connect/auth sequence is otherwise unchanged from r15.
- Removed unsupported role polling and automatic handoff. Restored Twin control readiness based on fresh existing connection/state checks. Both trainer entries remain selectable; neither stored role nor connection order is used to guess Paired/follower labels. Band/Damper controls and tap load/unload remain included. Automatic controller discovery is unresolved, not claimed repaired.
-2144 protocol,64 inherited,3514 model checks and native UI suite pass. Tests require missing/saved roles not to lock Twin controls or selector. Arduino build passed (1,247,110 bytes). Pre-rollback source archived at /tmp/voltra-before-twin-rollback.tgz for comparison.
- Awaiting user confirmation both unloaded/Beyond+ disconnected before installing this rollback. No further blind resets or Twin-mode restriction claims are justified by current evidence.
- User confirmed unloaded. Twin role-gate rollback installed; upload completed with written-data hash verified and hardware reset. Firmware no longer polls or requires the unsupported role field, and no longer performs speculative automatic handoff. Physical Twin connection/control testing remains pending after trainer selection. Phone-disconnection status was not separately reconfirmed in the user's short reply.

## Twin selector labels and explicit controller override

- User confirmed the rollback works and VTR-161230 is currently the physical controller. Added Control for the selected fresh Twin connection and Paired for the other stored trainer; Paired is muted and cannot be selected through touch or rotary selection. These are session selection labels, not automatic physical-role detection. No device identity is hardcoded.
- Switch controller unlocks the selector only while unloaded and without pending commands. This allows a later physical role reversal without locking the user onto an old selection. Stale or inactive Twin state clears this UI lock; backend role gating and speculative handoff remain removed.
- Native UI tests pass, including blocked paired selection, loaded override rejection, unloaded override, role reversal selection and clearing labels outside Twin. Selector preview visually checked. Arduino build passes (1,247,754 bytes).
- Fresh live state confirmed selected0, Twin connection2, fitness4 unloaded before upload. USB installation completed with verified written-data hash and hardware reset. Physical selection verification remains user-driven.

## Pre-connection Twin identification investigation

- User correctly reports Control/Paired labels appear only after connection; the session-label update does not meet automatic scan-time role identification.
- Two read-only macOS CoreBluetooth scans with user-confirmed161230 controller observed only VTR-161230. Manufacturer bytes28372f60a1e6 equal its known Bluetooth address; name, service UUID, connectable flag and Tx power are present. No identifiable role field or service data was observed.126226 was not observed in these scan windows; its selector entry is persisted and is not proof of a current advertisement.
- This observation cannot distinguish an absent follower from an offline/out-of-range trainer and does not justify assigning roles or disabling entries automatically. Requested comparison with knob powered off and126226 controlling. Firmware unchanged during this investigation. Read-only reusable scanner saved as tools/scan_advertisements.swift.

## Reversed-role advertisement comparison

- Discarded the first attempted scan after the user corrected “ready” to “not ready”; it is not evidence of role reversal.
- After the subsequent explicit “ready now” for the requested126226-controller/knob-off setup, a20-second read-only CoreBluetooth scan again observed only VTR-161230. Fields were unchanged: connectable1, manufacturer28372f60a1e6 (its address), VOLTRA service UUID, Tx power6. No126226 advertisement or identifiable role field was observed. Physical role was supplied by the user's setup confirmation, not electronically verified.
- Advertising visibility did not follow the reported controller reversal. Therefore neither presence/absence nor the observed manufacturer payload can justify automatic Control/Paired labels. No firmware changes, BLE connections or trainer writes were performed. Automatic pre-connection role identification remains unresolved; preserve working firmware.

## Loaded mode-menu guard, center spacing and Isokinetic

- Added loaded/armed/loading guards for opening the mode page, plus automatic closure on externally initiated load. Tightened center icon/name spacing and visually inspected the native LVGL render.
- Isokinetic main speed100mm/s confirmed against the user's0.1m/s screen. Eccentric mode0, speedAuto, constant5lb and max5lb read back successfully. Implemented dedicated speed/return/eccentric/limit controls with bounded captured-register writes, unloaded freshness guards, trainer-session isolation, readback confirmation and timeout restoration. Main-speed tap supports guarded load/unload.
-2215 protocol checks,64 inherited checks and3530 model checks pass. Native UI suite passes including loaded/armed/loading swipe blocking, external load closure, all Isokinetic editors, readback-before-confirmation, timeout, bounds, stale values, no steady redraw, and main-number load/unload event routing. Actual motor engagement is not performed by tests.
- Arduino build passed (1,251,430 bytes). Fresh selected0/mode7/fitness4/single-device state confirmed immediately before upload. USB installation started; completion recorded below when verified. Physical dial-write/load behavior still requires user validation.
- USB upload completed with verified written-data hash and reset. Reboot healthy at trainer selection, max loop gap108ms,2602 input reads and0 errors. Awaiting user reconnection and physical Isokinetic checks.

## Auto glyph fix, supported mode reduction, and Cardio implementation

- Auto glyphs came from a numeric-only large font. Empty main readout and unit for eccentric Auto; keep small tile text. Return-type text also moved to a full alphabet font. Five-mode picker and fallback to selection for unknown workout/fitness state implemented; all modes return safely to trainer selection from fallback. Settings screen IDs retaining the same workout state are not yet mapped.
- User described Row/Ski, Just/Distance and six distance presets. Hardware Cardio mode3/fitness4 reports resistance3=>4 and wear9=>10. Implemented Cardio activity, native distance/free start, running/stop UI and parameter editors; hardware state is authoritative.
-2334 protocol,64 inherited and3547 model checks pass. Native UI suite passes Cardio activity acknowledgement, distance2000 and free starts, preventing generic weight load, loaded mode-menu lockout, Stop, Ski resistance edits, hidden Ski wear, timeout and no steady redraw. Frame tests cover every distance action and all four parameter IDs, bounds and checksums. Unsupported-state fallback, full supported mode return and Auto blank readout regression checks pass.
- Native Cardio and five-mode picker previews inspected. Build passed (1,257,086 bytes). Fresh mode3/fitness4 stopped confirmed before USB upload. No autonomous workout start or motor test performed.
- USB upload hash verified and reset completed. Reboot healthy at trainer selector, max loop gap112ms, input reads293/errors0. Physical Cardio testing and new activity/Ski parameter readback await user reconnection.

## 2026-10-03 — offline voice recognition test

- Added PDM microphone (GPIO5 clock / GPIO4 data), 16 kHz mono, local WakeNet Hi ESP and MultiNet Load weight / Release weight. No voice-to-motor code exists in this build.
- Added the 4 MB model partition at 0xB00000 without moving existing partitions. Flashed application and 3,340,296-byte speech model together; esptool verified all write hashes.
- Compilation: 2,368,754 bytes sketch; 51,512 bytes globals. Host suites: 2,200 protocol, 64 inherited, 3,530 remote and 23 recognition checks passed.
- Voice started successfully while serial logging was closed. Later sampling: ready=1, 726 audio chunks, zero read errors/dropped events, 4,642,804 bytes free PSRAM, about 49 KB free internal heap; UI max loop gap 142 ms and zero touch read errors. Recognition accuracy and BLE-connected performance still require user testing.
- Speech acceptance requires a fresh wake within six seconds, consumes one command per wake, rejects delayed/duplicate events, and resets after timeout. Unit tests cover these plus timer rollover.
- Stable r16 shareable release remains unchanged. New packaging/upload helpers require the model file when its partition exists; release names must be explicit and existing releases cannot be overwritten.

## 2026-10-03 — dial acceleration

- Weight Training/modifiers, Band maximum and Isokinetic weight fields use 1×/2×/5× steps based on consecutive physical encoder timestamps. Fine first step after a pause, reversal, session change or different edited field. Mode picker, Damper and speed controls retain single steps.
- Protocol bounds, half-kg conversion and confirmation handling are unchanged. Twin display still combines both device values.
- Passed 2,200 protocol, 64 inherited, 3,546 remote and 23 voice checks. Actual LVGL interaction tests verify a fast three-pulse burst changes 27.5 to 31.5 kg in one coalesced write; a later single pulse changes to 32.0 kg, without issuing load commands.
- Firmware build: 2,369,098 bytes sketch, 51,528 bytes globals.

## 2026-10-03 — immediate dial writes and confirmed display

Supersedes the earlier pause-to-send/coalesced-burst behavior. The first turn queues a write in the same UI pass; subsequent turns update a retained target while the current write is in flight. Acknowledgement matches the submitted value, then immediately sends any newer target. Readback polling every 250 ms keeps device status fresh while waiting. There is no inactivity debounce before writing.

The main value, modifier values and tick range show trainer-confirmed data, never a speculative target that later rolls back. Timeout still discards unconfirmed changes. Sparse nonblocking edit/write/ack logs permit real-device latency diagnostics without changing motor state.

All host tests pass (2,200 protocol, 64 inherited, 3,563 remote, 23 voice), plus actual LVGL interaction tests for immediate writes on every acknowledged turn, delayed readbacks, continued turns during an in-flight write, and confirmed-only display. Hardware acceptance of the final dial feel remains with the user.

## 2026-10-03 — dial/Bluetooth latency

Captured a 591 ms gap between enqueue of target 134 and its BLE write, while earlier acknowledgements took roughly 109 ms. The worker sent six background queries in a batch, including after finishing a setting write.

Changed to one background read per scheduling pass, skipped whenever controls are queued, and immediately returns to the control queue after a completed command. State/Twin freshness queries remain bounded during sustained dialing. Dial setting writes/readbacks use Write Without Response only when advertised by the characteristic; load/unload and handshake transport remain unchanged. Application-level confirmation remains mandatory. Removed the duplicate initial confirmation read and reduced render pacing from 80 to 33 ms.

Reversing a pending change starts from the visible confirmed weight rather than accumulated unconfirmed travel. Symmetric acceleration and scheduler priority covered by host tests: 2,200 protocol, 64 inherited, 3,586 remote, 23 voice; LVGL interaction tests passed. Hardware latency/feel requires checking after installation.

Installed the Bluetooth-priority build with flash hash verification. Restart check: zero touch/audio read errors, speech engine ready, max loop gap 136 ms. At this check the knob remained on trainer selection, so no post-update write-latency measurements were available yet.

## 2026-10-03 — rollback after user-reported wrong direction

User reported that the latest latency/acceleration changes caused weight to move opposite the intended direction. Restored the exact r16 release binaries with hash-verified USB flashing after confirming unloaded status, and restored matching firmware source, README, control tests and build artifacts. Experimental voice/dial source and compiled build were preserved under build/dial-regression-20261003-231006. Voice test is temporarily absent in r16.

Restarted and reconnected successfully: input read errors zero, max loop gap 150 ms. Waiting for the user's slow clockwise/counterclockwise direction check before any further input changes. The unusually high rotation counter requires direct signal-level diagnosis; no new direction or acceleration assumptions are accepted as a hardware fix.

### Slow-turn setback: captured input evidence
- User reproduced a setback while turning clockwise only on r16 with diagnostics. `/tmp/voltra-setback-trace-2.log`: at 260355 ms both +1 and -1 entered the editor, changing 168→169→168; at 261235 ms a -1 event cancelled the preceding clockwise draft. No timeout or stale readback caused those two decreases.
- This establishes incorrect direction events at the board input boundary, but does not yet distinguish electrical bounce from a wrong encoding assumption. Added bounded, nonblocking raw GPIO1/GPIO2 CHANGE-edge tracing with microsecond timestamps. Control still dispatches low edges using the existing 4 ms per-pin debounce; no reversal suppression or new weight algorithm is installed.
- Diagnostic firmware compiled successfully. Connected VTR-161230 reported fitness=4 (unloaded) immediately before upload. The shareable r16 release is unchanged.

### Encoder setback: pulse qualification fix
- Raw GPIO capture (`/tmp/voltra-encoder-signals.log`) showed genuine active-low direction pulses lasting 39–1,225 ms, followed roughly 170–200 microseconds after release by short spikes on the opposite pin. The old 4 ms per-pin falling-edge debounce accepted the first opposite spike, directly cancelling the dial target. The signals are independent directional pulses, not quadrature.
- Replaced falling-edge dispatch with independent 500-microsecond timer sampling and 1 ms stable-level qualification for both press and release. Each qualified press emits one event; genuine reversals are not locked out. Timer sampling is independent of UI rendering. Removed high-volume raw-edge capture; bounded dial/readback diagnostics remain.
- Checked in an anonymized timing-only failing capture and replayed it at 50 sampling phases: exactly five clockwise and eight counterclockwise genuine pulses, with no false opposite events. Rapid 5 ms alternating pulses, held input/release bounce and micros rollover also pass. 106 encoder regression checks plus 2,200 protocol, 64 inherited and 3,530 remote checks pass.
- Shareable r16 remains unchanged pending physical acceptance of this fix.

### Mode center layout and loaded cable navigation
- User confirmed the encoder spike filter fixed the setback on hardware; retained it unchanged.
- Mode confirmation center is now a 112px circle. Icon and label form a vertically centered group with a 4px gap, using the label's actual height so Damper/Isokinetic and two-line Weight Training/Resistance Band are balanced. Moved the helper text below the enlarged circle.
- Right swipe no longer opens cable adjustment while loaded, armed or awaiting load confirmation. Existing unloaded cable navigation remains covered by the actual LVGL pointer tests.
- Actual LVGL interaction suite passed, including loaded/armed right-swipe assertions. Visually inspected rendered one- and two-line mode screens. ESP32 firmware build passed. Device reported fitness=4 before installation.
- Follow-up: centered the Damper main Factor numeral at the screen center, aligned the Roman level beside it, and moved Factor/Twin indicators below. Preserves font growth while turning and tap load/unload. Actual LVGL preview and interaction suite pass, including no needless redraw while idle; rendered Damper screen inspected. Rebuilt successfully and confirmed unloaded state before upload.
- Mode helper spacing correction: shortened all mode hints to a single line at y=238 with 240px text width. Visually checked clear separation from the center circle and bottom mode icon; actual LVGL suite and firmware build pass. User confirmed unloaded, serial fitness=4 verified, upload hash verified and post-flash runtime checked.

### Trainer discovery availability and selection cleanup
- Saved names/addresses no longer imply availability. Added non-persisted last-seen timestamps; unconnected trainers expire after 10 seconds without an advertisement. Connected trainers remain available even when they stop advertising. UI labels Connected/Nearby/Not detected; absent entries are dimmed and disabled, with matching selection guards.
- Repeated advertisement callbacks refresh timestamps. Scan results are callbacks-only to bound storage. The BLE worker scans while trainer selection is visible, including alongside an established connection; stops discovery on the control page and before connecting. No automatic Twin role changes or connection handoff added.
- Removed trainer selection Back button as requested.
- 2,200 protocol, 64 inherited, 3,538 remote, 106 encoder checks passed. Actual LVGL suite verifies stale entry disables selection and fresh discovery enables it; inspected rendered selector without Back. Firmware built successfully. Initial availability firmware reconnected successfully to VTR-161230; latest fitness=4 verified before final upload. Physical out-of-range/reappearance acceptance remains for the user.

### Mode page without helper copy
- Removed the turn/tap hint and swipe footer from the mode page. Center icon/name, mode icons, tap confirmation and swipe navigation remain.
- Updated the host interaction to confirm through the center instead of the removed helper label. Actual LVGL suite and firmware build pass; clean mode-page render visually checked. Connected Voltra reported unloaded before upload.

### Voice recognition diagnosis resumed on fixed encoder baseline
- User clarifies the earlier test did detect Hi ESP and show Listening, but never recognized Load weight / Release weight. Current firmware had no voice after r16 rollback; restored only the archived voice module and model partition onto the current fixed-dial/UI/discovery source. Preserved current non-voice source under build/pre-voice-restored.
- Recognition remains test-only with no trainer commands. Added manual listening entry by tapping voice badge, raw command/timeout counters, command registration count, command-mode return code, and windowed AC RMS/DC/peak diagnostics. Handles WakeNet channel-verified event as well as wake event. No microphone gain/threshold changes without evidence.
- 23 recognition acceptance checks plus current protocol/remote/encoder suites pass. ESP32 build passed (2,368,202 sketch bytes); speech model binary present. Device fitness=4 verified before installing diagnostic firmware/model partition. Physical command test pending.
- First live diagnostic run: microphone reads succeed, both phrases registered, command-mode return code=0. Four wakes produced one recognized Release weight and three timeouts; Load weight not yet accepted. Hence command recognition is intermittent, not wholly absent. Wake/speech RMS varies with utterance; no input gain change applied.
- Compared Arduino ESP_SR wrapper with Espressif ESP-Skainet main.c: wrapper omits MultiNet clean at new wake. Added local ESP_SR 3.3.11 copy with reset requested at command-mode entry, executed only by the detection task; diagnostic command frame/reset/state/registration-error counters added. Physical retest pending. This is a potential reliability contributor, not yet established as the sole cause.

### Voice input conditioning for 1–2 metre use
- User reports commands remain very unreliable at 1–2 metres. Latest reset diagnostic records three wakes/564 command frames/three resets with zero command results and zero command initialization errors; microphone reads have no errors. Measured DC offset is ~1360 counts, speech window AC RMS commonly ~400–800 counts.
- Added a 51 Hz DC-blocking high-pass and up to 4x input gain, with per-block peak protection at 28000 and gradual gain recovery after loud input. No recognition threshold reduction. Added LOAD THE WEIGHT / RELEASE THE WEIGHT variants alongside original phrases; command text uppercase per Espressif recommendation.
- Expanded recognizer timeout to 8 seconds and UI acceptance window to 10 seconds, avoiding the previous hard six-second cutoff while MultiNet was still detecting.
- 3,077 input-conditioning checks cover offset removal, quiet speech gain, loud-input bounds and recovery. Recognition 23 / encoder 106 / remote 3,538 / protocol 2,200 / inherited 64 checks pass. Still test-only; user acceptance pending.
- User adds loud workout music as a required condition; latest audio-conditioned trial still reports no good. Live counters: two wakes, 530 MultiNet frames, two model timeouts, zero recognized commands. Input conditioning alone has not solved recognition.
- Prepared next diagnostic build: explicitly enable available WebRTC single-channel noise suppression and VAD; expose processed-audio peak, speech-frame count, decoder raw/matched phoneme strings and confidence on completed attempts. Bounded copies/serial output only, no audio recording. No threshold lowering and no motor actions. Build passes (2,369,970 bytes). Awaiting user unload because live fitness=5 before installing. Noise suppression is not claimed to remove music vocals or to solve the issue without hardware results.

### MultiNet7 pronunciation correction
- The preceding noise-suppression diagnostic was installed, but the user reports repeated failures even without music. Live diagnostics show three wakes, 795 command frames, 738 VAD speech frames, no audio read errors, and three timeouts with empty decoder results. Hardware capability is not established as the cause.
- Correction: the earlier uppercase recommendation applies to MultiNet6, not the MultiNet7 model used here. Changed to lowercase phrases and explicit phonemes using Espressif's MultiNet7 mapping and CMU pronunciations. Extended the local wrapper with optional phonemes, retaining runtime G2P as the fallback.
- Firmware compiles (2,370,038 bytes); protocol, inherited, remote, encoder, voice and audio regression suites pass. Fresh connected-device readback reports fitness=4 before installation. Recognition remains test-only; no motor actions. Physical acceptance remains pending.

### Direct release recognition test
- Phoneme correction upload/model hashes verified. User still reports failure; fresh diagnostics show one wake, 265 command frames, zero recognized commands and one timeout. No claim of improved recognition.
- Removed wake gating for this recognition-only experiment. Start in command mode with WakeNet disabled; register only Release weight / Release the weight. Restart command mode after every detection and timeout. Remove tap-to-listen and Hi ESP copy. No BLE/motor action is wired to voice.
- Added direct recognition acceptance checks: no wake needed, reject Load and stale events, suppress duplicate releases, accept subsequent phrases after timeout. All suites pass (33 voice acceptance checks). Physical recognition and false-positive behavior remain unverified.
- Direct-release build installed with hash verification (2,369,390 bytes), after fresh fitness=4. Runtime reports directRelease=1, ready=1, registered=2, initialization/read errors=0, wake=0 and frames increasing after automatic timeout/reset. This confirms wake-free listening is running, not that speech recognition succeeds. No recognized phrase yet in verification window.

### Bounded microphone/recognizer audio diagnostic
- User authorized inspecting audio after the direct-release test recognized only three of their many attempts. Added an explicit screen-triggered, one-shot eight-second capture per boot. Raw microphone and AFE output use separate bounded single-producer PSRAM buffers (512 KB total); published samples are immutable. No recording starts automatically, no flash audio storage, no cloud transmission and no motor commands.
- Capture occurs before input conditioning and after AFE processing. These two mono clips have independent starts and must not be assumed sample-aligned. USB transfer starts only after both buffers fill; sends at most one bounded line per UI loop when transmit capacity is available. Retrieval checks exact offsets and block sizes before writing local WAVs.
- Added 12 buffer checks covering unarmed input, signed samples, truncation at capacity and immutable completion. All existing suites pass, plus Python signed-PCM transport/malformed-block checks. Hardware capture and acoustic assessment pending.
- Diagnostic firmware compiled (2,370,650 bytes); linked ELF confirms the strong processed-audio observer overrides the default no-op. Fresh fitness=4 checked, upload and model hashes verified, post-boot ready=1, registered=2 and microphone errors=0. Local retrieval is waiting for the user's explicit screen tap; no clip has been captured yet.
- Hardware capture retrieved successfully on a second USB retrieval attempt (the first desktop wait expired before transfer; the knob retained the completed clip). Both clips contain exactly 128,000 samples / 8 seconds, zero clipped samples. Raw DC 1363.44, AC RMS 297.86, peak 5438; processed DC -0.116, AC RMS 1180.78, peak 17151. Speech energy is concentrated around 0.5–4.2 seconds.
- Ran installed desktop Whisper base.en locally on both clips, without a vocabulary prompt (English, temperature 0). Both independently transcribe “release weight, release weight, release weight”. Model downloaded locally; audio was not submitted to an external transcription service. Files and transcripts remain in ignored build/voice-diagnostic-20261004.
- Knob runtime still reports rawCmd=0, release=0, microphone errors=0, registered=2. Evidence shows speech was captured and intelligible to a separate model; it does not establish that the embedded model/integration is correct or that microphone quality is adequate in all conditions. Next diagnostic should replay this same captured input against MultiNet to isolate command configuration/inference behavior without asking the user to repeat speech.

### On-device embedded recognition replay
- Used the exact captured post-AFE PCM as a diagnostic-only firmware fixture. Normal builds do not include the fixture. Release packaging now rejects either a private fixture header or a replay-marked binary; guard tested without creating a release artifact.
- Fresh unloaded state checked before replay firmware installation. Each trial creates a fresh MultiNet7 instance with the SDK's default settings unless explicitly overridden, registers commands, and feeds 16 kHz / 512-sample frames on the ESP32. No microphone/AFE replay dependency and no control actions.
- First matrix: default full phrase 1 detection; continuous decoding 1; Flite G2P 1 (same phonemes as explicit pronunciation); alternate first vowel 0; 2x gain 1; 4x gain 1; threshold 0.5 and 0.3 both 0; threshold 0.1 yielded 2. Matching just “release” yielded 3 detections at input offsets 1504, 2976 and 4192 ms, scores 0.151 / 0.140 / 0.229. These are detections on one recording, not a general accuracy estimate.
- Raw/processed speech-window correlation is 0.973–0.992 at a constant 544-sample (34 ms) offset. This capture shows no evident chunk loss through the AFE during those phrases.
- Preparing a second matrix with release-only vs release-plus-full-phrase grammars and synthetic unrelated speech, including Please wait / Load weight / Increase weight / Decrease weight / Relax. No threshold reduction selected for live use.
- Second matrix: release-only grammar detected all three recorded phrases (same 1504/2976/4192 ms offsets) and zero events from the synthetic unrelated-speech clip at full and 0.2x amplitude. Combined release + full phrases also detected three positives but falsely triggered once at 5024 ms in the unrelated-speech clip. Selected only the “release” keyword with the unchanged default threshold; a spoken “release weight” still contains this keyword.
- The synthetic negative clip was generated locally and independently transcribed to verify its five intended phrases. This narrow result is not evidence of broad false-trigger safety or performance in music. Actual live acceptance remains pending.
- Reduced direct-release duplicate suppression from 1500 to 500 ms: the old gate would discard the second legitimate detection 1472 ms after the first. Added regression coverage for all three observed event times and immediate duplicates; 38 voice checks and all other suites pass.
- Removed the private fixture from source and disabled replay in the next normal build. The manual recording UI now requires the explicit VOLTRA_VOICE_CAPTURE build flag. Normal test badge says “Say Release · test only”. No resistance actions enabled; published shareable release unchanged.
- Final normal keyword build compiled (2,370,046 sketch bytes); ELF has no replay fixture/model-test symbols and application binary has no replay marker. Fresh trainer fitness=4 verified before installation; application and model upload hashes verified.
- Post-install runtime: ready=1, registered=1, directRelease=1, initialization/audio errors=0, wake=0, testOnly=1. Live recognition acceptance remains for the user; recorded replay success is not a reliability guarantee. No voice-to-motor action was added.

### Live keyword acceptance remains insufficient
- User reports many repeated commands with only a few apparent detections after the keyword update. Fresh runtime counters: rawCmd=13, release=13, dropped=0, initErrors=0, microphone errors=0. All model detections passed the app's acceptance gate; total spoken attempts is unknown, so no live success percentage can be calculated.
- Repeated detections currently redraw the same notice and extend its visibility. This is ambiguous per-attempt feedback and must not be interpreted as a measured recognition failure or success count. Asked for the user's approximate attempt count without requesting more repetitions. No motor actions enabled and no claim of live reliability.
- Prepared and compiled clearer test feedback: each accepted release displays its incrementing detection number, with a persistent count in the badge. This changes observability only, not recognition behavior. Not installed yet; awaiting the user's approximate attempt count before interpreting the live result or making further recognition changes.
- User estimates their attempts were approximately the same as the 13 logged detections. This supports ambiguous feedback as a likely explanation for the perceived misses in that test; it does not provide an exact success rate or establish general reliability. Proceeding with the already-built per-detection counter update, leaving recognition settings unchanged. Fresh connected-device fitness=4 verified.
- Feedback update installed: application/model hashes verified. Boot reports ready=1, registered=1, directRelease=1, testOnly=1 and initialization/audio errors=0. Each accepted detection now updates a numbered notice and persistent total; physical display acceptance pending. No recognizer thresholds, grammar or resistance actions changed in this update.

### Load-weight recognition test preparation
- User confirmed the numbered release feedback works very well and requested Load weight next. Added the full “load weight” phrase (LbD WdT), retaining the accepted “release” keyword and default detection threshold. Both remain recognition-only; no trainer/motor calls.
- Direct command acceptance uses separate 500 ms duplicate filters for Load and Release; a Load cannot suppress an immediately following Release. Added same-command duplicate, stale-event, timeout and timestamp-rollover cases. Voice gate: 53 passing checks; all existing suites also pass.
- Test UI now shows per-command numbered notices and a persistent Load/Release total. Preparing on-device replay using the saved user release clip, independently verified local synthetic mixed commands and confusable phrases including “unload weight”. Live user's Load pronunciation remains untested.
- First Load/Release replay (fresh unloaded state before installation): the user's release recording retained all three correct Release detections. Mixed synthetic commands yielded zero detections at full amplitude and only the two Release detections at 0.2x amplitude; Load weight was missed. Confusable phrases yielded zero detections. Therefore the full-phrase Load configuration has NOT passed recognition testing.
- Prepared a short “load” keyword comparison, retaining Release, with and without an explicit non-action “unload” competitor. This is diagnostic only and does not authorize motor actions. Waiting for a fresh trainer connection/unloaded readback after replay reboot before the next install.
- Short-word replay: “load” + “release” detected all four mixed synthetic commands at full and 0.2x amplitude and retained all three user releases. However, the confusable clip falsely detected Load on “unload weight” (full-amplitude score 0.333; low-amplitude 0.138) and also falsely detected Release later at low amplitude. Adding an explicit non-action “unload” competitor did not resolve those errors. The short Load keyword is rejected; no detection threshold was lowered.
- Live vocabulary remains full “load weight” plus the accepted “release” keyword, both recognition-only. Asked the user for two live full-phrase Load attempts because synthetic speech did not establish positive recognition. Removing private replay data and rebuilding the normal firmware; no claim that Load recognition is ready for resistance control.
- User's two live full-phrase Load attempts were both missed; serial counters corroborate load=0. Load recognition is not accepted. Retained full phrase rather than unsafe short keyword, and prepared a targeted microphone recording to diagnose the user's actual pronunciation.
- Changed the existing one-shot recorder to be armed at runtime by the local USB capture tool instead of requiring a special build. Arming only reveals a screen-tap action; it does not start recording. Recording still requires an explicit tap, stops after eight seconds, and stores/transfers audio locally. This avoids rebuilds just to expose the recorder. Normal numbered command feedback remains until the recorder is armed.
- Normal build succeeds (2,371,022 bytes), private replay header and replay marker absent. Fresh fitness=4 checked before installing. Both commands remain test-only; Load is explicitly not yet working reliably.
- Recorder-capable normal firmware installed with application/model hash verification. Boot reports ready=1, registered=2, directLoad=1, directRelease=1, audio/init errors=0 and testOnly=1. Local capture tool has armed the visible tap action and is waiting; no Load recording has been captured yet.

### User Load recording retrieved
- User spoke Load weight about five or six times. The bounded capture contains eight seconds: local Whisper transcribes four complete Load weight phrases from recognizer input; raw transcription has four “load, wait” pairs plus a final “load”. Homophone spelling is not evidence of incorrect speech. Both clips have exactly 128,000 samples and no clipped samples.
- Raw AC RMS=678.04, DC=1361.66, peak=9872; processed AC RMS=2578.89, DC=0.064, peak=27992. Knob rawCmd/load/release counters remain zero for this live test. Clips saved locally in ignored build/voice-load-diagnostic-20261004.
- Preparing replay of this actual user sample with the full phrase, reduced input levels, reduced/linking pronunciation variants, and a diagnostic-only low threshold/short-word comparison. No low threshold or short Load keyword is selected for live use. Any candidate must be checked against the existing confusable speech and accepted Release clip.
- Added a local replay_config.h switch so replay can be compiled without rebuilding every third-party C++ library via global compiler flags. Release packaging blocks an enabled local switch as well as private fixtures/replay binaries.

- Actual-user replay: original full phonemes missed the recorded phrases; reducing input gain did not help. Omitting final T (LbD Wd) yielded three Load detections, but then falsely detected Load on synthetic “unload weight” at full amplitude (1152 ms, 0.188). Rejected this candidate. Release regression remained three correct detections. This is model pronunciation matching behavior, not evidence the user spoke incorrectly.
- Voiced-final variants LbD WdD and Lb WdD each detected three Load events and zero events on the low-amplitude confusable clip. Preparing full-amplitude confusable and Release regression checks before selecting either for live testing. None is enabled in live recognition yet; all commands remain recognition-only.

### Linked Load pronunciation candidate
- Fresh fitness=4 verified before installing the final pronunciation replay. LbD WdD produced three user Load detections but also one false Load on the full-amplitude “unload weight” clip (1472 ms, 0.175), so it was rejected.
- Lb WdD produced three user Load detections at 3136/5280/7776 ms (scores 0.356/0.273/0.189). It produced zero detections on the confusable clip at full and 0.2x amplitude and preserved all three accepted user Release detections at 1504/2976/4192 ms. Default threshold retained. Small saved-clip results do not establish broad accuracy, response latency or safety under background speech/music.
- Selected only Lb WdD for the live full-text “load weight” test; Release is unchanged. No model weights were retrained. Both commands remain recognition-only with separate numbered counters and no motor actions.
- Removed the private audio fixture and disabled offline replay before the normal build. Live installation and user acceptance pending; published r16 remains unchanged.
- Normal candidate build passed (2,371,022 sketch bytes). Binary contains the selected Load pronunciation and unchanged Release pronunciation, with no replay marker; private fixture absent and replay disabled. Awaiting fresh unloaded readback to install.
- Live candidate installed after fresh connected fitness=4 readback. Upload completed with application/model hash verification. Post-boot ready=1, registered=2, directLoad=1, directRelease=1, testOnly=1, initialization/audio errors=0; listening resumes after timeout. No replay reports in normal boot. User live acceptance pending; no voice resistance actions enabled.
- After requested three live Load attempts, counters report rawCmd=3/load=3/dropped=0, with initialization/audio errors=0. User also reports screen lag. Existing maxGapMs=526 is lifetime-since-boot and cannot establish current steady-state stalls. Code inspection: speech detection is priority 5 on core 1 (shared with Arduino UI); recognition notice intentionally intercepts touches in its rectangle for 3.5 s. Need distinguish animation latency from notice interception before selecting a UI/scheduling change.

### Screen responsiveness with continuous recognition
- User requested fixing screen lag. Prior firmware still showed live load=3/dropped=0 and unloaded fitness=4. Lifetime max loop gap was 526 ms; that number alone does not isolate steady-state lag.
- Found UI values throttled to 80 ms and helper setters escalating every text/style change to full-screen invalidation. Changed value refresh to 20 ms and retained full-screen invalidation only for page transitions; LVGL setters and explicit custom-icon invalidations handle changed regions.
- Removed intermediate generic details-text updates in Band/Damper/Isokinetic, where the same loop immediately overwrote them with mode-specific text, causing repeated invalidation even at rest.
- UI task now priority 6, above speech detection priority 5 on the same core; the existing 5 ms blocking delay remains to allow inference to run. Audio feed and BLE core assignments unchanged. Need hardware throughput verification to check for inference starvation.
- Recognition results now update only the persistent numbered badge below the controls; they no longer show a 3.5-second touch-intercepting popup over the weight. Explicit audio-capture notices remain. Voice commands remain test-only and their pronunciation/thresholds are unchanged.
- Added per-report maximum loop gap, slow-loop count, UI and render durations to distinguish current stalls from startup maxima. Serial remains nonblocking. Build/hardware validation pending.
- Build passed (2,370,970 sketch bytes); fresh fitness=4 verified and upload hashes passed. At idle on trainer selection, five post-boot windows show 7–8 ms maximum loop gaps and zero gaps over 50 ms. About 910 loops and 455 touch reads per 5 seconds, versus roughly 400/250 in prior steady-state logs (different page/state, so not a controlled benchmark). Voice advances 156–157 frames per 5 seconds, matching 16 kHz / 512 samples, with no audio/init errors. Physical interaction acceptance pending.

### Voice control enabled at user request
- User confirmed the screen lag fix works well and explicitly requested removing test mode and enabling voice control. Recognition vocabulary, default threshold and UI scheduling retained.
- Load/Release events now invoke the existing UI/link control path. Load requires the control page, fresh unloaded/settings readiness, no pending edit/load or link busy interval. It is an explicit Load request, never a toggle. Release uses the existing priority stop path, independent of page/pending settings/stale status, and suppresses another voice Load for 1.5 seconds.
- Events carry connection session and mode. Stale events, cross-session events and Load mode mismatches are rejected. Expected session is rechecked under the link mutex at request time. Diagnostic capture arming disables voice actuation until reboot.
- Feedback says Load/Release requested, not loaded/unloaded; actual state continues to come from the trainer. No popup obscures controls. No autonomous motor action will be used for verification.
- Host suites passed, including 70 voice checks covering loaded/disconnected/stale/wrong-session/wrong-mode/pending/busy cases and Release availability. Final build/install pending.
- Final build passed (2,371,594 sketch bytes). Installed after fresh connected fitness=4 readback; upload hashes verified. Runtime testOnly=0, ready=1, registered=2, initialization/audio errors=0, no commands recognized or resistance operations triggered during verification. Post-startup loop windows remain 7 ms maximum with no >50 ms gaps; inference keeps real-time pace. Real motor response via voice has not been exercised by the agent. Published release unchanged.
- User requested removing bottom voice text. Normal voice badge is now hidden, and routine readiness/request updates removed. Explicit USB-armed microphone diagnostics can still reveal their capture button. Voice actions and recognition settings unchanged; build/install pending.
- Bottom-text removal installed with verified hashes; normal boot confirms testOnly=0, two registered commands and no audio/init errors. r17 release preparation: host suites and native LVGL UI harness passed; current firmware rebuilt successfully. No voice motor action performed.

### Unintended unload report — 2026-10-12
- User reports occasional self-unloading. Read-only USB snapshot shows nine accepted Release recognitions, zero Load recognitions since boot; knob currently disconnected/on selector. Old firmware lacks per-request origin/write counters, so this does not establish that those detections caused the reported unloading or were false recognitions.
- Code audit finds STOP written only from the priority stopRequested path; requestUnload callers are touch and voice. Preparing a temporary default-off voice actuation gate (both Load and Release), retaining manual touch behavior and UI responsiveness. No threshold change or claim of recognizer reliability.
- Added RAM counters for touch/voice/other unload requests, request timestamp/session, successful STOP writes and last write timestamp. These survive connection changes and appear in bounded nonblocking status logs; they reset at reboot. A successful BLE write is not proof of a physical unload.
- Host tests pass including disabled-actuation checks (74 voice checks). Firmware build/native UI verification pending. Not installed and no trainer commands sent; awaiting confirmation both trainers are unloaded. Published GitHub r17 unchanged.
- User confirmed no Voltras nearby and authorized installation. Build passed (2,371,446 sketch bytes), all host suites and native UI checks passed, including no link calls from voice when disabled. Installed with flash hash verification. Boot reports testOnly=1, two registered recognition commands, no init/audio errors, zero unload requests/STOP writes and 7 ms post-startup loop windows. This mitigates voice-triggered load/unload; it does not prove the original cause. Public r17 remains unchanged.
