# BRIAN 2.0 — Schematic Review (collaborator prototype, KiCad 9.0.7)

**Reviewed file:** `project_eNose` (25-sheet hierarchical schematic)
**Date:** 2026-07-03
**Reviewed against:** `V2_design.md`, `heater_power.md`, `pwm_driver.md`, `sensor_output_architecture.md`
**Design intent:** flexible prototype PCB for testing, reconfigurable as needed (not a final production board)

---

## 1. Alignment with current design decisions

The schematic tracks the **two most recent** design docs well:

- **Dual-MCU split** (`pwm_driver.md`): ESP32-S3-WROOM-1 as host + RP2040 as dedicated PWM co-processor driving two **TBD62083APG** 8-ch DMOS arrays for the 14 low-side heater switches. Implemented as drawn. ✅
- **Readout chain** (`sensor_output_architecture.md`): per-sensor unity buffer (TLV2372) + passive RC (10 kΩ + 10 nF ≈ 1.6 kHz) → **2× ADS131M08** 24-bit ΔΣ ADC on a shared SPI bus with separate CS, **no MUX**. Implemented as drawn. ✅
- **Gain-config provision**: each buffer has jumpers (JPxx) + unpopulated 10 kΩ pads so it can be reconfigured from unity to inverting/non-inverting without a respin. ✅ Matches the "reconfigurable prototype" goal.
- **Power**: BQ24075 charger + BQ27441 fuel gauge; TPS63001 buck-boost for 3.3 V (correct choice — a 1-cell LiPo sags below 3.3 V, so boost capability is needed) and two MPM3805 bucks for the 1.8 V / 2.5 V heater rails. ✅

> **Doc inconsistency to fix (not a schematic error):** `V2_design.md` §Controller still specifies the _classic_ ESP32 + **CP2102N** USB-UART bridge, and §ADC still names the **ADS122C14**. Both are superseded — the schematic correctly uses ESP32-S3 (native USB, no CP2102N) and 2× ADS131M08. Update `V2_design.md` so it matches `pwm_driver.md` and `sensor_output_architecture.md`, otherwise the next reviewer will flag the schematic against the wrong baseline.

---

## 2. Expansion on the senior designer's six points

### (1) Give two separate USB blocks in the root schematic

Currently both USB-C receptacles (J2, J3) live in one `usb-conn-ckt` sheet. Splitting into **two blocks — one per MCU** — is worth doing, and not just for tidiness:

- USB0 → ESP32-S3 (native USB, IO19/IO20); USB1 → RP2040 (native USB, DP/DM via the 27 Ω series R79/R80). Separate blocks let each connector be placed and reasoned about next to its MCU.
- **More important — it surfaces a real problem the single block hides:** both receptacles' `VBUS` pins are currently tied to one common `VBUS` net. With two host ports, plugging into both simultaneously **shorts the 5 V of two computers together and back-feeds one host from the other.** Two separate blocks make it obvious that each `VBUS` needs isolating (ORing/ideal diodes, e.g. two Schottky or an ideal-diode ORing IC) before they combine to feed the charger input. See point (2) and §3 for the charger-input consequence.

### (2) VSYS vs BatP; charger and fuel-gauge circuits

These three nets are distinct and must not be conflated:

- **BatP** = the raw battery-terminal voltage (≈3.0–4.2 V, sags under load, rises while charging). It is the cell node.
- **VSYS** = the BQ24075 **power-path output** (OUT pins 10/11). The BQ24075 has dynamic power-path management: VSYS is fed from the USB input when present and seamlessly from the battery when not, so the board runs even with a flat or absent battery and VSYS is cleaner than BatP. **All regulators (3.3/2.5/1.8 V) should run from VSYS, not BatP** — the schematic does this correctly. ✅
- **VIN** = the BQ24075 **input** (IN pin 13). This must be driven by USB **VBUS**, and I could not confirm `VBUS → VIN` is actually connected on the root sheet (they appear as separate net names). **Verify this net.** If VIN is left unconnected the charger has no source. With two USB ports (point 1), VIN must be the **ORed** VBUS, not a raw tie.

