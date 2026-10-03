// BRIAN BLE emulator: serves the same BLE contract as the V1 firmware (via
// lib/BrianBLE) with synthetic sensor data, on any ESP32 dev board with no
// sensors attached. For developing and testing BrianReactNative / BrianWeb
// without a physical BRIAN. See README.md -> "BLE emulator".
//
// Compile-time options (set with -D in platformio.ini build_flags):
//   BRIAN_SIM_INTERVAL_MS  notify cycle in ms (default 5000, like V1)
//   BRIAN_SIM_BOARDS       board status bitmask of emulated boards
//                          (bit 0 ADS1, 1 ADS2, 2 ADS3, 3 BME680; default 0x0F)
//   BRIAN_SIM_SEED         PRNG seed; 0 (default) = random each boot
//   BRIAN_SIM_EVENT_MEAN_S mean time between odour events in s (default 90)
//
// Serial commands (115200 baud): e = odour event, p = pause/resume
// notifications, d = disconnect client, s = status, h = help.
#include <Arduino.h>
#include <BrianBLE.h>
#include <math.h>

#ifndef BRIAN_SIM_INTERVAL_MS
#define BRIAN_SIM_INTERVAL_MS 5000
#endif
#ifndef BRIAN_SIM_BOARDS
#define BRIAN_SIM_BOARDS BrianBLE::BOARD_STATUS_ALL
#endif
#ifndef BRIAN_SIM_SEED
#define BRIAN_SIM_SEED 0
#endif
#ifndef BRIAN_SIM_EVENT_MEAN_S
#define BRIAN_SIM_EVENT_MEAN_S 90
#endif

using BrianBLE::Channel;

// --- Random numbers --------------------------------------------------------
// Own PRNG (xorshift32) rather than random(), so a fixed seed reproduces the
// same data on every board and core version.
uint32_t rngState = 1;

uint32_t rngNext() {
  rngState ^= rngState << 13;
  rngState ^= rngState >> 17;
  rngState ^= rngState << 5;
  return rngState;
}

// Uniform in [0, 1)
float rngUniform() { return (rngNext() >> 8) * (1.0f / 16777216.0f); }

float rngRange(float lo, float hi) { return lo + (hi - lo) * rngUniform(); }

// Standard normal (Box-Muller)
float rngGaussian() {
  float u1 = rngUniform();
  if (u1 < 1e-7f) {
    u1 = 1e-7f;
  }
  float u2 = rngUniform();
  return sqrtf(-2.0f * logf(u1)) * cosf(2.0f * PI * u2);
}

// --- Gas channels ----------------------------------------------------------
// Baselines are in the range the V1 sensors show in clean air; maxVolts is
// the full scale of the ADS1115 gain V1 reads the channel at (VOC: GAIN_FOUR,
// NH3: GAIN_SIXTEEN, others GAIN_ONE), so the emulator clips where the real
// device would.
struct GasChannel {
  Channel channel;
  float baseline; // V, clean-air mean
  float drift;    // V, std dev of the slow baseline wander
  float noise;    // V, std dev of per-sample noise
  float maxVolts; // V, clip limit
  float response; // V per unit odour intensity (0 = does not react)
  float offset;   // current drift offset from baseline (state)
};

GasChannel gas[] = {
    {BrianBLE::HCHO, 0.55f, 0.020f, 0.003f, 4.096f, 0.0f, 0.0f},
    {BrianBLE::CH4, 0.80f, 0.020f, 0.003f, 4.096f, 0.0f, 0.0f},
    {BrianBLE::VOC, 0.35f, 0.010f, 0.002f, 1.024f, 0.0f, 0.0f},
    {BrianBLE::ODOR, 0.65f, 0.020f, 0.003f, 4.096f, 0.0f, 0.0f},
    {BrianBLE::ETOH, 0.70f, 0.020f, 0.003f, 4.096f, 0.0f, 0.0f},
    {BrianBLE::H2S, 0.45f, 0.015f, 0.003f, 4.096f, 0.0f, 0.0f},
    {BrianBLE::NO2, 0.90f, 0.020f, 0.003f, 4.096f, 0.0f, 0.0f},
    {BrianBLE::NH3, 0.08f, 0.004f, 0.0005f, 0.256f, 0.0f, 0.0f},
    {BrianBLE::CO, 0.60f, 0.020f, 0.003f, 4.096f, 0.0f, 0.0f},
    {BrianBLE::SMOKE, 0.50f, 0.020f, 0.003f, 4.096f, 0.0f, 0.0f},
    {BrianBLE::H2, 0.75f, 0.020f, 0.003f, 4.096f, 0.0f, 0.0f},
};
constexpr size_t gasCount = sizeof(gas) / sizeof(gas[0]);

// Time constant of the baseline drift (Ornstein-Uhlenbeck mean reversion)
constexpr float DRIFT_TAU_S = 600.0f;

// --- Odour events ----------------------------------------------------------
// An event rises with time constant riseTau and decays with decayTau. Each
// profile lists the channels that react together and their sensitivity
// (V at intensity 1).
struct ResponseEntry {
  Channel channel;
  float volts;
};

