#include <Arduino.h>
#include <Adafruit_NeoPixel.h>

#ifndef ARDUINO_USB_MODE
#error This sketch requires an ESP32-S3 board with native USB support.
#elif ARDUINO_USB_MODE == 1
#error Set Tools > USB Mode to USB-OTG (TinyUSB). Hardware CDC and JTAG mode cannot build USB HID keyboard sketches.
#else
#include "USB.h"
#include "USBHID.h"
#include "USBHIDKeyboard.h"
#endif

#include <HijelHID_BLEKeyboard.h>

// -----------------------------------------------------------------------------
// InfiniteTap one-button keyboard
// - Uses GPIO 37 as the external switch input on the ESP32-S3 board.
// - Prefers USB HID keyboard when a real USB host is present.
// - Falls back to BLE HID keyboard when powered from a charger or power-only source.
// - Sends F13 to reduce interference with normal computer input.
// -----------------------------------------------------------------------------

static const char *USB_PRODUCT_NAME = "InfiniteTap Key F13 USB";
static const char *BLE_DEVICE_NAME = "InfiniteTap Key F13 BT";
static const char *DEVICE_MANUFACTURER = "Primax";

static const uint8_t SWITCH_PIN = 37;
static const uint8_t CENTER_BUTTON_PIN = 3;
static const uint8_t NUM_LEDS = 1;

static const unsigned long DEBOUNCE_MS = 20;
static const unsigned long USB_PROBE_MS = 1200;
static const unsigned long USB_FALLBACK_MS = 400;
static const unsigned long BOND_RESET_HOLD_MS = 3000;
static const unsigned long RESET_BLINK_MS = 120;
static const unsigned long RESET_SEQUENCE_MS = 1600;
static const unsigned long BOOT_FLASH_MS = 180;
static const unsigned long PROBLEM_BLINK_MS = 150;
static const unsigned long PROBLEM_GAP_MS = 120;
static const unsigned long PROBLEM_FLASH_COUNT = 3;
static const uint8_t LED_BRIGHTNESS = 16;

// HID Usage ID for F13. Using the raw code avoids key macro clashes
// between the USB and BLE keyboard libraries.
static const uint8_t SWITCH_KEYCODE = 0x68;

enum OutputMode {
  OUTPUT_PROBING,
  OUTPUT_USB,
  OUTPUT_BLE
};

Adafruit_NeoPixel strip48(NUM_LEDS, 48, NEO_GRB + NEO_KHZ800);
Adafruit_NeoPixel strip38(NUM_LEDS, 38, NEO_GRB + NEO_KHZ800);

USBHID usbHid;
USBHIDKeyboard usbKeyboard;
HijelHID_BLEKeyboard bleKeyboard(BLE_DEVICE_NAME, DEVICE_MANUFACTURER, 100);

bool rawSwitchState = HIGH;
bool stableSwitchState = HIGH;
unsigned long switchDebounceStart = 0;

bool outputKeyPressed = false;

bool centerButtonWasDown = false;
unsigned long centerButtonPressStart = 0;
bool bondResetInProgress = false;
unsigned long bondResetStart = 0;

bool bootFlashActive = false;
unsigned long bootFlashUntil = 0;
bool problemIndicatorPending = false;
bool problemIndicatorActive = false;
unsigned long problemIndicatorStart = 0;

bool usbStarted = false;
bool usbSuspended = false;
unsigned long bootStartedAt = 0;
unsigned long lastUsbReadyAt = 0;

bool bleStarted = false;
bool lastBleConnectedState = false;
bool lastBlePairedState = false;

OutputMode activeMode = OUTPUT_PROBING;

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

void usbEventCallback(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
  if (event_base != ARDUINO_USB_EVENTS) {
    return;
  }

  arduino_usb_event_data_t *data = reinterpret_cast<arduino_usb_event_data_t *>(event_data);
  switch (event_id) {
    case ARDUINO_USB_STARTED_EVENT:
      usbStarted = true;
      usbSuspended = false;
      Serial.println("[USB] Device stack started");
      break;
    case ARDUINO_USB_STOPPED_EVENT:
      usbStarted = false;
      usbSuspended = false;
      Serial.println("[USB] Device stack stopped");
      break;
    case ARDUINO_USB_SUSPEND_EVENT:
      usbSuspended = true;
      Serial.printf("[USB] Suspended (remote wakeup: %u)\n", data->suspend.remote_wakeup_en);
      break;
    case ARDUINO_USB_RESUME_EVENT:
      usbSuspended = false;
      Serial.println("[USB] Resumed");
      break;
    default:
      break;
  }
}

bool isUsbReady() {
  if (!usbStarted || usbSuspended) {
    return false;
  }
  return usbHid.ready();
}

