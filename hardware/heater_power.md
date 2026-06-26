# Heater Power Supply Architecture: Design Decision

**Status:** Decided  
**Applies to:** BRIAN 2.0 14-sensor array  
**Sensors:** Winsen GM-x02B/GM-x12B series (Zhengzhou Winsen Electronics) and SMD10xx series (Suzhou Huiwen Nanotechnology)

---

## Decision Summary

The BRIAN 2.0 heater power supply uses **two shared DC-DC buck converters** (one per voltage rail) rather than per-sensor linear regulators (LDOs), with **individual N-channel MOSFETs** per sensor for switching and temperature modulation. This architecture is chosen because:

- The sensor array spans **two distinct heater voltages** — 1.8 V and 2.5 V — that cannot safely share a single rail without over- or under-driving some sensors.
- At 14 sensors with a combined heater load of ~570 mW, LDO inefficiency would waste 200–350 mW as heat on the PCB, physically near temperature-sensitive sensing elements.
- Temperature modulation (cycling each heater through a sequence of set-point temperatures during a measurement) is a core part of the measurement protocol, meaning per-sensor MOSFET switching is required regardless, and switching supplies fit naturally into this architecture.
- Shared buck converters per rail keep noise common-mode across sensors on the same rail, which is easier to handle in post-processing.

### Sensors removed from earlier design revisions

Two sensors were removed from an earlier 16-sensor array design to eliminate the need for 2.0 V and 3.0 V rails:

- **GM-402B** (combustible gas, 3.0 V, 70 mW) — removed; functionally redundant with SMD1008 (CH₄) and SMD1011 (C₃H₈), both on the 1.8 V rail. The 3.0 V requirement and high heater power (up to 80 mW) would have required a dedicated buck-boost converter.
- **GM-802B** (NH₃, 2.0 V) — removed; functionally redundant with SMD1002 (NH₃) on the 1.8 V rail.

---

## Sensor Inventory and Rail Assignments

All 14 sensors, their heater voltages, maximum heater power, and assigned rails are listed below.

| Sensor | Manufacturer | Target gas | V_H | P_H (max) | Rail |
|--------|-------------|-----------|-----|-----------|------|
| GM-102B | Winsen | NO₂ | 1.8 V ±0.1 V | ≤ 40 mW | 1.8 V |
| SMD1001 | IDM/Huiwen | Formaldehyde | 1.8 V ±0.05 V | ≤ 36 mW | 1.8 V |
| SMD1002 | IDM/Huiwen | Ammonia (NH₃) | 1.8 V ±0.05 V | ≤ 36 mW | 1.8 V |
| SMD1007 | IDM/Huiwen | Hydrogen sulfide (H₂S) | 1.8 V ±0.05 V | ≤ 36 mW | 1.8 V |
| SMD1008 | IDM/Huiwen | Methane (CH₄) | 1.8 V ±0.05 V | ≤ 30 mW | 1.8 V |
| SMD1011 | IDM/Huiwen | Propane (C₃H₈) | 1.8 V ±0.05 V | ≤ 30 mW | 1.8 V |
| SMD1013B | IDM/Huiwen | TVOC | 1.8 V ±0.1 V | ≤ 43 mW | 1.8 V |
| SMD1015 | IDM/Huiwen | Acetone | 1.8 V ±0.1 V | ≤ 30 mW | 1.8 V |
| GM-602B | Winsen | H₂S & benzene | 1.9 V ±0.1 V | ≤ 40 mW | 1.8 V† |
| GM-202B | Winsen | Smoke/alcohol | 2.5 V ±0.1 V | ≤ 50 mW | 2.5 V |
| GM-302B | Winsen | Ethanol | 2.5 V ±0.1 V | ≤ 50 mW | 2.5 V |
| GM-502B | Winsen | VOC | 2.5 V ±0.1 V | ≤ 50 mW | 2.5 V |
| GM-512B | Winsen | H₂S/alcohol/acetone | 2.5 V ±0.1 V | ≤ 50 mW | 2.5 V |
| GMV-2021B | Winsen | Hydrogen (H₂) | 2.5 V ±0.1 V | ≤ 50 mW | 2.5 V |