Fuel-gauge (BQ27441) items to check carefully — this is the part most likely to be wired wrong:

- The BQ27441 is a **single-cell, low-side coulomb counter.** The 10 mΩ sense resistor (R9) must sit in the **battery current path referenced to system ground**, with SRP/SRN across it per TI's reference, and the device VSS at ground. As drawn, the sense resistor appears to be spliced between BatP and **VSYS** (high side), which would put the gauge's current-sense reference on the wrong node and give incorrect state-of-charge. Redraw against the BQ27441 datasheet reference schematic (Figure in §"Application and Implementation"): battery **–** terminal → R_sense → system GND; SRP to the pack-negative side, SRN to GND; BAT pin senses BatP through its RC; VDD from BatP.
- Confirm the gauge measures **battery** current (charge + discharge), i.e. it sits in the battery leg, **not** in the VSYS leg (which would miss the USB-supplied load current and mis-estimate SoC).
- GPOUT → ESP interrupt is fine; make sure it has a pull-up (open-drain alert).

### (3) Add one more user switch for the ESP32

Agreed. The ESP32-S3 currently has **SW1 → GPIO0 (BOOT)** and **SW2 → EN (RESET)** — these are _programming_ functions, not user I/O. Add a **third button (SW → free GPIO, active-low, 10 kΩ pull-up to 3V3, 0.1 µF debounce to GND)** as a general-purpose user input for testing. Pick a non-strapping, non-USB, non-flash GPIO (see point 6 for the safe list). This mirrors what the RP2040 already has (SW3 on RUN).

### (4) The switch circuit "seems wrong" — correct it

The BOOT/EN buttons must be **active-low**: pin held **high** by a 10 kΩ pull-up to 3V3, button shorts the pin **to GND**, and the cap sits **pin-to-GND** (debounce / power-on-reset delay). Two things to correct/verify against Espressif's ESP32-S3 reference (the "ESP32-S3-WROOM-1 & Schematic" reference design):

1. **Pull-up direction / button polarity.** If, as it appears, the resistor is a pull-_down_ or the button pulls the pin to VDD, the logic is inverted — EN held low = chip stuck in reset; GPIO0 low at boot = permanent download mode. Each node must pull **high** by default and be pulled **low** only while pressed.
2. **No series R between button and pin, and don't let the button short a charged cap through zero resistance.** The canonical circuit is: `3V3 —[10 kΩ]— NODE —(button)— GND`, with `NODE —[0.1 µF]— GND` and NODE → GPIO. The resistor is the pull-up, _not_ in series with the pin. On EN, Espressif uses ~1 µF for the RC power-on delay (0.1 µF works but 1 µF is the reference value). Verify R2/C2 (GPIO0) and R1/C1 (EN) are arranged exactly this way; the current arrangement reads as R in series / cap misplaced.

### (5) UART{Tx Rx} between RP2040 and ESP32 — don't keep it as a bus

A UART link is **point-to-point and must cross over** (TX→RX, RX→TX). Drawing it as a bus named `UART{Tx Rx}` is fragile:

- As currently wired the crossover _appears_ to resolve correctly (net `UART.Tx` = RP2040-TX + ESP-RXD0; net `UART.Rx` = RP2040-RX + ESP-TXD0), so it may be functionally right — **but the bus notation is exactly what hides a TX↔TX / RX↔RX short.** A reader who assumes "UART.Tx" means "each device's own TX" would wire a dead link.
- **Fix:** drop the bus and use two explicit nets named by intent, e.g. `RP2040_TX_to_ESP_RX` and `ESP_TX_to_RP2040_RX` (or `MCU_UART_A/B`). Verify the ESP side uses a **spare UART**, not the USB-Serial/JTAG or the ROM console pins, if you want clean debug output.

### (6) Confirm ESP32 SPI pins; relabel SPI0… → ESP_MISO…