struct OdourProfile {
  const char *name;
  ResponseEntry responses[5];
  size_t count;
};

const OdourProfile profiles[] = {
    {"solvent/alcohol",
     {{BrianBLE::VOC, 0.30f},
      {BrianBLE::ETOH, 0.90f},
      {BrianBLE::ODOR, 0.60f},
      {BrianBLE::H2, 0.40f},
      {BrianBLE::HCHO, 0.20f}},
     5},
    {"combustion",
     {{BrianBLE::CO, 0.80f},
      {BrianBLE::SMOKE, 1.00f},
      {BrianBLE::NO2, 0.30f},
      {BrianBLE::H2, 0.30f},
      {BrianBLE::VOC, 0.15f}},
     5},
    {"sewage/organic",
     {{BrianBLE::H2S, 0.70f},
      {BrianBLE::NH3, 0.08f},
      {BrianBLE::ODOR, 0.80f},
      {BrianBLE::CH4, 0.40f}},
     4},
};
constexpr size_t profileCount = sizeof(profiles) / sizeof(profiles[0]);

struct OdourEvent {
  const OdourProfile *profile = nullptr;
  float amplitude = 0.0f; // peak intensity scale
  float riseTau = 0.0f;   // s
  float decayTau = 0.0f;  // s
  float age = 0.0f;       // s since start
};

OdourEvent event;
float secondsToNextEvent = 0.0f;

// Intensity (0..~1) of the current event at its age: difference of
// exponentials, normalised so the peak equals the amplitude.
float eventIntensity() {
  if (event.profile == nullptr) {
    return 0.0f;
  }
  float r = event.riseTau, d = event.decayTau;
  float tPeak = (r * d / (d - r)) * logf(d / r);
  float peak = expf(-tPeak / d) - expf(-tPeak / r);
  float value = expf(-event.age / d) - expf(-event.age / r);
  return event.amplitude * value / peak;
}

void startOdourEvent() {
  event.profile = &profiles[rngNext() % profileCount];
  event.amplitude = rngRange(0.3f, 1.0f);
  event.riseTau = rngRange(3.0f, 10.0f);
  event.decayTau = rngRange(25.0f, 70.0f);
  event.age = 0.0f;
  Serial.print("Odour event: ");
  Serial.print(event.profile->name);
  Serial.print(", amplitude ");
  Serial.println(event.amplitude, 2);
}

void scheduleNextEvent() {
  // Exponential inter-arrival times (Poisson process)
  secondsToNextEvent =
      -logf(1.0f - rngUniform()) * (float)BRIAN_SIM_EVENT_MEAN_S;
}

float responseFor(Channel channel) {
  if (event.profile == nullptr) {
    return 0.0f;
  }
  for (size_t i = 0; i < event.profile->count; i++) {
    if (event.profile->responses[i].channel == channel) {
      return event.profile->responses[i].volts;
    }
  }
  return 0.0f;
}

// --- Environment (BME680) --------------------------------------------------
// Temperature and absolute humidity wander slowly; relative humidity is
// derived from them, so RH falls as temperature rises like it does outdoors.
float temperatureOffset = 0.0f; // degC, OU state around the daily cycle
float dewPoint = 10.0f;         // degC, OU state
float pressureHpa = 1013.25f;   // hPa, OU state
float gasBaseline = 120000.0f;  // Ohm, clean-air BME680 gas resistance

// Saturation vapour pressure (Magnus formula), hPa
float saturationVapourPressure(float tempC) {
  return 6.112f * expf(17.62f * tempC / (243.12f + tempC));
}

// Ornstein-Uhlenbeck step: reverts to 0 with time constant tau, stationary
// std dev sigma.
float ouStep(float x, float dt, float tau, float sigma) {
  float a = expf(-dt / tau);
  return x * a + sigma * sqrtf(1.0f - a * a) * rngGaussian();
}

// --- Simulation state ------------------------------------------------------
uint8_t boardStatus = BRIAN_SIM_BOARDS;
bool paused = false;
uint32_t lastSampleMs = 0;
uint32_t lastStepMs = 0;
bool wasConnected = false;

void stepSimulation(float dt) {
  for (size_t i = 0; i < gasCount; i++) {
    gas[i].offset = ouStep(gas[i].offset, dt, DRIFT_TAU_S, gas[i].drift);
  }

  if (event.profile != nullptr) {
    event.age += dt;
    if (event.age > 6.0f * event.decayTau) {
      event.profile = nullptr;
    }
  }
  secondsToNextEvent -= dt;
  if (secondsToNextEvent <= 0.0f) {
    if (event.profile == nullptr) {
      startOdourEvent();
    }
    scheduleNextEvent();
  }

  temperatureOffset = ouStep(temperatureOffset, dt, 900.0f, 1.0f);
  dewPoint = 10.0f + ouStep(dewPoint - 10.0f, dt, 1800.0f, 2.0f);
  pressureHpa = 1013.25f + ouStep(pressureHpa - 1013.25f, dt, 3600.0f, 4.0f);
}

