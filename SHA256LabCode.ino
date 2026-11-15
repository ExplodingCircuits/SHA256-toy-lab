
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include <mbedtls/sha256.h>
#include <cstring>
#include <cstdio>
#include <cmath>

#include "driver/temperature_sensor.h"
#include "esp_sleep.h"
#include "driver/rtc_io.h"

// =============================================
// SHA256 LAB v1.3
// ESP32-S3 SUPERMINI HW-747
//
// Disclaimer: This code was made using the assistance of ChatGPT. Im currently trying to learn how to code, and will try writing my own code in the future. Thank you!
//
// =============================================


// =============================================
// 1. HARDWARE CONFIGURATION
// =============================================

#define SDA_PIN 8
#define SCL_PIN 9

#define BUTTON_UP     4
#define BUTTON_SELECT 5
#define BUTTON_DOWN   6

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_ADDRESS 0x3C

Adafruit_SH1106G display(
  SCREEN_WIDTH,
  SCREEN_HEIGHT,
  &Wire,
  -1
);


// =============================================
// 2. THERMAL PROTECTION
// =============================================

#define TEMP_PAUSE 60.0
#define TEMP_RESUME 55.0

temperature_sensor_handle_t tempSensor = NULL;

float chipTemperature = NAN;

bool tempSensorOK = false;
bool thermalPaused = false;

uint32_t previousTempTime = 0;


// =============================================
// 3. HASHING VARIABLES
// =============================================

uint64_t totalHashes = 0;
uint64_t nonce = 0;
uint64_t lastNonce = 0;

uint32_t hashRate = 0;

uint32_t previousRateTime = 0;
uint32_t previousDisplayTime = 0;

uint64_t previousRateHashes = 0;

unsigned char digest[32];
char digestHex[65];

bool paused = false;

int currentScreen = 0;


// =============================================
// 4. GRAPH HISTORY
// =============================================

// One sample per second.
// 3600 samples = 1 hour.

#define GRAPH_POINTS 3600

uint32_t rateHistory[GRAPH_POINTS];
float temperatureHistory[GRAPH_POINTS];

int historyIndex = 0;
int historyCount = 0;

// Available graph durations.

const uint16_t graphWindows[] = {
  30,
  60,
  120,
  300,
  600,
  3600
};

const uint8_t GRAPH_WINDOW_COUNT = 6;

// Default graph duration: 60 seconds.

uint8_t graphWindowIndex = 1;


// =============================================
// 5. BUTTON HANDLING
// =============================================

struct Button {

  int pin;

  bool stableState;
  bool lastReading;

  uint32_t lastChange;
};

Button upButton = {
  BUTTON_UP, HIGH, HIGH, 0
};

Button selectButton = {
  BUTTON_SELECT, HIGH, HIGH, 0
};

Button downButton = {
  BUTTON_DOWN, HIGH, HIGH, 0
};


bool buttonPressed(Button &button) {

  bool reading = digitalRead(button.pin);

  if (reading != button.lastReading) {

    button.lastChange = millis();

    button.lastReading = reading;
  }

  if (millis() - button.lastChange > 35) {

    if (reading != button.stableState) {

      button.stableState = reading;

      if (reading == LOW) {
        return true;
      }
    }
  }

  return false;
}


// =============================================
// 6. TEMPERATURE SENSOR
// =============================================

bool initializeTemperatureSensor() {

  temperature_sensor_config_t config =
    TEMPERATURE_SENSOR_CONFIG_DEFAULT(10, 80);

  if (
    temperature_sensor_install(
      &config,
      &tempSensor
    ) != ESP_OK
  ) {

    Serial.println(
      "Temperature sensor installation failed!"
    );

    return false;
  }

  if (
    temperature_sensor_enable(tempSensor) != ESP_OK
  ) {

    Serial.println(
      "Temperature sensor enable failed!"
    );

    temperature_sensor_uninstall(tempSensor);

    tempSensor = NULL;

    return false;
  }

  Serial.println("Temperature sensor initialized!");

  return true;
}


