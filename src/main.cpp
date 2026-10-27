// BRIAN V1 firmware: reads the MEMS gas sensors (3x ADS1115) and the BME680
// and streams them over BLE. The BLE profile itself (services,
// characteristics, security, advertising) lives in lib/BrianBLE so the
// emulator (src/emulator/) serves exactly the same contract.
#include <Arduino.h>

#include <Adafruit_ADS1X15.h>
#include <Adafruit_BME680.h>
#include <BrianBLE.h>
#include <SD.h>
#include <SPI.h>
#include <Wire.h>

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

// BME680 environmental sensor
Adafruit_BME680 bme680;
bool bme680_present = false;

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

void setup() {
  Serial.begin(115200);

  // The ADS1115/BME680 probes need the I2C bus to be initialized first.
  Wire.begin();
  Wire.setClock(400000);

  initMEMS();
  initBME680();
  // Board status: bit n set = that I2C board was detected at boot. It also
  // decides which sensor characteristics BrianBLE creates.
  uint8_t boardStatus = 0;
  if (getBoard(boardAds1)->present) {
    boardStatus |= (1 << BrianBLE::BOARD_STATUS_ADS1_BIT);
  }
  if (getBoard(boardAds2)->present) {
    boardStatus |= (1 << BrianBLE::BOARD_STATUS_ADS2_BIT);
  }
  if (getBoard(boardAds3)->present) {
    boardStatus |= (1 << BrianBLE::BOARD_STATUS_ADS3_BIT);
  }
  if (bme680_present) {
    boardStatus |= (1 << BrianBLE::BOARD_STATUS_BME680_BIT);
  }

  BrianBLE::begin("Brian-", boardStatus);
}

void loop() {
  if (BrianBLE::connected()) {
    // Read from board 0 (ADS1) sensors if present
    if (getBoard(boardAds1)->present) {
      Board *board = getBoard(boardAds1);

      // Formaldehyde sensor
      float hchoVolt = readChannelVolts(board, 0, GAIN_BOARD1_DEFAULT,
                                        GAIN_BOARD1_DEFAULT, "HCHO");
      BrianBLE::notify(BrianBLE::HCHO, hchoVolt);

      // CH4 sensor
      float ch4Volt = readChannelVolts(board, 1, GAIN_BOARD1_DEFAULT,
                                       GAIN_BOARD1_DEFAULT, "CH4");
      BrianBLE::notify(BrianBLE::CH4, ch4Volt);

      // VOC sensor (read at a higher gain for its lower output range)
      float vocVolt =
          readChannelVolts(board, 2, GAIN_VOC, GAIN_BOARD1_DEFAULT, "VOC");
      BrianBLE::notify(BrianBLE::VOC, vocVolt);

      // Odor sensor
      float odorVolt = readChannelVolts(board, 3, GAIN_BOARD1_DEFAULT,
                                        GAIN_BOARD1_DEFAULT, "Odor");
      BrianBLE::notify(BrianBLE::ODOR, odorVolt);
    }

    // Read from board 1 (ADS2) sensors if present
    if (getBoard(boardAds2)->present) {
      Board *board = getBoard(boardAds2);

      // Ethanol sensor
      float etohVolt = readChannelVolts(board, 0, GAIN_BOARD2_DEFAULT,
                                        GAIN_BOARD2_DEFAULT, "EtOH");
      BrianBLE::notify(BrianBLE::ETOH, etohVolt);

      // H2S sensor
      float h2sVolt = readChannelVolts(board, 1, GAIN_BOARD2_DEFAULT,
                                       GAIN_BOARD2_DEFAULT, "H2S");
      BrianBLE::notify(BrianBLE::H2S, h2sVolt);

      // NO2 sensor
      float no2Volt = readChannelVolts(board, 2, GAIN_BOARD2_DEFAULT,
                                       GAIN_BOARD2_DEFAULT, "NO2");
      BrianBLE::notify(BrianBLE::NO2, no2Volt);

      // NH3 sensor (read at a higher gain for its lower output range)
      float nh3Volt =
          readChannelVolts(board, 3, GAIN_NH3, GAIN_BOARD2_DEFAULT, "NH3");
      BrianBLE::notify(BrianBLE::NH3, nh3Volt);
    }

    // Read from board 2 (ADS3) sensors if present
    if (getBoard(boardAds3)->present) {
      Board *board = getBoard(boardAds3);

      // CO sensor
      float coVolt = readChannelVolts(board, 0, GAIN_BOARD3_DEFAULT,
                                      GAIN_BOARD3_DEFAULT, "CO");
      BrianBLE::notify(BrianBLE::CO, coVolt);

      // Smoke sensor
      float smokeVolt = readChannelVolts(board, 1, GAIN_BOARD3_DEFAULT,
                                         GAIN_BOARD3_DEFAULT, "Smoke");
      BrianBLE::notify(BrianBLE::SMOKE, smokeVolt);

      // H2 sensor
      float h2Volt = readChannelVolts(board, 2, GAIN_BOARD3_DEFAULT,
                                      GAIN_BOARD3_DEFAULT, "H2");
      BrianBLE::notify(BrianBLE::H2, h2Volt);

      Serial.print("CO:");
      Serial.print(coVolt);
      Serial.print(",Smoke:");
      Serial.print(smokeVolt);
      Serial.print(",H2:");
      Serial.println(h2Volt);
    }

    // Read from BME680 sensor if present
    if (bme680_present) {
      if (bme680.performReading()) {
        // Read temperature
        float temperature = bme680.temperature;
        BrianBLE::notify(BrianBLE::TEMPERATURE, temperature);

        // Read pressure (convert from Pa to hPa for BLE)
        float pressure = bme680.pressure / 100.0;
        BrianBLE::notify(BrianBLE::PRESSURE, pressure);

        // Read humidity
        float humidity = bme680.humidity;
        BrianBLE::notify(BrianBLE::HUMIDITY, humidity);

        // Read gas resistance
        float gas = bme680.gas_resistance;
        BrianBLE::notify(BrianBLE::GAS_RESISTANCE, gas);

        // Calculate and read altitude (assuming sea level pressure of 1013.25
        // hPa)
        float altitude = bme680.readAltitude(1013.25);
        BrianBLE::notify(BrianBLE::ALTITUDE, altitude);

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

    // we are sending the values every 5 seconds
    delay(5000);
  }
  BrianBLE::handleConnectionChanges();
}
