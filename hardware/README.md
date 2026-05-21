# E-Nose Hardware - V1

## Microcontroller

**[SparkFun MicroMod ESP32](https://www.sparkfun.com/products/16781)** — provides processing, BLE radio (advertised as `BRIAN`), and I2C bus for all peripherals. It is mounted on the MicroMod Data Logging Carrier Board via the M.2 connector.

| Resource     | Link                                                                             |
| ------------ | -------------------------------------------------------------------------------- |
| Product page | https://www.sparkfun.com/products/16781                                          |
| Hookup guide | https://learn.sparkfun.com/tutorials/micromod-esp32-processor-board-hookup-guide |

## Carrier Board

**[SparkFun MicroMod Data Logging Carrier Board](https://www.sparkfun.com/products/16829)** — hosts the ESP32 processor module and provides the MicroSD slot, USB-C programming/power port, LiPo battery charging circuit (MCP73831, up to 450 mA), RTC backup battery, Qwiic I2C connector, and independently switchable 3.3 V rails for low-power control of connected peripherals.

| Resource     | Link                                                                                  |
| ------------ | ------------------------------------------------------------------------------------- |
| Product page | https://www.sparkfun.com/products/16829                                               |
| Hookup guide | https://learn.sparkfun.com/tutorials/micromod-data-logging-carrier-board-hookup-guide |

## Battery

**[Adafruit Lithium Ion Battery Pack - 3.7V 4400mAh](https://www.adafruit.com/product/354)** — two 18650 cells in parallel, with integrated over-voltage, under-voltage, and over-current protection. Connects to the carrier board's JST LiPo port. Any JST PH 2-pin compatible LiPo/Li-Ion battery may be substituted.

| Resource     | Link                                 |
| ------------ | ------------------------------------ |
| Product page | https://www.adafruit.com/product/354 |

## Gas Sensors (MEMS)

11 analogue channels are distributed across three custom MEMS breakout boards. Each board routes four gas sensors into one **[Adafruit ADS1115](https://www.adafruit.com/product/1085)** (16-bit, 4-channel ADC). The three boards occupy I2C addresses `0x48`, `0x49`, and `0x4A`.

| Resource     | Link                                                        |
| ------------ | ----------------------------------------------------------- |
| Product page | https://www.adafruit.com/product/1085                       |
| Usage guide  | https://learn.adafruit.com/adafruit-4-channel-adc-breakouts |

> The PCB design is available in Autodesk Fusion 360: [MEMS Breakout Board](https://a360.co/4dJqguX)

| Board | I2C Address | A0                  | A1    | A2  | A3   |
| ----- | ----------- | ------------------- | ----- | --- | ---- |
| ADS1  | `0x48`      | HCHO (formaldehyde) | CH₄   | VOC | Odor |
| ADS2  | `0x49`      | EtOH (ethanol)      | H₂S   | NO₂ | NH₃  |
| ADS3  | `0x4A`      | CO                  | Smoke | H₂  | —    |

> **Note:** VOC (ADS1 A2) and NH₃ (ADS2 A3) are read at `GAIN_SIXTEEN` due to their low output voltage range. All other channels use `GAIN_ONE`.

## Environmental Sensor

**[SparkFun Environmental Sensor Breakout - BME680 (Qwiic)](https://www.sparkfun.com/products/16466)** — measures temperature, barometric pressure, relative humidity, and gas resistance. Addressed at `0x77` (fallback: `0x76`). Configured with IIR filter size 3 and a gas heater target of 320 °C / 150 ms.

| Resource     | Link                                      |
| ------------ | ----------------------------------------- |
| Product page | https://www.sparkfun.com/products/16466   |
| Hookup guide | https://learn.sparkfun.com/tutorials/1168 |

## Storage

**MicroSD card** via SPI — provided by the Data Logging Carrier Board. Used for local data logging.

## Wireless Communication

BLE 5 via the ESP32 radio, with MTU extended to 517 bytes. Two GATT services are advertised:

| Service                             | UUID                                   | Characteristics                                                                   |
| ----------------------------------- | -------------------------------------- | --------------------------------------------------------------------------------- |
| Environmental Sensing Service (ESS) | `0x181A` (standard)                    | CH₄, VOC, NH₃, NO₂, Temperature, Pressure, Humidity, Altitude                     |
| Custom Sensor Service               | `de664a17-7db4-449f-97ba-5514e19a9d94` | HCHO, Odor, EtOH, H₂S, CO, Smoke, H₂, BME680 gas resistance, Time sync (writable) |

The time sync characteristic accepts a Unix timestamp (4 or 8 bytes) written by a connected client to synchronise the ESP32's internal RTC.

## Enclosure

The enclosure uses a **magnetic closure** and is designed to house the MEMS breakout board PCB.

> Enclosure design: [Autodesk Fusion 360](https://a360.co/4swgDo5)

## Bill of Materials

| Part Number        | Manufacturer | Description                                            | Qty | Distributor | SKU           | Unit Price  |
| ------------------ | ------------ | ------------------------------------------------------ | --- | ----------- | ------------- | ----------- |
| WRL-16781          | SparkFun     | MicroMod ESP32 Processor                               | 1   | Mouser      | 474-WRL-16781 | $20.60      |
| DEV-16829          | SparkFun     | MicroMod Data Logging Carrier Board                    | 1   | Mouser      | 474-DEV-16829 | $22.95      |
| SEN-16466          | SparkFun     | Environmental Sensor Breakout - BME680 (Qwiic)         | 1   | Mouser      | 474-SEN-16466 | $22.95      |
| KIT-15081          | SparkFun     | Qwiic Cable Kit                                        | 1   | Mouser      | 474-KIT-15081 | $13.79      |
| 1085               | Adafruit     | ADS1115 16-bit 4-Channel ADC                           | 3   | Mouser      | 485-1085      | $14.95      |
| SEN0563            | DFRobot      | Fermion: MEMS Formaldehyde HCHO Sensor (0–3 ppm)       | 1   | Farnell     | 4733253       | $4.78       |
| SEN0564            | DFRobot      | Fermion: MEMS Carbon Monoxide CO Sensor (5–5000 ppm)   | 1   | Farnell     | 4733254       | $4.78       |
| SEN0565            | DFRobot      | Fermion: MEMS Methane CH₄ Sensor (1–10000 ppm)         | 1   | Farnell     | 4733255       | $4.78       |
| SEN0566            | DFRobot      | Fermion: MEMS VOC Sensor (1–500 ppm)                   | 1   | Farnell     | 4733256       | $4.78       |
| SEN0567            | DFRobot      | Fermion: MEMS Ammonia NH₃ Sensor (1–300 ppm)           | 1   | Farnell     | 4733257       | $7.31       |
| SEN0568            | DFRobot      | Fermion: MEMS Hydrogen Sulfide H₂S Sensor (0.5–50 ppm) | 1   | Farnell     | 4733258       | $7.31       |
| SEN0569            | DFRobot      | Fermion: MEMS Ethanol EtOH Sensor (1–500 ppm)          | 1   | Mouser      | 426-SEN0569   | $4.90       |
| SEN0570            | DFRobot      | Fermion: MEMS Smoke Sensor (10–1000 ppm)               | 1   | Farnell     | 4733259       | $4.78       |
| SEN0571            | DFRobot      | Fermion: MEMS Odor Sensor (0.5–50 ppm)                 | 1   | Farnell     | 4733260       | $4.78       |
| SEN0572            | DFRobot      | Fermion: MEMS Hydrogen H₂ Sensor (0.1–1000 ppm)        | 1   | Mouser      | 426-SEN0572   | $13.90      |
| SEN0574            | DFRobot      | Fermion: MEMS Nitrogen Dioxide NO₂ Sensor (0.1–10 ppm) | 1   | Farnell     | 4733261       | $7.31       |
| **Total BOM Cost** |              |                                                        |     |             |               | **$194.55** |

# E-Nose Hardware - V2 Plans
See [E-Nose Hardware - V2 Plans](V2_design.md)