void updateTemperature() {

  if (!tempSensorOK) {

    thermalPaused = true;

    return;
  }

  float reading;

  if (
    temperature_sensor_get_celsius(
      tempSensor,
      &reading
    ) != ESP_OK
  ) {

    chipTemperature = NAN;

    thermalPaused = true;

    Serial.println("Temperature reading failed!");

    return;
  }

  chipTemperature = reading;

  // Thermal hysteresis.

  if (chipTemperature >= TEMP_PAUSE) {

    thermalPaused = true;
  }

  else if (chipTemperature <= TEMP_RESUME) {

    thermalPaused = false;
  }
}


// =============================================
// 7. SHA-256 HASHING ENGINE
// =============================================

void calculateHash() {

  char input[80];

  lastNonce = nonce++;

  snprintf(
    input,
    sizeof(input),
    "ESP32-S3 SHA256 LAB %llu",
    (unsigned long long)lastNonce
  );

  int result = mbedtls_sha256(
    (const unsigned char*)input,
    strlen(input),
    digest,
    0
  );

  if (result == 0) {

    totalHashes++;

    for (int i = 0; i < 32; i++) {

      sprintf(
        &digestHex[i * 2],
        "%02x",
        digest[i]
      );
    }

    digestHex[64] = '\0';
  }
}


// =============================================
// 8. GRAPH DATA COLLECTION
// =============================================

void recordGraphData() {

  rateHistory[historyIndex] = hashRate;

  temperatureHistory[historyIndex] =
    chipTemperature;

  historyIndex++;

  if (historyIndex >= GRAPH_POINTS) {

    historyIndex = 0;
  }

  if (historyCount < GRAPH_POINTS) {

    historyCount++;
  }
}


// Retrieve a sample in chronological order.

float getGraphValue(
  int chronologicalIndex,
  bool temperatureGraph
) {

  int oldestIndex =
    (historyIndex - historyCount + GRAPH_POINTS)
    % GRAPH_POINTS;

  int actualIndex =
    (oldestIndex + chronologicalIndex)
    % GRAPH_POINTS;

  if (temperatureGraph) {

    return temperatureHistory[actualIndex];
  }

  return (float)rateHistory[actualIndex];
}


// =============================================
// 9. DISPLAY HEADER
// =============================================

void drawHeader(const char* title) {

  display.clearDisplay();

  display.setTextSize(1);

  display.setTextColor(SH110X_WHITE);

  display.setCursor(0, 0);

  display.print(title);

  // Temperature at top right.

  display.setCursor(94, 0);

  if (isnan(chipTemperature)) {

    display.print("--.-C");

  } else {

    display.print(chipTemperature, 1);

    display.print("C");
  }

  display.drawLine(
    0, 10,
    127, 10,
    SH110X_WHITE
  );
}


// =============================================
// 10. PAGE 1: MINING STATISTICS
// =============================================

void drawMiningScreen() {

  drawHeader("SHA256 LAB");

  display.setCursor(0, 14);

  display.print("HASH RATE");

  display.setCursor(0, 24);

  display.setTextSize(2);

  display.print(hashRate);

  display.setTextSize(1);

  display.println(" H/s");

  display.setCursor(0, 43);

  display.print("TOTAL: ");

  char totalText[24];

  snprintf(
    totalText,
    sizeof(totalText),
    "%llu",
    (unsigned long long)totalHashes
  );

  display.print(totalText);

  display.setCursor(0, 55);

  if (!tempSensorOK) {

    display.print("TEMP SENSOR ERROR");

  }

  else if (thermalPaused) {

    display.print("OVER TEMP - PAUSED");

  }

  else if (paused) {

    display.print("MANUAL PAUSE");

  }

  else {

    display.print("RUNNING");
  }
}


// =============================================
// 11. PAGE 2: LATEST HASH
// =============================================

