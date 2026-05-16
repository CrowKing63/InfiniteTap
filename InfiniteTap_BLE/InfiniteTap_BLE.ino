#include <Adafruit_NeoPixel.h>
#include <HijelHID_BLEKeyboard.h>

// -----------------------------------------------------------------------------
// InfiniteTap
// ESP32-C3(M5Stamp C3U) 기반 배터리리스 BLE HID 적응형 스위치 허브
//
// 현재 입력 구조:
// - 1회 클릭용 키 1개
// - 2회 클릭용 키 1개
//
// 왜 3회 클릭을 제거했는가?
// - 3회 클릭까지 지원하면 1회 클릭 확정이 더 늦어질 수밖에 없다.
// - 이 프로젝트에서는 "가장 자주 쓰는 1회 입력"의 반응성을 더 중요하게 본다.
// - 그래서 3회 클릭을 제거하고 1회/2회에 집중한다.
//
// 동작 개념:
// 1) 1회 클릭 hold
//    첫 press를 0.4초 이상 유지하면 1회 클릭 키를 press 상태로 유지한다.
//
// 2) 1회 클릭 tap
//    첫 release 이후 0.3초 동안 두 번째 press가 오지 않으면
//    그때 1회 클릭 키를 짧은 탭으로 확정한다.
//
// 3) 2회 클릭 hold
//    첫 release 후 0.3초 안에 두 번째 press가 시작되고,
//    그 두 번째 press를 0.3초 이상 유지하면 2회 클릭 키를 press 상태로 유지한다.
//
// 4) 2회 클릭 tap
//    두 번째 press가 시작되었지만 길게 유지하지 않고 곧 release하면
//    2회 클릭 키를 짧은 탭으로 처리한다.
//
// 결과:
// - Vision Pro의 Switch Control에서는
//   1회 클릭 키 / 2회 클릭 키 각각에 대해
//   짧게 누름 / 길게 누름을 자체적으로 분기할 수 있다.
//
// 중요한 점:
// - 1회 탭은 2회 클릭 가능성을 잠깐 확인해야 하므로
//   구조적으로 약간의 대기 시간이 남는다.
// - 대신 3회 클릭 제거로 판정 경로를 단순화해 체감 지연을 줄였다.
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
// 하드웨어 핀 정의
// -----------------------------------------------------------------------------
#define SWITCH_PIN                    8
#define LED_PIN                       2
#define BUTTON_PIN                    3
#define NUM_LEDS                      1

// -----------------------------------------------------------------------------
// 타이밍 파라미터
// -----------------------------------------------------------------------------
// 기계식 접점 채터링 제거 시간
#define DEBOUNCE_MS                   35

// Single press waits briefly before becoming hold so double-click can still branch.

// 1회 클릭 hold 확정 시간
#define SINGLE_HOLD_TRIGGER_MS        100

// The second press must start within this window after the first release.

// 첫 release 후 두 번째 press가 시작되어야 하는 최대 시간
#define DOUBLE_CLICK_GAP_MS           300

// 두 번째 press를 2회 클릭 hold로 승격하는 시간

// LED 이벤트 표시 시간
#define EVENT_FLASH_MS                120

// 광고 중 파란 LED 점멸 주기
#define ADVERTISING_BLINK_MS          500

// 중앙 버튼 3초 롱프레스 시 본딩 삭제
#define BOND_RESET_HOLD_MS            3000

// 본딩 삭제 시퀀스용 마젠타 점멸 주기
#define RESET_BLINK_MS                120

// 본딩 삭제 시퀀스 총 시간
#define RESET_SEQUENCE_MS             1600

// -----------------------------------------------------------------------------
// 제스처별 키 매핑
// -----------------------------------------------------------------------------
// Vision Pro 쪽에서는 아래 두 키를 각각 다른 스위치로 등록하면 된다.
// 짧게 누름 / 길게 누름의 실제 의미는 Vision Pro가 처리한다.
#define KEY_SINGLE_ACTION             KEY_SPACE
#define KEY_DOUBLE_ACTION             KEY_RETURN

// -----------------------------------------------------------------------------
// BLE 키보드 / LED 객체
// -----------------------------------------------------------------------------
HijelHID_BLEKeyboard bleKeyboard("InfiniteTap", "Primax", 100);
Adafruit_NeoPixel strip(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);

