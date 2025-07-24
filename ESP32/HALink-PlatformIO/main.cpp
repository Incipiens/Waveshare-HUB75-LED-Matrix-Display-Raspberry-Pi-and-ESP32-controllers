#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <time.h> 
#include <Adafruit_GFX.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include <FastLED.h>
#include <ArduinoJson.h>  

// config for users

// WiFi
#ifndef WIFI_SSID
#define WIFI_SSID "ssid"
#endif
#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD "password"
#endif

// MQTT Broker
#ifndef MQTT_HOST
#define MQTT_HOST "192.168.1.71"  // IP or hostname
#endif
#ifndef MQTT_PORT
#define MQTT_PORT 1883
#endif
#ifndef MQTT_USER
#define MQTT_USER "" // set to """" if no auth
#endif
#ifndef MQTT_PASS
#define MQTT_PASS "" // set to """" if no auth
#endif

// Device ID used for MQTT client name & optional topic templating
#ifndef DEVICE_ID
#define DEVICE_ID "esp32-ledmatrix"
#endif


// Topic Strings (change to match your HA config)
// cmd topics
static const char* TOPIC_CMD_PAGE = "ha/ledmatrix/cmd/page"; // expected: clock / temps / rotate
static const char* TOPIC_CMD_BRIGHT = "ha/ledmatrix/cmd/brightness"; // expected: 0-255
static const char* TOPIC_CMD_ROTATESECS = "ha/ledmatrix/cmd/rotate_secs";  // expected: seconds in int form

// Example sensor topics
static const char* TOPIC_SENSOR_LR_TEMP = "zigbee2mqtt/Living Room Temp/Humidity";
static const char* TOPIC_SENSOR_BR_TEMP = "zigbee2mqtt/Bedroom Temp/Humidity";
static const char* TOPIC_SENSOR_OUT_TEMP = "ha/ledmatrix/outside";  // outside temp

// Status + Telemetry topics
static const char* TOPIC_STATUS = "ha/ledmatrix/status"; // MQTT LWT and online status
static const char* TOPIC_TELE_PAGE = "ha/ledmatrix/tele/page"; // publishes current page on change

// Default auto-rotation time (ms)
// Can be updated via MQTT
#ifndef DEFAULT_ROTATE_MS
#define DEFAULT_ROTATE_MS (10UL * 1000UL)  // 10 seconds, this can overflow
#endif

// Panel brightness at boot (0-255)
#ifndef DEFAULT_BRIGHTNESS
#define DEFAULT_BRIGHTNESS 80
#endif

// Panel config
#define PANEL_RES_X 64
#define PANEL_RES_Y 32
#define PANEL_CHAIN 1

MatrixPanel_I2S_DMA *dma_display = nullptr;   // global display pointer

// Caching sensors

struct SensorValue {
  float value = NAN;
  uint32_t lastUpdate = 0;   // millis()
};

void publishCurrentPage();   // forward decl for global helper

static SensorValue tempLiving;
static SensorValue tempBedroom;
static SensorValue tempOutdoor;

// Utils

static inline uint16_t colorFromRGB(uint8_t r, uint8_t g, uint8_t b) {
  return dma_display->color565(r, g, b);
}

static inline uint16_t colorFromHSV(uint8_t h, uint8_t s, uint8_t v) {
  CRGB c = CHSV(h, s, v);
  return dma_display->color565(c.r, c.g, c.b);
}

// Base class for pages

class DisplayPage {
public:
  virtual ~DisplayPage() {}
  virtual void begin() {} // called when controller starts (once)
  virtual void onPageSelected() {} // called when page becomes current
  virtual void update(uint32_t now) = 0; // called from main loop
  virtual const char* name() = 0; // name for MQTT commands
};

// Clock page
class ClockPage : public DisplayPage {
public:
  ClockPage(MatrixPanel_I2S_DMA* d) : disp(d) {}
  const char* name() override { return "clock"; }

  void begin() override {
    lastDraw = 0;
  }

  void onPageSelected() override {
    // Force redraw on entry
    lastDraw = 0;
  }

