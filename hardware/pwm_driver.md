# MCU and PWM Controller Selection: Design Decision

**Status:** Decided  
**Applies to:** BRIAN 2.0 14-sensor array  
**Decision:** Original ESP32 (WROOM-32 or equivalent) with CP2102N USB-UART bridge

---

## Decision Summary

BRIAN 2.0 requires **14 independent hardware PWM channels** to drive the individual N-channel MOSFETs that control sensor heater power on each of the two voltage rails. After evaluating external PWM controller ICs and all current ESP32 family variants, the **original ESP32** is selected as the MCU because it is the only chip in the ESP32 family with enough hardware PWM channels (16 LEDC) to cover all 14 sensors without external assistance.

---

## Background: Why 14 Independent PWM Channels

Each of the 14 sensors has an individual N-channel MOSFET connecting its heater to the shared voltage rail. Temperature modulation — cycling each sensor through a sequence of discrete heater power set-points during a measurement — requires independent PWM control of each MOSFET gate. PWM duty cycle must be set on a power-equivalence basis:

```
D = (V_target / V_rail)²
```

PWM frequency must exceed **10 kHz** to keep thermal ripple negligible relative to the MEMS hot-plate thermal time constant (1–50 ms). All 14 channels may need to operate at different duty cycles simultaneously.

---

## Option 1: External PWM Controller IC

Before evaluating MCU options, a range of external I²C/SPI PWM ICs were considered to supplement the MCU's native PWM channels.

### PCA9955BTW (NXP) — Rejected

The chip that prompted this investigation. It is a **16-channel constant-current LED driver** with I²C (FM+) interface and per-channel 8-bit PWM at a fixed 31.25 kHz.

**Why rejected:**
- Its outputs are **constant-current sinks**, not logic-level voltage outputs. They are designed to drive LED anodes, not MOSFET gates.
- If used to drive MOSFET gates: the open-drain/current-sink architecture requires inverted logic with pull-up resistors — a workaround that uses a complex, expensive LED driver for a task the MCU itself can handle.
- If used to replace MOSFETs and drive heaters directly: the constant-current output model fundamentally conflicts with the voltage-mode PWM architecture. Heater power is controlled via calibrated V_rms equivalence (D = (V_target/V_rail)²), which requires knowing the heater resistance as a function of temperature. Constant-current drive bypasses the regulated rail entirely.

### TLC59711 (Texas Instruments) — Rejected

SPI-interfaced 12-channel constant-current LED driver. Same category of device as PCA9955BTW — constant-current sink outputs, wrong architecture for this application.

### PCA9685 (NXP) — Rejected

16-channel I²C PWM controller with 12-bit resolution. Unlike the LED drivers above, its outputs are **voltage-mode** (logic-level, push-pull compatible), which would be suitable for MOSFET gate driving. However, its maximum PWM frequency is approximately **1,526 Hz** using its internal oscillator — well below the 10 kHz minimum required to keep thermal ripple negligible.

### SX1509 (Semtech) — Considered but insufficient

16-channel I²C GPIO expander with per-channel PWM capability. Unlike the LED drivers, its outputs are **push-pull voltage-mode** and could drive MOSFET gates directly. However:

- Internal oscillator: 2 MHz
- PWM frequency = FOSC ÷ 2^(divider) ÷ 256 steps
- Maximum PWM frequency: 2 MHz ÷ 256 = **~7.8 kHz** — just below the 10 kHz threshold

Borderline in principle (the thermal time constant argument still holds at 7.8 kHz), but it does not meet the stated design requirement.

### CY8C9520A (Infineon/Cypress) — Insufficient channel count

I²C I/O expander with configurable PWM clock sources including 24 MHz, which would yield PWM frequencies up to ~93 kHz — well above the 10 kHz requirement. However, the 20-bit variant has only **4 PWM output blocks**, far short of the 14 channels needed. Larger variants (CY8C9540A, CY8C9560A) exist but are significantly more complex devices and difficult to source.

### Summary

There is no off-the-shelf I²C or SPI PWM expander that simultaneously satisfies all three requirements:
- ≥14 independent channels
- ≥10 kHz PWM frequency
- Voltage-mode (logic-level) outputs suitable for MOSFET gate drive

---

## Option 2: External PWM IC + ESP32-S3

The ESP32-S3 was an initial candidate due to its USB-native interface, AI/ML vector extensions, dual-core Xtensa LX7 at 240 MHz, and 45 usable GPIOs. However, the ESP32-S3 has only **8 LEDC channels** — insufficient for 14 independent simultaneous PWM outputs.

Two workarounds were considered:

### 2a: ESP32-S3 LEDC + MCPWM

The S3's MCPWM peripheral provides up to 12 additional PWM output signals (2 units × 3 operators × 2 generators), giving a theoretical total of 20 PWM outputs combined with LEDC. However:
- MCPWM is designed for motor control (complementary pairs with dead-time insertion)
- Using it for simple independent PWM requires working against the API's design intent
- Arduino ESP32 core support for MCPWM in non-motor-control configurations is limited and poorly documented
- The result is significant firmware complexity for a problem that does not exist on the original ESP32

### 2b: ESP32-S3 + External PWM IC

As established above, no suitable external IC exists. Combining the S3 with, for example, the SX1509 would add an I²C component, add firmware complexity, and still fall short on frequency (7.8 kHz).

---

## Option 3: Original ESP32 — Selected

The original ESP32 (Xtensa LX6, dual-core, 240 MHz) has **16 LEDC channels** (8 high-speed + 8 low-speed), covering all 14 sensor channels with 2 to spare. The LEDC peripheral generates PWM entirely in hardware — once configured, it requires no CPU cycles per PWM cycle, and updating a duty cycle is a single register write.

