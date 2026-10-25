# MVP v1.1 — flat e-ink / organic visual language

**Design status:** Proposed engineering specification. No physical fit, signal integration, e-ink refresh rate, or battery-runtime validation yet.

## Locked industrial design

- No pen nib or writing function. Slender rectangular, pen-like proportions.
- **Entirely FLAT display** on a flat, supported carrier. Do **not** curve, crown, wrap, or flex the display.
- Transparent or translucent **3D-printed PETG** shell, precise hardware silhouette, small corner radii, quiet Braun/Apple-inspired detailing.
- Single tactile record button on the side, USB-C on end, microphone acoustic aperture, removable microSD access, no fake speaker holes.
- **Target outer envelope: 132 × 40 × 23 mm**, an initial CAD maximum only, adjustable after component measurement. Previous renders at 18mm wide were not feasible. The 20mm-thick concept is also not verified.
- Two-piece screwed enclosure, serviceable and removable battery. Prefer captive brass heat-set inserts to self-tapping screws for repeated assembly.
- PETG can be translucent but will not behave like optically clear glass when FDM-printed. Place a thin, flat, transparent window over the active e-ink area or use a clean display opening with a recessed bezel. Do not view the e-ink through thick cloudy PETG.
- Print **simple dimensional dummy first**, not a finished shell: 1.6–2.0mm walls, design-specific mic and USB clearances, generous ribbon strain relief; measure real parts before fixing standoffs.

## Locked electronics for bench prototype

| Subsystem | Specific part | Critical geometry / notes |
| --- | --- | --- |
| MCU/voice/microSD | Seeed **XIAO ESP32S3 Sense** expansion module | Mic GPIO42 CLK/GPIO41 DATA; SD GPIO21 CS and GPIO7/8/9 SPI; camera not used |
| Display | Adafruit **4243**, 2.13-inch, 212×104, 4-level grayscale flex e-ink | **59.2 × 29.2 × 0.3 mm**, FPC ribbon ~70 mm. Its flexibility helps thinness, not curvature. FLAT mounting. **ILI0373** 2025+ revision |
| Display driver | Adafruit **4224**, eInk Breakout Friend with 32KB SRAM | **34.7 × 21.5 × 3.5 mm**; place alongside rather than on top of 15mm-high Sense stack |
| Battery | Protected single-cell 3.7V LiPo, test candidate 500mAh | Cell dimensions and charging current to be checked with the exact purchased battery |
| Control | One tactile momentary switch | D1 / GPIO2 → GND |
| LED | Optional external red/white LED + resistor | D0 / GPIO1. Avoid Sense onboard GPIO21 LED because it shares SD CS |

The earlier concept image incorrectly labels a 2.13-inch e-ink screen as 212×104mm; **212×104 is the resolution in pixels**, NOT dimensions in mm.

### SPI integration risk: address before CAD

The existing Sense microSD occupies XIAO SPI pins D8/D9/D10 (GPIO7/8/9) and SD CS GPIO21. The Adafruit e-ink requires an SPI bus too. **Seeed warns these pins are shared with its SD subsystem. Do not assume both will coexist without explicit chip-select arbitration and testing.**

**First strategy to test:** separate SPI controller/peripheral on remaining pads, or carefully controlled sharing with unique e-ink CS and all other CS lines deselected. On Arduino ESP32-S3, map e-ink SPI to otherwise free D2/D3 pads only after validating pin assignments and physical board revision. The display requires DC, CS, reset and busy lines as well. This wiring is still an **engineering spike**, not approved production pinout. Preserve D1 record button and optional D0 external LED; do not drive onboard USER_LED GPIO21.

Adafruit's official Arduino initialization for the *updated* 4243 display uses **ThinkInk_213_Grayscale4_T5** with the **ILI0373** revision. Do not use older monochrome-M21 code from pre-2025 documentation without checking actual chipset.

## UI spec: organic ferrofluid-inspired e-ink

**Physical screen stays completely flat.** Only the graphics are organic.

- Limited palette: white + black + **two intermediate grayscale values**, with intentional dither for soft rounded silhouettes. Never assume full 8-bit grayscale.
- Ready: one suspended, small irregular ink droplet, text `READY`, file number.
- Recording: slightly larger organic ink shape and unmistakable `REC` indicator. Optional slow state variation only if panel update timing, ghosting and capture stability allow it.
- Saving: sharply defined still form with label `SAVING`.
- Saved: compact blob + check mark, label `SAVED`.
- Error: broken/fragmented shape and clear `CARD ERROR` / `REC ERROR` message.
- The physical status LED, not the slow e-ink, indicates recording in real time. The display should update at state transitions rather than every second.
- No fake waveform animations: if a waveform is used, it is a snapshot generated from microphone data and refreshed only sparingly.
- No on-device transcript in v1; WAV → macOS whisper.cpp import remains separate.

## Assembly layout hypothesis (requires real measurements)

1. Flat display under front window, FPC passes through rounded strain-relief slot.
2. Breakout Friend behind display *in adjacent lengthwise pocket*, not vertically over the thick XIAO Sense stack.
3. XIAO Sense near one end with mic acoustic port; remove unused camera module if removable without damaging the board.
4. Flat LiPo lengthwise in back cavity away from screw bosses or sharp PCB edges.
5. Access USB-C through end; microSD accessible via side slot **without opening shell**.
6. Switch mounted flush on side with optional external LED near it.
7. Test USB and microSD insertion clearance with actual cables/cards before finalizing CAD.

## Next milestone order (do not skip bench testing)

- [ ] Source the **exact** 4243 revision, 4224 breakout, Sense board, SD card, switch, approved LiPo.
- [ ] Confirm standalone **30-second mic → WAV → Mac Whisper transcript** on Sense hardware.
- [ ] Independently drive the **flat** e-ink with `ThinkInk_213_Grayscale4_T5` and show Ready/Recording/Saved frames.
- [ ] Prove **simultaneous e-ink + SD** access with the recording task active (no lost audio, no bus contention).
- [ ] Test button debounce, failure modes, and powered charging before enclosure.
- [ ] Create dimensioned assembly CAD from the **measured** parts, ribbon routing, and battery clearance.
- [ ] Print low-cost draft in plain PETG, adjust, then print transparent PETG final and test acoustic performance.

## References

- https://www.adafruit.com/product/4243
- https://www.adafruit.com/product/4224
- https://learn.adafruit.com/bare-e-ink-displays-crash-course/2-13-flexible-212x104-monochrome-eink-ili0373
- https://wiki.seeedstudio.com/xiao_esp32s3_pin_multiplexing/
- https://wiki.seeedstudio.com/xiao_esp32s3_sense_mic/