**†** The GM-602B is rated 1.9 V ±0.1 V, giving an operating range of 1.8 V to 2.0 V. Running it at 1.8 V places it at the lower bound of its specified tolerance. Heater power will be slightly less than at the 1.9 V nominal (by a factor of (1.8/1.9)² ≈ 90%), which is conservative and within the ≤ 40 mW limit. Sensitivity curves in the GM-602B datasheet are characterised at 1.9 V; response function will be marginally different at 1.8 V but the sensor is not damaged or out-of-spec. This grouping avoids the need for a dedicated third rail.

### Per-rail power summary

| Rail | Sensors | Peak current (all on) | Max heater power |
|------|---------|----------------------|-----------------|
| 1.8 V | 9 | ~179 mA | ≤ 321 mW |
| 2.5 V | 5 | ~100 mA | ≤ 250 mW |
| **Total** | **14** | | **≤ 571 mW** |

Peak current and power figures are calculated from the rated maximum heater power at the rail voltage (I = P_max / V_rail). Actual in-use power will be lower, as not all sensors will be active simultaneously during temperature modulation sequences, and most sensors draw less than their rated maximum.

---

## Why Two Voltage Rails Are Required

All sensors in this array are MEMS resistive hot-plate sensors. Their heater elements are calibrated at a specific voltage which sets the hot-plate operating temperature, and therefore the sensitivity profile of the sensing material. Running a sensor at the wrong heater voltage shifts its operating temperature, degrading both sensitivity and cross-selectivity characteristics.

### Why not run all sensors from a single rail with PWM?

A common simplification is to power all sensors from the higher 2.5 V rail and use PWM with a lower duty cycle to reduce the effective drive for the 1.8 V sensors. This does not work correctly for resistive heaters, for the following reason.

A resistive heater responds to **average power**, not average voltage. Average power is proportional to V²:

```
P = V_rms² / R
```

With PWM at duty cycle D from a 2.5 V source:

```
V_avg = 2.5 × D
V_rms = 2.5 × √D
P_avg = (2.5)² × D / R
```

To set `V_avg = 1.8 V`, you need `D = 1.8 / 2.5 = 72%`. But this gives:

```
V_rms = 2.5 × √0.72 = 2.12 V
P_avg ∝ (2.12)² = 4.49   (vs 1.8² = 3.24 for true 1.8 V DC)
```

The heater would receive **38% more power** than intended, running significantly hotter than specified. To match the power of a 1.8 V DC supply, the duty cycle must be set to:

```
D = (1.8 / 2.5)² = 51.8%
```

— which gives a mean voltage of only ~1.3 V, not 1.8 V. The "average voltage = rated voltage" shortcut is incorrect for resistive loads.

Additionally, sensors in both groups prohibit exceeding 120 mW heater dissipation. With heater resistance tolerance of 60–100 Ω and 2.5 V applied during PWM ON-phases, instantaneous power can approach 104 mW — uncomfortably close to the limit with no margin for component variation.

Two separate regulated voltage rails eliminate this ambiguity and ensure each sensor operates within its characterised, calibrated conditions.

---

## Why Buck Converters at This Scale

### The efficiency argument is significant across ~570 mW of heater load

With a total heater load of ~571 mW, LDO losses from a 3.3 V input rail would be substantial:

| Rail | LDO efficiency (from 3.3 V) | Wasted power |
|------|---------------------------|--------------|
| 1.8 V (321 mW load) | 55% | ~262 mW wasted |
| 2.5 V (250 mW load) | 76% | ~79 mW wasted |

Total LDO waste would be ~341 mW — more than half the useful heater power, dissipated as heat on the PCB. Buck converters operating at 85–90% efficiency reduce total waste to roughly **60–85 mW**, a saving of ~260 mW.

### Thermal contamination of measurements

The wasted regulator power does not simply reduce battery runtime — it heats the PCB. All sensors in this array show strong temperature dependence: the Winsen GM-series datasheets show Rs/Rs₀ varying by factors of 2–3 across -20 °C to +50 °C. Heat dissipated by voltage regulators near the sensor array introduces a temperature offset and drift into all measurements. Buck converters, being far more efficient, dissipate far less heat locally.

