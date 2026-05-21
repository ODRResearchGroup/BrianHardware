# BRIAN Hardware Firmware

Firmware for the BRIAN e-nose hardware platform (ESP32 + MEMS gas sensors + BME680), built with PlatformIO.

## Getting Started

<img src="https://github.com/user-attachments/assets/94a2b5df-399c-4010-9e0d-b9c586ea3b50" alt="Getting Started" />

### Prerequisites
- [PlatformIO Core](https://docs.platformio.org/en/latest/core/installation/index.html)
- USB connection to the SparkFun MicroMod ESP32 board

### Build
```bash
pio run
```

### Upload
```bash
pio run -t upload
```

### Serial Monitor
```bash
pio device monitor -b 115200
```

The device advertises over BLE as `BRIAN`.

## Viewing Data

You can view live sensor data from:

- **Mobile app**: [BrianReactNative](https://github.com/ODRResearchGroup/BrianReactNative)
- **Web interface**: [BrianWeb](https://github.com/ODRResearchGroup/BrianWeb)

The firmware publishes data over BLE notifications (including MEMS gas channels and BME680 environmental channels).

## Development Guidelines

- Keep changes focused and small per PR.
- Preserve BLE UUID compatibility unless there is a coordinated app/web change.
- Verify behavior on hardware after firmware changes (sensor detection, BLE connect/notify, data cadence).
- Update documentation when changing sensor mapping, BLE characteristics, or wiring assumptions.
- Follow existing PlatformIO project structure (`src/`, `include/`, `lib/`, `test/`).

## Hardware Documentation

See the hardware docs in [`hardware/`](./hardware):

- [Hardware Overview (V1)](./hardware/README.md)
- [V2 Design Plans](./hardware/V2_design.md)
- [Heater Power Architecture](./hardware/heater_power.md)

## Repository Layout

- `src/` — firmware source code
- `include/` — headers
- `lib/` — private libraries
- `test/` — PlatformIO tests
- `hardware/` — hardware notes and design documents

## BLE Profile (Quick Reference)

- Device name: `BRIAN`
- Standard service: Environmental Sensing Service (`0x181A`)
- Custom service: `de664a17-7db4-449f-97ba-5514e19a9d94`
- Time sync characteristic (write): `a1b2c3d4-e5f6-4a5b-8c9d-0e1f2a3b4c5d`

## Troubleshooting

- **`ADS1015 not found` in logs:** check I2C wiring, addresses (`0x48`, `0x49`, `0x4A`), and power rails.
- **No BLE notifications:** confirm the client enabled notifications (CCCD) after connecting.
- **`BME680 not found`:** confirm Qwiic/I2C wiring and address (`0x77`, fallback `0x76`).
