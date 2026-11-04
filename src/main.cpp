/*
    Video: https://www.youtube.com/watch?v=oCMOYS71NIU
    Based on Neil Kolban example for IDF:
   https://github.com/nkolban/esp32-snippets/blob/master/cpp_utils/tests/BLE%20Tests/SampleNotify.cpp
    Ported to Arduino ESP32 by Evandro Copercini
    updated by chegewara

   Create a BLE server that, once we receive a connection, will send periodic
   notifications. The service advertises itself as:
   4fafc201-1fb5-459e-8fcc-c5c9c331914b And has a characteristic of:
   beb5483e-36e1-4688-b7f5-ea07361b26a8

   The design of creating the BLE server is:
   1. Create a BLE Server
   2. Create a BLE Service
   3. Create a BLE Characteristic on the Service
   4. Create a BLE Descriptor on the characteristic
   5. Start the service.
   6. Start advertising.

   A connect handler associated with the server starts a background task that
   performs notification every couple of seconds.
*/
#include <Arduino.h>
#include <BLE2902.h>
#include <BLEDevice.h>
#include <BLESecurity.h>
#include <BLEServer.h>
#include <BLEUtils.h>
// #include <BLE2901.h>

#include <Adafruit_ADS1X15.h>
#include <Adafruit_BME680.h>
#include <Adafruit_LC709203F.h>
#include <SD.h>
#include <SPI.h>
#include <Wire.h>
#include <esp_mac.h>
#include <string>
#include <sys/time.h>

BLEServer *pServer = NULL;
BLESecurity *pSecurity = NULL;
// BLE2901 *descriptor_2901 = NULL;

bool deviceConnected = false;
bool oldDeviceConnected = false;
// See the following for generating UUIDs:
// https://www.uuidgenerator.net/

// Here we are creating a service for the Environmental Sensing Service (ESS) –
// standardized ESS UUID:
// https://www.bluetooth.com/wp-content/uploads/Files/Specification/HTML/Assigned_Numbers/out/en/Assigned_Numbers.pdf
#define SERVICE_UUID (BLEUUID((uint16_t)0x181A))
#define BATTERY_SERVICE_UUID (BLEUUID((uint16_t)0x180F))
// Here we are creating a custom service for sensors that do not have a
// standardised service
#define CUSTOM_SERVICE_UUID                                                    \
  "de664a17-7db4-449f-97ba-5514e19a9d94" // custom service

// create characteristics for each gas sensor
// Board 1 (ADS1) sensors
BLECharacteristic *ch4Characteristic = NULL;
BLECharacteristic *vocCharacteristic = NULL;
BLECharacteristic *hchoCharacteristic = NULL;
BLECharacteristic *odorCharacteristic = NULL;

// Board 2 (ADS2) sensors
BLECharacteristic *nh3Characteristic = NULL;
BLECharacteristic *no2Characteristic = NULL;
BLECharacteristic *etohCharacteristic = NULL;
BLECharacteristic *h2sCharacteristic = NULL;

// Board 3 (ADS3) sensors
BLECharacteristic *coCharacteristic = NULL;
BLECharacteristic *smokeCharacteristic = NULL;
BLECharacteristic *h2Characteristic = NULL;

// BME680 environmental sensor characteristics
BLECharacteristic *temperatureCharacteristic = NULL;
BLECharacteristic *pressureCharacteristic = NULL;
BLECharacteristic *humidityCharacteristic = NULL;
BLECharacteristic *gasCharacteristic = NULL;
BLECharacteristic *altitudeCharacteristic = NULL;

// Battery level characteristic
BLECharacteristic *batteryLevelCharacteristic = NULL;

// Time synchronization characteristic
BLECharacteristic *timeSyncCharacteristic = NULL;

// Fixed binary sample frames. Values are little-endian float32 values.
#define GAS_FRAME_CHARACTERISTIC_UUID \
  "9f4c8f2b-5e0a-4b8e-9f0d-6d9a1c2e7b34"
#define ENV_FRAME_CHARACTERISTIC_UUID \
  "c2d7a8e1-7f34-4d6b-9a02-1b5e8c4f6d70"
BLECharacteristic *gasFrameCharacteristic = NULL;
BLECharacteristic *envFrameCharacteristic = NULL;