  void update(uint32_t now) override {
    if (now - lastDraw < 250) return;

    time_t t = time(nullptr);
    struct tm timeinfo;
    if (!localtime_r(&t, &timeinfo)) return;

    if (timeinfo.tm_sec == lastDrawnSecond && (now - lastDraw) < 1000) {
      // No change
      return;
    }

    lastDraw = now;
    lastDrawnSecond = timeinfo.tm_sec;

    // Clear screen, this can cause flickering and is not ideal, likely a better solution
    disp->fillScreen(0);

    // Format HH:MM (24h)
    char buf[6];
    snprintf(buf, sizeof(buf), "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);

    // We scale default font 2X to fit 64 width
    disp->setTextWrap(false);
    disp->setTextSize(2);
    disp->setCursor(2, 8); // y roughly 8 for large top line
    disp->setTextColor(colorFromRGB(0, 255, 0));
    disp->print(buf);

    // Reset text size for date line
    disp->setTextSize(1);
    disp->setCursor(0, 24);
    disp->setTextColor(colorFromRGB(0, 180, 255));

    // Format date: DD Mon
    static const char* months[] = {"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};
    char dbuf[16];
    snprintf(dbuf, sizeof(dbuf), "%02d %s", timeinfo.tm_mday, months[timeinfo.tm_mon]);
    disp->print(dbuf);
  }

private:
  MatrixPanel_I2S_DMA* disp;
  uint32_t lastDraw = 0;
  int lastDrawnSecond = -1;
};

// Temperature

class TempPage : public DisplayPage {
public:
  TempPage(MatrixPanel_I2S_DMA* d) : disp(d) {}
  const char* name() override { return "temps"; }

  void begin() override { lastDraw = 0; }
  void onPageSelected() override { lastDraw = 0; }

  void update(uint32_t now) override {
    if (now - lastDraw < 500) return;
    lastDraw = now;

    disp->fillScreen(0);
    disp->setTextWrap(false);

    // Header
    disp->setCursor(0, 0);
    disp->setTextColor(colorFromRGB(0, 255, 255));
    disp->setTextSize(1);
    disp->print("Temps");

    // Row spacing, 10px from top baseline (line heights vary with text size but 1 is small)
    int y = 10;
    drawSensorRow(y,  "LR", tempLiving.value);
    y += 10;
    //drawSensorRow(y,  "BR", tempBedroom.value);
    //y += 10;
    drawSensorRow(y,  "Out", tempOutdoor.value);
  }

private:
  MatrixPanel_I2S_DMA* disp;
  uint32_t lastDraw = 0;

  void drawSensorRow(int y, const char* label, float v) {
    disp->setTextSize(1);
    disp->setCursor(0, y);
    disp->setTextColor(colorFromRGB(255, 255, 0));
    disp->print(label);
    disp->print(": ");
    if (isnan(v)) {
      disp->setTextColor(colorFromRGB(255, 0, 0));
      disp->print("--.-");
    } else {
      disp->setTextColor(colorFromRGB(0, 255, 0));
      // Format to 1 decimal place
      char buf[8];
      dtostrf(v, 0, 1, buf);   // width auto, 1 dp
      disp->print(buf);
      disp->print("C");
    }
  }
};

// page controller

class PageController {
public:
  PageController() {}

  void addPage(DisplayPage* p) {
    if (pageCount >= MAX_PAGES) return;
    pages[pageCount++] = p;
  }

  void beginAll() {
    for (int i=0; i<pageCount; i++) pages[i]->begin();
    // Activate first page by default
    selectIndex(0, false);
  }

  void setRotateMs(uint32_t ms) { rotateMs = ms; }
  uint32_t getRotateMs() const { return rotateMs; }

  void unlockRotation() { rotationLocked = false; }
  void lockRotation()   { rotationLocked = true; }

  void setPageByName(const char* n) {
    for (int i=0; i<pageCount; i++) {
      if (strcmp(pages[i]->name(), n) == 0) {
        selectIndex(i, true); // lock rotation when directly selected via MQTT
        return;
      }
    }
  }

  void update(uint32_t now) {
    // Auto-rotate only if not locked and more than 1 page and rotateMs>0
    if (!rotationLocked && pageCount > 1 && rotateMs > 0) {
      if (now - lastPageChange >= rotateMs) {
        int next = (currentIndex + 1) % pageCount;
        selectIndex(next, false); // do not lock, standard rotation
      }
    }
    if (pageCount == 0) return;
    pages[currentIndex]->update(now);
  }

  const char* currentPageName() const {
    if (pageCount == 0) return "";
    return pages[currentIndex]->name();
  }

  bool isRotationLocked() const { return rotationLocked; }

private:
  static const int MAX_PAGES = 8;
  DisplayPage* pages[MAX_PAGES];
  int pageCount = 0;
  int currentIndex = 0;
  bool rotationLocked = false;
  uint32_t rotateMs = DEFAULT_ROTATE_MS;
  uint32_t lastPageChange = 0;

  void selectIndex(int idx, bool lock) {
    if (idx < 0 || idx >= pageCount) return;
    currentIndex = idx;
    lastPageChange = millis();
    if (lock) rotationLocked = true;
    pages[currentIndex]->onPageSelected();
    publishCurrentPage();
  }
};

static PageController pageController;  // global instance

// WiFi MQTT decl

WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);


// Forward decl
void mqttCallback(char* topic, byte* payload, unsigned int length);
void mqttEnsureConnected();

// Called by PageController when page changes
void publishCurrentPage() {
  if (!mqttClient.connected()) return;
  mqttClient.publish(TOPIC_TELE_PAGE, pageController.currentPageName(), false);
}

// Block until Wi-Fi is connected

void wifiConnectBlocking() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(250);
  }
}

