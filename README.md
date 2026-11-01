# CADSE Arcade

Firmware for the [CADSE PCB](https://git.tu-berlin.de/mse/se/cadse-hardware) (ESP32-S3 core + head board) from the Space Electronics class of the MSE program at TU Berlin: a game console with a menu, ten single-player games, a two-board radio duel, a synth, a sensor dashboard, and a BNO085 IMU logger with a live web page.

## Features

**Games** (each with its own top-5 high score table on the micro SD card)

| Game | Type | Controls |
|---|---|---|
| Orbit Dodger | dodge falling debris | tilt, laser, shield |
| Comet Tail | snake-style with nova bonus | touch pads or bumpers |
| Debris Breaker | brick breaker with power-up | tilt the deflector |
| Gravity Marble | tilt maze with black holes | tilt |
| Cave Thruster | one-button cave flyer | hold a bumper |
| Drone Patrol | maze chase with an EMP | touch or tilt, bumper |
| Cargo Drop | falling blocks with bomb pods | tilt or touch |
| Tank Battle | top-down tank shooter | touch or tilt, bumper |
| Pulse Cross | rhythm game, hits play a melody | touch pads, both bumpers pause |
| Derelict | first-person raycaster shooter on a space station | tilt to turn, touch to move, bumpers |
| Orbital Duel 2P | two boards over ESP-NOW | tilt, bumpers |

**Apps**

- **SYNTH**: play the buzzer with the touch pads; tilt bends the pitch and shifts the octave; ten presets and five public-domain demo songs.
- **SENSORS**: live BME280 (temperature, humidity, pressure), LSM6DS3 spirit level and gyro, ADS1115 analog inputs, battery and system info, scrolling graphs, CSV logging to SD.
- **IMU LOGGER**: BNO085 sensor fusion plus the onboard IMU, logged to SD at 10–100 Hz, with a live web page served over the board's own Wi-Fi.
- **SETTINGS**: volume, LED brightness, boot splash style, tilt calibration. All saved to flash.

## Building and flashing

Uses [PlatformIO](https://platformio.org/). With the board connected over USB-C:

```bash
pio run -t upload
pio device monitor
```

The code builds on both arduino-esp32 2.x and 3.x.

## Controls

| Input | Menu | In game |
|---|---|---|
| Touch Up / Down | choose | game specific |
| Touch X | start | pause (X resume, Left quit) |
| Touch Right | high scores | game specific |
| Left / right bumper | start / scroll | game specific |

Run **SETTINGS → TILT SETUP** once per board so tilting moves things the right way.

## BNO085 wiring

Pin numbers are taken from the CADSE core PCB layout.

| BNO085 | CADSE connector | Signal |
|---|---|---|
| VIN | H1 pin 5 or 6 | 3V3 |
| GND | H1 pin 1 or 2 | GND |
| SDA | H2 pin 17 | GPIO8 |
| SCL | H2 pin 18 | GPIO9 |
| RST (optional) | H2 pin 30 | GPIO40 |

The core board already has 10k pull-ups on SDA/SCL. I2C addresses 0x4A and 0x4B are both tried.

## IMU logger web page

1. Open **IMU LOGGER** on the board.
2. Join the open Wi-Fi network **CADSE-XXXX** shown on the screen. It has no password, so anyone in range can join and manage the log files.
3. Open **http://192.168.4.1**.

The page shows a live 3D view, angle, acceleration and gyro charts for the BNO085, the onboard IMU or both, logging controls, and the log files with download and delete. Logs are CSV files in `/bno/` on the SD card.

## SD card layout

| Path | Content |
|---|---|
| `/arcade/<game>.txt` | top 5 scores per game |
| `/arcade/runs.csv` | every finished run |
| `/sensors/log_N.csv` | SENSORS app logs |
| `/bno/log_NNN.csv` | IMU LOGGER logs |

## Credits

- Tank Battle is ported from `Tank.h` in [karthick965938/learn-tech-with-karthick](https://github.com/karthick965938/learn-tech-with-karthick) (MIT License, Copyright (c) 2026 Karthick Nagaraj). The full license notice is in `src/games/tank.cpp`.
- Hardware pin mapping from the [CADSE hardware](https://git.tu-berlin.de/mse/se/cadse-hardware) and [CADSE software](https://git.tu-berlin.de/mse/se/cadse-software) repositories of TU Berlin MSE.
- Sensor and display drivers by [Adafruit](https://github.com/adafruit).