// -----------------------------------------------------------------------------
// 현재 인식 중인 제스처 종류
// -----------------------------------------------------------------------------
enum GestureType {
  GESTURE_NONE = 0,
  GESTURE_SINGLE,
  GESTURE_DOUBLE
};

// -----------------------------------------------------------------------------
// 디바운스 상태
// -----------------------------------------------------------------------------
bool rawSwitchState = HIGH;
bool stableSwitchState = HIGH;
unsigned long switchDebounceStart = 0;

// -----------------------------------------------------------------------------
// 클릭 시퀀스 상태
// -----------------------------------------------------------------------------
bool sequenceActive = false;
bool switchPressed = false;

// 1 = 첫 번째 press/release 시퀀스
// 2 = 두 번째 press/release 시퀀스
uint8_t sequencePressCount = 0;
uint8_t sequenceReleaseCount = 0;

unsigned long firstPressStartTime = 0;
unsigned long currentPressStartTime = 0;
unsigned long lastReleaseTime = 0;

GestureType activeGesture = GESTURE_NONE;

// -----------------------------------------------------------------------------
// LED / 본딩 삭제 / BLE 로그 상태
// -----------------------------------------------------------------------------
unsigned long eventFlashUntil = 0;
unsigned long advertisingBlinkMark = 0;
bool advertisingBlinkOn = false;

bool centerButtonWasDown = false;
unsigned long centerButtonPressStart = 0;
bool bondResetInProgress = false;
unsigned long bondResetStart = 0;

bool lastConnectedState = false;
bool lastPairedState = false;

// -----------------------------------------------------------------------------
// 공통 LED 제어
// -----------------------------------------------------------------------------
void setColor(uint8_t r, uint8_t g, uint8_t b) {
  strip.setPixelColor(0, strip.Color(r, g, b));
  strip.show();
}

// -----------------------------------------------------------------------------
// 제스처 -> 실제 키 코드 변환
// -----------------------------------------------------------------------------
uint8_t keyForGesture(GestureType gesture) {
  switch (gesture) {
    case GESTURE_SINGLE:
      return KEY_SINGLE_ACTION;
    case GESTURE_DOUBLE:
      return KEY_DOUBLE_ACTION;
    case GESTURE_NONE:
    default:
      return 0;
  }
}

// -----------------------------------------------------------------------------
// 제스처 이름 문자열
// -----------------------------------------------------------------------------
const char* gestureName(GestureType gesture) {
  switch (gesture) {
    case GESTURE_SINGLE:
      return "1회 클릭";
    case GESTURE_DOUBLE:
      return "2회 클릭";
    case GESTURE_NONE:
    default:
      return "없음";
  }
}

// -----------------------------------------------------------------------------
// 클릭 시퀀스 상태 초기화
// -----------------------------------------------------------------------------
void resetSequenceState() {
  sequenceActive = false;
  switchPressed = false;
  sequencePressCount = 0;
  sequenceReleaseCount = 0;
  firstPressStartTime = 0;
  currentPressStartTime = 0;
  lastReleaseTime = 0;
  activeGesture = GESTURE_NONE;
}

// -----------------------------------------------------------------------------
// BLE 연결/페어링 상태 로그
// -----------------------------------------------------------------------------
void logBleStateIfChanged() {
  bool connected = bleKeyboard.isConnected();
  bool paired = bleKeyboard.isPaired();

  if (connected != lastConnectedState) {
    if (connected) {
      Serial.println("[BLE] 호스트와 연결되었습니다.");
    } else {
      Serial.println("[BLE] 연결이 끊어졌습니다. 라이브러리가 재광고/재연결 대기 상태로 들어갑니다.");
    }
    lastConnectedState = connected;
  }

  if (paired != lastPairedState) {
    if (paired) {
      Serial.println("[BLE] 본딩 정보가 존재합니다.");
    } else {
      Serial.println("[BLE] 본딩 정보가 없습니다. 새 페어링이 필요할 수 있습니다.");
    }
    lastPairedState = paired;
  }
}

