#include "BrianBLE.h"

#include <BLE2902.h>
#include <BLEDevice.h>
#include <BLESecurity.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <esp_mac.h>
#include <sys/time.h>

namespace BrianBLE {

namespace {

enum class Service : uint8_t { ESS, CUSTOM };

// One row per notifying characteristic. A row has either a 16-bit SIG UUID
// (uuid128 == nullptr) or a 128-bit custom UUID.
struct ChannelSpec {
  Channel channel;
  const char *name;
  uint8_t boardBit;
  Service service;
  uint16_t uuid16;
  const char *uuid128;
};

// Rows are in characteristic creation order, which fixes the GATT handle
// layout; keep the order when adding rows (append new ones per board).
constexpr ChannelSpec CHANNEL_SPECS[] = {
    // ADS1
    {CH4, "CH4", BOARD_STATUS_ADS1_BIT, Service::ESS, 0x2BD1, nullptr},
    {VOC, "VOC", BOARD_STATUS_ADS1_BIT, Service::ESS, 0x2BD3, nullptr},
    {HCHO, "HCHO", BOARD_STATUS_ADS1_BIT, Service::CUSTOM, 0,
     "6a135b89-f360-4f64-86fc-5a14092034b4"},
    {ODOR, "Odor", BOARD_STATUS_ADS1_BIT, Service::CUSTOM, 0,
     "4c28fcb8-d69b-404a-8668-41655d814e7f"},
    // ADS2
    {NH3, "NH3", BOARD_STATUS_ADS2_BIT, Service::ESS, 0x2BCF, nullptr},
    {NO2, "NO2", BOARD_STATUS_ADS2_BIT, Service::ESS, 0x2BD2, nullptr},
    {ETOH, "EtOH", BOARD_STATUS_ADS2_BIT, Service::CUSTOM, 0,
     "f8156843-6d98-4ba2-8014-1cf03d7dedb8"},
    {H2S, "H2S", BOARD_STATUS_ADS2_BIT, Service::CUSTOM, 0,
     "87dc71bd-29a4-4218-a2a7-83fd2a69cc40"},
    // ADS3
    {CO, "CO", BOARD_STATUS_ADS3_BIT, Service::CUSTOM, 0,
     "88f6fa6c-c4e0-4a3d-ba72-f435641251c4"},
    {SMOKE, "Smoke", BOARD_STATUS_ADS3_BIT, Service::CUSTOM, 0,
     "cafb955e-6e7b-424b-9e03-6d8d003aa286"},
    {H2, "H2", BOARD_STATUS_ADS3_BIT, Service::CUSTOM, 0,
     "0176655b-0007-4e02-abc1-e9f2d6815f46"},
    // BME680
    {TEMPERATURE, "Temperature", BOARD_STATUS_BME680_BIT, Service::ESS, 0x2A6E,
     nullptr},
    {PRESSURE, "Pressure", BOARD_STATUS_BME680_BIT, Service::ESS, 0x2A6D,
     nullptr},
    {HUMIDITY, "Humidity", BOARD_STATUS_BME680_BIT, Service::ESS, 0x2A6F,
     nullptr},
    {ALTITUDE, "Altitude", BOARD_STATUS_BME680_BIT, Service::ESS, 0x2A69,
     nullptr},
    {GAS_RESISTANCE, "Gas", BOARD_STATUS_BME680_BIT, Service::CUSTOM, 0,
     "5b0e3c0b-1a44-4b76-82ee-8c2adc2dd8e9"},
};
static_assert(sizeof(CHANNEL_SPECS) / sizeof(CHANNEL_SPECS[0]) == CHANNEL_COUNT,
              "every Channel needs exactly one row in CHANNEL_SPECS");

const char *const BOARD_NAMES[] = {"ADS1 (0x48)", "ADS2 (0x49)", "ADS3 (0x4A)",
                                   "BME680"};

BLEServer *pServer = NULL;
BLESecurity *pSecurity = NULL;
BLECharacteristic *characteristics[CHANNEL_COUNT] = {};
BLECharacteristic *timeSyncCharacteristic = NULL;
BLECharacteristic *boardStatusCharacteristic = NULL;

char name[24] = "";
volatile bool deviceConnected = false;
bool oldDeviceConnected = false;

// Helper function to add BLE2902 descriptor for notifications
void setupCCCDDescriptor(BLECharacteristic *characteristic) {
#if defined(CONFIG_NIMBLE_ENABLED)
  // NimBLE (Arduino-ESP32 3.x on e.g. the ESP32-C6) adds the CCCD itself for
  // every NOTIFY characteristic.
  (void)characteristic;
#else
  // Add the Client Characteristic Configuration Descriptor (CCCD)
  BLE2902 *descriptor = new BLE2902();
  characteristic->addDescriptor(descriptor);
#endif
}

// Callback class for handling time synchronization writes
class TimeSyncCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *pCharacteristic) {
    uint8_t *data = pCharacteristic->getData();
    size_t length = pCharacteristic->getLength();