/* Time config
Europe/Dublin TZ: IST (Irish Standard Time, UTC+1 summer) / GMT (UTC) winter
POSIX TZ string: "GMT0IST,M3.5.0/1,M10.5.0/2"
  - Transition: last Sunday of March at 01:00 -> +1h
  - Transition back: last Sunday of October at 02:00 -> 0h
*/

#define TZ_EUROPE_DUBLIN "GMT0IST,M3.5.0/1,M10.5.0/2"

void timeSetup() {
  // configTzTime applies TZ and starts NTP
  configTzTime(TZ_EUROPE_DUBLIN, "pool.ntp.org", "time.nist.gov", "time.google.com");
  // Wait briefly for time
  time_t now = 0;
  int retry = 0;
  while (now < 100000 && retry < 20) { // check if time is less than < ~Jan 1970? simple check
    delay(250);
    now = time(nullptr);
    retry++;
  }
}

// MQTT Connect and subscribe to topics
void mqttConnectBlocking() {
  // Set LWT so HA knows when we're offline
  while (!mqttClient.connected()) {
    if (mqttClient.connect(DEVICE_ID, MQTT_USER, MQTT_PASS,
                           TOPIC_STATUS, 0, true, "offline")) {
      // Publish that we're online (retained so HA sees current state)
      mqttClient.publish(TOPIC_STATUS, "online", true);

      // Subscribe to commands
      mqttClient.subscribe(TOPIC_CMD_PAGE);
      mqttClient.subscribe(TOPIC_CMD_BRIGHT);
      mqttClient.subscribe(TOPIC_CMD_ROTATESECS);

      // Subscribe to sensor topics
      mqttClient.subscribe(TOPIC_SENSOR_LR_TEMP);
      mqttClient.subscribe(TOPIC_SENSOR_BR_TEMP);
      mqttClient.subscribe(TOPIC_SENSOR_OUT_TEMP);

      // Publish current page once connected
      publishCurrentPage();

    } else {
      delay(2000); // retry
    }
  }
}

void mqttEnsureConnected() {
  if (!mqttClient.connected()) {
    mqttConnectBlocking();
  }
}

// MQTT callback helper to parse temperature JSON

static float parseTempField(const char* json, size_t len) {
  StaticJsonDocument<128> doc;
  DeserializationError err = deserializeJson(doc, json, len);
  if (err) return NAN;  // bad JSON
  return doc["temperature"] | NAN;  // returns value or NAN if key missing
}