// -----------------------------------------------------------------------------
// 실제 키를 press 상태로 만든다.
// -----------------------------------------------------------------------------
void activateGestureKey(GestureType gesture, unsigned long now) {
  if (!bleKeyboard.isConnected()) {
    Serial.println("[INPUT] BLE 미연결 상태이므로 입력 전송을 생략합니다.");
    return;
  }

  if (activeGesture != GESTURE_NONE) {
    bleKeyboard.releaseAll();
    activeGesture = GESTURE_NONE;
  }

  uint8_t keycode = keyForGesture(gesture);
  if (keycode == 0) {
    return;
  }

  bleKeyboard.press(keycode);
  activeGesture = gesture;
  eventFlashUntil = now + EVENT_FLASH_MS;

  Serial.print("[INPUT] ");
  Serial.print(gestureName(gesture));
  Serial.println(" 키를 press 상태로 유지합니다.");
}

// -----------------------------------------------------------------------------
// 현재 눌린 키 해제
// -----------------------------------------------------------------------------
void releaseActiveGestureKey() {
  if (activeGesture == GESTURE_NONE) {
    return;
  }

  Serial.print("[INPUT] ");
  Serial.print(gestureName(activeGesture));
  Serial.println(" 키를 release 합니다.");

  bleKeyboard.releaseAll();
  activeGesture = GESTURE_NONE;
}

// -----------------------------------------------------------------------------
// 1회 클릭 짧은 탭 확정
// -----------------------------------------------------------------------------
void finalizeSingleTap(unsigned long now) {
  Serial.println("[INPUT] 1회 클릭을 짧은 탭으로 확정합니다.");
  activateGestureKey(GESTURE_SINGLE, now);
  releaseActiveGestureKey();
  resetSequenceState();
}

// -----------------------------------------------------------------------------
// 2회 클릭 짧은 탭 확정
// -----------------------------------------------------------------------------
// 새로운 시퀀스 시작
// -----------------------------------------------------------------------------
void startNewSequence(unsigned long now) {
  sequenceActive = true;
  switchPressed = true;
  sequencePressCount = 1;
  sequenceReleaseCount = 0;
  firstPressStartTime = now;
  currentPressStartTime = now;
  lastReleaseTime = 0;
  activeGesture = GESTURE_NONE;

  Serial.println("[INPUT] 새 클릭 시퀀스를 시작했습니다. (1회 후보)");
}

// -----------------------------------------------------------------------------
// 두 번째 press 시작
// -----------------------------------------------------------------------------
void beginSecondPress(unsigned long now) {
  switchPressed = true;
  sequencePressCount = 2;
  currentPressStartTime = now;

  Serial.println("[INPUT] 2번째 press가 시작되었습니다. 즉시 2회 클릭 액션을 활성화합니다.");
  activateGestureKey(GESTURE_DOUBLE, now);
}

// -----------------------------------------------------------------------------
// 스위치 안정 눌림 처리
// -----------------------------------------------------------------------------
void handleStablePress(unsigned long now) {
  if (!sequenceActive) {
    startNewSequence(now);
    return;
  }

  // 이미 키를 눌러 둔 상태였다면 그 시퀀스는 끝난 것으로 보고 새로 시작한다.
  if (activeGesture != GESTURE_NONE) {
    releaseActiveGestureKey();
    resetSequenceState();
    startNewSequence(now);
    return;
  }

  // 1회 클릭 이후 release 상태에서만 2회 클릭 후보를 받을 수 있다.
  if (sequencePressCount == 1 && sequenceReleaseCount == 1) {
    if ((now - lastReleaseTime) <= DOUBLE_CLICK_GAP_MS) {
      beginSecondPress(now);
      return;
    }

    // 2회 클릭 허용 시간이 이미 지났으면 기존 1회 클릭을 탭으로 마무리하고
    // 이번 press를 새 시퀀스로 본다.
    finalizeSingleTap(now);
    startNewSequence(now);
    return;
  }

  // 그 외의 애매한 경우는 안전하게 시퀀스를 초기화하고 새로 시작한다.
  resetSequenceState();
  startNewSequence(now);
}

