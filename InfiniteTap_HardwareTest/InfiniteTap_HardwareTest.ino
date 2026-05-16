#include <Adafruit_NeoPixel.h>

// -----------------------------------------------------------------------------
// InfiniteTap 하드웨어 기본 동작 테스트
// - GPIO 8에 연결된 외부 스위치가 눌리면 LED를 끄고
// - 떼면 초록색 LED를 켠다.
// -----------------------------------------------------------------------------

#define SWITCH_PIN 8
#define LED_PIN    2
#define NUM_LEDS   1

Adafruit_NeoPixel strip(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);

void setup() {
  strip.begin();
  strip.setBrightness(150);
  pinMode(SWITCH_PIN, INPUT_PULLUP);
}

void loop() {
  if (digitalRead(SWITCH_PIN) == LOW) {
    strip.setPixelColor(0, 0);
  } else {
    strip.setPixelColor(0, strip.Color(0, 255, 0));
  }

  strip.show();
  delay(10);
}
