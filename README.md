BLE Multi-Mode Desk Controller (ESP32-S3 HOGP Macro Knob)

A wireless, driverless desktop macro controller built on the **ESP32-S3** using **Zephyr RTOS**. The device interfaces natively with host operating systems (Windows, macOS, Linux) over **Bluetooth Low Energy (BLE) HID over GATT Profile (HOGP)** to provide physical volume adjustments, document scrolling, display brightness control, and media management without requiring host-side companion software.

---

## Features

- **Composite BLE HID Profile (HOGP):** Integrates both **Consumer Control** and **Mouse Wheel** report descriptors over a single GATT service, allowing driverless plug-and-play operation.
- **Continuous Rotary Analog Control:** Samples a 10kΩ potentiometer using hardware ADC with 12 dB attenuation (`ADC_GAIN_1_4`) to map full 0–3.3V rotations linearly to relative input steps.
- **Signal Conditioning Pipeline:** Implements an Exponential Moving Average (EMA) filter combined with dynamic deadband thresholding to eliminate wiper noise, high-frequency bounce, and phantom inputs.
- **3-Mode Finite State Machine:** Cycles through operational modes using a tactile switch, accompanied by an asynchronous, work-queue-driven LED blink engine.
- **Non-Blocking RTOS Architecture:** Decoupled driver architecture separating the HID GATT server, ADC sampling loop, GPIO button state engine, and delayed work queues.

---

## Control Modes & Mappings

| Mode | LED Indicator Pattern | Rotate Clockwise ($\circlearrowright$) | Rotate Counter-Clockwise ($\circlearrowleft$) | Button 2 (GPIO 5) | Button 3 (GPIO 4) |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **Mode 1: Audio / Media** | Solid ON | Volume Up (`0xE9`) | Volume Down (`0xEA`) | Play / Pause | **Click:** Next Track<br>**Hold:** Prev Track |
| **Mode 2: Navigation** | 2 Short Blinks | Scroll Down ($\Delta\text{Wheel} = -1$) | Scroll Up ($\Delta\text{Wheel} = +1$) | Play / Pause | Mute Audio (`0xE2`) |
| **Mode 3: System / Display** | 3 Short Blinks | Brightness Up (`0x6F`) | Brightness Down (`0x70`) | Play / Pause | Mute Audio (`0xE2`) |

*Note: **Button 1 (GPIO 6)** cycles through modes sequentially: Mode 1 $\rightarrow$ Mode 2 $\rightarrow$ Mode 3 $\rightarrow$ Mode 1.*

---

## Hardware Architecture & Pinout

- **Microcontroller:** ESP32-S3 DevKitC-1 (Xtensa Dual-Core 32-bit LX7 @ 240 MHz)
- **Power Supply:** 5V via USB-C (Regulated to 3.3V via on-board LDO)

| Peripheral | Component | ESP32-S3 Pin | Configuration / Notes |
| :--- | :--- | :--- | :--- |
| **Knob Wiper** | 10kΩ Linear Potentiometer | `GPIO 7` | ADC1 Channel 6, 12 dB Gain, Internal Ref |
| **Button 1** | Tactile Switch (Mode Cycle) | `GPIO 6` | Input with Internal Pull-Up |
| **Button 2** | Tactile Switch (Play/Pause) | `GPIO 5` | Input with Internal Pull-Up |
| **Button 3** | Tactile Switch (Action/Mute) | `GPIO 4` | Input with Internal Pull-Up, Long-Press Timer |
| **Status LED** | Discrete LED + Resistor | `GPIO 10` | Active High, Asynchronous Work Queue |

---

## Firmware Architecture

```text
.
├── app.overlay           # Devicetree overlays (ADC channel 6 on GPIO 7)
├── prj.conf              # Kconfig: BLE stack, HOGP, NVS bonding, ADC, GPIO
├── CMakeLists.txt        # Build system sources
└── src/
    ├── main.c            # RTOS coordinator & event dispatch loop
    ├── ble_hid.c / .h    # Composite GATT server (Consumer Control + Mouse HID)
    ├── adc_knob.c / .h   # ADC sampling, EMA filter & delta step detection
    ├── button_mode.c / .h# State machine, button debouncing & long-press timer
    └── status_led.c / .h # Asynchronous work-queue LED blink engine


Getting StartedPrerequisitesZephyr 
SDK (v0.17.x+)west 
Meta-tool
ESP32-S3 Toolchain (xtensa-espressif_esp32s3_zephyr-elf)
Build & Flash
Clone repository:Bash git clone [https://github.com/Biruk-aki/esp32s3-ble-macro-knob.git](https://github.com/Biruk-aki/esp32s3-ble-macro-knob.git)
cd esp32s3-ble-macro-knob
Perform a pristine build:
Bash west build -p always -b esp32s3_devkitc/esp32s3/procpu .
Flash to ESP32-S3: Bash west flash
Pair with Host:Open host Bluetooth settings and pair with ESP32-DeskKnob.
The device will establish BLE Security Level 2 bonding and register natively as a composite input device.

Author: Biruk Ambaye
GitHub: @Biruk-aki
