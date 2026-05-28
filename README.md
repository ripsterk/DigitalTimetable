[README_smart_classroom_display.md](https://github.com/user-attachments/files/28323458/README_smart_classroom_display.md)
# Smart Classroom ESP32 Display

A WiFi-enabled ESP32 classroom information display with a TFT screen, timetable dashboard API, room selection keypad, PIR activity counter, indoor/outdoor temperature, and a weather information page.

The ESP32 shows the currently selected room (`SALA`), current course, professor, time interval, indoor temperature, Bucharest exterior temperature, WiFi status, and a PIR motion counter. A small Linux dashboard/server parses timetable data and exposes a JSON API consumed by the ESP32.

---

## Project Overview
![Project image](https://i.imgur.com/zIfTP7W.png)
### Main Features

- ESP32-WROOM-32D firmware built with **PlatformIO** and the **Arduino framework**
- SPI TFT display UI for classroom information
- BMP280 sensor for indoor temperature
- Open-Meteo API for Bucharest exterior weather
- HC-SR501 PIR motion/activity counter
- 3x4 matrix keypad for room selection
- WiFi status display
- Linux-hosted timetable dashboard/API
- Server-side timetable parsing from uploaded Excel files
- Optional AI-assisted course acronym/name normalization on the dashboard/server side
- Weather page opened from the keypad

---

## Hardware Components

| Component | Purpose |
|---|---|
| ESP32-WROOM-32D development board | Main microcontroller |
| 240x320 SPI TFT display, ILI9341-compatible | Main UI display |
| BMP280 sensor module | Indoor temperature and pressure sensor |
| HC-SR501 PIR motion sensor | Motion/activity counter |
| 3x4 matrix keypad | Room selection input |
| Jumper wires | Wiring |
| Breadboard or soldered prototype board | Assembly |
| USB power supply / USB cable | Power and serial programming |

> Note: The current code uses **BMP280**, which does **not** measure humidity. If indoor humidity is required later, replace it with a **BME280**, **SHT31**, **DHT22**, or similar humidity-capable sensor.

---

## Datasheets / References



* [ESP32-WROOM-32D datasheet](https://documentation.espressif.com/esp32-wroom-32d_esp32-wroom-32u_datasheet_en.pdf)
* [ILI9341 TFT controller datasheet](https://cdn-shop.adafruit.com/datasheets/ILI9341.pdf)
* [BMP280 datasheet](https://cdn-shop.adafruit.com/datasheets/BST-BMP280-DS001-11.pdf)
* [HC-SR501 PIR sensor datasheet](https://www.mpja.com/download/31227sc.pdf)
* [3x4 matrix keypad reference](https://arduinogetstarted.com/tutorials/arduino-keypad#content_about_keypad)


---

## GPIO Connections

### ESP32 Pin Usage

| ESP32 GPIO | Used for |
|---:|---|
| GPIO 2 | ESP32 status LED |
| GPIO 4 | Keypad column 3 |
| GPIO 5 | TFT CS |
| GPIO 13 | Keypad row 1 |
| GPIO 14 | Keypad row 2 |
| GPIO 16 | TFT DC/A0 |
| GPIO 17 | TFT RST |
| GPIO 18 | TFT SCK |
| GPIO 21 | Keypad row 3 |
| GPIO 22 | Keypad row 4 |
| GPIO 23 | TFT MOSI |
| GPIO 25 | Keypad column 1 |
| GPIO 26 | Keypad column 2 |
| GPIO 27 | PIR OUT |
| GPIO 32 | BMP280 SDA |
| GPIO 33 | BMP280 SCL |

---

## Wiring

### TFT SPI Display

| TFT pin | ESP32 pin |
|---|---|
| VCC | 3V3 |
| GND | GND |
| SCK / CLK | GPIO 18 |
| SDI / MOSI / DIN | GPIO 23 |
| CS | GPIO 5 |
| DC / A0 | GPIO 16 |
| RESET / RST | GPIO 17 |
| LED / BL | 3V3 |
| SDO / MISO | Not connected |

Touchscreen pins are present, but the Display does not have Touchscreen capabilities:

```text
T_CLK
T_CS
T_DIN
T_DO
T_IRQ
```

### BMP280 Sensor

| BMP280 pin | ESP32 pin |
|---|---|
| VCC | 3V3 |
| GND | GND |
| SDA | GPIO 32 |
| SCL | GPIO 33 |
| CSB | 3V3 |
| SDO | 3V3 |

### HC-SR501 PIR Motion Sensor

| PIR pin | ESP32 pin |
|---|---|
| VCC | 5V / VIN |
| GND | GND |
| OUT | GPIO 27 |

The software counts a motion event on the rising edge:

```text
LOW -> HIGH = +1 motion event
```

### 3x4 Matrix Keypad

Key layout:

```text
1 2 3
4 5 6
7 8 9
* 0 #
```

Current wiring:

| Keypad pin | ESP32 GPIO |
|---|---:|
| R1 | GPIO 13 |
| R2 | GPIO 14 |
| R3 | GPIO 21 |
| R4 | GPIO 22 |
| C1 | GPIO 25 |
| C2 | GPIO 26 |
| C3 | GPIO 4 |


---

## PlatformIO ESP32 Firmware

### Required Libraries

In `platformio.ini`:

```ini
lib_deps =
    bodmer/TFT_eSPI
    adafruit/Adafruit BMP280 Library
    adafruit/Adafruit Unified Sensor
    bblanchon/ArduinoJson
    chris--a/Keypad
```

### TFT_eSPI Build Flags

The working TFT configuration is:

```ini
build_flags =
    -D USER_SETUP_LOADED
    -D ILI9341_2_DRIVER
    -D TFT_WIDTH=240
    -D TFT_HEIGHT=320
    -D TFT_MOSI=23
    -D TFT_SCLK=18
    -D TFT_CS=5
    -D TFT_DC=16
    -D TFT_RST=17
    -D LOAD_GLCD
    -D LOAD_FONT2
    -D LOAD_FONT4
    -D LOAD_FONT6
    -D LOAD_FONT7
    -D LOAD_FONT8
    -D SPI_FREQUENCY=27000000
```

The working display rotation in `main.cpp` is:

```cpp
tft.setRotation(3);
```

### WiFi Credentials

The current development firmware contains a local WiFi SSID and password. Make sure to change the WiFi network to your preffered 2.4GHz one.

Recommended pattern:

```cpp
#include "secrets.h"
```

Local untracked `secrets.h`:

```cpp
#define WIFI_SSID "your_wifi"
#define WIFI_PASSWORD "your_password"
```

Add to `.gitignore`:

```gitignore
secrets.h
```

---

## Dashboard/API Server

The ESP32 does not parse Excel directly. Instead:

```text
Excel timetable -> Linux dashboard/server -> clean JSON API -> ESP32 display
```

### Current Dashboard Endpoint

The ESP32 server base URL should look like:

```cpp
String dashboardBaseUrl = "http://192.168.100.209:5000/api/current";
```

The ESP32 automatically appends the room parameter:

```text
http://192.168.100.209:5000/api/current?sala=EC002
```

Example response:

```json
{
  "found": true,
  "server_now": "2026-05-27 18:30",
  "sala": "EG 205",
  "curs": "Internet of Things",
  "curs_scurt": "SS",
  "profesor": "PROF NAME",
  "ora": "18:00 - 20:00",
  "zi": "LUNI"
}
```

If nothing is scheduled:

```json
{
  "found": false,
  "server_now": "2026-05-27 22:41",
  "sala": "PR705",
  "curs": "N/A",
  "curs_scurt": "N/A",
  "profesor": "N/A",
  "ora": "N/A",
  "zi": "N/A"
}
```

---

## Starting the Server on Linux

Assuming the dashboard folder is inside `~/Downloads`:

```bash
cd ~/Downloads/smart_classroom_project_v2_fixed_professor/smart_classroom_project_v2/smart_classroom_dashboard
```

Create and activate a virtual environment:

```bash
sudo apt update
sudo apt install python3-venv python3-pip libreoffice -y

python3 -m venv .venv
source .venv/bin/activate
```

Install dependencies:

```bash
pip install -r requirements.txt
```

Start the server:

```bash
python server.py
```

Open locally:

```text
http://localhost:5000
```

Find the Linux server IP:

```bash
hostname -I
```

Example API test:

```text
http://192.168.100.209:5000/api/current?sala=EC002
```

If the ESP32 cannot reach the server, allow port 5000:

```bash
sudo ufw allow 5000/tcp
```

---

## AI Integration

AI is used on the dashboard/server side, not inside the ESP32 firmware.

- Normalize long course names into short acronyms
- Clean professor names
- Review parsed timetable rows from messy Excel files
- Help convert human timetable layouts into structured schedule rows

Example:

```text
Securitatea în sistemele grid și cloud
```

can become:

```text
SSGC
```

The ESP32 receives:

```json
{
  "curs": "Securitatea în sistemele grid și cloud",
  "curs_scurt": "SSGC"
}
```

and displays:

```text
SSGC
```

### API Key Handling

Set the OpenAI key only on the Linux server:

```bash
export OPENAI_API_KEY="your_key_here"
python server.py
```

Or use a local `.env` file and add it to `.gitignore`.

---

## How to Use the ESP32 Display

### Main Screen

The main screen shows:

- `SALA`
- `Curs curent`
- `PROFESOR`
- `ORA`
- Indoor temperature
- Exterior temperature
- WiFi status
- Current time
- PIR counter
- Footer text: `Nitu Victor-Emanuil - ACS`

### Change Room Using Keypad

Press:

```text
*
```

Then select prefix:

```text
1 = EC
2 = ED
3 = EF
4 = EG
5 = PR
```

Then enter the room number and confirm with:

```text
#
```

Examples:

```text
* -> 1 -> 0 -> 0 -> 2 -> #  = EC002
* -> 4 -> 3 -> 0 -> 2 -> #  = EG302
* -> 5 -> 7 -> 0 -> 5 -> #  = PR705
```

Back/cancel:

```text
*
```

### Weather Page

From the main screen, press:

```text
#
```

The weather page shows:

- Current Bucharest temperature
- Humidity from weather API
- Wind speed
- Wind direction with arrow
- 3-day high/low forecast

Go back to main screen:

```text
*
```

---

## Temperature Correction

The BMP280 measured higher than expected, so the firmware applies an offset:

```cpp
#define TEMP_OFFSET -5.0
```

Adjust after comparing with a known accurate thermometer.

---

## PIR Motion Counter

The PIR motion sensor counts motion events, not exact students.

Current behavior:

```text
LOW -> HIGH = +1 counter event
Cooldown = 8 seconds
```

Current cooldown:

```cpp
#define PIR_COOLDOWN_MS 8000UL
```

Important limitation: a PIR sensor cannot reliably count individual people if multiple students pass close together. It is better described as an activity or traffic counter.

For more accurate directional people counting, consider:

- Two IR break-beam sensors
- Time-of-flight sensors
- mmWave presence sensor
- Camera/computer vision

---

## Weather API

The project uses Open-Meteo without an API key.

Current API request includes:

- Current temperature
- Relative humidity
- Wind speed
- Wind direction
- 3-day daily high/low forecast

Endpoint pattern:

```text
http://api.open-meteo.com/v1/forecast?latitude=44.4268&longitude=26.1025&current=temperature_2m,relative_humidity_2m,wind_speed_10m,wind_direction_10m&daily=temperature_2m_max,temperature_2m_min&forecast_days=3&timezone=Europe%2FBucharest
```

---

## Serial Monitor Features

On boot, the ESP32 waits before scanning WiFi so there is time to open Serial Monitor.

It prints visible WiFi networks:

```text
Scanning WiFi networks...
1: SSID='ExampleWiFi' RSSI=-45 dBm Channel=6 Encryption=WPA2
```

Type:

```text
s
```

in Serial Monitor to scan again.

Serial Monitor also returns the .JSON file retrieved from the Dashboard.

---

## Troubleshooting

### Display Is Rotated, Mirrored, or Duplicated

Current working config:

```cpp
tft.setRotation(3);
```

and:

```ini
-D ILI9341_2_DRIVER
```

If the display shows a gray strip or duplicated area, check the TFT driver flag and rotation.

### ESP32 Cannot Connect to WiFi

Check:

- WiFi is 2.4 GHz, not 5 GHz
- WPA2-Personal is enabled
- SSID/password are correct
- ESP32 is near the access point
- USB power is stable

### ESP32 Cannot Reach Dashboard

Test from a browser on the same network:

```text
http://SERVER_IP:5000/api/current?sala=EC002
```

Check Ubuntu firewall:

```bash
sudo ufw allow 5000/tcp
```

Correct ESP32 base URL:

```cpp
String dashboardBaseUrl = "http://SERVER_IP:5000/api/current";
```

Wrong:

```cpp
String dashboardBaseUrl = "http://SERVER_IP:5000/api/current?sala=EC002";
```

### API Returns `found: false`

This means no current course is scheduled for that room at the current server time.

Check:

- Server time
- Timetable entries
- Selected room
- Room spelling: `EC002`, `EC 002`, `PR705`
- Whether the course time is active now

### Professor Name Does Not Fit

The firmware removes common academic titles:

```text
Prof.
Dr.
Conf.
Lect.
Asist.
Ș.l.
S.I.
```

Long names are displayed with a smaller font where possible.

### Weather Page Shows N/A

Check:

- WiFi status is connected
- ESP32 can access the internet
- Open-Meteo API is reachable
- Serial Monitor for HTTP errors

---

## Suggested Repository Structure

```text
smart-classroom-display/
├── esp32/
│   ├── platformio.ini
│   └── src/
│       └── main.cpp
├── dashboard/
│   ├── server.py
│   ├── parser.py
│   ├── ai_cleanup.py
│   ├── requirements.txt
│   ├── data/
│   │   └── schedule.json
│   └── uploads/
├── docs/
│   ├── wiring.md
│   └── screenshots/
├── README.md
└── .gitignore
```

Suggested `.gitignore`:

```gitignore
.venv/
__pycache__/
*.pyc
.env
secrets.h
uploads/*
data/*.tmp
```

---

## Future Improvements

- Replace BMP280 with BME280 or SHT31 for real indoor humidity
- Add OTA firmware updates
- Add dashboard device status heartbeat
- Store PIR counter history on server
- Add daily counter reset
- Add real occupancy sensor instead of PIR-only counting
- Add configuration page for WiFi/server URL
- Add Google Sheets import
- Add AI timetable parsing and course abbreviation generation
- Add support for multiple ESP32 display units
- Add MQTT telemetry
- Add local fallback schedule cache on ESP32

---

## License

Choose a license before publishing. Suggested options:

- MIT License for open-source code
- Private/internal repository if this is for school infrastructure

---

## Author

Victor-Emanuil Nițu  
ACS / UPB  
Smart Classroom ESP32 Display Project
