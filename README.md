# InfiniteTap

InfiniteTap은 **Apple Vision Pro(visionOS) 스위치 제어(Switch Control)** 와 연동하기 위한
**배터리리스 BLE HID 스위치 허브** 프로젝트입니다.

이 프로젝트는 `M5Stamp C3U`와 `3.5mm` 적응형 스위치 입력을 사용하며,
배터리 없이 `USB-C` 상시 전원으로 동작하는 작고 단순한 스위치 동글을 목표로 합니다.

현재 펌웨어 방향은 의도적으로 단순합니다.

- 물리 스위치 입력 1개
- BLE HID 키 출력 1개
- 누름 -> 키 다운
- 떼기 -> 키 업
- 탭/홀드 해석은 Vision Pro에 위임

즉, 기기 자체가 제스처를 복잡하게 판정하지 않고,
최대한 **실제 물리 스위치에 가까운 반응성**을 내는 쪽으로 구성되어 있습니다.

## 저장소 구조

```text
InfiniteTap/
|-- AGENTS.md
|-- README.md
|-- InfiniteTap_HardwareTest/
|   `-- InfiniteTap_HardwareTest.ino
`-- InfiniteTap_BLE/
    `-- InfiniteTap_BLE.ino
```

## 하드웨어 구성

- `M5Stamp C3U`
- `PJ-320` 3.5mm 모노 잭
- 외부 적응형 스위치 1개
- USB-C 전원

### 배선

- `PJ-320 GND` -> `M5Stamp C3U GND`
- `PJ-320 Tip` -> `M5Stamp C3U GPIO 8 (G8)`

### 내장 자원

- RGB LED: `GPIO 2`
- 중앙 버튼: 본딩 초기화 용도

## 왜 GPIO 8을 쓰나

`GPIO 0`은 ESP32-C3의 스트래핑 핀이므로 부팅 동작에 영향을 줄 수 있습니다.
스위치가 눌린 상태에서 전원이 들어올 때 예기치 않은 부팅 모드로 들어가는 일을 피하기 위해,
현재는 `GPIO 8`을 입력 핀으로 사용합니다.

## 펌웨어 개요

### 1. 하드웨어 테스트 스케치

`InfiniteTap_HardwareTest/InfiniteTap_HardwareTest.ino`

이 스케치는 조립 직후 배선과 납땜 상태를 빠르게 확인하기 위한 테스트용입니다.

기대 동작:

- 평상시: 내장 LED 켜짐
- 스위치 누름: 내장 LED 완전히 꺼짐
- 스위치 해제: 내장 LED 다시 켜짐

BLE 문제와 하드웨어 문제를 분리하기 위해,
반드시 이 스케치로 먼저 기본 동작을 확인하는 것을 권장합니다.

### 2. BLE 펌웨어

`InfiniteTap_BLE/InfiniteTap_BLE.ino`

현재 BLE 펌웨어 동작:

- 외부 스위치를 누르면 `Space` 키 다운 전송
- 외부 스위치를 떼면 `Space` 키 업 전송
- 로컬 단에서 싱글/더블/탭/홀드 분기 없음
- 실제 탭/홀드 판정은 Vision Pro가 수행

즉, 이 장치는 제스처 해석기가 아니라
**저지연 BLE 스위치 인터페이스**로 동작합니다.

## BLE 상태 LED

- 파란색 점멸: 광고 중 / 연결 대기
- 초록색 고정: 연결됨
- 빨간색 점등: 입력 이벤트 발생
- 중앙 버튼 3초 이상 누름: 본딩 삭제 후 재시작

## 현재 타이밍

현재 활성 입력 경로 기준:

- 디바운스: `20ms`
- 로컬 tap/hold 분기: 비활성화

싱글 탭 지연을 줄이기 위해
기기 내부 제스처 판정은 제거하고, 입력을 그대로 전달하는 구조를 사용합니다.

## Vision Pro 연결 및 설정

1. USB-C로 기기에 전원을 공급합니다.
2. Bluetooth에서 `InfiniteTap`을 페어링합니다.
3. Vision Pro의 `Switch Control` 설정으로 이동합니다.
4. 해당 입력을 스위치로 등록합니다.
5. 원하는 동작을 매핑합니다.

권장 방식:

- 탭/홀드 구분은 Vision Pro가 처리
- 기기 쪽은 단순 press/release 전달만 수행

## 개발 환경

사용 가능한 환경:

- Arduino IDE
- PlatformIO

Arduino IDE 기준:

1. `ESP32 by Espressif Systems` 설치
2. `ESP32C3 Dev Module` 또는 호환 보드 선택
3. 필요한 라이브러리 설치

주요 라이브러리:

- `Adafruit NeoPixel`
- `HijelHID_BLEKeyboard`

## 권장 검증 순서

1. `InfiniteTap_HardwareTest` 업로드
2. 배선과 스위치 동작 확인
3. `InfiniteTap_BLE` 업로드
4. 광고 LED 확인
5. Vision Pro와 페어링
6. `Switch Control`에서 press/release 동작 확인

## 주의사항

- 이 프로젝트는 배터리가 아니라 `USB-C` 상시 전원을 전제로 합니다.
- 입력 핀은 `INPUT_PULLUP`을 사용하므로, 스위치를 누르면 `GND`로 떨어지는 구조여야 합니다.
- 보드와 잭을 직접 납땜하는 구조이므로 납땜 브리지와 단락을 주의해야 합니다.

## 문제 해결

### LED가 계속 꺼져 있거나 입력이 계속 눌린 상태로 보임

- `GPIO 8`이 계속 `LOW`로 끌려가고 있지 않은지 확인
- `G8`과 `GND` 사이 납땜 브리지가 없는지 확인
- 잭 핀 매핑이 올바른지 다시 확인

### 스위치를 눌러도 반응이 없음

- 적응형 스위치가 실제로 접점을 닫는 타입인지 확인
- 잭의 `Tip` / `GND` 배선이 맞는지 확인
- `InfiniteTap_HardwareTest`로 다시 검증

### BLE 페어링이나 재연결이 잘 안 됨

- 먼저 하드웨어 테스트가 정상인지 확인
- ESP32-C3 환경에서 라이브러리 구성이 맞는지 확인
- 기존 페어링 정보가 꼬였으면 중앙 버튼 본딩 초기화를 사용

## 현재 상태

- 하드웨어 검증 스케치 포함
- BLE HID 펌웨어 포함
- 현재 BLE 동작은 단순 press/release 전달 방식으로 정리됨

다음 단계로는 자동 재연결 튜닝, Vision Pro 실기 검증, 조립 사진/회로도 보강 등이 이어질 수 있습니다.
