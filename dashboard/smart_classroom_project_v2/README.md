# Smart Classroom Project v2

This package contains both parts:

1. `smart_classroom_dashboard/` - Ubuntu/Python dashboard + JSON API.
2. `esp32_platformio_project/` - PlatformIO ESP32 firmware.

## Important

Do not put your OpenAI API key inside the ESP32 code. If you want AI cleanup/acronym generation, put the key on the Ubuntu server only.

---

# A. Run the dashboard on Ubuntu

Open Terminal:

```bash
cd ~/Downloads/smart_classroom_project_v2/smart_classroom_dashboard
sudo apt update
sudo apt install python3-venv python3-pip libreoffice -y
python3 -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt
python server.py
```

Open in browser:

```text
http://localhost:5000
```

Find Ubuntu IP:

```bash
hostname -I
```

Example:

```text
192.168.10.38
```

From another device or ESP32, the API is:

```text
http://192.168.10.38:5000/api/current?sala=EC002
```

If firewall blocks it:

```bash
sudo ufw allow 5000/tcp
```

## Test API time manually

```text
http://192.168.10.38:5000/api/current?sala=ED011&now=2026-05-27T16:04:00+03:00
```

The server now uses Europe/Bucharest time internally.

## AI key, optional

Use only on the server:

```bash
export OPENAI_API_KEY="your_new_key_here"
python server.py
```

Then press the AI cleanup button in the dashboard. Never embed this key in ESP32 firmware.

---

# B. ESP32 firmware

Open `esp32_platformio_project/` in VS Code / PlatformIO.

Before upload, check in `src/main.cpp`:

```cpp
String dashboardBaseUrl = "http://192.168.10.38:5000/api/current";
```

Change `192.168.10.38` to your Ubuntu IP from `hostname -I`.

WiFi is currently set to:

```cpp
const char *WIFI_SSID = "Informatica";
const char *WIFI_PASSWORD = "Info2026";
```

Upload to ESP32.

---

# C. Keypad room selection

Flow:

```text
*          open room menu
1          EG
2          EF
3          ED
4          EC
5          PR
#          confirm prefix
002        room number
#          confirm room
```

Example:

```text
* -> 4 -> # -> 002 -> #
```

sets room to:

```text
EC002
```

---

# D. New features in v2

- Dashboard API fixed to use Europe/Bucharest time.
- API includes `server_now` for debugging.
- API returns `curs_scurt` acronym for the TFT.
- ESP32 shows next course if no active course exists.
- Interior thermometer has fill level and color:
  - below 22 = blue
  - 22-25 = green
  - 25-28 = orange
  - over 28 = red
- Exterior icon changes by Open-Meteo weather code: sun, cloud, rain, snow, wind.