void drawHashScreen() {

  drawHeader("LATEST SHA-256");

  for (int i = 0; i < 4; i++) {

    display.setCursor(0, 15 + i * 10);

    for (int j = 0; j < 16; j++) {

      display.print(
        digestHex[i * 16 + j]
      );
    }
  }

  display.setCursor(0, 56);

  display.print("NONCE: ");

  char nonceText[24];

  snprintf(
    nonceText,
    sizeof(nonceText),
    "%llu",
    (unsigned long long)lastNonce
  );

  display.print(nonceText);
}


// =============================================
// 12. PAGE 3: SYSTEM INFORMATION
// =============================================

void drawSystemScreen() {

  drawHeader("SYSTEM INFO");

  display.setCursor(0, 15);

  display.println("ESP32-S3 SUPERMINI");

  display.println("SHA-256 / Mbed TLS");

  display.println("SH1106 OLED");

  display.println();

  display.println("UP/DOWN: PAGE");

  display.println("CENTRE: SHUTDOWN");
}


// =============================================
// 13. GRAPH VALUE FORMATTING
// =============================================

void formatRate(
  float value,
  char* output,
  size_t length
) {

  if (value >= 1000000) {

    snprintf(
      output,
      length,
      "%.1fM",
      value / 1000000.0
    );

  }

  else if (value >= 1000) {

    snprintf(
      output,
      length,
      "%.1fk",
      value / 1000.0
    );

  }

  else {

    snprintf(
      output,
      length,
      "%.0f",
      value
    );
  }
}


// =============================================
// 14. PAGE 4 & 5: GRAPH DISPLAY
// =============================================

void drawGraph(bool temperatureGraph) {

  uint16_t windowSeconds =
    graphWindows[graphWindowIndex];

  char title[20];

  snprintf(
    title,
    sizeof(title),
    "%s %lus",
    temperatureGraph ? "TEMP" : "RATE",
    (unsigned long)windowSeconds
  );

  drawHeader(title);

  // Graph dimensions.

  const int plotLeft = 36;
  const int plotRight = 126;

  const int plotTop = 18;
  const int plotBottom = 44;

  const int plotWidth =
    plotRight - plotLeft;

  const int plotHeight =
    plotBottom - plotTop;

  // Determine number of available samples.

  int pointsToShow = historyCount;

  if (pointsToShow > windowSeconds) {

    pointsToShow = windowSeconds;
  }

  // Draw axes.

  display.drawLine(
    plotLeft,
    plotTop,
    plotLeft,
    plotBottom,
    SH110X_WHITE
  );

  display.drawLine(
    plotLeft,
    plotBottom,
    plotRight,
    plotBottom,
    SH110X_WHITE
  );

  if (pointsToShow < 2) {

    display.setCursor(39, 28);

    display.print("WAITING FOR DATA");

    display.setCursor(61, 57);

    display.print("TIME");

    return;
  }

  // Find minimum and maximum.

  float minimum = 999999999.0;

  float maximum = -999999999.0;

  int startIndex =
    historyCount - pointsToShow;

  int validPoints = 0;

  for (int i = 0; i < pointsToShow; i++) {

    float value = getGraphValue(
      startIndex + i,
      temperatureGraph
    );

    if (isnan(value)) {
      continue;
    }

    if (value < minimum) {
      minimum = value;
    }

    if (value > maximum) {
      maximum = value;
    }

    validPoints++;
  }

  if (validPoints < 2) {

    display.setCursor(39, 28);

    display.print("WAITING FOR DATA");

    return;
  }

  // Prevent zero-height scale.

  if (maximum - minimum < 0.01) {

    if (temperatureGraph) {

      minimum -= 1.0;

      maximum += 1.0;

    } else {

      minimum -= 100;

      maximum += 100;
    }
  }

  // Add 10% padding.

  float padding =
    (maximum - minimum) * 0.10;

  minimum -= padding;

  maximum += padding;

  // Draw middle grid line.

  int middleY =
    plotTop + plotHeight / 2;

  for (
    int x = plotLeft + 2;
    x < plotRight;
    x += 4
  ) {

    display.drawPixel(
      x,
      middleY,
      SH110X_WHITE
    );
  }

  // Y-axis labels.

  char topLabel[12];

  char bottomLabel[12];

  if (temperatureGraph) {

    snprintf(
      topLabel,
      sizeof(topLabel),
      "%.1f",
      maximum
    );

    snprintf(
      bottomLabel,
      sizeof(bottomLabel),
      "%.1f",
      minimum
    );

  } else {

    formatRate(
      maximum,
      topLabel,
      sizeof(topLabel)
    );

    formatRate(
      minimum,
      bottomLabel,
      sizeof(bottomLabel)
    );
  }

  display.setCursor(0, 17);

  display.print(topLabel);

  display.setCursor(0, 37);

  display.print(bottomLabel);

  // Draw graph.

  int previousX = -1;

  int previousY = -1;

  for (int i = 0; i < pointsToShow; i++) {

    float value = getGraphValue(
      startIndex + i,
      temperatureGraph
    );

    if (isnan(value)) {

      previousX = -1;

      previousY = -1;

      continue;
    }

    int x = plotLeft +
      (i * plotWidth) /
      (pointsToShow - 1);

    int y = plotBottom -
      (int)(
        ((value - minimum) /
        (maximum - minimum))
        * plotHeight
      );

    if (y < plotTop) {
      y = plotTop;
    }

    if (y > plotBottom) {
      y = plotBottom;
    }

    // Connect samples.

    if (previousX >= 0) {

      display.drawLine(
        previousX,
        previousY,
        x,
        y,
        SH110X_WHITE
      );
    }

    display.drawPixel(
      x,
      y,
      SH110X_WHITE
    );

    previousX = x;

    previousY = y;
  }

  // X-axis labels.

  char leftTimeLabel[12];

  snprintf(
    leftTimeLabel,
    sizeof(leftTimeLabel),
    "-%lus",
    (unsigned long)(pointsToShow - 1)
  );

  display.setCursor(plotLeft, 49);

  display.print(leftTimeLabel);

  display.setCursor(108, 49);

  display.print("NOW");

  display.setCursor(61, 57);

  display.print("TIME");
}


