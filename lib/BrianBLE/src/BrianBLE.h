// BrianBLE: the BLE contract shared by the BRIAN V1 firmware (src/main.cpp)
// and the BLE emulator (src/emulator/). Both builds go through this library,
// so the UUIDs, service placement, handle counts, security settings and
// payload format cannot drift between them.
//
// Do not change UUIDs, the payload format, or which service a characteristic
// lives in without a coordinated change in BrianReactNative and BrianWeb (see
// CLAUDE.md -> "BLE contract").
#pragma once

#include <Arduino.h>

namespace BrianBLE {

// Services
constexpr uint16_t ESS_SERVICE_UUID16 = 0x181A; // Environmental Sensing
constexpr const char *CUSTOM_SERVICE_UUID =
    "de664a17-7db4-449f-97ba-5514e19a9d94";

// Handle counts for the two services. Default is 15 handles, which is too
// small for this profile and can cause later characteristics to miss
// descriptors (e.g. missing CCCD). Raise these when adding characteristics.
constexpr uint32_t ESS_SERVICE_HANDLES = 40;
constexpr uint32_t CUSTOM_SERVICE_HANDLES = 50;

constexpr const char *TIME_SYNC_CHARACTERISTIC_UUID =
    "a1b2c3d4-e5f6-4a5b-8c9d-0e1f2a3b4c5d";
constexpr const char *BOARD_STATUS_CHARACTERISTIC_UUID =
    "407fd299-d6ed-45ed-ab21-437f101c8acd";

// Bit layout of the board status byte. Status is captured once at boot and
// never updated afterwards. A board's bit also decides whether its sensor
// characteristics are created.
constexpr uint8_t BOARD_STATUS_ADS1_BIT = 0;   // 0x48: HCHO, CH4, VOC, Odor
constexpr uint8_t BOARD_STATUS_ADS2_BIT = 1;   // 0x49: EtOH, H2S, NO2, NH3
constexpr uint8_t BOARD_STATUS_ADS3_BIT = 2;   // 0x4A: CO, Smoke, H2
constexpr uint8_t BOARD_STATUS_BME680_BIT = 3; // Environmental sensor
constexpr uint8_t BOARD_STATUS_ALL = 0x0F;

// Every notifying sensor channel. Values are sent as a 4-byte little-endian
// IEEE-754 float32: gas channels in volts, environmental channels in the unit
// noted below.
enum Channel : uint8_t {
  // ADS1 (0x48)
  HCHO,
  CH4,
  VOC,
  ODOR,
  // ADS2 (0x49)
  ETOH,
  H2S,
  NO2,
  NH3,
  // ADS3 (0x4A)
  CO,
  SMOKE,
  H2,
  // BME680
  TEMPERATURE,    // degC
  PRESSURE,       // hPa
  HUMIDITY,       // %
  ALTITUDE,       // m (assumes 1013.25 hPa sea level)
  GAS_RESISTANCE, // Ohm
  CHANNEL_COUNT
};

// Short label for a channel, for Serial output.
const char *channelName(Channel channel);

// The board status bit of the board a channel belongs to.
uint8_t boardBit(Channel channel);

// Initialise the BLE stack, create the services and the characteristics for
// the boards set in boardStatus (bits as above), and start advertising.
// The device is named "<namePrefix><last 3 bytes of the BT MAC in hex>",
// e.g. "Brian-" -> "Brian-A1B2C3".
void begin(const char *namePrefix, uint8_t boardStatus);

// The advertised device name (valid after begin()).
const char *deviceName();

// True while a client is connected.
bool connected();

// Set a channel's value and notify. No-op if the channel's characteristic
// was not created (its board is missing).
void notify(Channel channel, float value);

// Restart advertising after a client disconnects. Call from loop().
void handleConnectionChanges();

// Drop the current client connection (used by the emulator to test client
// reconnect handling).
void disconnect();

} // namespace BrianBLE
