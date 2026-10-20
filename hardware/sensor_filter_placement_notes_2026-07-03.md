# Sensor Front-End: Filter/Buffer Placement — Open Question

**Date:** 2026-07-03
**Relates to:** [sensor_output_architecture.md](sensor_output_architecture.md)
**Status:** Discussion notes — not yet a decision, flagging for team input

---

## The question

Our current architecture is: **unity buffer + passive RC per sensor → long trace → shared multichannel ΔΣ ADC, no MUX.** The buffer and RC filter both sit right next to the sensor (2–5 mm away); the ADC is centralized, so the run from the RC filter's output to the ADC input can be long and routed near noisy aggressors (two 1 MHz buck converters, 14 PWM heater gates, SPI bus).

That run is a problem the passive RC doesn't fully solve: a passive RC's output impedance is roughly equal to its own resistor (our target: 10 kΩ), not 0 Ω. So the long trace is effectively driven from a 10 kΩ source the whole way to the ADC — exactly the kind of moderate-impedance node that's vulnerable to capacitive noise coupling. Using the same coupling estimate we already use elsewhere in the design doc (0.1 pF coupling cap, 1 MHz/3.3 V aggressor), a 10 kΩ source picks up ~20 mV of noise — a meaningful fraction of a 24-bit ADC's usable range if that run passes near the buck converters or PWM gates.

We're also planning this section of the board as **reconfigurable** (unpopulated R/C + cuttable/solderable jumpers) so each channel can be strapped as: no buffer, active LPF, unity buffer, or non-inverting amp. That flexibility needs to factor into wherever we land on the long-trace question.

## Options

| Option | Signal chain | Op-amps/ch | Solves ADC sampling settling? | Solves long-trace noise pickup? | Notes |
|---|---|---|---|---|---|
| **A — current** | buffer → RC (10 kΩ/10 nF) → long trace → ADC | 1 | No | No | RC output impedance (10 kΩ) exposed for the full trace length |
| **B — add ADC-local RC** | buffer → RC #1 (near sensor) → long trace → RC #2 (small, sized to ADC's sampling network) → ADC | 1 | **Yes** | No | RC #2 protects the ADC's internal sampling cap but can't undo coupling picked up *before* it |
| **C — active 2nd stage** | buffer → active LPF (or buffer → RC → 2nd buffer) → long trace → ADC | 2 | Yes (low-Z drive) | **Yes** | 2nd op-amp's output is ~0 Ω, so the long trace is immune the same way the sensor-to-buffer run is. Also gets steeper (2-pole) rolloff if using a true active LPF. Doubles op-amp count (28 vs 14 across the array). |

Option B is worth doing regardless — it's cheap and fixes a real ADC-datasheet requirement (source impedance for the internal PGA/sampling cap). But it does **not** address the noise-pickup risk over the long run itself; only an active (op-amp-driven) stage at the head of that run does that (Option C).

## Recommendation

- Add the small ADC-local RC (Option B) unconditionally — low cost, fixes a real requirement, doesn't depend on how we resolve the rest.
- Since the front-end footprint is already reconfigurable, make sure it can also be strapped as Option C (active LPF, or buffer+buffer sandwiching the passive RC) on a given channel without a respin — this gives us a way to bench-test whether the long-trace noise pickup in Option A/B is actually a problem on the real layout, or just a worst-case estimate.
- Whether we need Option C in the final (non-prototype) BOM is a layout question, not just a math one — depends on how close the long runs actually get routed to the buck converters/PWM gates. Recommend deciding after we have real board measurements rather than committing now.

## Open items

- Confirm ADC (ADS131M08) datasheet's recommended max source impedance for its PGA/sampling input — sizes RC #2.
- Decide whether the reconfigurable footprint gets active-LPF-capable feedback pads (capacitor pads in addition to the existing R_f/R_g gain pads) or a separate second op-amp footprint for the buffer+buffer variant.
- Once routing is drafted, flag which channels have the longest RC-to-ADC runs — those are the ones worth populating as Option C first for bench comparison.
