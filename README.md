# InfiniteTap

InfiniteTap is a **batteryless BLE HID switch hub** project designed to work with many devices that support **Bluetooth-based Switch Control** (Vision Pro, iPad, iPhone, macOS, Android, etc.).

This project uses an ultra-compact **ESP32-C3 (RISC-V)** development board with a `3.5mm` adaptive switch input, and targets a small, simple switch dongle powered continuously by `USB-C` with no battery.

The current firmware direction is intentionally simple:

- One physical switch input
- One BLE HID key output
- Press -> key down
- Release -> key up
- Tap/hold interpretation is delegated to the host device

In other words, the device does not perform complex gesture classification internally; instead, it is designed to deliver responsiveness that is as close as possible to a **real physical switch**.

## Repository Structure

```text
InfiniteTap/
|-- AGENTS.md
|-- README.md
|-- InfiniteTap_HardwareTest/
|   `-- InfiniteTap_HardwareTest.ino
`-- InfiniteTap_BLE/
    `-- InfiniteTap_BLE.ino
```

## Hardware Configuration

- ESP32-C3 ultra-compact development board (e.g., `M5Stamp C3U`)
- `PJ-320` 3.5mm mono jack
- One external adaptive switch
- USB-C power

### Wiring

- `PJ-320 GND` -> `MCU GND`
- `PJ-320 Tip` -> `MCU GPIO 8 (G8)`

### Built-in Resources

- RGB LED: `GPIO 2`
- Center button: used for bonding reset

## Why GPIO 8?

`GPIO 0` is a strapping pin on the ESP32-C3 and can affect boot behavior.
To avoid unexpected boot modes when power is applied while the switch is pressed,
`GPIO 8` is now used as the input pin.

## Firmware Overview

### 1) Hardware Test Sketch

`InfiniteTap_HardwareTest/InfiniteTap_HardwareTest.ino`

This sketch is for quickly validating wiring and soldering condition right after assembly.

Expected behavior:

- Idle: built-in LED ON
- Switch press: built-in LED completely OFF
- Switch release: built-in LED ON again

To separate BLE issues from hardware issues,
it is strongly recommended to verify basic operation with this sketch first.

### 2) BLE Firmware

`InfiniteTap_BLE/InfiniteTap_BLE.ino`

Current BLE firmware behavior:

- Pressing the external switch sends `Space` key down
- Releasing the external switch sends `Space` key up
- No local single/double/tap/hold branching
- Actual tap/hold interpretation is done by the host device

So this device acts not as a gesture interpreter,
but as a **low-latency BLE switch interface**.

## BLE Status LED

- Blinking blue: advertising / waiting for connection
- Solid green: connected
- Red light: input event occurred
- Press center button for 3+ seconds: clear bonding and restart

## Current Timing

Based on the currently active input path:

- Debounce: `20ms`
- Local tap/hold branching: disabled

To reduce single-tap delay,
internal gesture classification was removed and raw input is forwarded directly.

## Host Device Pairing & Setup

1. Power the device via USB-C.
2. Pair `InfiniteTap` in Bluetooth settings.
3. Open the device's `Switch Control` settings.
4. Register this input as a switch.
5. Map it to the desired action.

Recommended approach:

- Let the host OS handle tap/hold distinction
- Keep this device focused on simple press/release delivery

## Development Environment

Supported environments:

- Arduino IDE
- PlatformIO

For Arduino IDE:

1. Install `ESP32 by Espressif Systems`
2. Select `ESP32C3 Dev Module` (or a compatible board)
3. Install required libraries

Main libraries:

- `Adafruit NeoPixel`
- `HijelHID_BLEKeyboard`

## Recommended Validation Flow

1. Upload `InfiniteTap_HardwareTest`
2. Verify wiring and switch operation
3. Upload `InfiniteTap_BLE`
4. Verify advertising LED status
5. Pair with host device
6. Verify press/release behavior in `Switch Control`

## Notes

- This project assumes continuous `USB-C` power, not battery operation.
- The input pin uses `INPUT_PULLUP`, so pressing the switch must pull the signal to `GND`.
- Since the board and jack are directly soldered, watch for solder bridges and shorts.

## Troubleshooting

### LED stays off, or input looks permanently pressed

- Check whether `GPIO 8` is being held `LOW`
- Check for a solder bridge between `G8` and `GND`
- Re-check jack pin mapping

### No response when pressing the switch

- Check that the adaptive switch actually closes the contact
- Check `Tip` / `GND` wiring on the jack
- Re-test with `InfiniteTap_HardwareTest`

### BLE pairing or reconnection is unstable

- Confirm hardware test passes first
- Confirm library setup is correct for ESP32-C3
- If existing pairing data is corrupted, use center button bonding reset

## Current Status

- Hardware validation sketch included
- BLE HID firmware included
- BLE behavior currently streamlined to simple press/release forwarding

Possible next steps include auto-reconnect tuning, real-device validation across different hosts,
and adding assembly photos/schematics.
