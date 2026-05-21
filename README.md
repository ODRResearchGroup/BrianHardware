# BRIAN Hardware Firmware

Firmware for the BRIAN e-nose hardware platform (ESP32 + MEMS gas sensors + BME680), built with PlatformIO.

## 1) Use the Hardware with Existing Data Viewers

<img src="https://github.com/user-attachments/assets/94a2b5df-399c-4010-9e0d-b9c586ea3b50" alt="Getting Started" />
<p><em>Reference e-nose test-rig setup used for commissioning and data collection.</em></p>

### Connect a Data Viewer

You can view live sensor data from:

- **Mobile app**: [BrianReactNative](https://github.com/ODRResearchGroup/BrianReactNative)
- **Web interface repo**: [BrianWeb](https://github.com/ODRResearchGroup/BrianWeb)
- **Web interface (GitHub Pages live site)**: https://odrresearchgroup.github.io/BrianWeb/

The firmware publishes data over BLE notifications (including MEMS gas channels and BME680 environmental channels), and the device advertises as `BRIAN`.

### Hardware Setup (Test Rig)
1. Confirm the processor board is fully seated in the MicroMod carrier.
2. Confirm sensor boards are connected to I2C and powered.
3. Confirm BME680 (Qwiic) is connected.
4. Place the device in the enclosure/test fixture shown in the Getting Started image above.
5. Ensure inlet/outlet airflow paths are open and unobstructed.
6. Power on and verify startup logs:
   - ADS boards detected at `0x48`, `0x49`, `0x4A`
   - BME680 detected at `0x77` (or fallback `0x76`)

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

### Bring-up Checklist Before Data Collection
- Firmware flashed and serial output healthy
- All expected sensors detected
- BLE device `BRIAN` visible
- Burn-in/warm-up complete
- Baseline in clean air recorded

## 2) Develop the Hardware/Firmware

### Prerequisites
- [PlatformIO Core](https://docs.platformio.org/en/latest/core/installation/index.html)
- USB-C data cable for the SparkFun MicroMod Data Logging Carrier Board
- Stable bench USB power source (**5 V, >=1 A recommended**) for commissioning and burn-in

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

### Repository Layout

- `src/` — firmware source code
- `include/` — headers
- `lib/` — private libraries
- `test/` — PlatformIO tests
- `hardware/` — hardware notes and design documents

### BLE Profile (Quick Reference)

- Device name: `BRIAN`
- Standard service: Environmental Sensing Service (`0x181A`)
- Custom service: `de664a17-7db4-449f-97ba-5514e19a9d94`
- Time sync characteristic (write): `a1b2c3d4-e5f6-4a5b-8c9d-0e1f2a3b4c5d`

### Troubleshooting

- **`ADS1015 not found` in logs:** check I2C wiring, addresses (`0x48`, `0x49`, `0x4A`), and power rails.
- **No BLE notifications:** confirm the client enabled notifications (CCCD) after connecting.
- **`BME680 not found`:** confirm Qwiic/I2C wiring and address (`0x77`, fallback `0x76`).
