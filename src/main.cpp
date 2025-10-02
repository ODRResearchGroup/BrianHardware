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

BLEServer *pServer = NULL;
BLECharacteristic *pCharacteristic = NULL;
// BLE2901 *descriptor_2901 = NULL;

bool deviceConnected = false;
bool oldDeviceConnected = false;
uint32_t value = 0;
// See the following for generating UUIDs:
// https://www.uuidgenerator.net/

// Here we are creating a servcie for Environmental Sensing Service (ESS) - standardised UUID for ESS https://www.bluetooth.com/wp-content/uploads/Files/Specification/HTML/Assigned_Numbers/out/en/Assigned_Numbers.pdf
#define SERVICE_UUID (BLEUUID((uint16_t)0x181A))

// Here we are creating a methane characteristic (standartised UUID for methane https://www.bluetooth.com/wp-content/uploads/Files/Specification/HTML/Assigned_Numbers/out/en/Assigned_Numbers.pdf )
#define CHARACTERISTIC_UUID_METHANE (BLEUUID((uint16_t)0x2BD1))

// here we are creating an object for the ADS
Adafruit_ADS1015 ads1;
void setUpMEMS()
{
  ads1.begin(0x48);
}
// Here is a function to initialize the MEMS sensor (only ads1)
void initMEMS()
{
  if (!ads1.begin(0x48))
  {
    Serial.println("Could not find ADS1015 at 0x48");
    while (1)
      ;
  }
}

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
  // Create the BLE Device
  BLEDevice::init("MyBLEDevice");

  // Create the BLE Server
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  // Create the BLE Service
  BLEService *pService = pServer->createService(SERVICE_UUID);

  // Create a BLE Characteristic
  pCharacteristic = pService->createCharacteristic(
      CHARACTERISTIC_UUID_METHANE,
      BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_NOTIFY | BLECharacteristic::PROPERTY_INDICATE);

  // Creates BLE Descriptor 0x2902: Client Characteristic Configuration Descriptor (CCCD)
  pCharacteristic->addDescriptor(new BLE2902());
  // Adds also the Characteristic User Description - 0x2901 descriptor
  // descriptor_2901 = new BLE2901();
  // descriptor_2901->setDescription("My own description for this characteristic.");
  // descriptor_2901->setAccessPermissions(ESP_GATT_PERM_READ); // enforce read only - default is Read|Write
  // pCharacteristic->addDescriptor(descriptor_2901);

  // Start the service
  pService->start();

  // Start advertising
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(false);
  pAdvertising->setMinPreferred(0x0); // set value to 0x00 to not advertise this parameter
  BLEDevice::startAdvertising();
  Serial.println("Waiting a client connection to notify...");
}

void loop()
{
  // here we are printing methane value
  if (deviceConnected)
  {
    int16_t ch4 = ads1.readADC_SingleEnded(1); // calling 1 (ch4 sensor on ads1)
    float ch4Volt = ads1.computeVolts(ch4);

    char voltageStr[8];                 // Buffer for the string
    dtostrf(ch4Volt, 1, 4, voltageStr); // Min width 1, 4 decimal places

    pCharacteristic->setValue(voltageStr);
    pCharacteristic->notify();
    Serial.println(ch4Volt);
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