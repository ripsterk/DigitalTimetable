# ESP32 PlatformIO Project

Open this folder in VS Code PlatformIO.

Change this line in `src/main.cpp` if your Ubuntu IP is different:

```cpp
String dashboardBaseUrl = "http://192.168.10.38:5000/api/current";
```

Current pin assumptions:

- TFT: MOSI 23, SCLK 18, CS 5, DC 16, RST 17
- BMP280: SDA 32, SCL 33, address 0x77
- PIR: GPIO 27
- Keypad rows: 13, 14, 21, 22
- Keypad columns: 25, 26, 4
- ESP32 LED: GPIO 2
