#include <Adafruit_NeoPixel.h>
#include <HijelHID_BLEKeyboard.h>

// -----------------------------------------------------------------------------
// InfiniteTap BLE 스위치 허브
// - 물리 스위치 입력 1개
// - BLE HID 키 출력 1개
// - 누름   -> 키 다운
// - 떼기   -> 키 업
// 탭/홀드 판정은 호스트 기기가 키 다운 유지 시간으로 처리한다.
// -----------------------------------------------------------------------------

// 하드웨어 핀
#define SWITCH_PIN 4
#define LED_PIN 2
#define BUTTON_PIN 3
#define NUM_LEDS 1

// 타이밍
#define DEBOUNCE_MS 20
#define SECONDARY_HOLD_MS 5000
#define BOND_RESET_HOLD_MS 3000
#define RESET_BLINK_MS 120
#define RESET_SEQUENCE_MS 1600
#define BOOT_FLASH_MS 180
#define PROBLEM_BLINK_MS 150
#define PROBLEM_GAP_MS 120
#define PROBLEM_FLASH_COUNT 3
#define LED_BRIGHTNESS 16

// HID 키 매핑
#define PRIMARY_SWITCH_KEYCODE KEY_F13
#define SECONDARY_SWITCH_KEYCODE KEY_F14

HijelHID_BLEKeyboard bleKeyboard("InfiniteTap", "Primax", 100);
Adafruit_NeoPixel strip(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);

bool rawSwitchState = HIGH;
bool stableSwitchState = HIGH;
unsigned long switchDebounceStart = 0;
unsigned long switchPressStart = 0;

bool switchKeyPressed = false;
bool secondaryHoldTriggered = false;

bool centerButtonWasDown = false;
unsigned long centerButtonPressStart = 0;
bool bondResetInProgress = false;
unsigned long bondResetStart = 0;

bool bootFlashActive = false;
unsigned long bootFlashUntil = 0;
bool problemIndicatorPending = false;
bool problemIndicatorActive = false;
unsigned long problemIndicatorStart = 0;

bool lastConnectedState = false;
bool lastPairedState = false;

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

  bleKeyboard.press(PRIMARY_SWITCH_KEYCODE);
  switchKeyPressed = true;
  switchPressStart = now;
  secondaryHoldTriggered = false;
  Serial.println("[INPUT] 스위치 누름 -> F13 키 다운");
}

void releaseSwitchKey() {
  if (!switchKeyPressed) {
    return;
  }

  bleKeyboard.releaseAll();
  switchKeyPressed = false;
  secondaryHoldTriggered = false;
  switchPressStart = 0;
  Serial.println("[INPUT] 스위치 해제 -> 키 업");
}

void triggerSecondaryHoldAction() {
  if (secondaryHoldTriggered || !switchKeyPressed) {
    return;
  }

  bleKeyboard.releaseAll();
  switchKeyPressed = false;
  bleKeyboard.press(SECONDARY_SWITCH_KEYCODE);
  bleKeyboard.releaseAll();
  secondaryHoldTriggered = true;
  switchPressStart = 0;
  Serial.println("[INPUT] 5초 홀드 감지 -> F13 해제 후 F14 단발 입력");
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

void updateSecondaryHold(unsigned long now) {
  if (stableSwitchState != LOW || secondaryHoldTriggered || !switchKeyPressed) {
    return;
  }

  if ((now - switchPressStart) >= SECONDARY_HOLD_MS) {
    triggerSecondaryHoldAction();
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
  Serial.println("InfiniteTap BLE 스위치 허브 시작");
  Serial.println("========================================");

  strip.begin();
  strip.setBrightness(LED_BRIGHTNESS);
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
  bootFlashActive = true;
  bootFlashUntil = millis() + BOOT_FLASH_MS;

  if (stableSwitchState == LOW) {
    problemIndicatorPending = true;
    Serial.println("[WARN] 부팅 시점에 외부 스위치 입력이 이미 눌려 있습니다.");
  }

  if (digitalRead(BUTTON_PIN) == LOW) {
    problemIndicatorPending = true;
    Serial.println("[WARN] 부팅 시점에 중앙 버튼이 이미 눌려 있습니다.");
  }

  Serial.println("[BLE] 광고를 시작했습니다. 기본 상태에서는 LED를 켜지 않습니다.");
  Serial.println("[INFO] 장치 이름: InfiniteTap");
  Serial.println("[INFO] 입력 매핑:");
  Serial.println("       스위치 누름  -> F13 key down");
  Serial.println("       스위치 해제  -> F13 key up");
  Serial.println("       5초 홀드     -> F13 해제 후 F14 1회 입력");
  Serial.println("[INFO] LED 정책:");
  Serial.println("       부팅 직후    -> 짧은 초록 점등 1회");
  Serial.println("       문제 감지    -> 주황 점멸 3회");
  Serial.println("       본드 리셋    -> 자홍 점멸");
  Serial.println("[INFO] 타이밍:");
  Serial.println("       디바운스: 20ms");
  Serial.println("       보조 키 홀드 분기: 5000ms");
}

void loop() {
  unsigned long now = millis();

  logBleStateIfChanged();
  updateCenterButton(now);

  if (!bondResetInProgress) {
    updateSwitchInput(now);
    updateSecondaryHold(now);
  }

  updateStatusLed(now);
}