// -----------------------------------------------------------------------------
// 스위치 안정 release 처리
// -----------------------------------------------------------------------------
void handleStableRelease(unsigned long now) {
  if (!sequenceActive || !switchPressed) {
    return;
  }

  switchPressed = false;
  sequenceReleaseCount++;
  lastReleaseTime = now;

  // 이미 실제 키가 눌린 상태였다면
  // release는 해당 키를 놓는 의미다.
  if (activeGesture != GESTURE_NONE) {
    releaseActiveGestureKey();
    resetSequenceState();
    return;
  }

  // 두 번째 click까지 완료된 상태에서 아직 key hold로 승격되지 않았다면
  // 2회 탭으로 바로 마무리한다.
  if (sequencePressCount == 2 && sequenceReleaseCount == 2) {
    resetSequenceState();
  }
}

// -----------------------------------------------------------------------------
// 디바운스 후 안정 에지 처리
// -----------------------------------------------------------------------------
void handleStableSwitchEdge(bool newStableState, unsigned long now) {
  if (newStableState == LOW) {
    handleStablePress(now);
  } else {
    handleStableRelease(now);
  }
}

// -----------------------------------------------------------------------------
// 시간 경과에 따라 1회/2회 hold 또는 1회 tap을 확정
// -----------------------------------------------------------------------------
void updateGestureRecognition(unsigned long now) {
  if (!sequenceActive) {
    return;
  }

  if (activeGesture != GESTURE_NONE) {
    return;
  }

  // 첫 번째 press를 0.4초 유지하면 1회 클릭 hold로 승격
  // Upgrade single press quickly so Vision Pro can own the final hold timing.
  if (switchPressed && sequencePressCount == 1 && sequenceReleaseCount == 0) {
    if ((now - currentPressStartTime) >= SINGLE_HOLD_TRIGGER_MS) {
      activateGestureKey(GESTURE_SINGLE, now);
      return;
    }
  }

  // 첫 번째 click을 release한 뒤 0.3초 동안 두 번째 press가 없으면
  // 1회 클릭 탭으로 확정
  if (!switchPressed && sequencePressCount == 1 && sequenceReleaseCount == 1) {
    if ((now - lastReleaseTime) > DOUBLE_CLICK_GAP_MS) {
      finalizeSingleTap(now);
      return;
    }
  }
}

// -----------------------------------------------------------------------------
// 스위치 입력 읽기 + 디바운스 처리
// -----------------------------------------------------------------------------
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

  updateGestureRecognition(now);
}

// -----------------------------------------------------------------------------
// 본딩 삭제 시작
// -----------------------------------------------------------------------------
void startBondReset(unsigned long now) {
  bondResetInProgress = true;
  bondResetStart = now;

  releaseActiveGestureKey();
  resetSequenceState();

  Serial.println("[BLE] 중앙 버튼 롱프레스 감지: 본딩 삭제 시퀀스를 시작합니다.");
}

// -----------------------------------------------------------------------------
// 중앙 버튼 3초 롱프레스 감지
// -----------------------------------------------------------------------------
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

// -----------------------------------------------------------------------------
// 본딩 삭제 시퀀스
// -----------------------------------------------------------------------------
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

  Serial.println("[BLE] BLE 스택을 종료한 뒤 보드를 재시작합니다.");
  bleKeyboard.end();
  ESP.restart();
}

// -----------------------------------------------------------------------------
// 상태 LED 갱신
// -----------------------------------------------------------------------------
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

// -----------------------------------------------------------------------------
// setup()
// -----------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  Serial.println();
  Serial.println("========================================");
  Serial.println("InfiniteTap BLE Switch Hub 시작");
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

  Serial.println("[BLE] 광고를 시작했습니다. 아직 연결 전이면 파란 LED가 점멸합니다.");
  Serial.println("[INFO] 장치 이름: InfiniteTap");
  Serial.println("[INFO] 제스처 매핑:");
  Serial.println("       1회 클릭 -> Space");
  Serial.println("       2회 클릭 -> Return");
  Serial.println("[INFO] 타이밍:");
  Serial.println("       1회 hold 확정: 0.1초");
  Serial.println("       2회 클릭 판정: 첫 release 후 0.3초");
  Serial.println("       2회 클릭 액션: 두 번째 press 시작 즉시 활성화");
}

// -----------------------------------------------------------------------------
// loop()
// -----------------------------------------------------------------------------
void loop() {
  unsigned long now = millis();

  logBleStateIfChanged();
  updateCenterButton(now);

  if (!bondResetInProgress) {
    updateSwitchInput(now);
  }

  updateStatusLed(now);
}
