#include <Adafruit_NeoPixel.h>

// -----------------------------------------------------------------------------
// InfiniteTap hardware input test
// - Target board: ESP32-S3
// - External switch input: GPIO 37
// - Onboard RGB LED candidates: GPIO 48 / GPIO 38
// - Boot identify: red -> green -> blue -> white
// - Released: green
// - Pressed to GND: red
// -----------------------------------------------------------------------------

static const uint8_t SWITCH_PIN = 37;
static const uint8_t NUM_LEDS = 1;
static const uint8_t LED_BRIGHTNESS = 32;
static const unsigned long DEBOUNCE_MS = 20;
static const unsigned long BOOT_STEP_MS = 220;

Adafruit_NeoPixel strip48(NUM_LEDS, 48, NEO_GRB + NEO_KHZ800);
Adafruit_NeoPixel strip38(NUM_LEDS, 38, NEO_GRB + NEO_KHZ800);

bool rawSwitchState = HIGH;
bool stableSwitchState = HIGH;
unsigned long switchDebounceStart = 0;
unsigned long bootStartedAt = 0;

void setColor(uint8_t r, uint8_t g, uint8_t b) {
  static uint8_t lastR = 255;
  static uint8_t lastG = 255;
  static uint8_t lastB = 255;

  if (r == lastR && g == lastG && b == lastB) {
    return;
  }

  lastR = r;
  lastG = g;
  lastB = b;
  uint32_t color48 = strip48.Color(r, g, b);
  uint32_t color38 = strip38.Color(r, g, b);
  strip48.setPixelColor(0, color48);
  strip38.setPixelColor(0, color38);
  strip48.show();
  strip38.show();
}

void updateLedFromState(unsigned long now) {
  unsigned long bootElapsed = now - bootStartedAt;

  if (bootElapsed < BOOT_STEP_MS) {
    setColor(255, 0, 0);
    return;
  }

  if (bootElapsed < (BOOT_STEP_MS * 2)) {
    setColor(0, 255, 0);
    return;
  }

  if (bootElapsed < (BOOT_STEP_MS * 3)) {
    setColor(0, 0, 255);
    return;
  }

  if (bootElapsed < (BOOT_STEP_MS * 4)) {
    setColor(255, 255, 255);
    return;
  }

  if (stableSwitchState == LOW) {
    setColor(255, 0, 0);
  } else {
    setColor(0, 255, 0);
  }
}

void setup() {
  strip48.begin();
  strip38.begin();
  strip48.setBrightness(LED_BRIGHTNESS);
  strip38.setBrightness(LED_BRIGHTNESS);
  setColor(0, 0, 0);

  pinMode(SWITCH_PIN, INPUT_PULLUP);

  rawSwitchState = digitalRead(SWITCH_PIN);
  stableSwitchState = rawSwitchState;
  switchDebounceStart = millis();
  bootStartedAt = switchDebounceStart;
}

void loop() {
  unsigned long now = millis();
  bool currentRawState = digitalRead(SWITCH_PIN);

  if (currentRawState != rawSwitchState) {
    rawSwitchState = currentRawState;
    switchDebounceStart = now;
  }

  if ((now - switchDebounceStart) >= DEBOUNCE_MS && stableSwitchState != rawSwitchState) {
    stableSwitchState = rawSwitchState;
  }

  updateLedFromState(now);
}