// =============================================
// 15. DISPLAY UPDATE
// =============================================

void updateDisplay() {

  switch (currentScreen) {

    case 0:

      drawMiningScreen();

      break;

    case 1:

      drawHashScreen();

      break;

    case 2:

      drawSystemScreen();

      break;

    case 3:

      drawGraph(false);

      break;

    case 4:

      drawGraph(true);

      break;
  }

  display.display();
}


// =============================================
// 16. SHUTDOWN / DEEP SLEEP
// =============================================

void enterShutdown() {

  Serial.println("Entering deep sleep...");

  // Wait for centre button release.

  while (digitalRead(BUTTON_SELECT) == LOW) {

    delay(10);
  }

  delay(50);

  // Turn off OLED.

  display.clearDisplay();

  display.display();

  Wire.beginTransmission(OLED_ADDRESS);

  Wire.write(0x00);

  Wire.write(0xAE);

  Wire.endTransmission();

  // Configure GPIO5 as RTC wakeup input.

  rtc_gpio_init(GPIO_NUM_5);

  rtc_gpio_set_direction(
    GPIO_NUM_5,
    RTC_GPIO_MODE_INPUT_ONLY
  );

  rtc_gpio_pullup_en(GPIO_NUM_5);

  rtc_gpio_pulldown_dis(GPIO_NUM_5);

  // Wake when centre button pulls GPIO5 LOW.

  esp_err_t result =
    esp_sleep_enable_ext1_wakeup(
      1ULL << BUTTON_SELECT,
      ESP_EXT1_WAKEUP_ANY_LOW
    );

  if (result != ESP_OK) {

    Serial.println("Wakeup configuration failed!");

    rtc_gpio_deinit(GPIO_NUM_5);

    pinMode(BUTTON_SELECT, INPUT_PULLUP);

    // Restore OLED.

    Wire.beginTransmission(OLED_ADDRESS);

    Wire.write(0x00);

    Wire.write(0xAF);

    Wire.endTransmission();

    updateDisplay();

    return;
  }

  Serial.flush();

  // Enter deep sleep.
  // The next centre-button press wakes the board.

  esp_deep_sleep_start();
}