// MQTT callback function

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  // Copy payload into temp string (not null-terminated in callback), buffer of fixed size is used
  char buf[128];
  unsigned int len = (length < sizeof(buf) - 1) ? length : sizeof(buf) - 1;
  memcpy(buf, payload, len);
  buf[len] = '\0';

  String msg(buf);
  msg.trim();

  // Commands
  if (strcmp(topic, TOPIC_CMD_PAGE) == 0) {
    if (msg.equalsIgnoreCase("rotate")) {
      pageController.unlockRotation();
    } else {
      pageController.setPageByName(msg.c_str());
    }
  }

  else if (strcmp(topic, TOPIC_SENSOR_BR_TEMP) == 0) {
    // Bedroom temp sensor
    tempBedroom.value = parseTempField(buf, len);
    tempBedroom.lastUpdate = millis();
  }
  else if (strcmp(topic, TOPIC_SENSOR_OUT_TEMP) == 0) {
    // Outdoor temp sensor
    tempOutdoor.value = msg.toFloat();
    tempOutdoor.lastUpdate = millis();
  }
  else if (strcmp(topic, TOPIC_CMD_BRIGHT) == 0) {
    int b = msg.toInt();
    if (b < 13) b = 13; // clamping brightness to 5% or above
    if (b > 255) b = 255;
    dma_display->setBrightness8((uint8_t)b);
  }
  else if (strcmp(topic, TOPIC_CMD_ROTATESECS) == 0) {
    uint32_t secs = msg.toInt();
    pageController.setRotateMs(secs * 1000UL);
  }
  else if (strcmp(topic, TOPIC_SENSOR_LR_TEMP) == 0) {
    tempLiving.value = parseTempField(buf, len);
    tempLiving.lastUpdate = millis();
  }
}

// Init display

void setupMatrix() {
  /* Adjust pin mapping to match your wiring; these are example pins. G and B pins are swapped as my Waveshare P2.5 has them incorrectly mapped in hardware
  
  These pins, on my unit, map to:
  #define R1_PIN 19
  #define G1_PIN 13
  #define B1_PIN 18
  #define R2_PIN 5
  #define G2_PIN 12
  #define B2_PIN 17

  #define A_PIN 16
  #define B_PIN 14
  #define C_PIN 4
  #define D_PIN 27
  #define E_PIN 25

  #define LAT_PIN 26
  #define OE_PIN 15
  #define CLK_PIN 2
  */

  HUB75_I2S_CFG::i2s_pins _pins = {19, 13, 18, 5, 12, 17, 16, 14, 4, 27, 25, 26, 15, 2};
  HUB75_I2S_CFG mxconfig(PANEL_RES_X, PANEL_RES_Y, PANEL_CHAIN, _pins);
  mxconfig.i2sspeed = HUB75_I2S_CFG::HZ_10M; // Adjust for panel quality

  dma_display = new MatrixPanel_I2S_DMA(mxconfig);
  dma_display->begin();
  dma_display->setBrightness8(DEFAULT_BRIGHTNESS);
  dma_display->clearScreen();
}

 // Allocating global pages after dma_display is created in setup()

ClockPage* clockPage = nullptr;
TempPage*  tempPage  = nullptr;

// Establish Wi-Fi and MQTT, pull time, create pages

void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println();
  Serial.println("ESP32 LED Matrix Home Assistant MQTT Dashboard");

  setupMatrix();

  // Wi-Fi first
  wifiConnectBlocking();
  Serial.print("WiFi connected. IP: ");
  Serial.println(WiFi.localIP());

  // Time (NTP)
  timeSetup();
  Serial.println("Time synced.");

  // MQTT
  mqttClient.setServer(MQTT_HOST, MQTT_PORT);
  mqttClient.setCallback(mqttCallback);
  mqttConnectBlocking();
  Serial.println("MQTT connected.");

  // Create pages now that display exists
  clockPage = new ClockPage(dma_display);
  tempPage  = new TempPage(dma_display);

  // Register pages with controller
  pageController.addPage(clockPage);
  pageController.addPage(tempPage);
  pageController.beginAll();
  Serial.println("Pages initialized.");
}

// Main loop

void loop() {
  // Maintain Wi-Fi
  if (WiFi.status() != WL_CONNECTED) {
    wifiConnectBlocking();
  }

  // Maintain MQTT connection & service incoming
  mqttEnsureConnected();
  mqttClient.loop();

  // Update display pages
  uint32_t now = millis();
  pageController.update(now);
}


