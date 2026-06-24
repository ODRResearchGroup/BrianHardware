# E-Nose Hardware — BRIAN 2.0 Design Reference

## Overview

BRIAN 2.0 is a 14-sensor electronic nose designed for discriminating volatile organic compounds and gas species. The core goals of the redesign are:

- **Expanded sensor array** using a curated mix of Winsen GM-x02B/x12B and IDM/Huiwen SMD10xx MEMS resistive hot-plate sensors
- **Controllable sensor heating** with per-sensor MOSFET switching to enable temperature modulation — cycling each heater through a set-point sequence during a measurement to extract richer discriminating information
- **Improved ADC** with higher resolution and gain for better signal quality
- **Direct ESP32 integration** on the PCB rather than via a daughterboard

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

**Decision:** Two shared DC-DC buck converters (one per voltage rail) with individual N-channel MOSFETs per sensor.

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

**Decision:** Hybrid B — buffer-first MUX with passive RC anti-aliasing, no active LPF. Full rationale in [sensor_output_architecture.md](sensor_output_architecture.md).

A 16:1 analog MUX centralises readout to a single ADC channel. A unity-gain buffer close to each sensor converts its high-impedance output to ~100 Ω before the signal travels to the MUX:

```
Each sensor → unity buffer (RRIO CMOS, <10 pA Ib, SOT-23-8 dual)
           → [low-Z routed trace] → 16:1 MUX → passive RC (1 kΩ + 100 nF) → ADC
```

No active LPF is needed: the ADS122C14's delta-sigma filter provides >100 dB rejection of heater PWM frequencies at 20 SPS, and the heater supply LC post-filter already suppresses V_H ripple to sub-millivolt levels. The passive RC acts only as an anti-aliasing filter (f_c ≈ 1.3 kHz) to protect the ADC during MUX switching.

This replaces the original 28-op-amp fully-distributed design with 14 buffers + 1 MUX + passive RC, saving ~$6.45/board and ~111 mm² of PCB area. The seventh dual op-amp package has a spare channel reserved for an active LPF if prototyping shows it is needed.

The MUX-first variant (no per-sensor buffers) was evaluated and rejected: sensor source impedance of up to 100 kΩ in combination with the board's switching noise environment would produce ~100–200 mV of coupled noise on sensor traces, negating the 24-bit ADC resolution entirely.

### Key parameters

| Parameter                                        | Value                                                               |
| ------------------------------------------------ | ------------------------------------------------------------------- |
| Buffer op-amp                                    | RRIO CMOS dual, Ib < 10 pA, Vos < 1 mV (e.g. OPA2334, TLV2372)      |
| MUX                                              | 16:1 analog, Ron < 200 Ω, leakage < 15 nA (e.g. TMUX16116, ADG1607) |
| Anti-aliasing filter                             | 1 kΩ + 100 nF passive RC, f_c ≈ 1.3 kHz, ~2 ms settling             |
| ADC channels required                            | 1 (vs. 14 in fully-distributed design)                              |
| Full scan time (4 temperature steps, Strategy B) | ~2.8 s at 90 SPS / 4 averages                                       |

## ADC

**Selected:** [TI ADS122C14](https://www.ti.com/product/ADS122C14) — 4 analog inputs, 24-bit, programmable gain up to 128, I²C interface. One device is sufficient with the MUX architecture (single active channel).

~~Analog Devices LTC2499 (24-bit 16-channel ADC) — superseded by ADS122C14 selection.~~

---

## Controller

**Selected:** Original ESP32 (e.g. ESP32-WROOM-32), mounted directly on the PCB, replacing the detachable controller board used in BRIAN 1.x.

The key constraint is **PWM channel count**. Each of the 14 sensors requires an independent hardware PWM signal (>10 kHz) to its MOSFET gate for temperature modulation. The original ESP32's LEDC peripheral provides **16 independent hardware PWM channels** — the only ESP32 variant with enough. All newer variants (S2, S3, C3, C6, H2, P4) have 8 or fewer LEDC channels.

External PWM ICs were evaluated and all rejected: constant-current LED drivers (PCA9955BTW, TLC59711) have the wrong output type; the PCA9685 is voltage-mode but tops out at ~1,526 Hz; the SX1509 reaches ~7.8 kHz (just under the 10 kHz threshold); the CY8C9520A can exceed 10 kHz but only has 4 PWM blocks. No single external IC satisfies all three requirements simultaneously (≥14 channels, ≥10 kHz, voltage-mode outputs).

Because the original ESP32 lacks native USB, a **CP2102N** USB-to-UART bridge is included on the PCB for programming and debug. BRIAN's primary data paths (BLE for the mobile app, WiFi for the web interface) are unaffected.

Full decision rationale, ESP32 family comparison, rejected alternatives, and Arduino LEDC implementation notes: [pwm_driver.md](pwm_driver.md)

---

## References

- https://amu.hal.science/hal-02114915/document
- https://pubmed.ncbi.nlm.nih.gov/30857123/
- https://share.google/srmmD0KdJVKQ4o7NJ
- Detailed heater power design rationale: [heater_power.md](heater_power.md)
- MCU and PWM controller selection: [pwm_driver.md](pwm_driver.md)
