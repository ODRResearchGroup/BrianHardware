> [!NOTE]
> The previous architecture decision (Hybrid B: unity buffer → 16:1 MUX → passive RC → single ADS122C14) is archived at [hardware/archive/26F26_sensor_output_mux_decision.md](archive/26F26_sensor_output_mux_decision.md).

# Sensor Readout Architecture

**Status:** Decided — Unity buffer + passive RC per sensor, multichannel ΔΣ ADC, no MUX
**Applies to:** BRIAN 2.0 14-sensor array
**Relates to:** [V2_design.md](V2_design.md)

---

## Architecture Decision History

| Version | Decision | Rationale |
|---|---|---|
| Initial | Fully distributed: buffer + active LPF + gain per sensor, 2× ADC | Maximum noise isolation per sensor |
| Hybrid B | Buffer per sensor → 16:1 MUX → passive RC → single ADC | Reduce op-amp count and ADC channels; active LPF not needed |
| **Current** | **Buffer + passive RC per sensor → multichannel ΔΣ ADC, no MUX** | **MUX saves nothing without active LPF; introduces noise; ΔΣ ADC preferred over SAR** |

---

## Current Architecture

```
Each sensor:
  Sensor → [2–5 mm trace] → Unity buffer + passive RC filter
                                         ↓
                              [one dedicated channel on multichannel ΔΣ ADC]
```

Each sensor has a unity-gain buffer and passive RC filter placed close to it. All sensor channels connect directly to one or more multichannel Delta-Sigma ADCs — no MUX.

The buffer is laid out for unity gain by default, but includes unpopulated resistor footprints and a cuttable/solderable trace jumper so it can be reconfigured as an inverting or non-inverting amplifier during prototyping without a board respin.

---

## Why Delta-Sigma ADC

For millivolt-level MEMS gas sensor signals, a Delta-Sigma (ΔΣ) ADC is strongly preferred over a SAR ADC. The required signal bandwidth is set by the application mode: equilibrium gas sensing requires only fractions of 1 Hz (sensor T90 = 15–120 s), while transient thermal modulation measurements require ~1–2 kHz to resolve resistance changes during heater steps with 1–50 ms thermal time constants.

### Noise shaping and oversampling

ΔΣ ADCs oversample at megahertz rates and push high-frequency noise out of the signal band, removing it entirely via the internal digital filter. This gives 20–22 effective bits of noise-free resolution (vs. ~14 effective bits from a 16-bit SAR), allowing sub-microvolt resolution of slow gas-concentration changes without heavy external amplification.

### Relaxed anti-aliasing filter requirement

A SAR ADC requires a sharp multi-pole active anti-aliasing filter at its input — the filter must provide a steep rolloff at the Nyquist frequency (typically within the signal band of interest). A ΔΣ ADC oversamples at MHz rates, pushing the Nyquist boundary far above the signal band. A single passive RC is sufficient to protect the input; the ADC's internal Sinc filter enforces the signal bandwidth cutoff in the digital domain. This eliminates the multi-stage active LPF entirely.

### High DC accuracy

ΔΣ architectures are engineered for DC precision: very low offset, minimal thermal drift. This matters for gas sensing, where a drifting ADC baseline could falsely signal a concentration change.

### Feature comparison for MEMS gas sensing

| Feature | ΔΣ ADC | SAR ADC |
|---|---|---|
| Millivolt signal suitability | Excellent — built-in noise shaping | Poor — requires heavy external amplification |
| Internal digital filtering | Yes — sharp, stable, zero component drift | No — external processing required |
| Front-end filter complexity | Very low — single passive RC | High — multi-pole active filter needed |
| Resolution | 24-bit standard (20–22 ENOB) | 12–18 bit (14 ENOB typical) |
| DC accuracy / drift | High | Moderate |
| Power consumption | Moderate–high | Very low |

---

## Why No MUX

### Original MUX motivation

The MUX was introduced in the Hybrid B design to reduce ADC channel count and thus reduce the number of active filter stages. With a single shared ADC channel, only one active LPF (or gain stage) was needed after the MUX rather than one per sensor.

### MUX is no longer justified

With the active LPF removed:

- **No op-amp savings.** Each sensor already requires one op-amp for the unity buffer. Without an active LPF, that is the minimum — one op-amp per sensor regardless. A MUX does not reduce this count further.
- **Noise is introduced.** An analog MUX introduces charge injection, switching glitches, and off-channel leakage that corrupt millivolt-level signals. With no active filtering stage between the MUX and the ADC input, these artefacts feed directly into the converter. At 100 Ω source impedance (buffer output), MUX leakage error is ~15 µV — acceptable — but charge injection transients during channel switching can reach millivolt levels and require lengthy RC settling before each conversion.
- **Scan speed is penalised.** Each MUX channel switch requires RC settling time (~2 ms at the passive RC values) before a valid conversion can begin. With a multichannel ADC, all channels convert continuously or on-demand with no switching overhead.
- **No switching overhead.** With a multichannel ADC, all channels are available without MUX settling delays, allowing high-rate scanning across all sensors during any given heater transient.

