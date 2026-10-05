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

## How to implement

**1. Wire the hardware**
- **I²C bus:** connect every module's SDA → **GPIO 21** and SCL → **GPIO 22**, VCC → **3.3 V**, and GND → **GND** (common).
- **Battery INA219:** bridge its **A0** pad so its address becomes **0x41**. Leave the solar INA219 at 0x40.
- **Power path:**
  - Solar panel **+** → solar INA219 **VIN+**; **VIN−** → CN3791 **solar input +**.
  - CN3791 **BAT+** → LiPo **+**.
  - LiPo **+** → battery INA219 **VIN+**; **VIN−** → load **+**.
  - Panel, CN3791, LiPo and load negatives all go to common GND.
- Set the CN3791's charge current to about **1 A** to match `CHARGING_CURRENT_MA`, and its MPPT voltage to suit your panel.

**2. Set up the Arduino IDE**
- Install the **esp32 by Espressif** board package. Boards Manager URL: `https://espressif.github.io/arduino-esp32/package_esp32_index.json`
- Library Manager: install **Adafruit INA219**, **Adafruit SSD1306**, **Adafruit GFX**, and **ArduinoJson** v7.

**3. Create the Firebase database (optional)**
- In the Firebase console, create a project → **Realtime Database** → start in test mode.
- Copy the database URL **without** `https://` into `firebaseHost`.
- Project settings → Service accounts → **Database secrets** → copy the secret into `firebaseAuth`.
- Skip this section to run the dashboard locally only. The upload just fails silently.

**4. Configure and upload**
- Open `firmware/solar_microgrid_monitor/solar_microgrid_monitor.ino`.
- Set `ssid` and `password`, plus `BATTERY_CAPACITY_MAH`, `SOLAR_PANEL_MAX_VOLTAGE` and `CHARGING_CURRENT_MA` for your parts.
- Select **ESP32 Dev Module** and the correct port, then upload.

**5. Check it works**
- In the Serial Monitor at **115200** baud, both INA219s should be reported as found.
- The OLED shows the IP address. Open `http://<that-ip>` on the same Wi-Fi to see the dashboard.
- Put the panel in sunlight. Solar power should rise, and the battery should show as charging.

**Troubleshooting**
- *"INA219 not found":* check SDA/SCL, the 3.3 V supply, and the A0 bridge on the battery sensor. Run an I²C scanner sketch; you should see 0x3C, 0x40 and 0x41.
- *Blank OLED:* some modules use address **0x3D**; change `OLED_ADDRESS`.
- *Readings stuck at zero:* the current must flow **through** VIN+ → VIN− of each INA219.
- *No Firebase data:* the host must not include `https://`, and the database rules must allow writes.

## Endpoints
| Route | Returns |
|---|---|
| `/` | Dashboard |
| `/data` | Live readings as JSON |

## Contributing
I'm open to open-source contributions and collaboration. Issues and pull requests are welcome.
You can reach me at **vipulatluri98@gmail.com**.
