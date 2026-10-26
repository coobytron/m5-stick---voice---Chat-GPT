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

## Screen correction — actual part number required

The user **already owns a four-color e-ink panel**, not the four-level grayscale panel assumed in the original draft. **Do not order Adafruit 4243, the 4224 driver, or build against ILI0373 until the exact owned display model has been confirmed.** Four real ink colors and four grayscale levels are different display technologies. Retain the FLAT display decision. Candidate **only if the user's part number matches**: Adafruit 6366 (integrated 2.13-inch 250×122 BRYW quad-color with SRAM and microSD) or 6373 (bare JD79661 quad-color panel requiring a compatible driver); other brands/chipsets remain possible. Do not assume board dimensions or pinout from the old 4243 design.

## Locked electronics for bench prototype

| Subsystem | Specific part | Critical geometry / notes |
| --- | --- | --- |
| MCU/voice/microSD | Seeed **XIAO ESP32S3 Sense** expansion module | Mic GPIO42 CLK/GPIO41 DATA; SD GPIO21 CS and GPIO7/8/9 SPI; camera not used |
| Display | **User-owned four-color e-ink, exact SKU unknown** | Measure and identify actual panel before specifying aperture, mounting or firmware. Mount FLAT. If Adafruit 6366/6373, likely 250×122 BRYW, JD79661; **not confirmed**. |
| Display driver | **TBD after identifying existing display** | Some four-color panels include an integrated controller board (e.g. 6366), while bare panels require an appropriate driver. Do not order until verified. |
| Battery | Protected single-cell 3.7V LiPo, test candidate 500mAh | Cell dimensions and charging current to be checked with the exact purchased battery |
| Control | One tactile momentary switch | D1 / GPIO2 → GND |
| LED | Optional external red/white LED + resistor | D0 / GPIO1. Avoid Sense onboard GPIO21 LED because it shares SD CS |

The earlier concept image incorrectly labels 212×104 as millimeters; it is a *pixel resolution* of a different grayscale panel. The correct physical measurements and resolution of the user-owned quad-color panel are not yet established.

### SPI integration risk: address before CAD

The existing Sense microSD occupies XIAO SPI pins D8/D9/D10 (GPIO7/8/9) and SD CS GPIO21. A quad-color e-ink module may require an SPI bus too, depending on its exact controller and breakout. **Seeed warns these pins are shared with its SD subsystem. Do not assume both will coexist without explicit chip-select arbitration and testing.**

**First strategy to test:** separate SPI controller/peripheral on remaining pads, or carefully controlled sharing with unique e-ink CS and all other CS lines deselected. On Arduino ESP32-S3, map e-ink SPI to otherwise free D2/D3 pads only after validating pin assignments and physical board revision. The display requires DC, CS, reset and busy lines as well. This wiring is still an **engineering spike**, not approved production pinout. Preserve D1 record button and optional D0 external LED; do not drive onboard USER_LED GPIO21.

**Remove old grayscale implementation assumption:** do NOT initialize ThinkInk_213_Grayscale4_T5 unless the owned display is genuinely the ILI0373 grayscale 4243. If the owned display is Adafruit 6366/6373, use the correct JD79661 quad-color driver and corresponding Adafruit guide. The actual controller must be identified before firmware integration.

## UI spec: organic ferrofluid-inspired e-ink

**Physical screen stays completely flat.** Only the graphics are organic.

- **Four real ink colors, palette TBD by owned SKU**. If BRYW (black, red, yellow, white), use black for organic mass, red sparingly for REC, yellow for 1–3 satellite droplets, and white space as background. Do not assume grayscale or alpha blending; simulate tonal softness with spatial dithering, if legible.
- Ready: one suspended, small irregular ink droplet, text `READY`, file number.
- Recording: slightly larger organic ink shape and unmistakable `REC` indicator. Optional slow state variation only if panel update timing, ghosting and capture stability allow it.
- Saving: sharply defined still form with label `SAVING`.
- Saved: compact blob + check mark, label `SAVED`.
- Error: broken/fragmented shape and clear `CARD ERROR` / `REC ERROR` message.
- The physical status LED, not the slow multi-pigment e-ink, indicates recording in real time. The display should update at state transitions rather than every second.
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

- [ ] Confirm model/product ID, electronics and physical dimensions of the **existing four-color display**; only source missing compatible connectors/drivers, Sense board, SD card, switch, protected LiPo.
- [ ] Confirm standalone **30-second mic → WAV → Mac Whisper transcript** on Sense hardware.
- [ ] Independently drive the **flat four-color e-ink** using its verified controller-specific Arduino driver and render Ready/Recording/Saved frames.
- [ ] Prove **simultaneous e-ink + SD** access with the recording task active (no lost audio, no bus contention).
- [ ] Test button debounce, failure modes, and powered charging before enclosure.
- [ ] Create dimensioned assembly CAD from the **measured** parts, ribbon routing, and battery clearance.
- [ ] Print low-cost draft in plain PETG, adjust, then print transparent PETG final and test acoustic performance.

## References

- https://www.adafruit.com/product/6366 (possible four-color integrated breakout; **not confirmed**)
- https://www.adafruit.com/product/6373 (possible four-color bare display; **not confirmed**)
- https://learn.adafruit.com/adafruit-2-13-250x122-quad-color-eink
- https://wiki.seeedstudio.com/xiao_esp32s3_pin_multiplexing/
- https://wiki.seeedstudio.com/xiao_esp32s3_sense_mic/