- **Confirmation:** the ESP32-S3 GPIO matrix lets **almost any GPIO** be routed to a hardware SPI peripheral. The S3 exposes **two general-purpose SPI controllers — SPI2 (FSPI) and SPI3** — which is exactly enough if you want the **ADC bus** on one and the **RP2040 link** on the other (or share one SPI2 bus across all three CS lines). So yes, HW SPI is available; the constraint is _which_ pins. **Avoid:** GPIO0/3/45/46 (strapping), GPIO19/20 (native USB D–/D+, already used), GPIO26–32 (SPI flash), GPIO33–37 (octal PSRAM — free **only** on the no-PSRAM `-N16`; occupied on `-N16R8`), and GPIO43/44 (default UART0). Prefer IO4–18, IO21, IO47/48 (and IO35–37 only if the module is truly `-N16`). Check every SPI/`A_SPI` assignment against this list.
- **Relabel:** replace the generic `SPI0..3` net names with directional names — `ESP_MOSI`, `ESP_MISO`, `ESP_SCLK`, `ESP_CS` (and for the ADC side, `ADC_MOSI/MISO/SCLK/DRDY/CS1/CS2/SYNC…`). The RP2040 sheet already maps `SPI0=RP_MOSI, SPI1=RP_MISO, SPI2=RP_SCLK, SPI3=RP_CS` — name both ends consistently so MOSI↔MOSI is visually obvious (SPI is not crossed, unlike UART).

---

## 3. Additional findings (my own review)

Prioritised. "Blocker" = will likely stop the board working; "Fix" = should correct before fab; "Improve" = prototype-quality / clarity.

### Blockers — verify before fabrication

- **ADC clock (ADS131M08 CLKIN / XTAL1) left unconnected.** On both U23 and U24, `XTAL1_CLKIN` (pin 23) is marked no-connect. The ADS131M08 has **no fully internal oscillator** — it needs an external CMOS clock on CLKIN (typ. 8.192 MHz) or a crystal on XTAL1/XTAL2. **As drawn, neither ADC will convert.** Drive CLKIN from a shared oscillator or an MCU clock output (route the same clock to both for synchronised sampling). **_FROM THE DATASHEET_**: `The master clock can either be sourced externally to the CLKIN pin or generated internally using the onboard oscillator that requires a crystal connected between the XTAL1/CLKIN and XTAL2 pins. For optimal performance, the modulator sampling clock must be synchronous with the serial data clock (SCLK). The modulator sampling clock is derived from the master clock, which means the master clock must be synchronous with SCLK. Therefore, for best performance, supply a master clock to CLKIN and make sure data retrieval is synchronous to the clock signal at CLKIN. When not in use, turn the internal oscillator off to save power.`
- **Charger input source (VBUS → VIN).** Confirm USB VBUS actually reaches BQ24075 IN. If the nets `VBUS` and `VIN` aren't joined (via the ORing diodes), the charger and power-path have no input. (See point 2.)
- **Dual-USB VBUS ORing.** Both receptacles' VBUS are common — isolate them before they meet VIN (see point 1). Without this, connecting both ports back-feeds one host from the other.

### Fixes — correct before fab

- **1.8 V regulator part value.** U27 (the 1.8 V DC-DC) carries the symbol value **`MPM3805GQB-25-Z`** (the 2.5 V variant) even though the sheet is the 1.8 V rail. Looks like a copy-paste from the 2.5 V sheet. Confirm the correct fixed-output/feedback config for 1.8 V and correct the value/part number.
- **BQ24075 TS pin.** TS (pin 1) is tied via R4 (10 kΩ) to GND. If there's no battery pack thermistor, TS must be biased into its valid window with a **resistor divider** per the datasheet; a single 10 kΩ to GND will read out-of-range (hot/cold fault) and **suspend charging.** Verify the TS network disables JEITA correctly (or fit the divider).
- **BME680 address/strap pins.** In I2C mode: `CSB → 3V3` ✅ selects I2C, but confirm `SCK→SCL`, `SDI→SDA`, and that **`SDO` is tied to a static level** (GND = 0x76 / VDD = 0x77) to set the address. The extracted netlist suggests SDO may be tied to SCL — that would be an error. Fix SDO to a defined level.
- **Unused op-amp halves (all 14 sensor sheets).** Each TLV2372 is a **dual**; only channel 1 is used, and channel 2's inputs (2IN+, 2IN−) are left floating. Floating CMOS op-amp inputs oscillate and draw excess current. Terminate every spare half as a follower with its + input tied to a defined level (e.g. `2IN+ → GND`, `2OUT → 2IN−`). Alternatively this spare half is the natural home for the optional active LPF/gain stage mentioned in `sensor_output_architecture.md` — either populate it or tie it off.
- **"Analog → I2C" mislabel.** The ADC subsheet is titled `Analog -> I2C` / "ADC to I2C Conversion", but the ADS131M08 is **SPI**, not I2C. Rename the sheet and title to `Analog → SPI` to avoid confusion downstream.