// Sample the simulated sensors and notify, in the same order as V1's loop().
void sampleAndNotify() {
  float intensity = eventIntensity();

  for (size_t i = 0; i < gasCount; i++) {
    if (!(boardStatus & (1 << BrianBLE::boardBit(gas[i].channel)))) {
      continue;
    }
    float volts = gas[i].baseline + gas[i].offset +
                  responseFor(gas[i].channel) * intensity +
                  gas[i].noise * rngGaussian();
    volts = constrain(volts, 0.0f, gas[i].maxVolts);
    BrianBLE::notify(gas[i].channel, volts);
  }

  if (boardStatus & (1 << BrianBLE::BOARD_STATUS_BME680_BIT)) {
    // Slow daily cycle (period 24 h from boot) plus wander
    float temperature = 20.0f +
                        3.0f * sinf(2.0f * PI * millis() / 86400000.0f) +
                        temperatureOffset + 0.02f * rngGaussian();
    float dew = min(dewPoint, temperature - 0.5f);
    float humidity = 100.0f * saturationVapourPressure(dew) /
                     saturationVapourPressure(temperature);
    humidity = constrain(humidity + 0.1f * rngGaussian(), 0.0f, 100.0f);
    float pressure = pressureHpa + 0.05f * rngGaussian();
    // Same formula as Adafruit_BME680::readAltitude(1013.25) used by V1
    float altitude = 44330.0f * (1.0f - powf(pressure / 1013.25f, 0.1903f));
    // Gas resistance drops when reducing gases are present, and a little
    // with humidity
    float gasResistance = gasBaseline / (1.0f + 4.0f * intensity) *
                          (1.0f - 0.003f * (humidity - 40.0f)) *
                          (1.0f + 0.01f * rngGaussian());

    BrianBLE::notify(BrianBLE::TEMPERATURE, temperature);
    BrianBLE::notify(BrianBLE::PRESSURE, pressure);
    BrianBLE::notify(BrianBLE::HUMIDITY, humidity);
    BrianBLE::notify(BrianBLE::GAS_RESISTANCE, gasResistance);
    BrianBLE::notify(BrianBLE::ALTITUDE, altitude);

    Serial.print("Temperature:");
    Serial.print(temperature);
    Serial.print(" C, Pressure:");
    Serial.print(pressure);
    Serial.print(" hPa, Humidity:");
    Serial.print(humidity);
    Serial.print(" %, Gas:");
    Serial.print(gasResistance);
    Serial.print(" Ohms, Altitude:");
    Serial.print(altitude);
    Serial.print(" m, odour intensity:");
    Serial.println(intensity, 2);
  }
}

void printHelp() {
  Serial.println("Commands: e = odour event, p = pause/resume notifications,");
  Serial.println("          d = disconnect client, s = status, h = help");
}

void printStatus() {
  Serial.print(BrianBLE::deviceName());
  Serial.print(BrianBLE::connected() ? ": connected" : ": not connected");
  Serial.print(paused ? ", paused" : ", notifying");
  Serial.print(", interval ");
  Serial.print(BRIAN_SIM_INTERVAL_MS);
  Serial.print(" ms, boards 0x");
  Serial.print(boardStatus, HEX);
  Serial.print(", event ");
  Serial.println(event.profile ? event.profile->name : "none");
}

void handleSerial() {
  while (Serial.available() > 0) {
    switch (Serial.read()) {
    case 'e':
      startOdourEvent();
      break;
    case 'p':
      paused = !paused;
      Serial.println(paused ? "Notifications paused" : "Notifications resumed");
      break;
    case 'd':
      Serial.println("Disconnecting client");
      BrianBLE::disconnect();
      break;
    case 's':
      printStatus();
      break;
    case 'h':
    case '?':
      printHelp();
      break;
    default:
      break;
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000); // let USB CDC enumerate so the banner isn't lost

  rngState = BRIAN_SIM_SEED != 0 ? (uint32_t)BRIAN_SIM_SEED : esp_random();
  if (rngState == 0) {
    rngState = 1; // xorshift must not start at 0
  }
  Serial.println("BRIAN BLE emulator - SYNTHETIC DATA, not a real sensor");
  Serial.print("Seed: ");
  Serial.println(rngState);

  gasBaseline = rngRange(80000.0f, 160000.0f);
  scheduleNextEvent();

  BrianBLE::begin("Brian-SIM-", boardStatus);
  printHelp();
  lastStepMs = millis();
}

void loop() {
  handleSerial();

  uint32_t now = millis();
  float dt = (now - lastStepMs) / 1000.0f;
  if (dt >= 0.25f) {
    stepSimulation(dt);
    lastStepMs = now;
  }

  // Like V1: only sample while a client is connected, the first sample right
  // after connecting, then one per interval.
  bool isConnected = BrianBLE::connected();
  if (isConnected && !paused &&
      (!wasConnected || now - lastSampleMs >= BRIAN_SIM_INTERVAL_MS)) {
    lastSampleMs = now;
    sampleAndNotify();
  }
  wasConnected = isConnected;

  BrianBLE::handleConnectionChanges();
  delay(10);
}
