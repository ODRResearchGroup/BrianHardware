> [!IMPORTANT]  
> This decision supersedes the decision at [archive/26F05_mcu_pwm_decision.md](archive/26F05_mcu_pwm_decision.md) which has been moved to the archive.

# Hardware Specification: Multi-Channel MEMS Heater Control Array

## 1. Application & Load Profile

- **Target Devices:** MEMS micro-heaters (purely resistive thermal loads).
- **Device Power Rating:** 50 mW maximum per device.
- **Current Draw:**
  - ~10 mA at 5V logic supply.
  - ~15.15 mA at 3.3V logic supply.
- **Operating Frequency:** 10 kHz PWM target (optimal sweet spot for MEMS thermal mass stabilization without thermal fatigue).

## 2. Microcontroller & Co-Processor Architecture

- **Main Host Controller:** ESP32 (Handles high-level logic, sensor data ingestion, and system routines).
- **Dedicated PWM Co-Processor:** Raspberry Pi RP2040 Silicon (QFN-56 package).
  - **Function:** Offloads high-frequency PWM generation to eliminate ESP32 CPU overhead and serial bus bottlenecks.
  - **PWM Clock Configuration:** System Clock = 133 MHz; Target Frequency = 10 kHz.
  - **Timer Resolution:** Hardware Counter Wrap Value set to `13299`, yielding 13,300 discrete duty cycle steps (~0.01% tuning resolution).
  - **Hardware System Limit:** 16 fully independent hardware PWM duty-cycle channels (8 slices × 2 channels). Frequency is shared per slice pair; duty cycle is unique per channel.
  - **Future Scalability:** Expandable past 32 channels using RP2040 Programmable I/O (PIO) state machines.

## 3. ESP32-to-RP2040 Programming Interface

- **Protocol:** Serial Wire Debug (SWD) In-System Programming.
- **Pin Resource Isolation:** 100% collision-free. SWD uses dedicated debug silicon pins that do not overlap with the 16 hardware PWM GPIOs.
- **Inter-Chip Interconnects (4 Lines):**
  - `ESP32 Digital Output GPIO` ───► `RP2040 SWCLK` (Dedicated Debug Clock)
  - `ESP32 Bi-directional GPIO` ───► `RP2040 SWDIO` (Dedicated Debug Data)
  - `ESP32 Digital Output GPIO` ───► `RP2040 RUN` (Dedicated Reset Pin, tied to a 10 kΩ external pull-up)
  - `Common System GND` ───► `RP2040 GND`
- **Design Restriction:** Do not allocate ESP32 input-only pins (GPIOs 34–39) for driving the programming lines.

## 4. Low-Side Driver Specifications

- **Primary Topology:** Low-Side Switching (Safe for purely resistive heating elements; avoids ground-lifting risks inherent to IMUs/data-carrying sensors).
- **Recommended Driver IC:** Toshiba TBD62083APG (8-Channel DMOS FET Array).
  - **Voltage Drop:** Highly efficient, calculated at ~18 mV (0.018V) per channel at 15 mA load.
  - **Power Dissipation:** Near-zero, calculated at ~0.27 mW wasted per active channel.
  - **Supply Requirements:** Completely passive power architecture. Requires no auxiliary high-voltage rails; derives gate drive directly from the RP2040 logic levels.
  - **COM Pin Configuration (Pin 10):** Must be left floating/disconnected. Resistive loads do not generate inductive voltage spikes, rendering clamp diodes unnecessary.
- _Alternative Rejected Driver:_ TI TPL7407L. (Rejected due to its internal gate-drive LDO requiring an auxiliary supply rail of ≥ 8.5V on the COM pin, adding unnecessary PCB complexity).

## 5. PCB Layout & Electromagnetic Compatibility (EMC) Guidelines

- **High-Frequency Radiation Hazard:** The 10 kHz base frequency is not the source of radiation; the nanosecond-range switching edge rise time ($t_r$) of the DMOS outputs creates high-frequency RF harmonics.
- **Ground Strategy:** A continuous, unbroken Ground Plane must run directly underneath the PWM signal lines to minimize loop area and cancel magnetic fields.
- **Trace Geometry:** High-current paths connecting the power rail, MEMS heaters, and the low-side driver pins must be kept as physically short as possible.
