Markdown
# BLE Multi-Mode Desk Controller (HOGP Macro Knob)

A wireless, driverless desktop macro controller built on the **ESP32-S3** using **Zephyr RTOS**. The device interfaces natively with host operating systems (Windows, macOS, Linux) over **Bluetooth Low Energy (BLE) HID over GATT Profile (HOGP)** to provide tactile volume control, document scrolling, and media management.

---

## Features

- **Native BLE HID Integration:** Functions as a plug-and-play Human Interface Device (Consumer Control & Mouse/Keyboard profiles) without requiring host-side companion software or custom drivers.
- **Continuous Rotary Control:** Maps analog potentiometer rotation via hardware ADC to continuous system inputs (Volume Up/Down, Page Scrolling, Brightness/Scrubbing).
- **Multi-Mode State Machine:** Cycle between active operational modes using a single push button, with visual state feedback via an RGB/status LED.
- **Software Signal Conditioning:** Implements Exponential Moving Average (EMA) filtering and dynamic deadbanding to eliminate analog wiper jitter and prevent phantom inputs.
- **Isolated Power Architecture:** Powered via standard 5V USB-C while running all telemetry and control over a 2.4 GHz BLE wireless link.

---

## Control Modes

| Mode | LED Indicator | Rotate Clockwise ($\circlearrowright$) | Rotate Counter-Clockwise ($\circlearrowleft$) | Short Button Press |
| :--- | :--- | :--- | :--- | :--- |
| **Mode 1: Audio / Media** | Blue | Volume Up | Volume Down | Play / Pause |
| **Mode 2: Navigation** | Green | Scroll Down | Scroll Up | Mode Switch |
| **Mode 3: System / Custom** | Magenta | Brightness / Scrub Up | Brightness / Scrub Down | Mute Audio |

---

## Hardware Architecture

- **Microcontroller:** ESP32-S3 DevKit (Tensilica Xtensa Dual-Core 32-bit LX7)
- **Rotary Input:** 10kΩ Linear Potentiometer (Wiper $\rightarrow$ ADC1 Channel)
- **Mode Selection:** Momentary Tactile Push Button (Interrupt-driven with internal pull-up)
- **Status Display:** On-board / External RGB LED (GPIO/PWM controlled)
- **Power:** 5V via USB-C (Regulated to 3.3V via on-board LDO)

---

## Firmware Architecture & Tech Stack

- **RTOS:** Zephyr RTOS
- **Language:** C (C99 / C11)
- **BLE Services:** 
  - Human Interface Device Service (HIDS - `0x1812`)
  - Battery Service (BAS - `0x180F`)
  - Device Information Service (DIS - `0x180A`)
- **Key Zephyr Subsystems:** `CONFIG_BT_HIDS`, `CONFIG_ADC`, `CONFIG_GPIO`, Zephyr Work Queues, Devicetree hardware overlays (`app.overlay`).

---

## Project Structure

```text
├── app.overlay          # Devicetree pin configurations (ADC, GPIO, LED)
├── prj.conf             # Zephyr Kconfig dependencies & BLE stack configuration
├── CMakeLists.txt       # Build system definition
├── src/
│   ├── main.c           # Main initialization & RTOS thread coordinator
│   ├── ble_hid.c        # BLE advertising, GATT server, and HOGP reports
│   ├── ble_hid.h        # HID report descriptors and connection callbacks
│   ├── adc_knob.c       # ADC sampling loop, moving average filter & delta detection
│   ├── adc_knob.h       # ADC driver prototypes
│   ├── button_mode.c    # GPIO interrupt handler, debouncing & state machine
│   └── button_mode.h    # Mode enum and event dispatchers
└── README.md
Getting Started
Prerequisites
Zephyr SDK & West Toolchain

ESP-IDF / Xtensa toolchain for ESP32-S3

Build & Flash
Bash
# 1. Initialize and configure the workspace
west init -l .
west update

# 2. Build for ESP32-S3
west build -b esp32s3_devkitm app

# 3. Flash to the board
west flash
Author
Biruk Ambaye (GitHub | Portfolio)