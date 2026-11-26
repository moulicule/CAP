# ESP32-S3-Zero Project

## Hardware

- Board: ESP32-S3-Zero
- Chip: ESP32-S3
- Flash: 4 MB
- PSRAM: 2 MB
- Framework: Arduino
- Build system: PlatformIO
- USB: Native USB Serial/JTAG

## Display

- Display: 1.28-inch round 240×240 TFT using the GC9A01 controller and 4-wire SPI.
- Use `TFT_eSPI` directly from `https://github.com/Bodmer/TFT_eSPI.git`; do not replace it with the PlatformIO registry release without validating ESP32-S3 initialization.
- Keep the TFT_eSPI configuration in `platformio.ini` using `USER_SETUP_LOADED` and the `GC9A01_DRIVER` build flag. Do not edit library files in `.pio/`.
- Verified wiring:
  - TFT `VCC` → ESP32 `3V3`
  - TFT `GND` → ESP32 `GND`
  - TFT `SCL` → GPIO12 (SPI SCLK)
  - TFT `SDA` → GPIO11 (SPI MOSI; this is not I²C SDA)
  - TFT `CS` → GPIO10
  - TFT `DC` → GPIO9
  - TFT `RST` → GPIO8
- This module has seven display pins: `VCC`, `GND`, `SCL`, `SDA`, `DC`, `CS`, and `RST`; it has no separate `BLK` pin.
- The display requires 3.3 V power. Do not connect its VCC pin to 5 V.
- GPIO13 is not the display clock in this project. Use GPIO12 for `SCL`.

## Project Structure

- `src/main.cpp` — main firmware
- `platformio.ini` — PlatformIO configuration
- `.pio/` — generated build files; do not edit manually

## Development

- Use PlatformIO, not Arduino IDE.
- Use the Arduino framework.
- Use NimBLE-Arduino for Bluetooth functionality.
- Inspect the existing code before making changes.
- Preserve existing functionality unless a change is necessary.
- Do not invent NimBLE APIs. Check the installed library/API when uncertain.
- Make small, testable changes.
- Never modify files inside `.pio/`.

## Testing

After modifying the code:

1. Build the project with `pio run`.
2. If the build fails, diagnose and fix the errors.
3. Before uploading, use `pio device list` to find the current ESP32-S3 USB port.
4. Upload the firmware to the connected ESP32-S3-Zero.
5. Monitor the serial output at 115200 baud.
6. Use the serial output to diagnose hardware/runtime problems.