### Shared rails, not per-sensor converters

The practical implementation is one buck converter per voltage rail, not one per sensor:

```
Input rail (e.g. 3.7 V LiPo or 5 V USB)
    ├── Buck A → 1.8 V ──→ [CH_1]  → GM-102B heater
    │                      [CH_2]  → SMD1001 heater
    │                      [CH_3]  → SMD1002 heater
    │                      [CH_4]  → SMD1007 heater
    │                      [CH_5]  → SMD1008 heater
    │                      [CH_6]  → SMD1011 heater
    │                      [CH_7]  → SMD1013B heater
    │                      [CH_8]  → SMD1015 heater
    │                      [CH_9]  → GM-602B heater  (1.9 V ±0.1 V; at lower tolerance limit)
    │
    └── Buck B → 2.5 V ──→ [CH_10] → GM-202B heater
                           [CH_11] → GM-302B heater
                           [CH_12] → GM-502B heater
                           [CH_13] → GM-512B heater
                           [CH_14] → GMV-2021B heater
```

Each `[CH_N]` is a low-side switch channel. For the driver IC and PWM co-processor implementation, see [pwm_driver.md](pwm_driver.md).

This results in two power conversion stages and two inductors.

### Noise becomes common-mode within each rail

Residual switching ripple from a shared buck rail appears identically on all sensors fed by that rail simultaneously — it is common-mode within the group. Common-mode noise is easier to reject in software (it can be subtracted as a baseline) and less problematic than independent noise sources per sensor.

---

## Per-Sensor Heater Switching and PWM Control

Temperature modulation — cycling each heater through a sequence of set-point temperatures during a single measurement — is a standard technique for improving selectivity in resistive gas sensor arrays. Different gas species produce distinct resistance-vs-temperature profiles, allowing a single sensor to contribute more discriminating information per measurement cycle.

This requires individual switching of each heater. The driver implementation — including the choice of low-side driver IC and PWM co-processor — is specified in [pwm_driver.md](pwm_driver.md). In summary: each sensor heater is switched via a dedicated channel of a **Toshiba TBD62083APG** 8-channel DMOS FET array, driven by a **Raspberry Pi RP2040** co-processor that generates independent 10 kHz PWM signals for all 14 channels without burdening the main ESP32.

### PWM for intermediate temperature set-points

The MEMS hot plate thermal time constant is typically in the 1–50 ms range. Temperature modulation sequences are typically stepped (discrete levels held for seconds), not rapidly dithered.

Where PWM is used to set intermediate power levels within a modulation step, the duty cycle must be set based on **power equivalence**, not average voltage:

```
D = (V_target / V_rail)²
```

For example, to achieve the equivalent of a 2.0 V heater drive from the 2.5 V rail:

```
D = (2.0 / 2.5)² = 0.64   (64%, not 80%)
```

The RP2040 runs PWM at 10 kHz with 13,300-step resolution (see [pwm_driver.md](pwm_driver.md)), which comfortably exceeds the minimum frequency needed to minimise temperature ripple relative to the MEMS thermal time constant.

---

## Buck Converter Design Considerations

### Converter selection — per rail

| Rail | Load current (peak) | Recommended output rating | Notes |
|------|-------------------|--------------------------|-------|
| 1.8 V | ~179 mA | 350–500 mA | Largest rail; 9 sensors including GM-602B at lower tolerance limit |
| 2.5 V | ~100 mA | 200–300 mA | 5 sensors |

Both converters should have:
- **Input voltage range** covering the full supply range (e.g. 3.0–5.5 V for LiPo operation)
- **Switching frequency** 400 kHz–2 MHz (higher allows smaller passives)
- **Enable pin** for MCU-controlled shutdown between measurement cycles
- **Fixed output voltage** preferred where available, to reduce component count

### Inductor selection

The inductor value for each buck converter is:

```
L = (V_in - V_out) × D / (f_sw × ΔI_L)
```