    if (length == 8) // Expecting 8 bytes for uint64_t Unix timestamp
    {
      // Extract Unix timestamp (seconds since epoch)
      uint64_t timestamp;
      memcpy(&timestamp, data, 8);

      // Set system time
      struct timeval tv;
      tv.tv_sec = timestamp;
      tv.tv_usec = 0;
      settimeofday(&tv, NULL);

      // Print confirmation
      Serial.print("RTC synchronized to: ");
      Serial.println((uint64_t)timestamp);

      // Print human-readable time
      time_t now = timestamp;
      struct tm timeinfo;
      localtime_r(&now, &timeinfo);
      char strftime_buf[64];
      strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
      Serial.print("Local time: ");
      Serial.println(strftime_buf);
    } else if (length == 4) // Alternative: 4 bytes for uint32_t
    {
      uint32_t timestamp;
      memcpy(&timestamp, data, 4);

      struct timeval tv;
      tv.tv_sec = timestamp;
      tv.tv_usec = 0;
      settimeofday(&tv, NULL);

      Serial.print("RTC synchronized to: ");
      Serial.println(timestamp);
    } else {
      Serial.println("Invalid time sync data length");
    }
  }
};

class ServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer *pServer) { deviceConnected = true; };

  void onDisconnect(BLEServer *pServer) { deviceConnected = false; }
};

} // namespace

const char *channelName(Channel channel) {
  for (const ChannelSpec &spec : CHANNEL_SPECS) {
    if (spec.channel == channel) {
      return spec.name;
    }
  }
  return "?";
}

uint8_t boardBit(Channel channel) {
  for (const ChannelSpec &spec : CHANNEL_SPECS) {
    if (spec.channel == channel) {
      return spec.boardBit;
    }
  }
  return 0;
}