// =============================================
// 17. SETUP
// =============================================

void setup() {

  Serial.begin(115200);

  // Initialize buttons.

  pinMode(BUTTON_UP, INPUT_PULLUP);

  pinMode(BUTTON_SELECT, INPUT_PULLUP);

  pinMode(BUTTON_DOWN, INPUT_PULLUP);

  // Initialize I2C.

  Wire.begin(
    SDA_PIN,
    SCL_PIN
  );

  // Initialize OLED.

  if (!display.begin(OLED_ADDRESS, true)) {

    Serial.println("OLED initialization failed!");

    while (true) {
      delay(100);
    }
  }

  display.clearDisplay();

  display.setTextColor(SH110X_WHITE);

  display.setTextSize(2);

  display.setCursor(0, 15);

  display.println("SHA256");

  display.setCursor(0, 35);

  display.println("LAB v1.3");

  display.display();

  delay(1200);

  // Initialize temperature sensor.

  tempSensorOK =
    initializeTemperatureSensor();

  updateTemperature();

  previousTempTime = millis();

  // Initialize first hash.

  if (!thermalPaused) {

    calculateHash();
  }

  // Initialize timers.

  previousRateTime = millis();

  previousDisplayTime = millis();

  // Initial graph sample will be recorded
  // after the first one-second rate measurement.

  Serial.println("SHA256 LAB v1.3 started!");

}


// =============================================
// 18. MAIN LOOP
// =============================================

void loop() {

  // -----------------------------------------
  // BUTTON INPUTS
  // -----------------------------------------

  // UP

  if (buttonPressed(upButton)) {

    currentScreen--;

    if (currentScreen < 0) {

      currentScreen = 4;
    }
  }

  // DOWN

  if (buttonPressed(downButton)) {

    currentScreen++;

    if (currentScreen > 4) {

      currentScreen = 0;
    }
  }

  // CENTRE

  if (buttonPressed(selectButton)) {

    // SYSTEM INFORMATION:
    // Shut down.

    if (currentScreen == 2) {

      enterShutdown();
    }

    // GRAPHS:
    // Change graph duration.

    else if (
      currentScreen == 3 ||
      currentScreen == 4
    ) {

      graphWindowIndex++;

      if (
        graphWindowIndex >= GRAPH_WINDOW_COUNT
      ) {

        graphWindowIndex = 0;
      }

      Serial.print("Graph window: ");

      Serial.print(
        graphWindows[graphWindowIndex]
      );

      Serial.println(" seconds");
    }

    // OTHER PAGES:
    // Manual pause/resume.

    else {

      paused = !paused;
    }
  }


  // -----------------------------------------
  // TEMPERATURE MONITORING
  // -----------------------------------------

  if (
    millis() - previousTempTime >= 250
  ) {

    updateTemperature();

    previousTempTime = millis();
  }


  // -----------------------------------------
  // HASHING ENGINE
  // -----------------------------------------

  uint32_t batchStart = millis();

  if (
    !paused &&
    !thermalPaused &&
    tempSensorOK
  ) {

    // Short batches keep the interface responsive.

    while (millis() - batchStart < 80) {

      calculateHash();
    }
  }


  // -----------------------------------------
  // HASH RATE + GRAPH RECORDING
  // -----------------------------------------

  if (
    millis() - previousRateTime >= 1000
  ) {

    uint32_t elapsed =
      millis() - previousRateTime;

    uint64_t difference =
      totalHashes - previousRateHashes;

    // Calculate hashes per second.

    hashRate =
      (difference * 1000ULL) / elapsed;

    previousRateHashes = totalHashes;

    previousRateTime = millis();

    // Record one graph sample per rate update.

    recordGraphData();
  }


  // -----------------------------------------
  // DISPLAY REFRESH
  // -----------------------------------------

  if (
    millis() - previousDisplayTime >= 200
  ) {

    updateDisplay();

    previousDisplayTime = millis();
  }

}
