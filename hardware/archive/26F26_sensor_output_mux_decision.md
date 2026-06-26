> [!IMPORTANT]
> This archived decision has been superseded by [hardware/sensor_output_architecture.md](../sensor_output_architecture.md).

# Sensor Readout Architecture: MUX Design Decision

**Status:** Decided — Hybrid B (buffer-first, passive RC anti-aliasing, no active LPF)  
**Applies to:** BRIAN 2.0 14-sensor array  
**Relates to:** [V2_design.md](../V2_design.md)

---

## Proposal Under Evaluation

A 16:1 analog MUX placed directly at the sensor outputs, routing all 14 sensor signals to a single shared buffer/gain amplifier and one ADC channel. This would reduce the op-amp count from 28 (2 per sensor in a buffer-gain configuration) to 2 total.

---

## Decision Summary

**Do not place the MUX directly at the sensor outputs.** Use **Hybrid B**: a unity-gain buffer close to each sensor, a 16:1 MUX downstream of the buffers, a passive RC anti-aliasing filter, and one ADC channel. No active LPF or gain stage is required.

This preserves measurement quality while still capturing most of the cost, footprint, and ADC-channel savings offered by the MUX proposal.

---

## Why MUX-First Fails: Impedance Analysis

The MEMS sensors are variable resistors. In a voltage-divider readout, the source impedance at the output node is:

```
Z_source = R_sensor ∥ R_load
```

R_sensor varies from ~1 kΩ (high gas concentration) to hundreds of kΩ or low MΩ (clean air, depending on sensor type). With a practical load resistor of 10–100 kΩ, Z_source is in the range **1–100 kΩ**, dominated by R_load in clean air conditions.

### Capacitive noise pickup

PCB traces couple noise capacitively from nearby aggressor sources. On this board, the dominant aggressors are:
- Two buck converters switching at ~1 MHz
- 14 PWM-driven MOSFET gates (temperature modulation signals)
- I²C/SPI lines to the ADC

With coupling capacitance C_couple of just 0.1 pF and a 1 MHz, 3.3 V aggressor:

```
Z_couple at 1 MHz = 1 / (2π × 1 MHz × 0.1 pF) ≈ 1.6 MΩ
```

| Z_source | Coupled noise voltage | Effect |
|----------|-----------------------|--------|
| 100 kΩ | 3.3 V × 100 kΩ / 1.7 MΩ ≈ **194 mV** | Destroys 24-bit resolution |
| 10 kΩ | 3.3 V × 10 kΩ / 1.61 MΩ ≈ **20 mV** | ~14 bits of noise on a 3.3 V scale |
| **100 Ω (buffer output)** | 3.3 V × 100 Ω / 1.6 MΩ ≈ **0.2 mV** | Acceptable; well within ADC LSB |

Even conservative PCB layout (shielded traces, guard rings) cannot reduce C_couple below ~0.01 pF between nearby switching traces and a 50 mm sensor routing trace. The only reliable fix is to reduce Z_source at the point where the long trace begins — i.e., a local buffer.

### MUX off-channel leakage error

A precision 16:1 analog MUX (e.g. TMUX16116) has ~5–15 nA of leakage per off channel. With 15 off channels active simultaneously:

```
I_leak_total ≈ 15 × 10 nA = 150 nA
V_error = I_leak × Z_source
```

| Z_source | Leakage error |
|----------|---------------|
| 100 kΩ | **15 mV** |
| 10 kΩ | 1.5 mV |
| 100 Ω (buffer output) | 15 µV (negligible) |

With Z_source in the high-kΩ range at low gas concentration, leakage error alone is unacceptable for a 24-bit system.

---

## Recommended Architecture: Hybrid B (Buffer-First, Passive RC)

```
Each sensor:
  Sensor → [2–5 mm trace] → Unity buffer (RRIO CMOS, <10 pA Ib)
                                    ↓
                             [longer routed trace, ~100 Ω source Z]
                                    ↓
                            16:1 Analog MUX
                                    ↓
                         Passive RC filter (1 kΩ + 100 nF)
                                    ↓
                         Single ADC channel (ADS122C14)
```

The unity buffer converts the high sensor source impedance to ~100 Ω before any long trace routing. The MUX, passive RC, and ADC are centralised. The ADS122C14's internal programmable gain (up to 128×) handles any gain requirement per measurement.

### Why no active LPF

The original fully-distributed design included an active low-pass filter per sensor as a precautionary measure against heater and supply noise. This is not needed in BRIAN 2.0 for three reasons:

1. **The heater supply noise is already handled.** The LC post-filter on each buck converter output (see `heater_power.md`) reduces V_H ripple to sub-millivolt levels before it reaches the sensor heater pins.

