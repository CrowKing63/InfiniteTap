#include <Adafruit_NeoPixel.h>
#include <HijelHID_BLEKeyboard.h>

// -----------------------------------------------------------------------------
// InfiniteTap BLE 스위치 허브
// - 물리 스위치 입력 1개
// - BLE HID 키 출력 1개
// - 누름   -> 키 다운
// - 떼기   -> 키 업
// 탭/홀드 판정은 Vision Pro가 키 다운 유지 시간으로 처리한다.
// -----------------------------------------------------------------------------

// 하드웨어 핀
#define SWITCH_PIN 8
#define LED_PIN 2
#define BUTTON_PIN 3
#define NUM_LEDS 1

// 타이밍
#define DEBOUNCE_MS 20
#define EVENT_FLASH_MS 120
#define ADVERTISING_BLINK_MS 500
#define BOND_RESET_HOLD_MS 3000
#define RESET_BLINK_MS 120
#define RESET_SEQUENCE_MS 1600

// HID 키 매핑
#define SWITCH_KEYCODE KEY_SPACE

HijelHID_BLEKeyboard bleKeyboard("InfiniteTap", "Primax", 100);
Adafruit_NeoPixel strip(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);

bool rawSwitchState = HIGH;
bool stableSwitchState = HIGH;
unsigned long switchDebounceStart = 0;

bool switchKeyPressed = false;

unsigned long eventFlashUntil = 0;
unsigned long advertisingBlinkMark = 0;
bool advertisingBlinkOn = false;

bool centerButtonWasDown = false;
unsigned long centerButtonPressStart = 0;
bool bondResetInProgress = false;
unsigned long bondResetStart = 0;

bool lastConnectedState = false;
bool lastPairedState = false;

void setColor(uint8_t r, uint8_t g, uint8_t b) {
  strip.setPixelColor(0, strip.Color(r, g, b));
  strip.show();
}

void pressSwitchKey(unsigned long now) {
  if (switchKeyPressed) {
    return;
  }

  if (!bleKeyboard.isConnected()) {
    Serial.println("[INPUT] BLE 미연결 상태라서 누름 입력을 무시합니다.");
    return;
  }

  bleKeyboard.press(SWITCH_KEYCODE);
  switchKeyPressed = true;
  eventFlashUntil = now + EVENT_FLASH_MS;
  Serial.println("[INPUT] 스위치 누름 -> 키 다운");
}

void releaseSwitchKey() {
  if (!switchKeyPressed) {
    return;
  }

  bleKeyboard.releaseAll();
  switchKeyPressed = false;
  Serial.println("[INPUT] 스위치 해제 -> 키 업");
}

void logBleStateIfChanged() {
  bool connected = bleKeyboard.isConnected();
  bool paired = bleKeyboard.isPaired();

  if (connected != lastConnectedState) {
    if (connected) {
      Serial.println("[BLE] 연결되었습니다.");
    } else {
      Serial.println("[BLE] 연결이 끊어졌습니다. 자동으로 재광고 상태로 돌아갑니다.");
      releaseSwitchKey();
    }
    lastConnectedState = connected;
  }

  if (paired != lastPairedState) {
    if (paired) {
      Serial.println("[BLE] 본딩 정보가 있습니다.");
    } else {
      Serial.println("[BLE] 저장된 본딩 정보가 없습니다.");
    }
    lastPairedState = paired;
  }
}

void handleStableSwitchEdge(bool newStableState, unsigned long now) {
  if (newStableState == LOW) {
    pressSwitchKey(now);
  } else {
    releaseSwitchKey();
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
    handleStableSwitchEdge(stableSwitchState, now);
  }
}

void startBondReset(unsigned long now) {
  bondResetInProgress = true;
  bondResetStart = now;
  releaseSwitchKey();
  Serial.println("[BLE] 중앙 버튼 롱프레스 감지. 본딩 초기화를 시작합니다.");
}

void updateCenterButton(unsigned long now) {
  bool buttonDown = (digitalRead(BUTTON_PIN) == LOW);

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
    if (blinkOn) {
      setColor(255, 0, 255);
    } else {
      setColor(0, 0, 0);
    }
    return;
  }

  Serial.println("[BLE] 저장된 본딩 정보를 삭제합니다.");
  bleKeyboard.clearBonds();

  Serial.println("[BLE] BLE 스택을 재시작합니다.");
  bleKeyboard.end();
  ESP.restart();
}

void updateStatusLed(unsigned long now) {
  if (bondResetInProgress) {
    updateBondResetState(now);
    return;
  }

  if (now < eventFlashUntil) {
    setColor(255, 0, 0);
    return;
  }

  if (bleKeyboard.isConnected()) {
    setColor(0, 255, 0);
    return;
  }

  if ((now - advertisingBlinkMark) >= ADVERTISING_BLINK_MS) {
    advertisingBlinkMark = now;
    advertisingBlinkOn = !advertisingBlinkOn;
  }

  if (advertisingBlinkOn) {
    setColor(0, 0, 255);
  } else {
    setColor(0, 0, 0);
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println();
  Serial.println("========================================");
  Serial.println("InfiniteTap BLE 스위치 허브 시작");
  Serial.println("========================================");

  strip.begin();
  strip.setBrightness(100);
  setColor(0, 0, 0);

  pinMode(SWITCH_PIN, INPUT_PULLUP);
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  bleKeyboard.setSecurityMode(HIDSecurity::JustWorks);
  bleKeyboard.setLogLevel(HIDLogLevel::Normal);
  bleKeyboard.setTapDelay(25);
  bleKeyboard.setKeyGap(25);
  bleKeyboard.begin();

  rawSwitchState = digitalRead(SWITCH_PIN);
  stableSwitchState = rawSwitchState;
  lastConnectedState = bleKeyboard.isConnected();
  lastPairedState = bleKeyboard.isPaired();

  Serial.println("[BLE] 광고를 시작했습니다. 연결 전에는 파란 LED가 점멸합니다.");
  Serial.println("[INFO] 장치 이름: InfiniteTap");
  Serial.println("[INFO] 입력 매핑:");
  Serial.println("       스위치 누름  -> Space key down");
  Serial.println("       스위치 해제  -> Space key up");
  Serial.println("[INFO] 타이밍:");
  Serial.println("       디바운스: 20ms");
  Serial.println("       로컬 tap/hold 분기: 비활성화");
}

void loop() {
  unsigned long now = millis();

  logBleStateIfChanged();
  updateCenterButton(now);

  if (!bondResetInProgress) {
    updateSwitchInput(now);
  }

  updateStatusLed(now);
}