constexpr uint8_t FRAME_VERSION = 1;
constexpr size_t GAS_FRAME_LENGTH = 58;
constexpr size_t ENV_FRAME_LENGTH = 29;
constexpr uint8_t GAS_SLOT_COUNT = 12;
constexpr uint8_t ENV_VALUE_COUNT = 5;

// Gas slots: ADS board * 4 + ADC channel. Slot 11 (ADS3 A3) is unused.
enum GasSlot : uint8_t {
  GAS_HCHO = 0,
  GAS_CH4 = 1,
  GAS_VOC = 2,
  GAS_ODOR = 3,
  GAS_ETOH = 4,
  GAS_H2S = 5,
  GAS_NO2 = 6,
  GAS_NH3 = 7,
  GAS_CO = 8,
  GAS_SMOKE = 9,
  GAS_H2 = 10
};

uint16_t sampleCycle = 0;
int lastBatteryPercent = -1;
bool gasMtuWarningLogged = false;
bool envMtuWarningLogged = false;

void writeFrameHeader(uint8_t *frame, uint8_t flags, uint16_t cycle,
                      uint32_t timestamp) {
  frame[0] = FRAME_VERSION;
  frame[1] = flags;
  memcpy(frame + 2, &cycle, sizeof(cycle));
  memcpy(frame + 4, &timestamp, sizeof(timestamp));
}

void writeFloat(uint8_t *frame, size_t offset, float value) {
  memcpy(frame + offset, &value, sizeof(value));
}

void notifyFrameIfFits(BLECharacteristic *characteristic, uint8_t *frame,
                       size_t frameLength, bool *warningLogged) {
  if (characteristic == NULL || pServer == NULL) {
    return;
  }

  for (const auto &peer : pServer->getPeerDevices(false)) {
    const uint16_t payloadLength = peer.second.mtu - 3;
    if (frameLength > payloadLength) {
      if (!*warningLogged) {
        Serial.print("WARNING: frame needs ");
        Serial.print(frameLength + 3);
        Serial.print(" byte MTU, peer negotiated ");
        Serial.println(peer.second.mtu);
        *warningLogged = true;
      }
      return;
    }
  }

  characteristic->setValue(frame, frameLength);
  characteristic->notify();
}

// Board status characteristic: reports which I2C boards were detected at
// boot, independent of whether any per-sensor characteristics exist for them.
// Always created (unlike the sensor characteristics above), so clients can
// show hardware health even for a board that is entirely missing.
#define BOARD_STATUS_CHARACTERISTIC_UUID "407fd299-d6ed-45ed-ab21-437f101c8acd"
BLECharacteristic *boardStatusCharacteristic = NULL;

// Bit layout of the board status byte. Status is captured once at boot
// (matches initMEMS()/initBME680()) and never updated afterwards.
constexpr uint8_t BOARD_STATUS_ADS1_BIT = 0;   // 0x48: HCHO, CH4, VOC, Odor
constexpr uint8_t BOARD_STATUS_ADS2_BIT = 1;   // 0x49: EtOH, H2S, NO2, NH3
constexpr uint8_t BOARD_STATUS_ADS3_BIT = 2;   // 0x4A: CO, Smoke, H2
constexpr uint8_t BOARD_STATUS_BME680_BIT = 3; // Environmental sensor

// Board structure to hold information about each ADS1115 board.
// The MEMS breakouts carry ADS1115 (16-bit) chips; using the ADS1115 driver
// preserves the full 16-bit resolution (the ADS1015 driver would right-shift
// every reading by 4 bits, giving only 12-bit resolution).
struct Board {
  Adafruit_ADS1115 ads;
  uint8_t i2c_address;
  bool present;
};

constexpr size_t boardCount = 3;
constexpr uint8_t boardAds1 = 0;
constexpr uint8_t boardAds2 = 1;
constexpr uint8_t boardAds3 = 2;

// ADS1115 gain settings per sensor.
// TODO: expose these as configurable settings from the web interface (see #7).
constexpr adsGain_t GAIN_BOARD1_DEFAULT = GAIN_ONE; // CH4, HCHO, Odor
constexpr adsGain_t GAIN_VOC = GAIN_FOUR;           // VOC (board 1, channel 2)
constexpr adsGain_t GAIN_BOARD2_DEFAULT = GAIN_ONE; // EtOH, H2S, NO2
constexpr adsGain_t GAIN_NH3 = GAIN_SIXTEEN;        // NH3 (board 2, channel 3)
constexpr adsGain_t GAIN_BOARD3_DEFAULT = GAIN_ONE; // CO, Smoke, H2

