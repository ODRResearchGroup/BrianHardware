/*
    Video: https://www.youtube.com/watch?v=oCMOYS71NIU
    Based on Neil Kolban example for IDF: https://github.com/nkolban/esp32-snippets/blob/master/cpp_utils/tests/BLE%20Tests/SampleNotify.cpp
    Ported to Arduino ESP32 by Evandro Copercini
    updated by chegewara

   Create a BLE server that, once we receive a connection, will send periodic notifications.
   The service advertises itself as: 4fafc201-1fb5-459e-8fcc-c5c9c331914b
   And has a characteristic of: beb5483e-36e1-4688-b7f5-ea07361b26a8

   The design of creating the BLE server is:
   1. Create a BLE Server
   2. Create a BLE Service
   3. Create a BLE Characteristic on the Service
   4. Create a BLE Descriptor on the characteristic
   5. Start the service.
   6. Start advertising.

   A connect handler associated with the server starts a background task that performs notification
   every couple of seconds.
*/
#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
// #include <BLE2901.h>

#include <SPI.h>
#include <SD.h>
#include <Wire.h>
#include <Adafruit_ADS1X15.h>
#include <Adafruit_BME680.h>
#include <sys/time.h>

BLEServer *pServer = NULL;
// BLE2901 *descriptor_2901 = NULL;

bool deviceConnected = false;
bool oldDeviceConnected = false;
// See the following for generating UUIDs:
// https://www.uuidgenerator.net/

// Here we are creating a service for the Environmental Sensing Service (ESS) – standardized ESS UUID: https://www.bluetooth.com/wp-content/uploads/Files/Specification/HTML/Assigned_Numbers/out/en/Assigned_Numbers.pdf
#define SERVICE_UUID (BLEUUID((uint16_t)0x181A))
// Here we are creating a custom service for sensors that do not have a standardised service
#define CUSTOM_SERVICE_UUID "de664a17-7db4-449f-97ba-5514e19a9d94" // custom service

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

// Time synchronization characteristic
BLECharacteristic *timeSyncCharacteristic = NULL;

// Board structure to hold information about each ADS1015 board
struct Board
{
  Adafruit_ADS1015 ads;
  uint8_t i2c_address;
  bool present;
};

constexpr size_t boardCount = 3;
constexpr uint8_t boardAds1 = 0;
constexpr uint8_t boardAds2 = 1;
constexpr uint8_t boardAds3 = 2;

// Array of boards
Board boards[boardCount] = {
    {Adafruit_ADS1015(), 0x48, false},
    {Adafruit_ADS1015(), 0x49, false},
    {Adafruit_ADS1015(), 0x4A, false}};

// Function to get a board by number
Board *getBoard(uint8_t board_num)
{
  if (board_num < boardCount)
  {
    return &boards[board_num];
  }
  return NULL;
}

// BME680 environmental sensor
Adafruit_BME680 bme680;
bool bme680_present = false;

// Initialize BME680 sensor and detect if present
void initBME680()
{
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
  bme680.setIIRFilterSize(BME680_FILTER_SIZE_3);
  bme680.setGasHeater(320, 150); // 320°C for 150 ms
}

// Here is a function to initialize the MEMS sensor and detect which boards are present
void initMEMS()
{
  for (size_t i = 0; i < boardCount; i++)
  {
    if (boards[i].ads.begin(boards[i].i2c_address))
    {
      boards[i].present = true;
      Serial.print("Found ADS1015 at 0x");
      Serial.println(boards[i].i2c_address, HEX);
    }
    else
    {
      Serial.print("ADS1015 not found at 0x");
      Serial.println(boards[i].i2c_address, HEX);
    }
  }

  // Check if at least one board is present
  bool any_present = false;
  for (size_t i = 0; i < boardCount; i++)
  {
    if (boards[i].present)
    {
      any_present = true;
      break;
    }
  }

  if (!any_present)
  {
    Serial.println("ERROR: No ADS1015 boards found!");
  }
}

// Helper function to add BLE2902 descriptor for notifications
void setupCCCDDescriptor(BLECharacteristic *characteristic)
{
  // Add the Client Characteristic Configuration Descriptor (CCCD)
  BLE2902 *descriptor = new BLE2902();
  // Set read and write permissions on the descriptor to allow CCCD negotiation
  // descriptor->setAccessPermissions(ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE);
  characteristic->addDescriptor(descriptor);
}

