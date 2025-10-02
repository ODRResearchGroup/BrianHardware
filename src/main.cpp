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
// BLE2901 *descriptor_2901 = NULL;

bool deviceConnected = false;
bool oldDeviceConnected = false;
// See the following for generating UUIDs:
// https://www.uuidgenerator.net/

// Here we are creating a servcie for Environmental Sensing Service (ESS) - standardised UUID for ESS https://www.bluetooth.com/wp-content/uploads/Files/Specification/HTML/Assigned_Numbers/out/en/Assigned_Numbers.pdf
#define SERVICE_UUID (BLEUUID((uint16_t)0x181A))
//Here we are creating a custom service for sensors that do not have a standardised service
#define CUSTOM_SERVICE_UUID "de664a17-7db4-449f-97ba-5514e19a9d94" //custom service

// create characteristics for each gas sensor
BLECharacteristic* ch4Characteristic = NULL;
BLECharacteristic* vocCharacteristic = NULL;
BLECharacteristic* nh3Characteristic = NULL;
BLECharacteristic* no2Characteristic = NULL;
BLECharacteristic* hchoCharacteristic = NULL;
BLECharacteristic* odorCharacteristic = NULL;
BLECharacteristic* etohCharacteristic = NULL;
BLECharacteristic* h2sCharacteristic = NULL;

//here we are creating an object for the ADS
Adafruit_ADS1015 ads1, ads2; // we are using two ADS1015 to have more channels
void setUpMEMS() {
ads1.begin(0x48);
ads2.begin(0x49);
}
//Here is a function to initialize the MEMS sensor
void initMEMS(){
 if (!ads1.begin(0x48)) {
    Serial.println("Could not find ADS1015 at 0x48");
    while (1); 
  }
  if (!ads2.begin(0x49)) {
    Serial.println("Could not find ADS1015 at 0x49");
    while (1); 
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
  BLEDevice::init("esp32");
  //this is for increasing the MTU size - default is 23 bytes, we can set it up to 517 bytes
  BLEDevice::setMTU(517);

  // Create the BLE Server
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  // Create the BLE Service both ESS and custom
    BLEService *essService = pServer->createService(SERVICE_UUID);
BLEService *customService = pServer->createService(CUSTOM_SERVICE_UUID);
  // Create characteristics

  //these are for ESS standardised characteristics
  ch4Characteristic = essService->createCharacteristic(
      BLEUUID((uint16_t)0x2BD1),
      BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
  ch4Characteristic->addDescriptor(new BLE2902());

  vocCharacteristic = essService->createCharacteristic(
      BLEUUID((uint16_t)0x2BD3),
      BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
  vocCharacteristic->addDescriptor(new BLE2902());

  nh3Characteristic = essService->createCharacteristic(
      BLEUUID((uint16_t)0x2BCF),
      BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
  nh3Characteristic->addDescriptor(new BLE2902());

  no2Characteristic = essService->createCharacteristic(  // Fixed name
      BLEUUID((uint16_t)0x2BD2),
      BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
  no2Characteristic->addDescriptor(new BLE2902());

  //these are for custom characteristics
  hchoCharacteristic = customService->createCharacteristic(
      BLEUUID("6a135b89-f360-4f64-86fc-5a14092034b4"),
      BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
  hchoCharacteristic->addDescriptor(new BLE2902());

  odorCharacteristic = customService->createCharacteristic(
      BLEUUID("4c28fcb8-d69b-404a-8668-41655d814e7f"),
      BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
  odorCharacteristic->addDescriptor(new BLE2902());

  etohCharacteristic = customService->createCharacteristic(
      BLEUUID("f8156843-6d98-4ba2-8014-1cf03d7dedb8"),
      BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
  etohCharacteristic->addDescriptor(new BLE2902());

  h2sCharacteristic = customService->createCharacteristic(
      BLEUUID("87dc71bd-29a4-4218-a2a7-83fd2a69cc40"),
      BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
  h2sCharacteristic->addDescriptor(new BLE2902());

  //we are starting both services
  essService->start();
customService->start();

  // Start advertising
  //we are advertising both services
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
    // Read raw ADC value for formaldehyde
    int16_t hchoRaw = ads1.readADC_SingleEnded(0);
        // Converting to voltage using the function from the library 
    float hchoVolt = ads1.computeVolts(hchoRaw);
        // Here we are sending the voltage value as float
    hchoCharacteristic->setValue((uint8_t*)&hchoVolt, sizeof(hchoVolt));
        // Send float value directly over BLE (4 bytes)
    hchoCharacteristic->notify();

    //do the same for the rest of the sensors
    int16_t ch4Raw = ads1.readADC_SingleEnded(1);
    float ch4Volt = ads1.computeVolts(ch4Raw);
    ch4Characteristic->setValue((uint8_t*)&ch4Volt, sizeof(ch4Volt));
    ch4Characteristic->notify();

int16_t vocRaw = ads1.readADC_SingleEnded(2);
    float vocVolt = ads1.computeVolts(vocRaw);
    vocCharacteristic->setValue((uint8_t*)&vocVolt, sizeof(vocVolt));
    vocCharacteristic->notify();

   int16_t odorRaw = ads1.readADC_SingleEnded(3);
    float odorVolt = ads1.computeVolts(odorRaw);
    odorCharacteristic->setValue((uint8_t*)&odorVolt, sizeof(odorVolt));
    odorCharacteristic->notify(); 

    int16_t etohRaw = ads2.readADC_SingleEnded(0);
    float etohVolt = ads2.computeVolts(etohRaw);
    etohCharacteristic->setValue((uint8_t*)&etohVolt, sizeof(etohVolt));
    etohCharacteristic->notify();

    int16_t h2sRaw = ads2.readADC_SingleEnded(1);
    float h2sVolt = ads2.computeVolts(h2sRaw);
    h2sCharacteristic->setValue((uint8_t*)&h2sVolt, sizeof(h2sVolt));
    h2sCharacteristic->notify();

    int16_t no2Raw = ads2.readADC_SingleEnded(2);
    float no2Volt = ads2.computeVolts(no2Raw);
    no2Characteristic->setValue((uint8_t*)&no2Volt, sizeof(no2Volt));
    no2Characteristic->notify();

    int16_t nh3Raw = ads2.readADC_SingleEnded(3);
    float nh3Volt = ads2.computeVolts(nh3Raw);
    nh3Characteristic->setValue((uint8_t*)&nh3Volt, sizeof(nh3Volt));
    nh3Characteristic->notify();
    // we are sending the values every second
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