// Array of boards
Board boards[boardCount] = {{Adafruit_ADS1115(), 0x48, false},
                            {Adafruit_ADS1115(), 0x49, false},
                            {Adafruit_ADS1115(), 0x4A, false}};

// Function to get a board by number
Board *getBoard(uint8_t board_num) {
  if (board_num < boardCount) {
    return &boards[board_num];
  }
  return NULL;
}

bool boardAvailable(uint8_t board_num) {
  return getBoard(board_num)->present;
}

// BME680 environmental sensor
Adafruit_BME680 bme680;
bool bme680_present = false;

// Battery fuel gauge
Adafruit_LC709203F batteryMonitor;
bool batteryMonitorPresent = false;

// Initialize BME680 sensor and detect if present
void initBME680() {
  if (!bme680.begin(0x77)) // Default I2C address
  {
    if (!bme680.begin(0x76)) // Alternative I2C address
    {
      Serial.println("BME680 not found!");
      return;
    }
  }

  bme680_present = true;
  Serial.println("Found BME680 sensor");

  // Set up oversampling and filter initialization
  bme680.setTemperatureOversampling(BME680_OS_8X);
  bme680.setHumidityOversampling(BME680_OS_4X);
  bme680.setPressureOversampling(BME680_OS_2X);
  bme680.setIIRFilterSize(BME680_FILTER_SIZE_3);
  bme680.setGasHeater(320, 150); // 320°C for 150 ms
}

// Here is a function to initialize the MEMS sensor and detect which boards are
// present
void initMEMS() {
  for (size_t i = 0; i < boardCount; i++) {
    if (boards[i].ads.begin(boards[i].i2c_address)) {
      boards[i].present = true;
      // Set the data rate explicitly. 128 SPS (the ADS1115 default) gives ~7.8
      // ms per conversion; a full sweep of 11 channels then takes well under
      // 100 ms, comfortably inside the 5 s notify cycle. Setting it here also
      // documents the choice rather than relying on the library default.
      boards[i].ads.setDataRate(RATE_ADS1115_128SPS);
      Serial.print("Found ADS1115 at 0x");
      Serial.println(boards[i].i2c_address, HEX);
    } else {
      Serial.print("ADS1115 not found at 0x");
      Serial.println(boards[i].i2c_address, HEX);
    }
  }

  // Check if at least one board is present
  bool any_present = false;
  for (size_t i = 0; i < boardCount; i++) {
    if (boards[i].present) {
      any_present = true;
      break;
    }
  }

  if (!any_present) {
    Serial.println("ERROR: No ADS1115 boards found!");
  }
}

// Read one single-ended channel at a specific gain and return the voltage.
//
// Adafruit_ADS1X15::computeVolts() converts a raw count using the gain that is
// *currently* set on the object, so the voltage must be computed with the same
// gain that was used for the reading, BEFORE the default gain is restored.
// Doing the set/read/convert/restore in one place keeps this correct as more
// per-channel gains are added (see #17, #7).
//
// The board's default gain is restored on exit so callers that read at a
// non-default gain don't leave the board in an unexpected state. If the raw
// count is at (or beyond) full scale the input exceeds the selected range and
// the reported voltage is clipped, so a warning is logged.
float readChannelVolts(Board *board, uint8_t channel, adsGain_t gain,
                       adsGain_t defaultGain, const char *label) {
  board->ads.setGain(gain);
  int16_t raw = board->ads.readADC_SingleEnded(channel);
  // Convert while the reading's gain is still set.
  float volts = board->ads.computeVolts(raw);
  board->ads.setGain(defaultGain);

  // Single-ended readings span 0..32767 counts on the ADS1115. A count pinned
  // at the positive full-scale limit means the input voltage is above the
  // selected range and the value is silently clipped.
  if (raw >= 32767) {
    Serial.print("WARNING: ");
    Serial.print(label);
    Serial.println(
        " reading at full scale - value clipped, reduce gain / range");
  }
  return volts;
}

