# E-Nose Hardware — BRIAN 2.0 Design Reference

## Overview

BRIAN 2.0 is a 14-sensor electronic nose designed for discriminating volatile organic compounds and gas species. The core goals of the redesign are:

- **Expanded sensor array** using a curated mix of Winsen GM-x02B/x12B and IDM/Huiwen SMD10xx MEMS resistive hot-plate sensors
- **Controllable sensor heating** with per-sensor MOSFET switching to enable temperature modulation — cycling each heater through a set-point sequence during a measurement to extract richer discriminating information
- **Improved ADC** with higher resolution and gain for better signal quality
- **Direct on-board MCU integration** — ESP32-S3 host + RP2040 PWM co-processor mounted on the PCB, rather than a detachable controller/daughterboard

---

## Sensor Array

The array contains **14 sensors** across two heater voltage rails. Two sensors from an earlier 16-sensor design were removed to eliminate the need for 2.0 V and 3.0 V rails:

- **GM-402B** (combustible gas, 3.0 V, ≤80 mW) — removed; functionally redundant with SMD1008 (CH₄) and SMD1011 (C₃H₈). Its 3.0 V requirement would have required a dedicated buck-boost converter.
- **GM-802B** (NH₃, 2.0 V) — removed; functionally redundant with SMD1002 (NH₃) on the 1.8 V rail.

### Sensor inventory

| Sensor    | Manufacturer | Target gas             | V_H           | P_H (max) | Rail   |
| --------- | ------------ | ---------------------- | ------------- | --------- | ------ |
| GM-102B   | Winsen       | NO₂                    | 1.8 V ±0.1 V  | ≤ 40 mW   | 1.8 V  |
| SMD1001   | IDM/Huiwen   | Formaldehyde           | 1.8 V ±0.05 V | ≤ 36 mW   | 1.8 V  |
| SMD1002   | IDM/Huiwen   | Ammonia (NH₃)          | 1.8 V ±0.05 V | ≤ 36 mW   | 1.8 V  |
| SMD1007   | IDM/Huiwen   | Hydrogen sulfide (H₂S) | 1.8 V ±0.05 V | ≤ 36 mW   | 1.8 V  |
| SMD1008   | IDM/Huiwen   | Methane (CH₄)          | 1.8 V ±0.05 V | ≤ 30 mW   | 1.8 V  |
| SMD1011   | IDM/Huiwen   | Propane (C₃H₈)         | 1.8 V ±0.05 V | ≤ 30 mW   | 1.8 V  |
| SMD1013B  | IDM/Huiwen   | TVOC                   | 1.8 V ±0.1 V  | ≤ 43 mW   | 1.8 V  |
| SMD1015   | IDM/Huiwen   | Acetone                | 1.8 V ±0.1 V  | ≤ 30 mW   | 1.8 V  |
| GM-602B   | Winsen       | H₂S & benzene          | 1.9 V ±0.1 V  | ≤ 40 mW   | 1.8 V† |
| GM-202B   | Winsen       | Smoke/alcohol          | 2.5 V ±0.1 V  | ≤ 50 mW   | 2.5 V  |
| GM-302B   | Winsen       | Ethanol                | 2.5 V ±0.1 V  | ≤ 50 mW   | 2.5 V  |
| GM-502B   | Winsen       | VOC                    | 2.5 V ±0.1 V  | ≤ 50 mW   | 2.5 V  |
| GM-512B   | Winsen       | H₂S/alcohol/acetone    | 2.5 V ±0.1 V  | ≤ 50 mW   | 2.5 V  |
| GMV-2021B | Winsen       | Hydrogen (H₂)          | 2.5 V ±0.1 V  | ≤ 50 mW   | 2.5 V  |

**†** GM-602B is rated 1.9 V ±0.1 V; its operating range is 1.8–2.0 V. Running at 1.8 V is at the lower tolerance bound and delivers ~90% of nominal heater power — conservative and within spec. Sensitivity curves are characterised at 1.9 V; response will differ marginally at 1.8 V but the sensor is not damaged.

### Per-rail power summary

| Rail      | Sensors | Peak current (all on) | Max heater power |
| --------- | ------- | --------------------- | ---------------- |
| 1.8 V     | 9       | ~179 mA               | ≤ 321 mW         |
| 2.5 V     | 5       | ~100 mA               | ≤ 250 mW         |
| **Total** | **14**  |                       | **≤ 571 mW**     |

Datasheets for all proposed sensors are in `hardware/sensors/`.

---

## Heater Power Architecture

**Decision:** Two shared DC-DC buck converters (one per voltage rail) with individual low-side switching per sensor.

