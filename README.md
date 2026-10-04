# IoT-Enabled Solar Microgrid Monitoring and Management System

An ESP32-based monitor for a small solar microgrid with **CN3791 MPPT charging**. It measures solar and battery power, estimates battery state, and streams live data to a **web dashboard**, an **OLED display**, and **Firebase**.

> Paper: A. Pravallika, V. Atluri, N. C. Cherukri, A. Chauhan, and V. Madireddy, "IoT-Enabled Solar Microgrid Monitoring and Management System," IEEE PARC 2026.
> Built as an MPMC course-based project at VNR VJIET.

## System
```
Solar panel ─► INA219 (0x40) ─► CN3791 MPPT charger ─► LiPo battery ─► INA219 (0x41) ─► load
                    │                                                        │
                    └──────────────────── I²C ──── ESP32 ────────────────────┘
                                                     │
                       ┌─────────────────┬───────────┴───────────┐
                  SSD1306 OLED    Local web dashboard     Firebase Realtime DB
                                  (http://<esp32-ip>)       (every 5 s)
```

## Features

**Measurement:** solar and battery voltage, current and power from two INA219 sensors.

**Battery estimation:**
- SOC from a piecewise single-cell LiPo voltage curve
- SOH from cycle count and voltage-range capacity fade
- Charge and discharge amp-hour integration with partial-cycle counting

**Solar:** energy harvested (Wh), peak power, and maximum power point (power and voltage) seen by the CN3791.

**Interfaces:**
- Responsive web dashboard served by the ESP32, refreshing every 2 s
- 128×64 OLED status screen
- JSON upload to Firebase Realtime Database
- Detailed serial log

> SOC, SOH, coulombic efficiency and MPPT efficiency are **model-based estimates** computed from the measured voltage and current, not direct measurements.

## Hardware

| Part | Notes |
|---|---|
| ESP32 dev board | I²C on GPIO 21 (SDA) / 22 (SCL) |
| 2 × INA219 | Solar at 0x40; battery at 0x41 (A0 bridged) |
| CN3791 MPPT solar charge controller | 1 A charge current |
| Solar panel | ≤ 7 V open-circuit |
| 1-cell LiPo, 2000 mAh | |
| SSD1306 OLED 128×64 | I²C 0x3C |

## Setup
1. In the Arduino IDE, install the ESP32 board package and these libraries: **Adafruit INA219**, **Adafruit SSD1306**, **Adafruit GFX**, **ArduinoJson**.
2. Open `firmware/solar_microgrid_monitor/solar_microgrid_monitor.ino`.
3. Set your Wi-Fi SSID and password, Firebase host, and database secret at the top of the file.
4. Upload it, then open the IP address shown on the OLED or in the Serial Monitor (115200 baud).

## Endpoints
| Route | Returns |
|---|---|
| `/` | Dashboard |
| `/data` | Live readings as JSON |

## Contributing
I'm open to open-source contributions and collaboration. Issues and pull requests are welcome.
You can reach me at **vipulatluri98@gmail.com**.