---

## Signal Chain Detail

### 1. Unity buffer (gain-configurable)

A CMOS-input op-amp in unity-gain (voltage follower) configuration placed within 2–5 mm of the sensor output. Its purpose is impedance transformation: it presents a high impedance to the sensor and a low impedance (~100 Ω) to the downstream trace and ADC input, preventing load-dependent measurement errors and eliminating high-impedance routing.

**Gain upgrade provision:** The PCB footprint includes unpopulated resistor pads for a feedback resistor (R_f) and gain-setting resistor (R_g), plus a cuttable trace jumper that shorts the feedback path for unity gain by default. To convert to a non-inverting amplifier with gain G = 1 + R_f/R_g: cut the trace jumper and populate R_f and R_g. This allows gain to be added per-channel during prototyping — to compensate for sensors with particularly weak output or to use the full ADC input range — without requiring a board respin.

### 2. Passive RC anti-aliasing filter

A single-pole passive RC immediately after the buffer. Its cutoff is set by two competing constraints:

- **Lower bound — signal bandwidth:** BRIAN 2.0 uses temperature modulation and captures resistance *during* heater transients (not just at equilibrium). The MEMS hot-plate thermal time constant is 1–50 ms; to resolve the transient shape faithfully requires bandwidth to approximately 5× the highest frequency component of the fastest transient, giving a **minimum cutoff of ~1 kHz**.
- **Upper bound — PWM noise rejection:** The heater PWM runs at 10 kHz. A passive RC at 1–2 kHz provides ~14–20 dB of analog attenuation at 10 kHz before the signal reaches the ADC, complementing the ADC's internal Sinc3 digital filter.

**Target: f₋₃dB ≈ 1–2 kHz** (e.g. R = 10 kΩ, C = 10 nF → f₋₃dB ≈ 1.6 kHz).

Note: a cutoff at 16 kHz (as would be implied by a "5 kHz bandwidth" spec) is too high — it provides only ~4 dB attenuation at 10 kHz and is not grounded in the actual signal physics. The equilibrium gas-sensing signal bandwidth is a fraction of 1 Hz (sensor T90 values of 15–120 s from datasheets); the thermal transient bandwidth is the binding constraint at ~1 kHz.

Exact R and C values are confirmed once the ADC input impedance is known, to ensure the source impedance of the RC does not load the ADC input.

### 3. ΔΣ ADC(s)

One or more ΔΣ ADCs digitise all 14 sensor channels. A single 14- or 16-channel ΔΣ ADC would be ideal but may not exist; if not, two or more smaller devices (e.g. 2× 8-channel, or 4× 4-channel) are used in parallel on a shared SPI/I²C bus with individual chip-select lines.

Key requirements per device:

- 24-bit resolution
- Output data rate ≥ 32 kSPS per channel for transient thermal capture (≥ 1 kSPS acceptable for equilibrium-only use)
- Sinc filter passband flat to ≥ 2 kHz
- SPI or I²C compatible with the MCU, supporting multi-device bus sharing
- Single 3.3 V supply