void startBleIfNeeded() {
  if (bleStarted) {
    return;
  }

  bleKeyboard.setSecurityMode(HIDSecurity::JustWorks);
  bleKeyboard.setLogLevel(HIDLogLevel::Normal);
  bleKeyboard.setTapDelay(25);
  bleKeyboard.setKeyGap(25);
  bleKeyboard.begin();

  bleStarted = true;
  lastBleConnectedState = bleKeyboard.isConnected();
  lastBlePairedState = bleKeyboard.isPaired();
  Serial.println("[BLE] Advertising started");
}

void stopBleIfNeeded() {
  if (!bleStarted) {
    return;
  }

  bleKeyboard.end();
  bleStarted = false;
  lastBleConnectedState = false;
  lastBlePairedState = false;
  Serial.println("[BLE] Stopped");
}

void releaseOutputKey() {
  if (!outputKeyPressed) {
    return;
  }

  if (activeMode == OUTPUT_USB) {
    usbKeyboard.releaseRaw(SWITCH_KEYCODE);
    Serial.println("[INPUT] Release -> USB F13 up");
  } else if (activeMode == OUTPUT_BLE) {
    bleKeyboard.releaseAll();
    Serial.println("[INPUT] Release -> BLE F13 up");
  }

  outputKeyPressed = false;
}

void setMode(OutputMode newMode) {
  if (activeMode == newMode) {
    return;
  }

  releaseOutputKey();

  if (newMode == OUTPUT_USB) {
    stopBleIfNeeded();
    Serial.println("[MODE] USB keyboard active");
  } else if (newMode == OUTPUT_BLE) {
    startBleIfNeeded();
    Serial.println("[MODE] BLE keyboard active");
  } else {
    Serial.println("[MODE] Probing USB host");
  }

  activeMode = newMode;
}

void pressOutputKey() {
  if (outputKeyPressed) {
    return;
  }

  if (activeMode == OUTPUT_USB) {
    if (!isUsbReady()) {
      Serial.println("[INPUT] USB not ready; press ignored");
      return;
    }
    if (usbKeyboard.pressRaw(SWITCH_KEYCODE) == 0) {
      Serial.println("[INPUT] USB key press failed");
      return;
    }
    Serial.println("[INPUT] Press -> USB F13 down");
  } else if (activeMode == OUTPUT_BLE) {
    if (!bleStarted || !bleKeyboard.isPaired()) {
      Serial.println("[INPUT] BLE not paired; press ignored");
      return;
    }
    bleKeyboard.press(SWITCH_KEYCODE);
    Serial.println("[INPUT] Press -> BLE F13 down");
  } else {
    Serial.println("[INPUT] Still probing transport; press ignored");
    return;
  }

  outputKeyPressed = true;
}

void handleStableSwitchEdge(bool newStableState) {
  if (newStableState == LOW) {
    pressOutputKey();
  } else {
    releaseOutputKey();
  }
}

void updateSwitchInput(unsigned long now) {
  bool currentRawState = digitalRead(SWITCH_PIN);

  if (currentRawState != rawSwitchState) {
    rawSwitchState = currentRawState;
    switchDebounceStart = now;
  }

  if ((now - switchDebounceStart) >= DEBOUNCE_MS && stableSwitchState != rawSwitchState) {
    stableSwitchState = rawSwitchState;
    handleStableSwitchEdge(stableSwitchState);
  }
}

void startBondReset(unsigned long now) {
  bondResetInProgress = true;
  bondResetStart = now;
  releaseOutputKey();
  startBleIfNeeded();
  Serial.println("[BLE] Long press detected. BLE bond reset requested");
}

void updateCenterButton(unsigned long now) {
  bool buttonDown = (digitalRead(CENTER_BUTTON_PIN) == LOW);

  if (buttonDown) {
    if (!centerButtonWasDown) {
      centerButtonWasDown = true;
      centerButtonPressStart = now;
    }

    if (!bondResetInProgress && (now - centerButtonPressStart) >= BOND_RESET_HOLD_MS) {
      startBondReset(now);
    }
  } else {
    centerButtonWasDown = false;
  }
}

void updateBondResetState(unsigned long now) {
  if (!bondResetInProgress) {
    return;
  }

  unsigned long elapsed = now - bondResetStart;
  if (elapsed < RESET_SEQUENCE_MS) {
    bool blinkOn = ((elapsed / RESET_BLINK_MS) % 2) == 0;
    setColor(blinkOn ? 255 : 0, 0, blinkOn ? 255 : 0);
    return;
  }

  Serial.println("[BLE] Deleting all bonds and rebooting");
  bleKeyboard.clearBonds();
  bleKeyboard.end();
  ESP.restart();
}

void updateBleConnectionLog() {
  bool bleConnected = bleStarted && bleKeyboard.isConnected();
  bool blePaired = bleStarted && bleKeyboard.isPaired();

  if (bleConnected != lastBleConnectedState) {
    if (bleConnected) {
      Serial.println("[BLE] Connected");
    } else {
      Serial.println("[BLE] Disconnected");
      if (activeMode == OUTPUT_BLE) {
        releaseOutputKey();
      }
    }
    lastBleConnectedState = bleConnected;
  }

  if (blePaired != lastBlePairedState) {
    if (blePaired) {
      Serial.println("[BLE] Ready to send");
    } else {
      Serial.println("[BLE] Not ready yet");
    }
    lastBlePairedState = blePaired;
  }
}