2. **The ADS122C14's delta-sigma filter provides inherent noise rejection.** At 20 SPS, each conversion integrates over 50 ms — averaging across 500 cycles of a 10 kHz heater PWM signal. The sinc⁴ filter provides >100 dB rejection of periodic noise well above the output data rate. This eliminates the need for an analog LPF to reject heater switching frequencies.

3. **Synchronised sampling avoids switching transients.** When using PWM to set intermediate heater temperatures, the MCU triggers the ADC conversion after the first few microseconds of a PWM ON-phase, once the switching transient has decayed. Only steady-state readings are captured.

The passive RC (1 kΩ + 100 nF, f_c ≈ 1.3 kHz) serves purely as an **anti-aliasing filter** to prevent large transients from overdriving the ADC input during MUX switching. It is not a noise filter.

**If prototyping reveals residual heater-coupling noise** that the delta-sigma filter and synchronised sampling do not adequately suppress, an active LPF stage can be inserted between the MUX output and the ADC without changing the per-sensor buffer or MUX. The spare op-amp channel available in the seventh dual package is reserved for this purpose if needed.

### Buffer op-amp selection criteria

- **Input bias current (Ib):** < 10 pA (CMOS input required; at 100 kΩ source, even 10 nA Ib causes 1 mV offset)
- **Input offset voltage:** < 1 mV (limits DC accuracy)
- **Rail-to-rail input and output (RRIO):** required for full dynamic range on 3.3 V supply
- **Supply current:** < 500 µA per op-amp (14 buffers always powered)
- **Package:** SOT-23-5 (single) or SOT-23-8 (dual); dual preferred for density

Suitable parts: OPA2334 (25 µV offset, 1 pA Ib, dual SOT-23-8), TLV2372 (0.5 mV offset, 1 pA Ib, dual SOT-23-8).

### MUX selection criteria

