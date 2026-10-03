# BRIAN Hardware Firmware

Firmware for the BRIAN e-nose hardware platform (ESP32 + MEMS gas sensors + BME680), built with PlatformIO.

## Getting Started: Using the Hardware with Existing Data Viewers

<img src="https://github.com/user-attachments/assets/94a2b5df-399c-4010-9e0d-b9c586ea3b50" alt="Getting Started" />
<p><em>Reference e-nose test-rig setup used for commissioning and data collection.</em></p>

### Connect a Data Viewer

You can view live sensor data from the **Web interface (GitHub Pages live site)**: https://odrresearchgroup.github.io/BrianWeb/

Code for the web interface and the mobile app under development is available at:

- **Mobile app**: [BrianReactNative](https://github.com/ODRResearchGroup/BrianReactNative)
- **Web interface repo**: [BrianWeb](https://github.com/ODRResearchGroup/BrianWeb)

The firmware publishes data over BLE notifications (including MEMS gas channels and BME680 environmental channels), and the device advertises as `BRIAN`.

### Sensor Handling and Care

- Handle sensor PCBs by the edges only; do not touch sensor caps/membranes.
- Avoid liquid exposure, condensation, dust, and direct contact with adhesives or oils.
- Avoid silicone vapors, solvent fumes, or corrosive gases during storage and non-test operation.
- Keep the enclosure clean and dry; use filtered ambient air for idle/baseline periods.
- Avoid unnecessary power cycling; heater-based MEMS sensors are more stable when run in consistent conditions.
- After high-concentration exposure, allow recovery time in clean air before collecting baseline/reference data.

### Burn-in and Warm-up

- **Initial burn-in (new rig or replaced sensors):** run continuously in clean, well-ventilated air for **24-48 hours** before calibration/measurement campaigns.
- **Session warm-up:** allow at least **30-60 minutes** after power-on before recording critical measurements.
- Track baseline drift during burn-in/warm-up and start formal runs only after readings stabilize.

### Power Requirements and Recommendations

- **USB operation (recommended for bench/testing):**
  - Use the carrier board USB-C input with a stable **5 V** source.
  - Prefer a supply capable of **>=1 A** to avoid brownouts during BLE + sensor operation.
- **Battery operation (field/portable):**
  - Use a protected **3.7 V LiPo/Li-Ion (JST PH 2-pin)** pack (project reference battery: 4400 mAh).
  - Charge via the carrier board charger (MCP73831, up to 450 mA).
- For burn-in and first commissioning, prefer wired USB power over battery to keep supply conditions stable.

### Troubleshooting Checklist

- Firmware flashed and serial output healthy
- All expected sensors detected
- BLE device `BRIAN` visible
- Burn-in/warm-up complete
- Baseline in clean air recorded

## Developing the Hardware/Firmware

### Prerequisites

- [PlatformIO Core](https://docs.platformio.org/en/latest/core/installation/index.html)
- USB-C data cable for the SparkFun MicroMod Data Logging Carrier Board
- Stable bench USB power source (**5 V, >=1 A recommended**) for commissioning and burn-in

### Hardware Setup (Test Rig)

1. Confirm the processor board is fully seated in the MicroMod carrier.
2. Confirm sensor boards are connected to I2C and powered.
3. Confirm BME680 (Qwiic) is connected.
4. Place the device in the enclosure/test fixture shown in the Getting Started image above.
5. Ensure inlet/outlet airflow paths are open and unobstructed.
6. Power on and verify startup logs:
   - ADS boards detected at `0x48`, `0x49`, `0x4A`
   - BME680 detected at `0x77` (or fallback `0x76`)

### Build and Flash Firmware

```bash
pio run
```

```bash
pio run -t upload
```

### Open Serial Monitor

```bash
pio device monitor -b 115200
```

### BLE Emulator (no sensors needed)

`src/emulator/` is a stand-in firmware for developing and testing **BrianReactNative**, **BrianWeb** and the data pipeline without a physical BRIAN. It runs on any ESP32 dev board with nothing attached and serves exactly the same BLE contract as the V1 firmware (same services, characteristic UUIDs, payload format, time sync, board status, MTU, bonding), with synthetic data:

- 11 gas channels: per-channel baseline voltage, slow drift and small noise, clipped to the ADC range V1 reads each channel at.
- Occasional odour events (roughly every few minutes): a rise and exponential decay on a plausible group of channels together (e.g. VOC/EtOH/Odor/H₂, or CO/Smoke/NO₂).
- BME680 values that agree with each other: slowly varying temperature and dew point (relative humidity is derived from them), pressure around 1013 hPa, altitude from pressure with V1's formula, gas resistance dropping during odour events.

Both firmwares build their BLE profile from the shared library `lib/BrianBLE`, so a change to the contract applies to the emulator automatically.

**Recognising emulator data:** the emulator advertises as **`Brian-SIM-XXXXXX`** (real devices: `Brian-XXXXXX`). Both clients' scan filters (`Brian` / `Brian-` prefix) find it, and the app records the device name as the data source, so filter on the `Brian-SIM-` prefix to keep synthetic data out of analyses (e.g. in Influx). Its serial banner also says `SYNTHETIC DATA`.

**Supported boards** (one PlatformIO env each):

| Env | Board | Arduino-ESP32 core |
|---|---|---|
| `emulator_esp32` | Any classic ESP32 dev board (`esp32dev`) | 2.x, same as V1 |
| `emulator_esp32s3` | ESP32-S3-DevKitC-1 and most S3 boards | 2.x |
| `emulator_esp32c3` | ESP32-C3-DevKitM-1 and most C3 boards | 2.x |
| `emulator_esp32c6` | ESP32-C6-DevKitC-1 | 3.x (pioarduino platform) |
| `emulator_xiao_esp32c6` | Seeed Studio XIAO ESP32C6 (built-in antenna) | 3.x (pioarduino platform) |

For another board, copy an env and change `board =`. Boards whose only USB port is the chip's native USB (e.g. C3/S3 "super mini" boards) need `-DARDUINO_USB_CDC_ON_BOOT=1` in `build_flags` to see Serial output. The ESP32-C6 is only supported by Arduino-ESP32 3.x, which uses the NimBLE stack instead of Bluedroid; `lib/BrianBLE` handles the differences (CCCDs, encrypted time-sync write).

**Flash and monitor:**

```bash
pio run -e emulator_xiao_esp32c6 -t upload
pio device monitor -b 115200
```

**Options** (compile-time, add to the env's `build_flags`, or for one build e.g. `PLATFORMIO_BUILD_FLAGS="-DBRIAN_SIM_INTERVAL_MS=1000" pio run -e emulator_esp32 -t upload`):

| Flag | Default | Meaning |
|---|---|---|
| `BRIAN_SIM_INTERVAL_MS` | `5000` | Notify cycle (V1 is ~5 s); e.g. `1000` for stress tests |
| `BRIAN_SIM_BOARDS` | `0x0F` | Emulated boards, same bits as the board status characteristic (bit 0 ADS1, 1 ADS2, 2 ADS3, 3 BME680). E.g. `0x0B` = no ADS3, `0x07` = no BME680. Missing boards get no characteristics, like V1's detect-and-skip |
| `BRIAN_SIM_SEED` | `0` | PRNG seed; `0` = different every boot, any other value gives the same data every run |
| `BRIAN_SIM_EVENT_MEAN_S` | `90` | Mean seconds between odour event triggers |

**Serial commands** (type in the monitor): `e` start an odour event, `p` pause/resume notifications (connection stays up), `d` disconnect the client (to test reconnects), `s` status, `h` help.

Like V1, the emulator only sends notifications while a client is connected.

> **Note on PlatformIO platforms:** the ESP32-C6 envs use the [pioarduino](https://github.com/pioarduino/platform-espressif32) platform, which is also named `espressif32`. That is why the V1 env pins `platformio/espressif32` explicitly. Switching between a C6 env and the other envs makes PlatformIO swap the shared framework package, so the first build after a switch re-downloads it.

### Development Guidelines

- Keep changes focused and small per PR.
- Preserve BLE UUID compatibility unless there is a coordinated app/web change.
- Verify behavior on hardware after firmware changes (sensor detection, BLE connect/notify, data cadence).
- Update documentation when changing sensor mapping, BLE characteristics, or wiring assumptions.
- Follow existing PlatformIO project structure (`src/`, `include/`, `lib/`, `test/`).

### Hardware Documentation

See the hardware docs in [`hardware/`](./hardware):

- [Hardware Overview (V1)](./hardware/README.md)
- [V2 Design Plans](./hardware/V2_design.md)
- [Heater Power Architecture](./hardware/heater_power.md)
- [PCB Layout Feedback (2026-07-14)](./hardware/pcb_layout_feedback_2026-07-14.md)

### Repository Layout

- `src/` — V1 firmware source code (`src/main.cpp`)
- `src/emulator/` — BLE emulator firmware (synthetic data, see above)
- `lib/BrianBLE/` — the BLE contract (UUIDs, services, characteristics) shared by both
- `include/` — headers
- `lib/` — private libraries
- `test/` — PlatformIO tests
- `hardware/` — hardware notes and design documents

### BLE Profile (Quick Reference)

- Device name: `BRIAN`
- Standard service: Environmental Sensing Service (`0x181A`)
- Custom service: `de664a17-7db4-449f-97ba-5514e19a9d94`
- Time sync characteristic (write): `a1b2c3d4-e5f6-4a5b-8c9d-0e1f2a3b4c5d`
- Board status characteristic (read, custom service): `407fd299-d6ed-45ed-ab21-437f101c8acd` — 1-byte bitmask, bit 0/1/2 = ADS1/ADS2/ADS3 detected, bit 3 = BME680 detected, captured once at boot

### Troubleshooting

- **`ADS1115 not found` in logs:** check I2C wiring, addresses (`0x48`, `0x49`, `0x4A`), and power rails.
- **No BLE notifications:** confirm the client enabled notifications (CCCD) after connecting.
- **`BME680 not found`:** confirm Qwiic/I2C wiring and address (`0x77`, fallback `0x76`).
