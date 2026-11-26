# Smart-glasses companion

An ESP32-S3-Zero companion with a round GC9A01 display, ADXL345 double-tap input, and vibration feedback. Eyes react to nearby matching Bluetooth advertisements and become sleepy after 30 seconds without activity, then sleep after 60 seconds. A double tap starts a 7.5-second wake sequence. Fresh Bluetooth detections can interrupt that sequence.

## Build and hardware

Use PlatformIO:

```sh
pio run
pio device list
pio run -t upload --upload-port /dev/cu.usbmodem101
pio device monitor --port /dev/cu.usbmodem101 --baud 115200
```

Find the USB port before every upload. The example port may change.

| Module | ESP32 connection |
| --- | --- |
| Display VCC / GND | 3V3 / GND |
| Display SCL / SDA (SPI clock / MOSI) | GPIO12 / GPIO11 |
| Display CS / DC / RST | GPIO10 / GPIO9 / GPIO8 |
| ADXL345 SDA / SCL / INT1 | GPIO1 / GPIO2 / GPIO3 |
| Motor driver control | GPIO4 |

The display uses 3.3 V and has no separate backlight pin. Keep the existing motor driver and protection components; GPIO4 is its control signal.

TFT_eSPI drives the physical display and its 240×240 software canvas using the settings in `platformio.ini`. Rounded eyes are drawn directly into that canvas with subpixel positions, rotation, and smooth edges; no separate eye sprites or second graphics library are needed. The canvas is allocated in internal RAM to avoid PSRAM latency. Display buffers and task creation are checked before normal operation.

This board uses the standard SPI transfer path. The TFT_eSPI DMA path produced a blank/corrupted screen despite reporting successful transfers. Direct eye drawing leaves enough frame time to maintain 30 FPS with standard transfers.
The platform is pinned to 6.12.0 (Arduino 2.0.17). On this board, rebuilding the same renderer under Arduino 3.3.12 reduced frame rate from about 19 to 7 FPS. The current direct eye renderer avoids that expensive image-resampling step. Keep the dependency versions pinned when reproducing those measurements. The GC9A01 mounting orientation is preserved explicitly in setup.

## Detection and its limits

Detection uses advertised product names with word boundaries. Manufacturer IDs provide supporting metadata and never identify a product by themselves. Ambiguous names such as `g1`, `stories`, and `monocle` alone do not qualify. The Nothing company ID is `0x0CCB`; the incorrect Razer and Brilliant manufacturer entries have been removed.

Nothing Ear devices are an explicit test target. The checked-in build enables them with `ENABLE_NOTHING_TEST_DEVICE=1`, because the Ear (2)s are the available test hardware. Use `test off` for glasses-only matching, or set that build flag to `0` for the default on subsequent boots. Runtime settings are not written to flash.

The tracker keeps at most eight matching BLE addresses, each with its own three-packet median signal filter and proximity thresholds. It selects the strongest fresh device and expires samples after 1.5 seconds. The first packet is classified immediately. Later proximity changes follow the median of the last three packets: a single isolated spike is rejected, while a sustained move responds on the second fresh packet. There is no timed hold before a tier change. Filtering runs only when a packet arrives; rendering a held packet never changes its signal estimate.

| Tier | Enter | Exit |
| --- | --- | --- |
| Angry | RSSI ≥ −73 dBm | RSSI < −78 dBm |
| Rage | RSSI ≥ −55 dBm | RSSI < −62 dBm |

Signal strength is affected by placement, bodies, radio power, and the environment. These thresholds indicate relative signal strength, not a calibrated distance. A device must advertise a matching name to be detected. Glasses without such a name need a captured, verified product signature before support can be claimed. Actual glasses coverage has not been verified with physical glasses.

## Controls and diagnostics

Send a complete line at 115200 baud. Partial lines never wait inside the animation loop; lines longer than 63 characters are discarded. `help` lists commands.

| Command | Result |
| --- | --- |
| `status` | Mode, emotion, target, filtered signal, sensor/BLE state, test mode, motor timing |
| `perf` | Frame rate, drawing and transfer times, overruns, free/minimum heap; resets timing counters |
| `happy`, `sleepy`, `sleep`, `curious`, `angry`, `rage`, `confused` | Explicit visual simulation for 10 seconds |
| `far`, `near` | Curious/angry simulation with the corresponding haptic |
| `live` | End simulation and resume live detection immediately |
| `tap` | Wake from sleepy/sleeping state |
| `buzz` / `stop` | Test / stop motor feedback |
| `test on` / `test off` | Enable / disable Nothing test-device matching |
| `sensor` or `r` | Retry and verify ADXL345 initialization |

Simulations have explicit precedence over live inputs for 10 seconds, then expire. A tap exits simulation and returns to live behaviour. With an advertising device nearby, live detection can therefore interrupt the wake sequence.

Motor pulses run on a separate task, independent of frame rendering and serial parsing. The original single, double, and triple pulse durations are retained. Sensor tap interrupts are cleared but ignored while the motor is running or settling for 150 ms, to avoid interpreting the companion's own vibration as input.

The accelerometer's device ID and configuration registers are verified. I2C reads have a short timeout. Sensor health is checked periodically; a failure disables double tap and schedules up to three configuration attempts, five seconds apart. `sensor` restarts the attempt budget. Display animation and Bluetooth scanning continue when the sensor is unavailable.

## Verification

The host checks use Python 3 and `clang++`:

```sh
clang++ -std=c++17 -Wall -Wextra -fsanitize=undefined tests/test_logic.cpp -o /tmp/screen-test-logic
/tmp/screen-test-logic
python3 tests/test_sensor.py
clang++ -std=c++17 -O2 -Wall -Wextra -fsanitize=undefined tests/test_eye_renderer.cpp -o /tmp/screen-test-eyes
/tmp/screen-test-eyes
```

They cover manufacturer-only false positives, test-device gating, separate devices, RSSI spikes, expiry, hysteresis, timer rollover, saturation of long idle periods, haptic sequences, colour convergence, bounded serial input, I2C failures/recovery, and rounded-eye rotation, clipping, colour byte order, and edge coverage against an independent 16×16 supersampling reference.

Hardware checks must include `status` and `perf` after flashing, a live Ear (2) detection, simulations, and a physical double tap. Serial timing cannot confirm the physical motor feel or the appearance of the screen.