void begin(const char *namePrefix, uint8_t boardStatus) {
  // Create the BLE Device with a unique name from the BT MAC
  uint8_t bluetoothMac[6];
  esp_read_mac(bluetoothMac, ESP_MAC_BT);
  snprintf(name, sizeof(name), "%s%02X%02X%02X", namePrefix, bluetoothMac[3],
           bluetoothMac[4], bluetoothMac[5]);
  BLEDevice::init(name);
  // this is for increasing the MTU size - default is 23 bytes, we can set it up
  // to 517 bytes
  BLEDevice::setMTU(517);

  // Require an encrypted (bonded) link before allowing time sync writes.
  // "Just Works" pairing provides encryption without requiring a PIN/passkey.
  pSecurity = new BLESecurity();
  pSecurity->setAuthenticationMode(ESP_LE_AUTH_REQ_SC_BOND);
  pSecurity->setCapability(ESP_IO_CAP_NONE);
  pSecurity->setInitEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);

  // Create the BLE Server
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new ServerCallbacks());

  BLEService *essService =
      pServer->createService(BLEUUID(ESS_SERVICE_UUID16), ESS_SERVICE_HANDLES);
  BLEService *customService = pServer->createService(
      BLEUUID(CUSTOM_SERVICE_UUID), CUSTOM_SERVICE_HANDLES);

  // Sensor characteristics: only for boards that are present
  for (uint8_t bit = 0; bit <= BOARD_STATUS_BME680_BIT; bit++) {
    Serial.print(BOARD_NAMES[bit]);
    Serial.println((boardStatus & (1 << bit))
                       ? " present - creating characteristics"
                       : " NOT present - skipping characteristics");
  }
  for (const ChannelSpec &spec : CHANNEL_SPECS) {
    if (!(boardStatus & (1 << spec.boardBit))) {
      continue;
    }
    BLEService *service =
        spec.service == Service::ESS ? essService : customService;
    BLEUUID uuid = spec.uuid128 ? BLEUUID(spec.uuid128) : BLEUUID(spec.uuid16);
    BLECharacteristic *characteristic = service->createCharacteristic(
        uuid,
        BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
    setupCCCDDescriptor(characteristic);
    characteristics[spec.channel] = characteristic;
  }

  // Create time synchronization characteristic (always available)
  // Custom UUID for time sync - client writes Unix timestamp to set RTC.
  // Requires an encrypted (bonded) link so only authenticated clients can
  // change the system time.
  timeSyncCharacteristic = customService->createCharacteristic(
      BLEUUID(TIME_SYNC_CHARACTERISTIC_UUID),
#if defined(CONFIG_NIMBLE_ENABLED)
      // NimBLE ignores setAccessPermissions(); encryption is a property there.
      BLECharacteristic::PROPERTY_WRITE |
          BLECharacteristic::PROPERTY_WRITE_ENC);
#else
      BLECharacteristic::PROPERTY_WRITE);
  timeSyncCharacteristic->setAccessPermissions(ESP_GATT_PERM_WRITE_ENCRYPTED);
#endif
  timeSyncCharacteristic->setCallbacks(new TimeSyncCallbacks());

  // Board status: always created, regardless of what was detected, so a
  // client can distinguish "board missing" from "characteristic not
  // discovered". Captured once here; never updated afterwards.
  boardStatusCharacteristic = customService->createCharacteristic(
      BLEUUID(BOARD_STATUS_CHARACTERISTIC_UUID),
      BLECharacteristic::PROPERTY_READ);
  boardStatusCharacteristic->setValue(&boardStatus, 1);

  // we are starting both services
  essService->start();
  customService->start();

  // Start advertising
  // we are advertising both services
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(BLEUUID(ESS_SERVICE_UUID16));
  pAdvertising->addServiceUUID(CUSTOM_SERVICE_UUID);

  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(
      0x0); // set value to 0x00 to not advertise this parameter
  BLEDevice::startAdvertising();
  Serial.print("Advertising as ");
  Serial.println(name);
  Serial.println("Waiting a client connection to notify...");
}

const char *deviceName() { return name; }

bool connected() { return deviceConnected; }

void notify(Channel channel, float value) {
  if (channel >= CHANNEL_COUNT || characteristics[channel] == NULL) {
    return;
  }
  // Send the value as a 4-byte float over BLE
  characteristics[channel]->setValue((uint8_t *)&value, sizeof(value));
  characteristics[channel]->notify();
}

void handleConnectionChanges() {
  // disconnecting
  if (!deviceConnected && oldDeviceConnected) {
    delay(500); // give the bluetooth stack the chance to get things ready
    pServer->startAdvertising(); // restart advertising
    Serial.println("start advertising");
    oldDeviceConnected = deviceConnected;
  }
  // connecting
  if (deviceConnected && !oldDeviceConnected) {
    oldDeviceConnected = deviceConnected;
  }
}

void disconnect() {
  if (pServer != NULL && deviceConnected) {
    pServer->disconnect(pServer->getConnId());
  }
}

} // namespace BrianBLE