> **Update:** per-sensor switching is now implemented with **2× Toshiba TBD62083APG** 8-channel DMOS low-side driver arrays (14 of 16 channels used), driven directly from the RP2040 PWM co-processor, rather than discrete N-channel MOSFETs. This is a passive, low-side topology safe for the purely-resistive heaters and needs no auxiliary gate-drive rail. Full rationale in [pwm_driver.md](pwm_driver.md). The discrete-MOSFET selection criteria below are retained for reference.
>
> **Package update (2026-07-14):** switch to the **SSOP package variant, TBD62083AFNG**, in place of the through-hole PDIP `...APG` above — see [pcb_layout_feedback_2026-07-14.md](pcb_layout_feedback_2026-07-14.md#12-dmos-driver-package-pdip--ssop-tbd62083a).

### Why two voltage rails

The sensor array requires two heater voltages (1.8 V and 2.5 V). Running all sensors from a single 2.5 V rail with PWM to reduce power for 1.8 V sensors does not work: a resistive heater responds to RMS voltage (average power ∝ V²), not average voltage. To achieve 1.8 V equivalent power from a 2.5 V PWM source, the correct duty cycle is:

```
D = (1.8 / 2.5)² = 51.8%
```

This gives a mean voltage of ~1.3 V, not 1.8 V. The "average voltage = rated voltage" shortcut is wrong for resistive loads and would over-drive sensors. Two separate regulated rails eliminate this ambiguity.

### Why buck converters, not LDOs

At ~571 mW total heater load, LDOs from a 3.3 V input would waste ~341 mW as heat on the PCB — physically near temperature-sensitive sensing elements whose resistance varies by factors of 2–3 across the operating temperature range. Buck converters at 85–90% efficiency reduce this waste to ~60–85 mW.

### Architecture

```
Input rail (3.7 V LiPo or 5 V USB)
    ├── Buck A → 1.8 V → [MOSFET_1..9]  → 9× 1.8 V sensors
    └── Buck B → 2.5 V → [MOSFET_10..14] → 5× 2.5 V sensors
```

Shared rails mean residual switching noise is common-mode within each group, making it easier to subtract in post-processing.

### Buck converter parameters

| Rail  | Peak load | Recommended converter rating | Inductor (from 3.7 V, 1 MHz) | Notes                                                   |
| ----- | --------- | ---------------------------- | ---------------------------- | ------------------------------------------------------- |
| 1.8 V | ~179 mA   | 350–500 mA output            | 15–22 µH                     | Target 1.85 V output to compensate post-filter DCR drop |
| 2.5 V | ~100 mA   | 200–300 mA output            | 22–33 µH                     |                                                         |

Both converters require: input voltage range 3.0–5.5 V; switching frequency 400 kHz–2 MHz; MCU-controlled enable pin; fixed output voltage preferred.

**Output filtering:** Add a second-stage LC post-filter between each buck output and the sensor heater rail:

```
Buck output → [L2: 10 µH] → [C2: 47 µF ceramic + 100 µF bulk] → sensor heater rail
```

This provides ~40 dB of additional attenuation at the switching frequency, reducing residual ripple from ~10 mV to sub-millivolt. Corner frequency ≈ 7.3 kHz.

**Capacitor note:** Ceramic capacitor capacitance derates significantly with DC bias. A 22 µF 6.3 V X5R cap may measure only 8–12 µF at 1.8 V DC. Verify effective capacitance at rated output voltage and derate nominal values by 30–50% when sizing.

### MOSFET selection criteria

- **V_DS rating:** ≥ 6 V (covers both rails with margin)
- **R_DS(on):** ≤ 0.5 Ω for Winsen sensors (±0.1 V tolerance); ≤ 0.2 Ω for SMD series (±0.05 V tolerance)
- **Gate threshold V_GS(th):** 1–2 V (logic-level, compatible with 3.3 V MCU)
- **Package:** SOT-23 or equivalent small SMD

### PWM for temperature modulation

When using PWM to set intermediate heater power levels, duty cycle must be based on **power equivalence**, not average voltage:

```
D = (V_target / V_rail)²
```

PWM frequency should exceed 10 kHz to minimise temperature ripple relative to the MEMS thermal time constant (1–50 ms).

### PCB layout guidelines

- Minimise the high-current switching loop (input cap → high-side FET → inductor → output cap)
- Keep the SW node short; route away from sensor measurement signal traces
- Use star ground: connect converter ground and sensor measurement ground at a single point
- Locate both converters away from the sensor array; route clean filtered rails via dedicated power traces
- Keep the two converter sections isolated from each other

---

## Signal Readout Architecture

**Decision:** Per-sensor unity buffer + passive RC anti-aliasing → multichannel ΔΣ ADC, **no MUX**, no active LPF. Full rationale in [sensor_output_architecture.md](sensor_output_architecture.md).

A unity-gain buffer close to each sensor converts its high-impedance output (up to ~100 kΩ in clean air) to ~100 Ω before routing; a single-pole passive RC then feeds one dedicated ΔΣ ADC channel per sensor:

```
Each sensor → unity buffer (RRIO CMOS, <10 pA Ib, SOT-23-8 dual)
           → passive RC (≈10 kΩ + 10 nF, f_c ≈ 1.6 kHz)
           → [dedicated channel on multichannel ΔΣ ADC]
```

The buffer is unity-gain by default but includes unpopulated R_f / R_g pads and a cuttable trace jumper, so each channel can be reconfigured as an inverting/non-inverting amplifier during prototyping without a respin.

> **Part update (2026-07-14):** buffer op-amp switches to **TI TLV9152** in the **SOT-23-THN** package, in place of the OPA2334/TLV2372 SOT-23-8 recommendation below — see [pcb_layout_feedback_2026-07-14.md](pcb_layout_feedback_2026-07-14.md#11-buffer-op-amp-tlv2372--tlv9152-sot-23-thn). Note the footprint change (SOT-23-THN ≠ SOT-23-8).

**Why no MUX (supersedes Hybrid B):** once the active LPF is removed, a MUX saves no op-amps — one buffer per sensor is the floor regardless — while introducing charge-injection and switching noise directly into millivolt signals and imposing per-channel RC settling that penalises scan speed. A multichannel ΔΣ ADC reads every channel with no switching overhead. The earlier Hybrid B design (buffer → 16:1 MUX → single ADC) is archived at [hardware/archive/26F26_sensor_output_mux_decision.md](archive/26F26_sensor_output_mux_decision.md).

No active LPF is needed: the ΔΣ ADC's internal Sinc filter enforces the band limit with no component drift, and the heater-supply LC post-filter suppresses V_H ripple upstream. The passive RC provides ~14–20 dB of analog attenuation at the 10 kHz PWM frequency and protects the ADC input.

### Key parameters

| Parameter                                        | Value                                                          |
| ------------------------------------------------ | -------------------------------------------------------------- |
| Buffer op-amp                                    | RRIO CMOS dual, Ib < 10 pA, Vos < 1 mV (e.g. OPA2334, TLV2372) |
| Anti-aliasing filter                             | ≈10 kΩ + 10 nF passive RC, f_c ≈ 1.6 kHz                       |
| ADC                                              | 2× ADS131M08 (16 ch total, 14 used) — see ADC section          |
| ADC channels required                            | 14 (one per sensor, no MUX)                                    |
| Full scan time (4 temperature steps, Strategy B) | ~345 ms                                                        |

## ADC

**Selected:** 2× [TI ADS131M08](https://www.ti.com/product/ADS131M08) — 8-channel, 24-bit ΔΣ, simultaneous-sampling, built-in PGA (1–128×), SPI, single 3.3 V supply. Two devices give 16 channels (14 used + 2 spare), sharing SCLK/MOSI/MISO with separate /CS lines. Note: the ADS131M08 has no internal oscillator — it requires an external CLKIN (≈8.192 MHz), ideally shared between both devices for synchronised sampling.

~~TI ADS122C14 (4-ch, I²C) — superseded; it assumed a single-channel MUX front end that has since been dropped (see Signal Readout Architecture).~~
~~Analog Devices LTC2499 (24-bit 16-channel ADC) — superseded.~~

Full ADC rationale and part comparison (ADS131M08 vs. ADS1258 vs. MCP3914): [sensor_output_architecture.md](sensor_output_architecture.md).

---

## Controller

**Selected:** dual-MCU architecture, both mounted directly on the PCB, replacing the detachable controller board used in BRIAN 1.x:

- **Host — ESP32-S3-WROOM-1:** high-level logic, BLE/WiFi radios, sensor-data ingestion, ADC readout (SPI), and the I²C bus (BME680, fuel gauge). Native USB — no external USB-UART bridge required.
- **PWM co-processor — RP2040 (QFN-56):** generates all 14 heater PWM signals, offloading high-frequency PWM from the host. Its own native USB and W25Q32 flash are on-board; the host programs/controls it over SWD plus a UART and an SPI link.

The driving constraint remains **PWM channel count**: each of the 14 sensors needs an independent hardware PWM (≥10 kHz) to its low-side driver for temperature modulation. The RP2040 provides **16 independent hardware PWM channels** (8 slices × 2), meeting the requirement with margin (expandable via PIO). This decouples the PWM requirement from the host-MCU choice, freeing the host selection to prioritise radios and USB — hence the ESP32-S3.

> **Note (superseded approach):** an earlier plan used a single *classic* ESP32 (16 LEDC channels) for both host and PWM duty, with a **CP2102N** USB-UART bridge because it lacks native USB. That is replaced by the ESP32-S3 + RP2040 split above: the RP2040 handles PWM, and native USB on both parts removes the CP2102N. External dedicated PWM ICs were also evaluated and rejected (PCA9685 tops out ~1.5 kHz; SX1509 ~7.8 kHz; CY8C9520A only 4 blocks; LED drivers have the wrong output type).

Full decision rationale, MCU/PWM comparison, driver-array (TBD62083APG) selection, and inter-chip interface details: [pwm_driver.md](pwm_driver.md)

---

## References

- https://amu.hal.science/hal-02114915/document
- https://pubmed.ncbi.nlm.nih.gov/30857123/
- https://share.google/srmmD0KdJVKQ4o7NJ
- Detailed heater power design rationale: [heater_power.md](heater_power.md)
- MCU and PWM controller selection: [pwm_driver.md](pwm_driver.md)