// Helper function to add BLE2902 descriptor for notifications
void setupCCCDDescriptor(BLECharacteristic *characteristic) {
  // Add the Client Characteristic Configuration Descriptor (CCCD)
  BLE2902 *descriptor = new BLE2902();
  // Set read and write permissions on the descriptor to allow CCCD negotiation
  // descriptor->setAccessPermissions(ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE);
  characteristic->addDescriptor(descriptor);
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

class MyServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer *pServer) { deviceConnected = true; };

  void onDisconnect(BLEServer *pServer) { deviceConnected = false; }
};

void setup() {
  Serial.begin(115200);

  // The ADS1115/BME680 probes need the I2C bus to be initialized first.
  Wire.begin();
  Wire.setClock(400000);

  Serial.println("I2C scanner");

  for (uint8_t address = 1; address < 127; address++) {
    Wire.beginTransmission(address);
    uint8_t error = Wire.endTransmission();

    if (error == 0) {
      Serial.printf("Found device at 0x%02X\n", address);
    }
  }

  initMEMS();
  initBME680();

  if (batteryMonitor.begin()) {
    batteryMonitorPresent = true;
    Serial.println("Found LC709203F battery monitor");
    batteryMonitor.setAlarmVoltage(3.8f);
    // Pack size is configured by the part being used. The 4400 mAh pack is
    // closest to the 3000 mAh profile supported by the LC709203F library.
    batteryMonitor.setPackSize(LC709203F_APA_3000MAH);
  } else {
    Serial.println("LC709203F not found or no battery connected");
  }

  // Create the BLE Device
  uint8_t bluetoothMac[6];
  esp_read_mac(bluetoothMac, ESP_MAC_BT);
  char deviceName[16];
  snprintf(deviceName, sizeof(deviceName), "Brian-%02X%02X%02X",
           bluetoothMac[3], bluetoothMac[4], bluetoothMac[5]);
  BLEDevice::init(deviceName);
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
  pServer->setCallbacks(new MyServerCallbacks());

  // Create BLE services with enough handles for all characteristics + CCCDs.
  // Default is 15 handles, which is too small for this profile and can cause
  // later characteristics to miss descriptors (e.g. missing CCCD).
  BLEService *essService = pServer->createService(SERVICE_UUID, 40);
  BLEService *batteryService = pServer->createService(BATTERY_SERVICE_UUID, 20);
  BLEService *customService =
      pServer->createService(BLEUUID(CUSTOM_SERVICE_UUID), 50);
  // Create characteristics

  batteryLevelCharacteristic = batteryService->createCharacteristic(
      BLEUUID((uint16_t)0x2A19),
      BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
  setupCCCDDescriptor(batteryLevelCharacteristic);

  // these are for ESS standardised characteristics
  // Taken partially from
  // https://www.bluetooth.com/specifications/assigned-numbers/
  if (boardAvailable(boardAds1)) {
    ch4Characteristic = essService->createCharacteristic(
        BLEUUID((uint16_t)0x2BD1),
        BLECharacteristic::PROPERTY_READ);

    vocCharacteristic = essService->createCharacteristic(
        BLEUUID((uint16_t)0x2BD3),
        BLECharacteristic::PROPERTY_READ);

    // these are for custom characteristics, generated at
    // https://www.uuidgenerator.net/guid (ADS1 sensors)
    hchoCharacteristic = customService->createCharacteristic(
        BLEUUID("6a135b89-f360-4f64-86fc-5a14092034b4"),
        BLECharacteristic::PROPERTY_READ);

    odorCharacteristic = customService->createCharacteristic(
        BLEUUID("4c28fcb8-d69b-404a-8668-41655d814e7f"),
        BLECharacteristic::PROPERTY_READ);
  }

  if (boardAvailable(boardAds2)) {

    nh3Characteristic = essService->createCharacteristic(
        BLEUUID((uint16_t)0x2BCF),
        BLECharacteristic::PROPERTY_READ);

    no2Characteristic = essService->createCharacteristic( // Fixed name
        BLEUUID((uint16_t)0x2BD2),
        BLECharacteristic::PROPERTY_READ);

    etohCharacteristic = customService->createCharacteristic(
        BLEUUID("f8156843-6d98-4ba2-8014-1cf03d7dedb8"),
        BLECharacteristic::PROPERTY_READ);

    h2sCharacteristic = customService->createCharacteristic(
        BLEUUID("87dc71bd-29a4-4218-a2a7-83fd2a69cc40"),
        BLECharacteristic::PROPERTY_READ);
  }

  Serial.print("BoardAds3 (0x4A) present flag: ");
  Serial.println(boardAvailable(boardAds3) ? "TRUE" : "FALSE");

  if (boardAvailable(boardAds3)) {
    Serial.println("Creating CO, Smoke, H2 characteristics...");
    coCharacteristic = customService->createCharacteristic(
        BLEUUID("88f6fa6c-c4e0-4a3d-ba72-f435641251c4"),
        BLECharacteristic::PROPERTY_READ);
    Serial.println("  Created CO characteristic");

    smokeCharacteristic = customService->createCharacteristic(
        BLEUUID("cafb955e-6e7b-424b-9e03-6d8d003aa286"),
        BLECharacteristic::PROPERTY_READ);
    Serial.println("  Created Smoke characteristic");

    h2Characteristic = customService->createCharacteristic(
        BLEUUID("0176655b-0007-4e02-abc1-e9f2d6815f46"),
        BLECharacteristic::PROPERTY_READ);
    Serial.println("  Created H2 characteristic");
  } else {
    Serial.println(
        "BoardAds3 NOT present - skipping CO, Smoke, H2 characteristics");
  }

  // Create characteristics for BME680 environmental sensor if present
  if (bme680_present) {
    // Temperature (ESS standard UUID 0x2A6E)
    temperatureCharacteristic = essService->createCharacteristic(
        BLEUUID((uint16_t)0x2A6E),
        BLECharacteristic::PROPERTY_READ);
    Serial.println("Created Temperature characteristic with NOTIFY property");

    // Pressure (ESS standard UUID 0x2A6D)
    pressureCharacteristic = essService->createCharacteristic(
        BLEUUID((uint16_t)0x2A6D),
        BLECharacteristic::PROPERTY_READ);
    Serial.println("Created Pressure characteristic with NOTIFY property");

    // Humidity (ESS standard UUID 0x2A6F)
    humidityCharacteristic = essService->createCharacteristic(
        BLEUUID((uint16_t)0x2A6F),
        BLECharacteristic::PROPERTY_READ);

    // Altitude (ESS standard UUID 0x2A69)
    altitudeCharacteristic = essService->createCharacteristic(
        BLEUUID((uint16_t)0x2A69),
        BLECharacteristic::PROPERTY_READ);

    // Gas resistance (custom UUID)
    gasCharacteristic = customService->createCharacteristic(
        BLEUUID("5b0e3c0b-1a44-4b76-82ee-8c2adc2dd8e9"),
        BLECharacteristic::PROPERTY_READ);
  }

  // Create time synchronization characteristic (always available)
  // Custom UUID for time sync - client writes Unix timestamp to set RTC.
  // Requires an encrypted (bonded) link so only authenticated clients can
  // change the system time.
  timeSyncCharacteristic = customService->createCharacteristic(
      BLEUUID("a1b2c3d4-e5f6-4a5b-8c9d-0e1f2a3b4c5d"),
      BLECharacteristic::PROPERTY_WRITE);
  timeSyncCharacteristic->setAccessPermissions(ESP_GATT_PERM_WRITE_ENCRYPTED);
  timeSyncCharacteristic->setCallbacks(new TimeSyncCallbacks());

  // Gas and environmental samples use fixed-size binary notifications.
  gasFrameCharacteristic = customService->createCharacteristic(
      BLEUUID(GAS_FRAME_CHARACTERISTIC_UUID),
      BLECharacteristic::PROPERTY_NOTIFY);
  setupCCCDDescriptor(gasFrameCharacteristic);
  envFrameCharacteristic = customService->createCharacteristic(
      BLEUUID(ENV_FRAME_CHARACTERISTIC_UUID),
      BLECharacteristic::PROPERTY_NOTIFY);
  setupCCCDDescriptor(envFrameCharacteristic);
  Serial.println("Gas frame requires a negotiated MTU of at least 61 bytes");

  // we are starting all services
  // Board status: always created, regardless of what was detected, so a
  // client can distinguish "board missing" from "characteristic not
  // discovered". Captured once here; never updated in loop().
  uint8_t boardStatus = 0;
  if (boardAvailable(boardAds1)) {
    boardStatus |= (1 << BOARD_STATUS_ADS1_BIT);
  }
  if (boardAvailable(boardAds2)) {
    boardStatus |= (1 << BOARD_STATUS_ADS2_BIT);
  }
  if (boardAvailable(boardAds3)) {
    boardStatus |= (1 << BOARD_STATUS_ADS3_BIT);
  }
  if (bme680_present) {
    boardStatus |= (1 << BOARD_STATUS_BME680_BIT);
  }
  boardStatusCharacteristic = customService->createCharacteristic(
      BLEUUID(BOARD_STATUS_CHARACTERISTIC_UUID),
      BLECharacteristic::PROPERTY_READ);
  boardStatusCharacteristic->setValue(&boardStatus, 1);

  // we are starting both services
  essService->start();
  batteryService->start();
  customService->start();

  // Start advertising
  // we are advertising all services
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->addServiceUUID(BATTERY_SERVICE_UUID);
  pAdvertising->addServiceUUID(CUSTOM_SERVICE_UUID);

  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(
      0x0); // set value to 0x00 to not advertise this parameter
  BLEDevice::startAdvertising();
  Serial.println("Waiting a client connection to notify...");
}

void loop() {
  if (deviceConnected) {
    float gasValues[GAS_SLOT_COUNT];
    for (uint8_t i = 0; i < GAS_SLOT_COUNT; i++) {
      gasValues[i] = NAN;
    }
    uint16_t gasMask = 0;
    float envValues[ENV_VALUE_COUNT] = {NAN, NAN, NAN, NAN, NAN};
    uint8_t envMask = 0;

    // Read from board 0 (ADS1) sensors if present
    if (boardAvailable(boardAds1)) {
      Board *board = getBoard(boardAds1);

      // Formaldehyde sensor
      float hchoVolt = readChannelVolts(board, 0, GAIN_BOARD1_DEFAULT,
                                        GAIN_BOARD1_DEFAULT, "HCHO");
      if (hchoCharacteristic != NULL) {
        hchoCharacteristic->setValue((uint8_t *)&hchoVolt, sizeof(hchoVolt));
      }
      gasValues[GAS_HCHO] = hchoVolt;
      gasMask |= (1 << GAS_HCHO);

      // CH4 sensor
      float ch4Volt = readChannelVolts(board, 1, GAIN_BOARD1_DEFAULT,
                                       GAIN_BOARD1_DEFAULT, "CH4");
      if (ch4Characteristic != NULL) {
        ch4Characteristic->setValue((uint8_t *)&ch4Volt, sizeof(ch4Volt));
      }
      gasValues[GAS_CH4] = ch4Volt;
      gasMask |= (1 << GAS_CH4);

      // VOC sensor (read at a higher gain for its lower output range)
      float vocVolt =
          readChannelVolts(board, 2, GAIN_VOC, GAIN_BOARD1_DEFAULT, "VOC");
      if (vocCharacteristic != NULL) {
        vocCharacteristic->setValue((uint8_t *)&vocVolt, sizeof(vocVolt));
      }
      gasValues[GAS_VOC] = vocVolt;
      gasMask |= (1 << GAS_VOC);

      // Odor sensor
      float odorVolt = readChannelVolts(board, 3, GAIN_BOARD1_DEFAULT,
                                        GAIN_BOARD1_DEFAULT, "Odor");
      if (odorCharacteristic != NULL) {
        odorCharacteristic->setValue((uint8_t *)&odorVolt, sizeof(odorVolt));
      }
      gasValues[GAS_ODOR] = odorVolt;
      gasMask |= (1 << GAS_ODOR);
    }

    // Read from board 1 (ADS2) sensors if present
    if (boardAvailable(boardAds2)) {
      Board *board = getBoard(boardAds2);

      // Ethanol sensor
      float etohVolt = readChannelVolts(board, 0, GAIN_BOARD2_DEFAULT,
                                        GAIN_BOARD2_DEFAULT, "EtOH");
      if (etohCharacteristic != NULL) {
        etohCharacteristic->setValue((uint8_t *)&etohVolt, sizeof(etohVolt));
      }
      gasValues[GAS_ETOH] = etohVolt;
      gasMask |= (1 << GAS_ETOH);

      // H2S sensor
      float h2sVolt = readChannelVolts(board, 1, GAIN_BOARD2_DEFAULT,
                                       GAIN_BOARD2_DEFAULT, "H2S");
      if (h2sCharacteristic != NULL) {
        h2sCharacteristic->setValue((uint8_t *)&h2sVolt, sizeof(h2sVolt));
      }
      gasValues[GAS_H2S] = h2sVolt;
      gasMask |= (1 << GAS_H2S);

      // NO2 sensor
      float no2Volt = readChannelVolts(board, 2, GAIN_BOARD2_DEFAULT,
                                       GAIN_BOARD2_DEFAULT, "NO2");
      if (no2Characteristic != NULL) {
        no2Characteristic->setValue((uint8_t *)&no2Volt, sizeof(no2Volt));
      }
      gasValues[GAS_NO2] = no2Volt;
      gasMask |= (1 << GAS_NO2);

      // NH3 sensor (read at a higher gain for its lower output range)
      float nh3Volt =
          readChannelVolts(board, 3, GAIN_NH3, GAIN_BOARD2_DEFAULT, "NH3");
      if (nh3Characteristic != NULL) {
        nh3Characteristic->setValue((uint8_t *)&nh3Volt, sizeof(nh3Volt));
      }
      gasValues[GAS_NH3] = nh3Volt;
      gasMask |= (1 << GAS_NH3);
    }

    // Read from board 2 (ADS3) sensors if present
    if (boardAvailable(boardAds3)) {
      Board *board = getBoard(boardAds3);

      // CO sensor
      float coVolt = readChannelVolts(board, 0, GAIN_BOARD3_DEFAULT,
                                      GAIN_BOARD3_DEFAULT, "CO");
      if (coCharacteristic != NULL) {
        coCharacteristic->setValue((uint8_t *)&coVolt, sizeof(coVolt));
      }
      gasValues[GAS_CO] = coVolt;
      gasMask |= (1 << GAS_CO);

      // Smoke sensor
      float smokeVolt = readChannelVolts(board, 1, GAIN_BOARD3_DEFAULT,
                                         GAIN_BOARD3_DEFAULT, "Smoke");
      if (smokeCharacteristic != NULL) {
        smokeCharacteristic->setValue((uint8_t *)&smokeVolt, sizeof(smokeVolt));
      }
      gasValues[GAS_SMOKE] = smokeVolt;
      gasMask |= (1 << GAS_SMOKE);

      // H2 sensor
      float h2Volt = readChannelVolts(board, 2, GAIN_BOARD3_DEFAULT,
                                      GAIN_BOARD3_DEFAULT, "H2");
      if (h2Characteristic != NULL) {
        h2Characteristic->setValue((uint8_t *)&h2Volt, sizeof(h2Volt));
      }
      gasValues[GAS_H2] = h2Volt;
      gasMask |= (1 << GAS_H2);

      Serial.print("CO:");
      Serial.print(coVolt);
      Serial.print(",Smoke:");
      Serial.print(smokeVolt);
      Serial.print(",H2:");
      Serial.println(h2Volt);
    }

    // Read battery percentage if present
    if (batteryMonitorPresent && batteryLevelCharacteristic != NULL) {
      const float batteryVoltage = batteryMonitor.cellVoltage();
      const float batteryPercentRaw = batteryMonitor.cellPercent();

      uint8_t batteryPercent =
          (uint8_t)constrain(batteryPercentRaw, 0.0f, 100.0f);

      batteryLevelCharacteristic->setValue(&batteryPercent,
                                           sizeof(batteryPercent));

      if (lastBatteryPercent != batteryPercent) {
        batteryLevelCharacteristic->notify();
        lastBatteryPercent = batteryPercent;
      }

      Serial.print("Battery voltage: ");
      Serial.print(batteryVoltage, 3);
      Serial.print(" V | Raw percent: ");
      Serial.print(batteryPercentRaw, 1);
      Serial.print("% | BLE percent: ");
      Serial.print(batteryPercent);
      Serial.println("%");
    }

    // Read from BME680 sensor if present
    if (bme680_present) {
      bool bmeReadingAvailable = false;
      float temperature = 0.0f;
      float pressure = 0.0f;
      float humidity = 0.0f;
      float gas = 0.0f;
      float altitude = 0.0f;
      if (bme680.performReading()) {
        temperature = bme680.temperature;
        pressure = bme680.pressure / 100.0f;
        humidity = bme680.humidity;
        gas = bme680.gas_resistance;
        altitude = bme680.readAltitude(1013.25);
        bmeReadingAvailable = true;
      }
      if (bmeReadingAvailable) {
        // Read temperature
        if (temperatureCharacteristic != NULL) {
          temperatureCharacteristic->setValue((uint8_t *)&temperature,
                                              sizeof(temperature));
        }
        envValues[0] = temperature;
        envMask |= (1 << 0);

        // Read pressure (convert from Pa to hPa for BLE)
        if (pressureCharacteristic != NULL) {
          pressureCharacteristic->setValue((uint8_t *)&pressure,
                                           sizeof(pressure));
        }
        envValues[1] = pressure;
        envMask |= (1 << 1);

        // Read humidity
        if (humidityCharacteristic != NULL) {
          humidityCharacteristic->setValue((uint8_t *)&humidity,
                                           sizeof(humidity));
        }
        envValues[2] = humidity;
        envMask |= (1 << 2);

        // Read gas resistance
        if (gasCharacteristic != NULL) {
          gasCharacteristic->setValue((uint8_t *)&gas, sizeof(gas));
        }
        envValues[3] = gas;
        envMask |= (1 << 3);

        // Calculate and read altitude (assuming sea level pressure of 1013.25
        // hPa)
        if (altitudeCharacteristic != NULL) {
          altitudeCharacteristic->setValue((uint8_t *)&altitude,
                                           sizeof(altitude));
        }
        envValues[4] = altitude;
        envMask |= (1 << 4);

        // Debug output
        Serial.print("Temperature:");
        Serial.print(temperature);
        Serial.print(" C, Pressure:");
        Serial.print(pressure);
        Serial.print(" hPa, Humidity:");
        Serial.print(humidity);
        Serial.print(" %, Gas:");
        Serial.print(gas);
        Serial.print(" Ohms, Altitude:");
        Serial.print(altitude);
        Serial.println(" m");
      } else {
        Serial.println("BME680 reading failed");
      }
    }

    time_t currentTime = time(NULL);
    bool timeSynced = currentTime >= 1700000000;
    uint32_t timestamp =
        timeSynced ? static_cast<uint32_t>(currentTime) : millis() / 1000;
    uint8_t frameFlags = timeSynced ? 0x01 : 0x00;
    uint16_t cycle = sampleCycle++;

    uint8_t gasFrame[GAS_FRAME_LENGTH];
    writeFrameHeader(gasFrame, frameFlags, cycle, timestamp);
    memcpy(gasFrame + 8, &gasMask, sizeof(gasMask));
    for (uint8_t i = 0; i < GAS_SLOT_COUNT; i++) {
      writeFloat(gasFrame, 10 + (i * sizeof(float)), gasValues[i]);
    }
    notifyFrameIfFits(gasFrameCharacteristic, gasFrame, sizeof(gasFrame),
                      &gasMtuWarningLogged);

    uint8_t envFrame[ENV_FRAME_LENGTH];
    writeFrameHeader(envFrame, frameFlags, cycle, timestamp);
    envFrame[8] = envMask;
    for (uint8_t i = 0; i < ENV_VALUE_COUNT; i++) {
      writeFloat(envFrame, 9 + (i * sizeof(float)), envValues[i]);
    }
    notifyFrameIfFits(envFrameCharacteristic, envFrame, sizeof(envFrame),
                      &envMtuWarningLogged);

    delay(5000);
  }
  // disconnecting
  if (!deviceConnected && oldDeviceConnected) {
    delay(500); // give the bluetooth stack the chance to get things ready
    pServer->startAdvertising(); // restart advertising
    Serial.println("start advertising");
    oldDeviceConnected = deviceConnected;
  }
  // connecting
  if (deviceConnected && !oldDeviceConnected) {
    // do stuff here on connecting
    oldDeviceConnected = deviceConnected;
  }
}