void updateTransportMode(unsigned long now) {
  if (bondResetInProgress) {
    return;
  }

  bool usbReady = isUsbReady();
  if (usbReady) {
    lastUsbReadyAt = now;
  }

  if (activeMode != OUTPUT_USB && usbReady) {
    setMode(OUTPUT_USB);
    return;
  }

  if (activeMode == OUTPUT_USB) {
    if ((now - lastUsbReadyAt) > USB_FALLBACK_MS) {
      setMode(OUTPUT_BLE);
    }
    return;
  }

  if (activeMode == OUTPUT_PROBING) {
    if ((now - bootStartedAt) >= USB_PROBE_MS) {
      setMode(usbReady ? OUTPUT_USB : OUTPUT_BLE);
    }
    return;
  }

  if (activeMode == OUTPUT_BLE && usbReady) {
    setMode(OUTPUT_USB);
  }
}

void startProblemIndicator(unsigned long now) {
  problemIndicatorPending = false;
  problemIndicatorActive = true;
  problemIndicatorStart = now;
}

bool updateProblemIndicator(unsigned long now) {
  if (!problemIndicatorActive) {
    return false;
  }

  const unsigned long cycleMs = PROBLEM_BLINK_MS + PROBLEM_GAP_MS;
  const unsigned long totalMs = PROBLEM_FLASH_COUNT * cycleMs;
  unsigned long elapsed = now - problemIndicatorStart;

  if (elapsed >= totalMs) {
    problemIndicatorActive = false;
    setColor(0, 0, 0);
    return false;
  }

  if ((elapsed % cycleMs) < PROBLEM_BLINK_MS) {
    setColor(255, 96, 0);
  } else {
    setColor(0, 0, 0);
  }

  return true;
}

void updateStatusLed(unsigned long now) {
  if (bondResetInProgress) {
    updateBondResetState(now);
    return;
  }

  if (bootFlashActive) {
    if (now < bootFlashUntil) {
      setColor(0, 48, 0);
      return;
    }

    bootFlashActive = false;
    setColor(0, 0, 0);

    if (problemIndicatorPending) {
      startProblemIndicator(now);
    }
  }

  if (updateProblemIndicator(now)) {
    return;
  }

  setColor(0, 0, 0);
}

void setup() {
  Serial.begin(115200);
  Serial.println();
  Serial.println("========================================");
  Serial.println("InfiniteTap One-Button Keyboard");
  Serial.println("========================================");

  strip48.begin();
  strip38.begin();
  strip48.setBrightness(LED_BRIGHTNESS);
  strip38.setBrightness(LED_BRIGHTNESS);
  setColor(0, 0, 0);

  pinMode(SWITCH_PIN, INPUT_PULLUP);
  pinMode(CENTER_BUTTON_PIN, INPUT_PULLUP);

  rawSwitchState = digitalRead(SWITCH_PIN);
  stableSwitchState = rawSwitchState;
  bootStartedAt = millis();
  lastUsbReadyAt = bootStartedAt;
  bootFlashActive = true;
  bootFlashUntil = bootStartedAt + BOOT_FLASH_MS;

  if (stableSwitchState == LOW) {
    problemIndicatorPending = true;
    Serial.println("[WARN] Boot detected the external switch already pressed");
  }

  if (digitalRead(CENTER_BUTTON_PIN) == LOW) {
    problemIndicatorPending = true;
    Serial.println("[WARN] Boot detected the center button already pressed");
  }

  USB.onEvent(usbEventCallback);
  USB.VID(0x303A);
  USB.PID(0x1013);
  USB.productName(USB_PRODUCT_NAME);
  USB.manufacturerName(DEVICE_MANUFACTURER);

  usbHid.begin();
  usbKeyboard.begin();
  USB.begin();

  Serial.println("[USB] Waiting for host enumeration");
  Serial.println("[BLE] Will start only if USB host is not ready");
  Serial.printf("[INFO] USB name: %s\n", USB_PRODUCT_NAME);
  Serial.printf("[INFO] BLE name: %s\n", BLE_DEVICE_NAME);
  Serial.println("[MAP] Physical switch -> F13 key");
  Serial.println("[INFO] LED policy:");
  Serial.println("       Boot only      -> short green flash");
  Serial.println("       Problem only   -> orange blink x3");
  Serial.println("       Bond reset     -> magenta blink");
}

void loop() {
  unsigned long now = millis();

  updateBleConnectionLog();
  updateCenterButton(now);
  updateTransportMode(now);

  if (!bondResetInProgress) {
    updateSwitchInput(now);
  }

  updateStatusLed(now);
}
