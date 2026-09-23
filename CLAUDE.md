# CLAUDE.md — BrianHardware

Guidance for Claude Code when working in this repository.

## What this repo is

Firmware and hardware design documents for **BRIAN**, an electronic nose (e-nose) built by the ODR research group at Malmö University. BRIAN measures gas-sensor signals and streams them over Bluetooth Low Energy (BLE) to two clients:

- **BrianReactNative** (https://github.com/ODRResearchGroup/BrianReactNative) — the mobile app used on smell walks (Android now, iOS must keep working)
- **BrianWeb** (https://github.com/ODRResearchGroup/BrianWeb) — a Web Bluetooth dashboard on GitHub Pages

The repo contains two very different things:

1. **V1 firmware (`src/`)** — the device that exists today. PlatformIO + Arduino on a SparkFun MicroMod ESP32.
2. **BRIAN 2.0 design docs (`hardware/`)** — the next-generation 14-sensor board, still in the design phase. No V2 firmware exists yet.

Keep these separate: don't apply V2 design decisions to the V1 firmware unless the task says so.

## Commands

```bash
pio run                        # build (env: MicroMod_ESP32)
pio run -t upload              # flash
pio device monitor -b 115200   # serial monitor
```

There are no automated tests yet (`test/` is empty). The build succeeding is the minimum check; anything that touches sensor reading or BLE must also be verified on hardware by a person. Say so explicitly in your PR description, with what to check.

## V1 hardware (current device)

Full description: `hardware/README.md`.

- MCU: SparkFun MicroMod ESP32 on the MicroMod Data Logging Carrier (USB-C, MCP73831 LiPo charger ≤450 mA, microSD, Qwiic).
- Battery: 3.7 V 4400 mAh Li-ion pack (JST PH).
- Gas sensors: 11 DFRobot Fermion MEMS modules, read through 3 ADS1115 16-bit ADCs on I²C.
- Environmental: BME680 (Qwiic) at `0x77` (fallback `0x76`).

| Board | I²C | A0 | A1 | A2 | A3 |
|---|---|---|---|---|---|
| ADS1 | `0x48` | HCHO | CH₄ | VOC | Odor |
| ADS2 | `0x49` | EtOH | H₂S | NO₂ | NH₃ |
| ADS3 | `0x4A` | CO | Smoke | H₂ | — |

## Firmware structure (`src/main.cpp`)

Everything is in one file:

- `setup()`: I²C at 400 kHz → `initMEMS()` (detects each ADS board) → `initBME680()` → BLE init with a unique name `Brian-XXXXXX` (last 3 bytes of the BT MAC), MTU 517, bonding ("Just Works") → creates characteristics **only for boards/sensors that were detected** → starts advertising.
- `loop()`: **only samples while a BLE client is connected.** Reads each channel in turn, sets the characteristic value and notifies, then `delay(5000)`. A full cycle is therefore a little over 5 s.
- Libraries (`platformio.ini`): Adafruit ADS1X15, Adafruit BME680, Adafruit LC709203F (declared, not yet used), sunset, Chrono.

### Known issues in the current firmware (see open issues)

- **#17:** VOC and NH₃ use a non-default gain, but the gain is reset *before* `computeVolts()`, which uses the currently set gain → wrong voltages (×4 and ×16). The code also uses the `Adafruit_ADS1015` class although the boards carry ADS1115 chips (loses 4 bits). The README's gain note disagrees with the code.
- **#7:** per-sensor gains should become configurable or automatic.
- **#16:** battery level reporting (fuel gauge or voltage divider → BLE Battery Service).
- **#18:** fan for active sampling (low priority).
- **#1:** track usage and suggest burn-in time.

## BLE contract — shared with the app and web interface

**Do not change UUIDs, the payload format, or which service a characteristic lives in without a coordinated change in BrianReactNative and BrianWeb.** If a change is needed, list the required client changes in the PR.

- Every sensor value is a **4-byte little-endian IEEE-754 float32** (gas channels: volts; env: units below).
- Services: Environmental Sensing Service `0x181A`; custom service `de664a17-7db4-449f-97ba-5514e19a9d94`.
- Both services are created with explicit handle counts (40 and 50). Adding characteristics may require raising these, or later characteristics silently lose their CCCD.

| Channel | Service | Characteristic | Value |
|---|---|---|---|
| CH₄ | ESS | `0x2BD1` | V |
| VOC | ESS | `0x2BD3` | V |
| NH₃ | ESS | `0x2BCF` | V |
| NO₂ | ESS | `0x2BD2` | V |
| HCHO | custom | `6a135b89-f360-4f64-86fc-5a14092034b4` | V |
| Odor | custom | `4c28fcb8-d69b-404a-8668-41655d814e7f` | V |
| EtOH | custom | `f8156843-6d98-4ba2-8014-1cf03d7dedb8` | V |
| H₂S | custom | `87dc71bd-29a4-4218-a2a7-83fd2a69cc40` | V |
| CO | custom | `88f6fa6c-c4e0-4a3d-ba72-f435641251c4` | V |
| Smoke | custom | `cafb955e-6e7b-424b-9e03-6d8d003aa286` | V |
| H₂ | custom | `0176655b-0007-4e02-abc1-e9f2d6815f46` | V |
| Temperature | ESS | `0x2A6E` | °C |
| Pressure | ESS | `0x2A6D` | hPa |
| Humidity | ESS | `0x2A6F` | % |
| Altitude | ESS | `0x2A69` | m (assumes 1013.25 hPa sea level) |
| BME680 gas resistance | custom | `5b0e3c0b-1a44-4b76-82ee-8c2adc2dd8e9` | Ω |
| Time sync (write) | custom | `a1b2c3d4-e5f6-4a5b-8c9d-0e1f2a3b4c5d` | Unix seconds, 8 or 4 bytes LE; encrypted link required |
| Board status | custom | `407fd299-d6ed-45ed-ab21-437f101c8acd` | 1-byte bitmask, read-only, captured once at boot (see below) |

**Board status bitmask** (`BOARD_STATUS_CHARACTERISTIC_UUID`): bit *n* set = that I2C board was detected in `initMEMS()`/`initBME680()` at boot, independent of whether its sensor characteristics were created. Not updated after boot (no live re-check). Battery monitor status is out of scope (tracked separately in #16).

| Bit | Board |
|---|---|
| 0 | ADS1 (`0x48`): HCHO, CH₄, VOC, Odor |
| 1 | ADS2 (`0x49`): EtOH, H₂S, NO₂, NH₃ |
| 2 | ADS3 (`0x4A`): CO, Smoke, H₂ |
| 3 | BME680 |

When adding new data (e.g. battery), prefer standard SIG services/characteristics where they exist (Battery Service `0x180F` / Battery Level `0x2A19`), and document them here and in `README.md`.

## BRIAN 2.0 design docs (`hardware/`)

| File | Content |
|---|---|
| `V2_design.md` | Overview: 14 sensors (Winsen GM-x02B/x12B + Huiwen SMD10xx), two heater rails, ADC and controller |
| `heater_power.md` | Decided: two buck converters (1.8 V and 2.5 V) + one N-MOSFET per sensor; PWM duty = (V_target/V_rail)² |
| `sensor_output_architecture.md` | **Current** readout decision: unity buffer + passive RC per sensor, multichannel ΔΣ ADC, no MUX (recommended 2× ADS131M08) |
| `pwm_driver.md` | **Current** PWM decision: ESP32 host + RP2040 PWM co-processor at 10 kHz |
| `archive/` | Superseded decisions (e.g. the MUX + ADS122C14 design, the ESP32-only LEDC design) |
| `sensors/` | Datasheets for all proposed V2 sensors |

⚠️ `V2_design.md` has not been fully updated: its "Signal Readout Architecture", "ADC" and "Controller" sections still describe the **superseded** MUX/ADS122C14 and ESP32-LEDC-only decisions. Where they conflict, **`sensor_output_architecture.md` and `pwm_driver.md` are authoritative**. If you edit V2 docs, fix `V2_design.md` to link to them rather than duplicating content.

Decision docs follow a pattern: a status line, the decision, the rationale, rejected alternatives, and a note linking to the archived previous decision. Keep that pattern. When a decision changes, move the old file to `hardware/archive/` with a date-code prefix (e.g. `26F26_...`) and link it from the new one.

When doing V2 design work, check numbers against the datasheets in `hardware/sensors/` (heater voltage, heater power, load resistance, sensitivity) rather than relying on memory.

## Conventions

- Keep changes small and focused, one topic per PR (see README "Development Guidelines").
- Branching: PRs usually go through `develop` → `main`.
- C++: follow the existing style (clang-format defaults as in the current file); keep Serial debug output useful but not flooding.
- Firmware must keep working when a board or the BME680 is missing: detect and skip, never crash.
- Update `README.md` / `hardware/README.md` whenever sensor mapping, gains, wiring or BLE characteristics change.

## Device care (relevant to any test you ask a person to run)

- New or replaced sensors need 24–48 h burn-in in clean air; every session needs 30–60 min warm-up before critical measurements.
- Don't touch sensor membranes; avoid silicone vapours, solvents and condensation.

## Current priorities (September 2026)

An outdoor smell walk with several e-noses is planned for **25 Sept 2026**. Walk-blocking work is labelled `walk-blocker`; the overall checklist is BrianReactNative #30. Hardware-side blockers: #17 (gain fix), #15 (sensor QC and side-by-side test), #14 (battery runtime). Issues labelled `claude-code` are scoped for an agent to attempt.