// Callback class for handling time synchronization writes
class TimeSyncCallbacks : public BLECharacteristicCallbacks
{
  void onWrite(BLECharacteristic *pCharacteristic)
  {
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
    }
    else if (length == 4) // Alternative: 4 bytes for uint32_t
    {
      uint32_t timestamp;
      memcpy(&timestamp, data, 4);

      struct timeval tv;
      tv.tv_sec = timestamp;
      tv.tv_usec = 0;
      settimeofday(&tv, NULL);

      Serial.print("RTC synchronized to: ");
      Serial.println(timestamp);
    }
    else
    {
      Serial.println("Invalid time sync data length");
    }
  }
};

class MyServerCallbacks : public BLEServerCallbacks
{
  void onConnect(BLEServer *pServer)
  {
    deviceConnected = true;
  };

  void onDisconnect(BLEServer *pServer)
  {
    deviceConnected = false;
  }
};

void setup()
{
  Serial.begin(115200);

  initMEMS();
  initBME680();
  // Create the BLE Device
  BLEDevice::init("BRIAN");
  // this is for increasing the MTU size - default is 23 bytes, we can set it up to 517 bytes
  BLEDevice::setMTU(517);

  // Create the BLE Server
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  // Create BLE services with enough handles for all characteristics + CCCDs.
  // Default is 15 handles, which is too small for this profile and can cause
  // later characteristics to miss descriptors (e.g. missing CCCD).
  BLEService *essService = pServer->createService(SERVICE_UUID, 40);
  BLEService *customService = pServer->createService(BLEUUID(CUSTOM_SERVICE_UUID), 50);
  // Create characteristics

  // these are for ESS standardised characteristics
  // Taken partially from https://www.bluetooth.com/specifications/assigned-numbers/
  if (getBoard(boardAds1)->present)
  {
    ch4Characteristic = essService->createCharacteristic(
        BLEUUID((uint16_t)0x2BD1),
        BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
    setupCCCDDescriptor(ch4Characteristic);

    vocCharacteristic = essService->createCharacteristic(
        BLEUUID((uint16_t)0x2BD3),
        BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
    setupCCCDDescriptor(vocCharacteristic);

    // these are for custom characteristics, generated at https://www.uuidgenerator.net/guid (ADS1 sensors)
    hchoCharacteristic = customService->createCharacteristic(
        BLEUUID("6a135b89-f360-4f64-86fc-5a14092034b4"),
        BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
    setupCCCDDescriptor(hchoCharacteristic);

    odorCharacteristic = customService->createCharacteristic(
        BLEUUID("4c28fcb8-d69b-404a-8668-41655d814e7f"),
        BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
    setupCCCDDescriptor(odorCharacteristic);
  }

  if (getBoard(boardAds2)->present)
  {

    nh3Characteristic = essService->createCharacteristic(
        BLEUUID((uint16_t)0x2BCF),
        BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
    setupCCCDDescriptor(nh3Characteristic);

    no2Characteristic = essService->createCharacteristic( // Fixed name
        BLEUUID((uint16_t)0x2BD2),
        BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
    setupCCCDDescriptor(no2Characteristic);

    etohCharacteristic = customService->createCharacteristic(
        BLEUUID("f8156843-6d98-4ba2-8014-1cf03d7dedb8"),
        BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
    setupCCCDDescriptor(etohCharacteristic);

    h2sCharacteristic = customService->createCharacteristic(
        BLEUUID("87dc71bd-29a4-4218-a2a7-83fd2a69cc40"),
        BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
    setupCCCDDescriptor(h2sCharacteristic);
  }

  if (getBoard(boardAds3)->present)
  {
    coCharacteristic = customService->createCharacteristic(
        BLEUUID("88f6fa6c-c4e0-4a3d-ba72-f435641251c4"),
        BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
    setupCCCDDescriptor(coCharacteristic);

    smokeCharacteristic = customService->createCharacteristic(
        BLEUUID("cafb955e-6e7b-424b-9e03-6d8d003aa286"),
        BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
    setupCCCDDescriptor(smokeCharacteristic);

    h2Characteristic = customService->createCharacteristic(
        BLEUUID("0176655b-0007-4e02-abc1-e9f2d6815f46"),
        BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
    setupCCCDDescriptor(h2Characteristic);
  }

  // Create characteristics for BME680 environmental sensor if present
  if (bme680_present)
  {
    // Temperature (ESS standard UUID 0x2A6E)
    temperatureCharacteristic = essService->createCharacteristic(
        BLEUUID((uint16_t)0x2A6E),
        BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
    setupCCCDDescriptor(temperatureCharacteristic);
    Serial.println("Created Temperature characteristic with NOTIFY property");

    // Pressure (ESS standard UUID 0x2A6D)
    pressureCharacteristic = essService->createCharacteristic(
        BLEUUID((uint16_t)0x2A6D),
        BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
    setupCCCDDescriptor(pressureCharacteristic);
    Serial.println("Created Pressure characteristic with NOTIFY property");

    // Humidity (ESS standard UUID 0x2A6F)
    humidityCharacteristic = essService->createCharacteristic(
        BLEUUID((uint16_t)0x2A6F),
        BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
    setupCCCDDescriptor(humidityCharacteristic);

    // Altitude (ESS standard UUID 0x2A69)
    altitudeCharacteristic = essService->createCharacteristic(
        BLEUUID((uint16_t)0x2A69),
        BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
    setupCCCDDescriptor(altitudeCharacteristic);

    // Gas resistance (custom UUID)
    gasCharacteristic = customService->createCharacteristic(
        BLEUUID("5b0e3c0b-1a44-4b76-82ee-8c2adc2dd8e9"),
        BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
    setupCCCDDescriptor(gasCharacteristic);
  }

  // Create time synchronization characteristic (always available)
  // Custom UUID for time sync - client writes Unix timestamp to set RTC
  timeSyncCharacteristic = customService->createCharacteristic(
      BLEUUID("a1b2c3d4-e5f6-4a5b-8c9d-0e1f2a3b4c5d"),
      BLECharacteristic::PROPERTY_WRITE);
  timeSyncCharacteristic->setCallbacks(new TimeSyncCallbacks());

  // we are starting both services
  essService->start();
  customService->start();

  // Start advertising
  // we are advertising both services
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->addServiceUUID(CUSTOM_SERVICE_UUID);

  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x0); // set value to 0x00 to not advertise this parameter
  BLEDevice::startAdvertising();
  Serial.println("Waiting a client connection to notify...");
}

void loop()
{
  if (deviceConnected)
  {
    // Read from board 0 (ADS1) sensors if present
    if (getBoard(boardAds1)->present)
    {
      Board *board = getBoard(boardAds1);
      // Read raw ADC value for formaldehyde
      int16_t hchoRaw = board->ads.readADC_SingleEnded(0);
      // Converting to voltage using the function from the library
      float hchoVolt = board->ads.computeVolts(hchoRaw);
      // Here we are sending the voltage value as float
      if (hchoCharacteristic != NULL)
      {
        hchoCharacteristic->setValue((uint8_t *)&hchoVolt, sizeof(hchoVolt));
        // Send float value directly over BLE (4 bytes)
        hchoCharacteristic->notify();
      }

      // CH4 sensor
      int16_t ch4Raw = board->ads.readADC_SingleEnded(1);
      float ch4Volt = board->ads.computeVolts(ch4Raw);
      if (ch4Characteristic != NULL)
      {
        ch4Characteristic->setValue((uint8_t *)&ch4Volt, sizeof(ch4Volt));
        ch4Characteristic->notify();
      }

      // VOC sensor
      int16_t vocRaw = board->ads.readADC_SingleEnded(2);
      float vocVolt = board->ads.computeVolts(vocRaw);
      if (vocCharacteristic != NULL)
      {
        vocCharacteristic->setValue((uint8_t *)&vocVolt, sizeof(vocVolt));
        vocCharacteristic->notify();
      }

      // Odor sensor
      int16_t odorRaw = board->ads.readADC_SingleEnded(3);
      float odorVolt = board->ads.computeVolts(odorRaw);
      if (odorCharacteristic != NULL)
      {
        odorCharacteristic->setValue((uint8_t *)&odorVolt, sizeof(odorVolt));
        odorCharacteristic->notify();
      }
    }

    // Read from board 1 (ADS2) sensors if present
    if (getBoard(boardAds2)->present)
    {
      Board *board = getBoard(boardAds2);
      // Ethanol sensor
      int16_t etohRaw = board->ads.readADC_SingleEnded(0);
      float etohVolt = board->ads.computeVolts(etohRaw);
      if (etohCharacteristic != NULL)
      {
        etohCharacteristic->setValue((uint8_t *)&etohVolt, sizeof(etohVolt));
        etohCharacteristic->notify();
      }

      // H2S sensor
      int16_t h2sRaw = board->ads.readADC_SingleEnded(1);
      float h2sVolt = board->ads.computeVolts(h2sRaw);
      if (h2sCharacteristic != NULL)
      {
        h2sCharacteristic->setValue((uint8_t *)&h2sVolt, sizeof(h2sVolt));
        h2sCharacteristic->notify();
      }

      // NO2 sensor
      int16_t no2Raw = board->ads.readADC_SingleEnded(2);
      float no2Volt = board->ads.computeVolts(no2Raw);
      if (no2Characteristic != NULL)
      {
        no2Characteristic->setValue((uint8_t *)&no2Volt, sizeof(no2Volt));
        no2Characteristic->notify();
      }

      // NH3 sensor
      int16_t nh3Raw = board->ads.readADC_SingleEnded(3);
      float nh3Volt = board->ads.computeVolts(nh3Raw);
      if (nh3Characteristic != NULL)
      {
        nh3Characteristic->setValue((uint8_t *)&nh3Volt, sizeof(nh3Volt));
        nh3Characteristic->notify();
      }
    }

    // Read from board 2 (ADS3) sensors if present
    if (getBoard(boardAds3)->present)
    {
      Board *board = getBoard(boardAds3);
      // CO sensor
      int16_t coRaw = board->ads.readADC_SingleEnded(0);
      float coVolt = board->ads.computeVolts(coRaw);
      if (coCharacteristic != NULL)
      {
        coCharacteristic->setValue((uint8_t *)&coVolt, sizeof(coVolt));
        coCharacteristic->notify();
      }

      // Smoke sensor
      int16_t smokeRaw = board->ads.readADC_SingleEnded(1);
      float smokeVolt = board->ads.computeVolts(smokeRaw);
      if (smokeCharacteristic != NULL)
      {
        smokeCharacteristic->setValue((uint8_t *)&smokeVolt, sizeof(smokeVolt));
        smokeCharacteristic->notify();
      }

      // H2 sensor
      int16_t h2Raw = board->ads.readADC_SingleEnded(2);
      float h2Volt = board->ads.computeVolts(h2Raw);
      if (h2Characteristic != NULL)
      {
        h2Characteristic->setValue((uint8_t *)&h2Volt, sizeof(h2Volt));
        h2Characteristic->notify();
      }

      Serial.print("CO:");
      Serial.print(coVolt);
      Serial.print(",Smoke:");
      Serial.print(smokeVolt);
      Serial.print(",H2:");
      Serial.println(h2Volt);
    }

    // Read from BME680 sensor if present
    if (bme680_present)
    {
      if (bme680.performReading())
      {
        // Read temperature
        float temperature = bme680.temperature;
        if (temperatureCharacteristic != NULL)
        {
          temperatureCharacteristic->setValue((uint8_t *)&temperature, sizeof(temperature));
          temperatureCharacteristic->notify();
        }

        // Read pressure (convert from Pa to hPa for BLE)
        float pressure = bme680.pressure / 100.0;
        if (pressureCharacteristic != NULL)
        {
          pressureCharacteristic->setValue((uint8_t *)&pressure, sizeof(pressure));
          pressureCharacteristic->notify();
        }

        // Read humidity
        float humidity = bme680.humidity;
        if (humidityCharacteristic != NULL)
        {
          humidityCharacteristic->setValue((uint8_t *)&humidity, sizeof(humidity));
          humidityCharacteristic->notify();
        }

        // Read gas resistance
        float gas = bme680.gas_resistance;
        if (gasCharacteristic != NULL)
        {
          gasCharacteristic->setValue((uint8_t *)&gas, sizeof(gas));
          gasCharacteristic->notify();
        }

        // Calculate and read altitude (assuming sea level pressure of 1013.25 hPa)
        float altitude = bme680.readAltitude(1013.25);
        if (altitudeCharacteristic != NULL)
        {
          altitudeCharacteristic->setValue((uint8_t *)&altitude, sizeof(altitude));
          altitudeCharacteristic->notify();
        }

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
      }
      else
      {
        Serial.println("BME680 reading failed");
      }
    }

    // we are sending the values every 5 seconds
    delay(5000);
  }
  // disconnecting
  if (!deviceConnected && oldDeviceConnected)
  {
    delay(500);                  // give the bluetooth stack the chance to get things ready
    pServer->startAdvertising(); // restart advertising
    Serial.println("start advertising");
    oldDeviceConnected = deviceConnected;
  }
  // connecting
  if (deviceConnected && !oldDeviceConnected)
  {
    // do stuff here on connecting
    oldDeviceConnected = deviceConnected;
  }
}