Where D = V_out / V_in, and ΔI_L is the target peak-to-peak ripple current (typically 20–40% of I_out).

**Example — 1.8 V rail from 3.7 V (LiPo) at 1 MHz, 30% ripple on 179 mA (ΔI_L = 54 mA):**

```
D = 1.8 / 3.7 = 0.486
L = (3.7 - 1.8) × 0.486 / (1,000,000 × 0.054) ≈ 17 µH
```

A standard 15–22 µH shielded power inductor is suitable.

**Example — 2.5 V rail from 3.7 V at 1 MHz, 30% ripple on 100 mA (ΔI_L = 30 mA):**

```
D = 2.5 / 3.7 = 0.676
L = (3.7 - 2.5) × 0.676 / (1,000,000 × 0.030) ≈ 27 µH
```

For each inductor, verify:
- **Saturation current (I_sat)** ≥ I_out + ΔI_L/2, with ≥ 50% margin
- **DCR** ≤ 0.5 Ω to limit conduction losses
- **Shielded construction** to reduce EMI near the sensor array

### Output capacitor selection

Target output ripple ΔV_out ≤ 10 mV. Ripple is:

```
ΔV_out ≈ ΔI_L × ESR  +  ΔI_L / (8 × f_sw × C_out)
```

Use X5R or X7R ceramic capacitors for low ESR (1–5 mΩ). Typical values:

- **1.8 V rail**: 2× 22 µF (0805 or 1206) in parallel + 100 µF polymer bulk
- **2.5 V rail**: 2× 22 µF ceramic + 100 µF bulk

**Important**: Ceramic capacitor capacitance derates significantly with DC bias. A 22 µF 6.3 V X5R capacitor may measure only 8–12 µF at 1.8 V DC. Always verify effective capacitance at rated output voltage using the manufacturer's derating curve, and derate the nominal value by at least 30–50% when sizing.

### Input capacitor selection

Each switching converter draws pulsed current from the input rail. Place:
- 10–22 µF ceramic capacitor as close as possible to each converter's VIN pin
- 100 µF bulk capacitor at the input rail entry point
- 1–4.7 Ω series resistor or ferrite bead between the main supply rail and the converter input to attenuate high-frequency noise propagation

### Additional LC post-filter

For sensor applications where supply noise is critical, a second-stage LC low-pass filter between each buck output and the sensor heater rail is strongly recommended:

```
Buck output → [L2: 10 µH] → [C2: 47 µF ceramic + 100 µF bulk] → sensor heater rail
```

This provides an additional ~40 dB of attenuation at the switching frequency, reducing residual ripple from ~10 mV to the sub-millivolt range.

The corner frequency of this filter:

```
f_corner = 1 / (2π × √(L2 × C2))
         = 1 / (2π × √(10×10⁻⁶ × 47×10⁻⁶))
         ≈ 7.3 kHz
```

This is comfortably below a 500 kHz–1 MHz switching frequency. The inductor's DCR causes a small DC voltage drop; at 179 mA load and 0.3 Ω DCR, the drop is ~54 mV on the 1.8 V rail. Account for this in the buck output set-point by targeting ~1.85 V at the converter output.

### PCB layout guidelines

Buck converter layout is critical for both efficiency and noise performance:

- **Minimise the high-current switching loop**: The path from input capacitor → high-side MOSFET → inductor → output capacitor → back to input capacitor should be as short and wide as possible, with a solid ground return directly beneath.
- **Isolate the switching node**: The SW node is a high dV/dt EMI source. Keep traces short and route them away from sensitive measurement signal traces and the sensor array.
- **Separate ground planes**: Use a solid ground plane under the converter section. Connect the converter ground and the sensor measurement ground at a single point (star ground) to prevent switching return currents from flowing through the measurement ground path.
- **Locate both converters away from the sensor array**: Route the clean, filtered output voltages to the sensor section via dedicated power traces, keeping the noisy switching circuitry physically separated.
- **Place all input and output capacitors as close as possible** to their respective converter pins to minimise parasitic inductance in bypass paths.
- **Keep the two converter sections isolated from each other** to prevent inter-rail switching noise coupling.