> [!NOTE]
> ADC part candidates are evaluated in the [ADC Part Candidates](#adc-part-candidates) section below. Recommended: 2× ADS131M08.

---


## ADC Part Candidates

Three realistic options are evaluated below. No single ≥14-channel ΔΣ ADC exists at 3.3 V — all options either use multiple devices or require a 5 V supply.

### Option 1 (Recommended): 2× ADS131M08 (Texas Instruments)

| Parameter | Value |
|---|---|
| Channels per device | 8 (simultaneous sampling) |
| Devices needed | 2 (16 channels total, 2 spare) |
| Resolution | 24-bit ΔΣ |
| Max ODR per channel | 32 kSPS |
| Filter | Sinc3 (−3 dB at ~8.7 kHz at 32 kSPS) |
| Supply | 1.8 V – 3.6 V (3.3 V compatible) |
| Interface | SPI — both devices share SCLK/MOSI/MISO, separate /CS |
| Package | WQFN-48 |
| Approx. price | ~$8–12 each (LCSC/Mouser, small qty) |

**Why recommended:** The ADS131M08 includes a built-in PGA (gains 1–128), which directly amplifies millivolt-level sensor signals before digitisation without requiring external gain stages. This makes the gain-upgrade provision on the unity buffer (trace jumper + resistor pads) a fallback rather than a necessity. Combined with 3.3 V single-supply operation — no additional power rail required — 32 kSPS per channel, and strong TI application note support, this is the most self-contained solution for the signal chain. At 32 kSPS the Sinc3 filter is ~−0.1 dB at 1 kHz and −3 dB at 8.7 kHz, flat across the full thermal transient bandwidth.

**ODR setting for thermal transient capture:** Use 32 kSPS. This gives one sample every ~31 µs, resolving thermal transients with time constants ≥ 1 ms with ample fidelity. The Sinc3 −3 dB point at 8.7 kHz is well above the 1–2 kHz signal bandwidth, so the ADC filter does not limit the measurement. For equilibrium-only measurements (no transient capture), 1–4 kSPS is sufficient and reduces noise further via averaging.

---

### Option 2: 1× ADS1258 (Texas Instruments)

| Parameter | Value |
|---|---|
| Channels | 16 single-ended (covers all 14 + 2 spare on one chip) |
| Resolution | 24-bit ΔΣ |
| Max ODR (all 16 ch) | ~7.8 kSPS per channel (125 kSPS aggregate ÷ 16) |
| Filter | Sinc3 (internal) |
| Supply | **AVDD 4.75–5.25 V required; DVDD 1.8–3.6 V** |
| Interface | SPI |
| Package | TQFP-32 |
| Approx. price | ~$12–18 |

**Advantage:** Single chip covers all 14 channels, simplifying routing and firmware.

**Drawbacks:** Requires a 5 V analog supply rail — if not already present, adds a boost converter or LDO to the BOM. No built-in PGA, so external gain stages or the buffer's gain-upgrade provision would be needed for millivolt-level signals. Internally multiplexed (not simultaneous), though at ~7.8 kSPS per channel the inter-channel delay of ~128 µs is negligible for gas sensing timescales.

---

### Option 3 (Budget): 2× MCP3914 (Microchip)

| Parameter | Value |
|---|---|
| Channels per device | 8 |
| Devices needed | 2 (16 channels total, 2 spare) |
| Resolution | 24-bit ΔΣ |
| Max ODR per channel | 125 kSPS |
| Filter | Sinc3 |
| Supply | 2.7 V – 3.6 V (3.3 V compatible) |
| Interface | SPI |
| Package | TSSOP-28 |
| Approx. price | ~$4–7 each |

**Advantage:** Significantly cheaper than the ADS131M08. 3.3 V compatible, SPI.

**Drawback:** Less commonly used in precision measurement applications; fewer reference designs and application notes for gas sensing. Not simultaneous-sampling by default — channels are internally multiplexed within each device. Worth considering if BOM cost is the primary constraint.

---

### Summary

| | 2× ADS131M08 | 1× ADS1258 | 2× MCP3914 |
|---|---|---|---|
| Chips | 2 | **1** | 2 |
| 3.3 V supply | ✅ | ❌ (needs 5 V AVDD) | ✅ |
| Simultaneous sampling | ✅ (not critical with individual heater control) | ❌ | ❌ |
| Per-channel ODR | ✅ 32 kSPS | ✅ ~7.8 kSPS/ch (sufficient for 1–2 kHz BW) | ✅ 125 kSPS |
| BOM cost (2 devices) | ~$20–24 | ~$12–18 | **~$8–14** |
| Built-in PGA | ✅ (1–128×) | ❌ | ❌ |
| Support / app notes | Excellent | Good | Moderate |
| **Verdict** | **Recommended** | If 5 V available | Budget fallback |

## Why Buffer-First Is Still Required

The MEMS sensors are variable resistors. In a voltage-divider readout, the source impedance at the sensor output node is:

```
Z_source = R_sensor ∥ R_load
```

R_sensor varies from ~1 kΩ (high gas concentration) to hundreds of kΩ or low MΩ (clean air). With a 10–100 kΩ load resistor, Z_source is in the range **1–100 kΩ** in clean air conditions.

### Capacitive noise pickup on undriven traces

PCB traces couple noise capacitively from nearby aggressors. On BRIAN 2.0:
- Two buck converters switching at ~1 MHz
- 14 PWM-driven MOSFET gates (heater modulation)
- SPI lines to the ADC

With coupling capacitance C_couple of 0.1 pF and a 1 MHz, 3.3 V aggressor:

```
Z_couple at 1 MHz = 1 / (2π × 1 MHz × 0.1 pF) ≈ 1.6 MΩ
```

| Z_source | Coupled noise voltage | Effect |
|---|---|---|
| 100 kΩ | 3.3 V × 100 kΩ / 1.7 MΩ ≈ **194 mV** | Destroys 24-bit resolution |
| 10 kΩ | 3.3 V × 10 kΩ / 1.61 MΩ ≈ **20 mV** | ~14 bits of noise on a 3.3 V scale |
| **100 Ω (buffer output)** | 3.3 V × 100 Ω / 1.6 MΩ ≈ **0.2 mV** | Acceptable; within ADC noise floor |

Even careful PCB layout cannot reduce C_couple below ~0.01 pF between adjacent switching traces and a sensor routing trace. The only reliable fix is to reduce Z_source at the sensor — i.e., a local unity buffer.

---

## Buffer Op-Amp Selection Criteria

- **Input bias current (Ib):** < 10 pA (CMOS input required; at 100 kΩ source, 10 nA Ib causes 1 mV offset)
- **Input offset voltage:** < 1 mV
- **Rail-to-rail input and output (RRIO):** required for full dynamic range on 3.3 V supply
- **Supply current:** < 500 µA per op-amp (14 buffers always powered)
- **Package:** SOT-23-8 dual preferred for density

Suitable parts: OPA2334 (25 µV offset, 1 pA Ib, dual SOT-23-8), TLV2372 (0.5 mV offset, 1 pA Ib, dual SOT-23-8).

---

## Why No Active LPF

No active low-pass filter is needed between the sensor and the ADC for three reasons:

1. **ΔΣ internal filter.** The ADC's Sinc digital filter enforces the signal bandwidth limit (−3 dB at 8.7 kHz at 32 kSPS) with better roll-off stability than any analog filter — no component drift, no tolerances to match across channels.

2. **Heater supply noise is handled upstream.** The LC post-filter on each buck converter output (see `heater_power.md`) reduces V_H ripple to sub-millivolt levels before it reaches the sensor heater pins.

3. **Synchronised sampling (equilibrium mode).** When capturing equilibrium readings rather than transients, the MCU can trigger ADC conversions after PWM switching transients have decayed, further reducing PWM coupling.

The passive RC (f₋₃dB ≈ 1–2 kHz) serves dual purpose: it attenuates 10 kHz PWM noise by ~14–20 dB before the ADC, and protects the ADC input from large transients. It is not a substitute for the ADC's digital filter but provides meaningful analog pre-filtering.

---

## Measurement Timing

With no MUX switching overhead, all channels can be read in parallel (depending on ADC architecture) or scanned rapidly in sequence with no mandatory settling delay between channels.

### Full scan time (no temperature modulation)

Indicative values; actual throughput depends on the chosen ADC's per-channel conversion rate.

| Mode | Time per sensor | 14 sensors |
|---|---|---|
| 1 conversion @ 32 kSPS (transient mode) | ~0.031 ms | **~0.43 ms** |
| 1 conversion @ 1 kSPS (equilibrium mode) | ~1 ms | **~14 ms** |
| 1 conversion @ 100 SPS | ~10 ms | **~140 ms** |

### Full scan time (with temperature modulation, Strategy B)

With all heaters set to the same temperature level simultaneously, then all 14 channels read, then move to the next level:

| Step | Duration |
|---|---|
| Set all heaters to temperature set-point K | <1 µs |
| Thermal settling (5τ, MEMS hot-plate) | ~75 ms |
| Read all 14 channels (4 averages @ high ODR) | ~11 ms |
| **Per temperature level** | **~86 ms** |
| **T=4 temperature levels** | **~345 ms** |

This is substantially faster than the Hybrid B MUX design (~691 ms per temperature level) because there is no per-channel MUX switch and RC settling overhead.

### Where time is actually spent

Chemical response time of resistive gas sensors (seconds to minutes for equilibrium) dominates total measurement duration by a large margin. ADC throughput is not the bottleneck.

---

## Open Questions

- **ADC part selection and count:** Three candidates are evaluated above (ADS131M08, ADS1258, MCP3914). Confirm final choice and whether the board includes or can add a 5 V rail (required for ADS1258).
- **Output Data Rate:** 32 kSPS recommended for transient thermal capture (resolves ≥ 1 ms transients); lower ODR (1–4 kSPS) acceptable for equilibrium-only measurements. Confirm based on measurement protocol.
- **Power budget:** ΔΣ ADCs consume more quiescent current than SAR. Confirm acceptable given BRIAN 2.0 power supply design.
- **RC component values:** Calculate R and C once ADC input impedance is confirmed.
- **Sampling strategy:** With individual heater control, simultaneous cross-channel sampling is not critical. Per-channel ODR and aggregate scan rate across all 14 channels during a transient are the relevant figures. Confirm scan rate is sufficient to track the fastest heater transient across all active channels.