- **On-resistance Ron:** < 200 Ω (causes negligible voltage divider error into op-amp's GΩ input)
- **Off-channel leakage:** < 15 nA (at 100 Ω source, effect is <1.5 µV)
- **Charge injection:** < 20 pC
- **Supply range:** compatible with 3.3 V single supply
- **Channels:** 16 (covers 14 sensors with 2 spare)

Suitable parts: TMUX16116 (16:1, TSSOP-20, Ron ~75 Ω, leakage 5 nA), ADG1607 (16:1, TSSOP-24, Ron ~180 Ω, leakage 2 nA).

---

## Cost Comparison

Prices are LCSC/JLCPCB estimates at 10-unit R&D quantities. Production pricing will be lower.

| Architecture | Op-amps | MUX | ADC | **BOM subtotal** |
|---|---|---|---|---|
| Fully distributed (original) | 14× dual, e.g. TLV2372 @ $0.35 = **$4.90** | — | 2× ADS122C14 @ $6.50 = **$13.00** | **~$17.90** |
| MUX-first (2 op-amps only) | 1× dual = **$0.35** | 1× TMUX16116 @ $2.50 = **$2.50** | 1× ADS122C14 = **$6.50** | **~$9.35** |
| **Hybrid (recommended)** | 7× dual = **$2.45** | 1× TMUX16116 = **$2.50** | 1× ADS122C14 = **$6.50** | **~$11.45** |

The hybrid approach saves **~$6.45 per board** vs. fully distributed. The dominant saving is the ADC ($6.50), not the op-amps ($2.45 saved). Compared to MUX-first, the hybrid costs **~$2.10 more** — the price of seven extra dual op-amp packages — in exchange for measurement quality that is otherwise unachievable at 24-bit resolution.

---

## PCB Footprint Comparison

Package areas below are courtyard-inclusive (component body + recommended clearance).

| Component | Package | Area |
|---|---|---|
| Dual op-amp (SOT-23-8) | SOT-23-8 | ~17 mm² |
| 16:1 MUX (TMUX16116) | TSSOP-20 | ~33 mm² |
| ADS122C14 ADC | WQFN-16 3×3 mm | ~25 mm² |

| Architecture | Op-amp area | MUX area | ADC area | **Total** |
|---|---|---|---|---|
| Fully distributed | 14 × 17 = **238 mm²** | — | 2 × 25 = **50 mm²** | **~288 mm²** |
| MUX-first | 1 × 17 = **17 mm²** | 33 mm² | 25 mm² | **~75 mm²** |
| **Hybrid** | 7 × 17 = **119 mm²** | 33 mm² | 25 mm² | **~177 mm²** |

The hybrid saves **~111 mm²** (~38%) vs. fully distributed. The op-amp area is distributed (one dual package per two adjacent sensors); the MUX and ADC are centralised.

Note: the 7 dual op-amp packages must be placed close to their respective sensors (within ~5 mm), so their area is spread across the sensor section of the board, not clustered. The MUX and ADC can be placed anywhere convenient in the signal chain.

---

## Measurement Timing

### Delay budget per MUX channel switch (hybrid architecture)

| Step | Duration | Notes |
|------|----------|-------|
| MUX switch command (SPI/digital) | < 1 µs | Negligible |
| MUX switching time (Ron settling) | < 1 µs | TMUX16116 ton ~200 ns |
| RC filter settling at ADC input | **~2 ms** | R_total (~1.2 kΩ) × C (100 nF) → τ ≈ 120 µs; 17τ for 24-bit accuracy ≈ 2 ms |
| ADC conversion (ADS122C14, 90 SPS normal mode) | **11 ms** | Per single conversion |
| ADC conversion (20 SPS, maximum noise rejection) | **50 ms** | Per single conversion |

The RC settling time (2 ms) is small but not negligible — it adds ~28 ms overhead across 14 channels per full scan. ADC conversion still dominates.

### Full scan time (no temperature modulation)

| ADC setting | Time per sensor | 14 sensors |
|---|---|---|
| 1 conversion @ 90 SPS | ~11 ms | **~154 ms** |
| 4 averages @ 90 SPS | ~44 ms | **~616 ms** |
| 1 conversion @ 20 SPS | ~50 ms | **~700 ms** |
| 4 averages @ 20 SPS | ~200 ms | **~2.8 s** |

### Full scan time (with temperature modulation)

Temperature modulation cycles each sensor heater through multiple set-point temperatures during a measurement to extract the resistance-vs-temperature profile, improving selectivity. The MEMS hot-plate thermal time constant is ~1–50 ms; practical settling allowance is 3–5 time constants.

Two sequencing strategies are possible:

**Strategy A — per-sensor sequential (all temps for sensor 1, then sensor 2, …):**

| Step | Duration |
|------|----------|
| Switch MUX to sensor N | <1 µs |
| For each of T temperature set-points: set PWM, thermal settle (5τ ≈ 75 ms), 4 ADC conversions @ 90 SPS (~44 ms) | T × 119 ms |
| **Per sensor total** | **T × ~119 ms** |
| **14 sensors, T=4 set-points** | **~6.7 s** |
| **14 sensors, T=4, 8 averages @ 20 SPS (400 ms/step)** | **~26 s** |

**Strategy B — parallel heaters, sequential readout (set all heaters to temp 1, MUX through all 14, then temp 2, …):**

This strategy is preferable where sensors have similar heater voltages and set-points. All heaters on the same rail can switch simultaneously.

| Step | Duration |
|------|----------|
| Set all heaters to temperature set-point K | <1 µs |
| Thermal settling (5τ) | ~75 ms |
| MUX through all 14 sensors, 4 averages @ 90 SPS | 14 × 44 ms = 616 ms |
| **Per temperature level** | **~691 ms** |
| **T=4 temperature levels** | **~2.8 s** |
| **T=4, 8 averages @ 20 SPS** | **~11.5 s** |

Strategy B is 2–3× faster and is recommended where the measurement protocol allows all sensors to share the same temperature sequence.

### Summary: where time is actually spent

At 90 SPS with 4 averages and 4 temperature steps (Strategy B):

```
Thermal settling:   4 × 75 ms  =  300 ms   (10%)
ADC conversion:    56 × 11 ms  =  616 ms   (22%)
Chemical response:             >>2 s       (>>68%)
```

The chemical response time of resistive gas sensors (seconds to minutes for equilibrium) dominates total measurement duration by a large margin. Optimising MUX switching time or ADC data rate has negligible impact on practical throughput.

---

## Summary of Trade-offs

| | MUX-first (proposed) | **Hybrid B (chosen)** | Fully distributed |
|---|---|---|---|
| BOM cost | Lowest (~$9.35) | **~$11.45** | Highest (~$17.90) |
| PCB area | Smallest (~75 mm²) | **~177 mm²** | Largest (~288 mm²) |
| ADC channels | 1 | **1** | 14 |
| Active LPF per sensor | ❌ No | **❌ No (not needed)** | ✅ Yes (precautionary) |
| Noise on long traces | ❌ ~100 mV at 100 kΩ source | **✅ ~0.2 mV at 100 Ω source** | ✅ No long high-Z traces |
| MUX leakage error | ❌ Up to 15 mV at 100 kΩ | **✅ <15 µV at 100 Ω** | ✅ N/A |
| Simultaneous readings | ❌ No | **❌ No** | ✅ Yes |
| 24-bit ADC resolution usable | ❌ No | **✅ Yes** | ✅ Yes |
| Full scan time (4T, Strategy B) | ~2.8 s | **~2.8 s** | ~2.8 s (2 ADC in parallel) |