### ESP32 Family LEDC Channel Comparison

| Chip | LEDC Channels | Wireless | Status |
|------|--------------|----------|--------|
| ESP32 (original) | **16** | WiFi + BT Classic + BLE 4.2 | Production |
| ESP32-S2 | 8 | WiFi only | Production |
| ESP32-S3 | 8 | WiFi + BLE 5.0 | Production |
| ESP32-C2 | 6 | WiFi + BLE 5.0 | Production |
| ESP32-C3 | 6 | WiFi + BLE 5.0 | Production |
| ESP32-C6 | 6 | WiFi 6 + BLE 5.3 + Thread/Zigbee | Production |
| ESP32-H2 | 6 | BLE 5.0 + Thread/Zigbee (no WiFi) | Production |
| ESP32-C5 | TBD | WiFi 6 dual-band + BLE 5.2 | Not yet available |
| ESP32-P4 | 8 | **None** (requires external module) | Early access |

The original ESP32 is the only current chip in the family with ≥14 LEDC channels. No newer variant matches or exceeds it on this metric.

### Why Hardware PWM Does Not Burden the CPU

The LEDC peripheral runs independently of the CPU once configured. At 14 channels running at 20 kHz, all 280,000 PWM transitions per second occur without CPU involvement. Duty cycle updates during temperature modulation sequences happen at human-timescale rates (once every few seconds per sensor) and require a single register write each. There is no timing conflict with concurrent I²C ADC reads or data processing.

### RAM and CPU Assessment

The original ESP32's 520 KB SRAM and dual-core 240 MHz Xtensa LX6 are more than sufficient for BRIAN 2.0's workload:
- Raw sensor data: 14 sensors × 4 bytes × even 1000 samples = 56 KB
- BLE stack: ~100 KB
- Signal processing headroom: substantial

The ESP32-S3's faster LX7 core and AI/ML vector extensions are relevant for on-device neural network inference (e.g. TensorFlow Lite). Since on-device AI is not part of the current BRIAN 2.0 scope, the S3 offers no practical advantage over the original ESP32 for this application.

---

## Trade-offs of Choosing the Original ESP32

### USB-UART Bridge Required

The original ESP32 lacks native USB. A **CP2102N** (or equivalent: CH340C, FT232RL) USB-to-UART bridge IC must be included on the PCB to provide USB connectivity for:
- Firmware flashing via the Arduino IDE / esptool
- Serial debug output
- Optional wired data streaming to a host PC

The CP2102N is a standard, well-supported component (~$0.80–1.50 in unit quantities, SOIC-16 or QFN-24 package). An auto-reset circuit (RTS → EN, DTR → GPIO0) allows one-click programming from the Arduino IDE without manual button-pressing. This is the approach used by virtually all commercial ESP32 development boards.

BRIAN's primary data paths are BLE (React Native mobile app) and WiFi (web interface), so USB is used mainly for development and debug rather than live data transfer. The bridge imposes no practical limitation for this use case.

### Maintenance Mode

Espressif has indicated the original ESP32 is in maintenance mode — new silicon revisions are not planned, and new designs are encouraged to use newer variants. For a research instrument produced in small quantities, this is not a practical concern: the chip remains in full production, is widely stocked, and has a long projected availability window. If BRIAN is ever productised at scale or if on-device AI becomes a design requirement, the MCU choice should be revisited at that point.

### Bluetooth Classic

The original ESP32 includes Bluetooth Classic (BR/EDR) in addition to BLE 4.2. The BRIAN firmware uses BLE for the mobile app. Bluetooth Classic is unused but costs nothing to have available.

---

## Implementation Notes

### Arduino LEDC API

```cpp
// One-time setup per channel
ledcSetup(channel, 20000, 8);   // channel 0–15, 20 kHz, 8-bit (0–255)
ledcAttachPin(pin, channel);

// Set heater power level (called during temperature modulation steps)
// D = (V_target / V_rail)^2, scaled to 8-bit
uint8_t duty = (uint8_t)(255.0f * powf(v_target / v_rail, 2.0f));
ledcWrite(channel, duty);
```

All 14 channels can share the same 20 kHz timer. A channel set to duty = 255 is fully on; duty = 0 is fully off. Discrete on/off switching (no intermediate temperature levels) is handled the same way.

### Channel Assignments (suggested)

| Channel | Sensor | Rail |
|---------|--------|------|
| 0 | GM-102B | 1.8 V |
| 1 | SMD1001 | 1.8 V |
| 2 | SMD1002 | 1.8 V |
| 3 | SMD1007 | 1.8 V |
| 4 | SMD1008 | 1.8 V |
| 5 | SMD1011 | 1.8 V |
| 6 | SMD1013B | 1.8 V |
| 7 | SMD1015 | 1.8 V |
| 8 | GM-602B | 1.8 V |
| 9 | GM-202B | 2.5 V |
| 10 | GM-302B | 2.5 V |
| 11 | GM-502B | 2.5 V |
| 12 | GM-512B | 2.5 V |
| 13 | GMV-2021B | 2.5 V |
| 14–15 | (spare) | — |

### PCB Notes

- Include CP2102N with auto-reset circuit (RTS → 100 nF → EN; DTR → 100 nF → GPIO0)
- GPIO0 must be pulled high (10 kΩ) for normal boot; pulled low during flash mode
- LEDC outputs connect directly to MOSFET gates via 100 Ω series resistors to dampen ringing
- No additional gate driver IC is required at the current levels (gate charge << 1 nC for SOT-23 logic-level MOSFETs)