### Improve — prototype quality & clarity

- **Heater-rail LC post-filter is missing.** `heater_power.md` specifies a second-stage `L2 10 µH + 47 µF/100 µF` post-filter on each buck output (≈40 dB extra at f_sw, sub-mV ripple), and the readout doc leans on it to justify dropping the active LPF. The MPM3805 sheets show only 10 µF output caps. For a measurement board where ripple couples into the sensors, **at least lay down the L+C footprints** so you can populate them if prototyping shows ripple. Also target the buck set-point ~50 mV high to offset the L2 DCR drop (per the doc).
- **Local op-amp decoupling.** I don't see a 0.1 µF from each TLV2372 VDD (pin 8) to GND — the 10 nF is the RC filter cap, not supply decoupling. Add a 0.1 µF per op-amp at the VDD pin.
- **Per-ADC decoupling.** AVDD/IOVDD/REFIN appear to share one 1 µF bank (C54–C56) across both ADS131M08. Give **each** ADC its own AVDD/IOVDD/REFIN 0.1 µF + 1 µF close to the pins; keep the 220 nF LDOCAP per chip (present ✅). Confirm whether REFIN uses the internal 1.2 V reference (cap-to-GND only) or an external ref.
- **USB data-line ESD.** For a bench prototype that gets plugged/unplugged a lot, consider ESD diodes on the D±/CC lines of both USB-C ports (many designs rely on the SoC's internal clamps, but the arrays are cheap insurance).
- **Net-naming consistency.** `VDD` and `3p3V` seem to refer to the same 3.3 V rail in places — pick one name for the logic rail to avoid a phantom net at ERC time. Same spirit as the reviewer's SPI/UART naming notes: explicit, directional, single-source-of-truth net names throughout.
- **Run ERC.** Several of the above (floating inputs, unconnected CLKIN, VBUS/VIN, SDO) are exactly what KiCad's ERC flags. A clean ERC pass with intentional "no-connect" flags only where truly intended will catch most of this class of issue.

---

## 4. Quick checklist for the collaborator

| #   | Item                                                                                  | Type          |
| --- | ------------------------------------------------------------------------------------- | ------------- |
| 1   | Split USB into two per-MCU blocks; add VBUS ORing diodes                              | (1) + blocker |
| 2   | Confirm VBUS→VIN; separate VSYS/BatP/VIN; redraw BQ27441 low-side sense per datasheet | (2) + blocker |
| 3   | Add 3rd ESP32 user button (free GPIO, PU + debounce)                                  | (3)           |
| 4   | Correct BOOT/EN buttons: active-low, PU to 3V3, cap to GND, no series R               | (4)           |
| 5   | Un-bus the UART; explicit crossed, directionally-named nets                           | (5)           |
| 6   | Confirm SPI pins avoid strap/USB/flash/PSRAM; relabel SPI0..3 → ESP_MOSI/MISO/SCLK/CS | (6)           |
| 7   | **Connect ADS131M08 CLKIN on both ADCs**                                              | blocker       |
| 8   | Fix 1.8 V regulator part value (reads -25-Z)                                          | fix           |
| 9   | Fix BQ24075 TS bias; BME680 SDO to static level                                       | fix           |
| 10  | Terminate 14 spare op-amp halves                                                      | fix           |
| 11  | Rename "Analog→I2C" sheet to SPI                                                      | fix           |
| 12  | Add heater-rail LC post-filter footprints; per-part decoupling                        | improve       |
| 13  | Update `V2_design.md` Controller/ADC sections to match current design                 | doc           |
