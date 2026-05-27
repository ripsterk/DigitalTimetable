#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>
#include <Keypad.h>
#include <TFT_eSPI.h>
#include <Adafruit_BMP280.h>
#include <math.h>

// ======================================================
// WIFI SETTINGS
// ======================================================
const char *WIFI_SSID = "wifi";
const char *WIFI_PASSWORD = "wifipass";

// Change IP.
String dashboardBaseUrl = "http://192.168.100.209:5000/api/current";

// Default room shown at boot.
String selectedRoom = "EC002";

// Open-Meteo Bucharest, no API key needed.
// Includes current temp, humidity, wind, and 3-day forecast high/low.
const char *WEATHER_URL =
    "http://api.open-meteo.com/v1/forecast?"
    "latitude=44.4268&longitude=26.1025"
    "&current=temperature_2m,relative_humidity_2m,wind_speed_10m,wind_direction_10m"
    "&daily=temperature_2m_max,temperature_2m_min"
    "&forecast_days=3"
    "&timezone=Europe%2FBucharest";

// ======================================================
// BMP280 SENSOR
// ======================================================
#define BMP_SDA 32
#define BMP_SCL 33
#define BMP280_ADDRESS 0x77

// Temperature correction.
// Originally -4.0, now subtracts one more degree.
#define TEMP_OFFSET -5.0

// ======================================================
// PIR SENSOR
// ======================================================
#define PIR_PIN 27
#define PIR_COOLDOWN_MS 8000UL

volatile unsigned long pirCounter = 0;
bool lastPirState = LOW;
unsigned long lastPirTrigger = 0;

// ======================================================
// ESP32 LED
// ======================================================
#define STATUS_LED 2

// ======================================================
// REFRESH TIMING
// ======================================================
#define FULL_REFRESH_INTERVAL_MS 60000UL
#define TIME_REFRESH_INTERVAL_MS 10000UL

unsigned long lastFullRefresh = 0;
unsigned long lastTimeRefresh = 0;

// ======================================================
// KEYPAD 3x4
// ======================================================
// Layout:
// 1 2 3
// 4 5 6
// 7 8 9
// * 0 #
const byte ROWS = 4;
const byte COLS = 3;

char keys[ROWS][COLS] = {
    {'1', '2', '3'},
    {'4', '5', '6'},
    {'7', '8', '9'},
    {'*', '0', '#'}
};

// Your 3-column keypad wiring.
byte rowPins[ROWS] = {13, 14, 21, 22};
byte colPins[COLS] = {25, 26, 4};

Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

// ======================================================
// UI STATE
// ======================================================
enum UiMode
{
    UI_MAIN,
    UI_SELECT_PREFIX,
    UI_ENTER_ROOM_NUMBER,
    UI_WEATHER
};

UiMode uiMode = UI_MAIN;

String selectedPrefix = "";
String roomNumberBuffer = "";

// Prefix menu:
// 1 = EC
// 2 = ED
// 3 = EF
// 4 = EG
// 5 = PR

// ======================================================
// OBJECTS
// ======================================================
TFT_eSPI tft = TFT_eSPI();
Adafruit_BMP280 bmp;

bool bmpOK = false;
bool wifiOK = false;
bool timeOK = false;

float lastExteriorTemp = NAN;
float lastInteriorTemp = NAN;

// Weather extended data
bool weatherOK = false;
float weatherCurrentTemp = NAN;
float weatherHumidity = NAN;
float weatherWindSpeed = NAN;
float weatherWindDirection = NAN;
float weatherHigh[3] = {NAN, NAN, NAN};
float weatherLow[3] = {NAN, NAN, NAN};
String weatherDate[3] = {"N/A", "N/A", "N/A"};

// ======================================================
// COLORS
// ======================================================
#define C_BLACK      TFT_BLACK
#define C_WHITE      TFT_WHITE
#define C_TEXT       TFT_BLACK
#define C_HEADER     0x18E3
#define C_BG         0xEF5D
#define C_LINE       0xC618
#define C_MUTED      0x7BEF
#define C_GREEN      TFT_GREEN
#define C_RED        TFT_RED
#define C_BLUE       TFT_BLUE

// ======================================================
// DISPLAY DATA
// ======================================================
String sala = "N/A";
String curs = "N/A";
String cursShort = "N/A";
String profesor = "N/A";
String ora = "N/A";
String currentTimeText = "N/A";

// ======================================================
// ICONS
// ======================================================
void drawClockIcon(int x, int y, uint16_t color)
{
    tft.drawCircle(x, y, 9, color);
    tft.drawLine(x, y, x, y - 6, color);
    tft.drawLine(x, y, x + 6, y, color);
}

void drawThermometerIcon(int x, int y, uint16_t color)
{
    tft.fillCircle(x + 5, y + 25, 5, color);
    tft.fillRoundRect(x + 3, y + 2, 5, 25, 3, color);
    tft.drawRoundRect(x + 1, y, 9, 30, 4, color);
}

void drawSunIcon(int x, int y, uint16_t color)
{
    tft.drawCircle(x, y, 10, color);
    tft.fillCircle(x, y, 5, color);

    tft.drawLine(x, y - 18, x, y - 13, color);
    tft.drawLine(x, y + 13, x, y + 18, color);
    tft.drawLine(x - 18, y, x - 13, y, color);
    tft.drawLine(x + 13, y, x + 18, y, color);

    tft.drawLine(x - 13, y - 13, x - 9, y - 9, color);
    tft.drawLine(x + 13, y - 13, x + 9, y - 9, color);
    tft.drawLine(x - 13, y + 13, x - 9, y + 9, color);
    tft.drawLine(x + 13, y + 13, x + 9, y + 9, color);
}

void drawWifiIcon(int x, int y, uint16_t color)
{
    // Small aligned WiFi icon.
    // x/y = dot center
    tft.fillCircle(x, y, 1, color);
    tft.drawArc(x, y - 1, 6, 5, 220, 320, color, C_BG);
    tft.drawArc(x, y - 1, 10, 8, 220, 320, color, C_BG);
}

void drawWindArrow(int cx, int cy, float degrees, uint16_t color)
{
    if (isnan(degrees))
    {
        tft.drawString("N/A", cx - 10, cy - 6, 2);
        return;
    }

    // Meteorological direction is "from". For visual arrow, point where wind goes.
    float arrowDeg = degrees + 180.0;

    if (arrowDeg >= 360.0)
    {
        arrowDeg -= 360.0;
    }

    float rad = arrowDeg * PI / 180.0;

    int len = 18;
    int x2 = cx + (int)(sin(rad) * len);
    int y2 = cy - (int)(cos(rad) * len);

    tft.drawCircle(cx, cy, 20, color);
    tft.drawLine(cx, cy, x2, y2, color);
    tft.fillCircle(x2, y2, 3, color);

    int tx = cx - (int)(sin(rad) * 8);
    int ty = cy + (int)(cos(rad) * 8);
    tft.drawLine(tx, ty, cx, cy, color);
}

// ======================================================
// UTILS
// ======================================================
void flashStatusLed()
{
    for (int i = 0; i < 2; i++)
    {
        digitalWrite(STATUS_LED, HIGH);
        delay(120);
        digitalWrite(STATUS_LED, LOW);
        delay(120);
    }
}

String normalizeRoomNumber(String input)
{
    input.trim();
    input.replace(" ", "");

    while (input.length() < 3)
    {
        input = "0" + input;
    }

    return input;
}

String urlEncodeRoom(String room)
{
    room.replace(" ", "");
    return room;
}

String removeRepeatedSpaces(String s)
{
    s.trim();

    while (s.indexOf("  ") >= 0)
    {
        s.replace("  ", " ");
    }

    return s;
}

String cleanProfessorName(String name)
{
    name.trim();

    if (name.length() == 0 || name == "N/A")
    {
        return "N/A";
    }

    name.replace("Ș.L.", " ");
    name.replace("Ș.l.", " ");
    name.replace("ș.l.", " ");
    name.replace("S.L.", " ");
    name.replace("S.l.", " ");
    name.replace("s.l.", " ");
    name.replace("ȘI.", " ");
    name.replace("Ș.I.", " ");
    name.replace("S.I.", " ");
    name.replace("S.I", " ");
    name.replace("Sef lucrari", " ");
    name.replace("Șef lucrări", " ");
    name.replace("SEF LUCRARI", " ");

    String titles[] = {
        "Prof. univ. dr.", "Prof.univ.dr.", "Prof. dr.", "Profesor",
        "Prof.", "PROF.", "prof.",
        "Dr.", "DR.", "dr.",
        "Conf. univ. dr.", "Conf.univ.dr.", "Conf. dr.", "Conf.",
        "CONF.", "conf.",
        "Lect. dr.", "Lect.", "LECT.", "lect.",
        "Asist. dr.", "Asist.", "ASIST.", "asist.",
        "Ing.", "ING.", "ing.",
        "Univ.", "UNIV.", "univ."
    };

    for (unsigned int i = 0; i < sizeof(titles) / sizeof(titles[0]); i++)
    {
        name.replace(titles[i], " ");
    }

    name = removeRepeatedSpaces(name);
    name.trim();

    if (name.length() == 0)
    {
        return "N/A";
    }

    return name;
}

String shortenForDisplay(String s, int maxLen)
{
    s.trim();

    if ((int)s.length() <= maxLen)
    {
        return s;
    }

    return s.substring(0, maxLen);
}

bool isStopWord(String word)
{
    word.toLowerCase();

    return word == "si" ||
           word == "și" ||
           word == "in" ||
           word == "în" ||
           word == "de" ||
           word == "la" ||
           word == "cu" ||
           word == "pe" ||
           word == "the" ||
           word == "and" ||
           word == "of" ||
           word == "for" ||
           word == "to" ||
           word == "a" ||
           word == "an";
}

String cleanToken(String token)
{
    token.trim();

    String cleaned = "";

    for (unsigned int i = 0; i < token.length(); i++)
    {
        char c = token.charAt(i);

        if ((c >= 'A' && c <= 'Z') ||
            (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9'))
        {
            cleaned += c;
        }
    }

    return cleaned;
}

String makeCourseAcronym(String name)
{
    name.trim();

    if (name.length() == 0 || name == "N/A")
    {
        return "N/A";
    }

    if (name.length() <= 8)
    {
        return name;
    }

    String source = "";
    bool insideParen = false;

    for (unsigned int i = 0; i < name.length(); i++)
    {
        char c = name.charAt(i);

        if (c == '(')
        {
            insideParen = true;
            continue;
        }

        if (c == ')')
        {
            insideParen = false;
            continue;
        }

        if (!insideParen)
        {
            source += c;
        }
    }

    String acronym = "";
    String token = "";

    for (unsigned int i = 0; i <= source.length(); i++)
    {
        char c = i < source.length() ? source.charAt(i) : ' ';

        if (c == ' ' || c == '-' || c == '_' || c == '/' || c == ',' || c == '.')
        {
            token = cleanToken(token);

            if (token.length() > 0 && !isStopWord(token))
            {
                char first = token.charAt(0);

                if (first >= 'a' && first <= 'z')
                {
                    first -= 32;
                }

                acronym += first;
            }

            token = "";
        }
        else
        {
            token += c;
        }
    }

    if (acronym.length() == 0)
    {
        return name.substring(0, min((int)name.length(), 8));
    }

    if (acronym.length() > 8)
    {
        acronym = acronym.substring(0, 8);
    }

    return acronym;
}

// ======================================================
// WIFI SCAN + CONNECT
// ======================================================
String encryptionTypeToString(wifi_auth_mode_t encryptionType)
{
    switch (encryptionType)
    {
        case WIFI_AUTH_OPEN:
            return "OPEN";
        case WIFI_AUTH_WEP:
            return "WEP";
        case WIFI_AUTH_WPA_PSK:
            return "WPA";
        case WIFI_AUTH_WPA2_PSK:
            return "WPA2";
        case WIFI_AUTH_WPA_WPA2_PSK:
            return "WPA/WPA2";
        case WIFI_AUTH_WPA2_ENTERPRISE:
            return "WPA2 Enterprise";
        case WIFI_AUTH_WPA3_PSK:
            return "WPA3";
        case WIFI_AUTH_WPA2_WPA3_PSK:
            return "WPA2/WPA3";
        default:
            return "UNKNOWN";
    }
}

void scanAvailableWiFi()
{
    Serial.println();
    Serial.println("Scanning WiFi networks...");
    Serial.println("-------------------------");

    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);

    WiFi.disconnect(false);
    delay(1000);

    int networkCount = WiFi.scanNetworks(false, true);

    if (networkCount < 0)
    {
        Serial.print("WiFi scan failed. Code: ");
        Serial.println(networkCount);
        return;
    }

    if (networkCount == 0)
    {
        Serial.println("No WiFi networks found.");
    }
    else
    {
        Serial.print("Networks found: ");
        Serial.println(networkCount);
        Serial.println();

        for (int i = 0; i < networkCount; i++)
        {
            Serial.print(i + 1);
            Serial.print(": SSID='");
            Serial.print(WiFi.SSID(i));
            Serial.print("' RSSI=");
            Serial.print(WiFi.RSSI(i));
            Serial.print(" dBm Channel=");
            Serial.print(WiFi.channel(i));
            Serial.print(" Encryption=");
            Serial.println(encryptionTypeToString(WiFi.encryptionType(i)));
        }
    }

    WiFi.scanDelete();

    Serial.println("-------------------------");
    Serial.println("Type 's' in Serial Monitor to scan again.");
    Serial.println();
}

bool connectWiFi()
{
    if (WiFi.status() == WL_CONNECTED)
    {
        return true;
    }

    Serial.println();
    Serial.print("Connecting to WiFi: ");
    Serial.println(WIFI_SSID);

    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);

    WiFi.disconnect(false);
    delay(1000);

    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    unsigned long startAttempt = millis();

    while (WiFi.status() != WL_CONNECTED && millis() - startAttempt < 20000)
    {
        Serial.print(".");
        delay(500);
    }

    Serial.println();

    if (WiFi.status() == WL_CONNECTED)
    {
        Serial.println("WiFi connected.");
        Serial.print("IP address: ");
        Serial.println(WiFi.localIP());
        Serial.print("Gateway: ");
        Serial.println(WiFi.gatewayIP());
        Serial.print("DNS: ");
        Serial.println(WiFi.dnsIP());
        Serial.print("RSSI: ");
        Serial.print(WiFi.RSSI());
        Serial.println(" dBm");
        return true;
    }

    Serial.println("WiFi connection FAILED.");
    Serial.print("WiFi status code: ");
    Serial.println(WiFi.status());

    return false;
}

void handleSerialCommands()
{
    if (Serial.available())
    {
        char command = Serial.read();

        if (command == 's' || command == 'S')
        {
            scanAvailableWiFi();
        }
    }
}

// ======================================================
// TIME
// ======================================================
void setupInternetTime()
{
    if (WiFi.status() != WL_CONNECTED)
    {
        timeOK = false;
        return;
    }

    configTzTime("EET-2EEST,M3.5.0/3,M10.5.0/4", "pool.ntp.org", "time.nist.gov");

    struct tm timeinfo;

    if (getLocalTime(&timeinfo, 10000))
    {
        timeOK = true;
        Serial.println("Internet time synchronized.");
    }
    else
    {
        timeOK = false;
        Serial.println("Internet time sync failed.");
    }
}

String getCurrentTimeText()
{
    struct tm timeinfo;

    if (!getLocalTime(&timeinfo, 1000))
    {
        return "N/A";
    }

    char buffer[6];
    strftime(buffer, sizeof(buffer), "%H:%M", &timeinfo);

    return String(buffer);
}

// ======================================================
// WEATHER
// ======================================================
bool fetchWeather()
{
    if (WiFi.status() != WL_CONNECTED)
    {
        Serial.println("Cannot fetch weather: WiFi not connected.");
        return false;
    }

    Serial.println("Fetching weather for Bucharest...");

    WiFiClient client;
    HTTPClient http;

    http.setTimeout(20000);
    http.useHTTP10(true);

    if (!http.begin(client, WEATHER_URL))
    {
        Serial.println("Weather HTTP begin failed.");
        return false;
    }

    http.addHeader("User-Agent", "ESP32-Classroom-Display");

    int httpCode = http.GET();

    if (httpCode != HTTP_CODE_OK)
    {
        Serial.print("Weather HTTP error: ");
        Serial.println(httpCode);
        http.end();
        return false;
    }

    String payload = http.getString();
    http.end();

    StaticJsonDocument<4096> doc;
    DeserializationError error = deserializeJson(doc, payload);

    if (error)
    {
        Serial.print("Weather JSON parse failed: ");
        Serial.println(error.c_str());
        return false;
    }

    weatherCurrentTemp = doc["current"]["temperature_2m"] | NAN;
    weatherHumidity = doc["current"]["relative_humidity_2m"] | NAN;
    weatherWindSpeed = doc["current"]["wind_speed_10m"] | NAN;
    weatherWindDirection = doc["current"]["wind_direction_10m"] | NAN;

    for (int i = 0; i < 3; i++)
    {
        weatherHigh[i] = doc["daily"]["temperature_2m_max"][i] | NAN;
        weatherLow[i] = doc["daily"]["temperature_2m_min"][i] | NAN;

        const char *dateStr = doc["daily"]["time"][i] | "N/A";
        weatherDate[i] = String(dateStr);

        if (weatherDate[i].length() >= 10)
        {
            weatherDate[i] = weatherDate[i].substring(5); // MM-DD
        }
    }

    if (!isnan(weatherCurrentTemp))
    {
        lastExteriorTemp = weatherCurrentTemp;
    }

    weatherOK = !isnan(weatherCurrentTemp);

    Serial.print("Weather temp: ");
    Serial.println(weatherCurrentTemp);

    Serial.print("Wind speed: ");
    Serial.println(weatherWindSpeed);

    Serial.print("Wind direction: ");
    Serial.println(weatherWindDirection);

    return weatherOK;
}

float fetchExteriorTemperature()
{
    if (fetchWeather())
    {
        return weatherCurrentTemp;
    }

    return NAN;
}

// ======================================================
// DASHBOARD API
// ======================================================
bool fetchScheduleFromDashboard()
{
    if (WiFi.status() != WL_CONNECTED)
    {
        Serial.println("Cannot fetch schedule: WiFi not connected.");
        sala = selectedRoom;
        curs = "N/A";
        cursShort = "N/A";
        profesor = "N/A";
        ora = "N/A";
        return false;
    }

    String url = dashboardBaseUrl + "?sala=" + urlEncodeRoom(selectedRoom);

    Serial.print("Fetching schedule from: ");
    Serial.println(url);

    WiFiClient client;
    HTTPClient http;

    http.setTimeout(20000);
    http.useHTTP10(true);

    if (!http.begin(client, url))
    {
        Serial.println("Schedule HTTP begin failed.");
        sala = selectedRoom;
        curs = "N/A";
        cursShort = "N/A";
        profesor = "N/A";
        ora = "N/A";
        return false;
    }

    http.addHeader("User-Agent", "ESP32-Classroom-Display");

    int httpCode = http.GET();

    if (httpCode != HTTP_CODE_OK)
    {
        Serial.print("Schedule HTTP error: ");
        Serial.println(httpCode);
        http.end();
        sala = selectedRoom;
        curs = "N/A";
        cursShort = "N/A";
        profesor = "N/A";
        ora = "N/A";
        return false;
    }

    String payload = http.getString();
    http.end();

    Serial.print("Schedule payload: ");
    Serial.println(payload);

    StaticJsonDocument<2048> doc;
    DeserializationError error = deserializeJson(doc, payload);

    if (error)
    {
        Serial.print("Schedule JSON parse failed: ");
        Serial.println(error.c_str());
        sala = selectedRoom;
        curs = "N/A";
        cursShort = "N/A";
        profesor = "N/A";
        ora = "N/A";
        return false;
    }

    bool found = doc["found"] | false;

    sala = selectedRoom;

    if (!found)
    {
        curs = "N/A";
        cursShort = "N/A";
        profesor = "N/A";
        ora = "N/A";
        return true;
    }

    sala = doc["sala"] | selectedRoom;
    curs = doc["curs"] | "N/A";
    profesor = doc["profesor"] | "N/A";
    ora = doc["ora"] | "N/A";

    profesor = cleanProfessorName(profesor);

    const char *apiShort1 = doc["curs_scurt"] | nullptr;
    const char *apiShort2 = doc["course_short"] | nullptr;
    const char *apiShort3 = doc["acronym"] | nullptr;

    if (apiShort1 != nullptr && String(apiShort1).length() > 0)
    {
        cursShort = String(apiShort1);
    }
    else if (apiShort2 != nullptr && String(apiShort2).length() > 0)
    {
        cursShort = String(apiShort2);
    }
    else if (apiShort3 != nullptr && String(apiShort3).length() > 0)
    {
        cursShort = String(apiShort3);
    }
    else
    {
        cursShort = makeCourseAcronym(curs);
    }

    Serial.print("SALA: ");
    Serial.println(sala);
    Serial.print("Course full: ");
    Serial.println(curs);
    Serial.print("Course short: ");
    Serial.println(cursShort);
    Serial.print("Professor cleaned: ");
    Serial.println(profesor);
    Serial.print("Ora: ");
    Serial.println(ora);

    return true;
}

// ======================================================
// BMP280
// ======================================================
void setupBMP280()
{
    Serial.println("Starting BMP280...");
    Serial.println("SDA = GPIO 32");
    Serial.println("SCL = GPIO 33");
    Serial.println("Address = 0x77");

    Wire.begin(BMP_SDA, BMP_SCL);
    Wire.setClock(100000);

    delay(300);

    if (!bmp.begin(BMP280_ADDRESS))
    {
        Serial.println("BMP280 initialization FAILED.");
        bmpOK = false;
        return;
    }

    bmpOK = true;
    Serial.println("BMP280 initialization OK.");

    bmp.setSampling(
        Adafruit_BMP280::MODE_NORMAL,
        Adafruit_BMP280::SAMPLING_X2,
        Adafruit_BMP280::SAMPLING_X16,
        Adafruit_BMP280::FILTER_X16,
        Adafruit_BMP280::STANDBY_MS_500
    );
}

float readInteriorTemperature()
{
    if (!bmpOK)
    {
        Serial.println("BMP280 not available.");
        return NAN;
    }

    float rawTemp = bmp.readTemperature();
    float correctedTemp = rawTemp + TEMP_OFFSET;

    Serial.print("Raw interior temperature: ");
    Serial.print(rawTemp, 2);
    Serial.println(" C");

    Serial.print("Corrected interior temperature: ");
    Serial.print(correctedTemp, 2);
    Serial.println(" C");

    return correctedTemp;
}

// ======================================================
// DRAWING
// ======================================================
void drawCounterBox()
{
    int W = tft.width();

    int boxX = W - 82;
    int boxY = 16;
    int boxW = 66;
    int boxH = 42;

    tft.fillRoundRect(boxX, boxY, boxW, boxH, 5, C_BG);
    tft.drawRoundRect(boxX, boxY, boxW, boxH, 5, C_WHITE);

    tft.setTextDatum(TC_DATUM);
    tft.setTextColor(C_WHITE, C_BG);
    tft.drawString("Counter", boxX + boxW / 2, boxY + 5, 1);

    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(C_WHITE, C_BG);
    tft.drawString(String(pirCounter), boxX + boxW / 2, boxY + 27, 4);

    tft.setTextDatum(TL_DATUM);
}

void drawProfessorName(String name, int x, int y)
{
    name.trim();

    if (name.length() == 0)
    {
        name = "N/A";
    }

    if (name.length() <= 12)
    {
        tft.setTextColor(C_TEXT, C_BG);
        tft.drawString(name, x, y, 4);
    }
    else if (name.length() <= 18)
    {
        tft.setTextColor(C_TEXT, C_BG);
        tft.drawString(name, x, y + 4, 2);
    }
    else
    {
        String shortName = name.substring(0, 18);
        tft.setTextColor(C_TEXT, C_BG);
        tft.drawString(shortName, x, y + 4, 2);
    }
}

void drawFooterOnly()
{
    if (uiMode != UI_MAIN)
    {
        return;
    }

    int W = tft.width();
    int H = tft.height();

    int x = 8;
    int footerY = H - 28;

    tft.fillRect(x + 4, footerY + 1, W - 24, 20, C_BG);
    tft.drawFastHLine(x + 4, footerY, W - 24, C_LINE);

    drawWifiIcon(x + 14, footerY + 11, wifiOK ? C_GREEN : C_MUTED);

    if (wifiOK)
    {
        tft.setTextColor(C_GREEN, C_BG);
        tft.drawString("Connected", x + 26, footerY + 5, 1);
    }
    else
    {
        tft.setTextColor(C_MUTED, C_BG);
        tft.drawString("WiFi N/A", x + 26, footerY + 5, 1);
    }

    tft.setTextColor(C_MUTED, C_BG);
    tft.drawString(currentTimeText, x + 88, footerY + 5, 1);

    tft.drawString("Nitu Victor-Emanuil - ACS", x + 128, footerY + 5, 1);
}

void drawDashboard(float interiorTemp, float exteriorTemp)
{
    int W = tft.width();
    int H = tft.height();

    tft.fillScreen(C_BG);

    int x = 8;
    int y = 8;
    int w = W - 16;
    int h = H - 16;

    tft.drawRoundRect(x, y, w, h, 6, C_BLACK);
    tft.drawRoundRect(x + 1, y + 1, w - 2, h - 2, 6, C_LINE);

    int headerH = 58;
    tft.fillRect(x + 4, y + 4, w - 8, headerH, C_HEADER);

    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(C_WHITE, C_HEADER);

    tft.drawString("SALA", x + 16, y + 10, 2);
    tft.drawString(sala, x + 16, y + 30, 4);

    tft.setTextColor(C_WHITE, C_HEADER);
    tft.drawString("Press *", x + 16, y + 51, 1);

    tft.drawString("Curs curent", x + 100, y + 10, 2);
    tft.drawString(cursShort, x + 100, y + 30, 4);

    drawCounterBox();

    int bodyY = y + 4 + headerH;
    int tempY = y + 132;

    tft.setTextColor(C_TEXT, C_BG);

    tft.drawFastHLine(x + 4, bodyY, w - 8, C_LINE);
    tft.drawFastHLine(x + 4, tempY, w - 8, C_LINE);

    tft.drawString("PROFESOR", x + 16, bodyY + 17, 2);
    drawProfessorName(profesor, x + 130, bodyY + 12);

    tft.drawString("ORA", x + 16, bodyY + 49, 2);
    drawClockIcon(x + 132, bodyY + 57, C_TEXT);
    tft.drawString(ora, x + 154, bodyY + 45, 4);

    int middleX = W / 2;
    tft.drawFastVLine(middleX, tempY, 72, C_LINE);

    // Interior temperature only
    tft.drawString("INTERIOR", x + 16, tempY + 8, 2);
    drawThermometerIcon(x + 14, tempY + 36, C_TEXT);

    if (isnan(interiorTemp))
    {
        tft.drawString("N/A", x + 42, tempY + 34, 4);
    }
    else
    {
        String tempText = String(interiorTemp, 1) + "C";
        tft.drawString(tempText, x + 42, tempY + 32, 4);
    }

    // Exterior temperature + Weather hint
    int extX = middleX + 18;
    tft.setTextColor(C_TEXT, C_BG);
    tft.drawString("EXTERIOR", extX, tempY + 8, 2);
    drawSunIcon(extX + 15, tempY + 52, C_TEXT);

    if (isnan(exteriorTemp))
    {
        tft.drawString("N/A", extX + 48, tempY + 34, 4);
    }
    else
    {
        String extText = String(exteriorTemp, 1) + "C";
        tft.drawString(extText, extX + 48, tempY + 32, 4);
    }

    tft.setTextColor(C_MUTED, C_BG);
    tft.drawString("# Weather", extX + 48, tempY + 56, 2);

    drawFooterOnly();
}

// ======================================================
// WEATHER PAGE
// ======================================================
void drawWeatherPage()
{
    int W = tft.width();
    int H = tft.height();

    tft.fillScreen(C_BG);

    int x = 8;
    int y = 8;
    int w = W - 16;
    int h = H - 16;

    tft.drawRoundRect(x, y, w, h, 6, C_BLACK);
    tft.drawRoundRect(x + 1, y + 1, w - 2, h - 2, 6, C_LINE);

    tft.fillRect(x + 4, y + 4, w - 8, 34, C_HEADER);

    tft.setTextDatum(TC_DATUM);
    tft.setTextColor(C_WHITE, C_HEADER);
    tft.drawString("WEATHER - BUCHAREST", W / 2, y + 12, 2);

    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(C_TEXT, C_BG);

    // Current weather left side
    tft.drawString("NOW", x + 16, y + 50, 2);

    if (!isnan(weatherCurrentTemp))
    {
        String t = String(weatherCurrentTemp, 1) + "C";
        tft.drawString(t, x + 16, y + 70, 4);
    }
    else
    {
        tft.drawString("N/A", x + 16, y + 70, 4);
    }

    if (!isnan(weatherHumidity))
    {
        tft.setTextColor(C_MUTED, C_BG);
        tft.drawString("Hum " + String(weatherHumidity, 0) + "%", x + 16, y + 100, 2);
    }
    else
    {
        tft.setTextColor(C_MUTED, C_BG);
        tft.drawString("Hum N/A", x + 16, y + 100, 2);
    }

    // Wind right side
    tft.setTextColor(C_TEXT, C_BG);
    tft.drawString("WIND", x + 160, y + 50, 2);

    drawWindArrow(x + 205, y + 88, weatherWindDirection, C_TEXT);

    tft.setTextColor(C_MUTED, C_BG);

    if (!isnan(weatherWindSpeed))
    {
        tft.drawString(String(weatherWindSpeed, 1) + " km/h", x + 176, y + 115, 2);
    }
    else
    {
        tft.drawString("N/A km/h", x + 176, y + 115, 2);
    }

    if (!isnan(weatherWindDirection))
    {
        tft.drawString(String(weatherWindDirection, 0) + " deg", x + 176, y + 135, 2);
    }
    else
    {
        tft.drawString("N/A deg", x + 176, y + 135, 2);
    }

    // Forecast columns
    int forecastY = y + 160;
    tft.drawFastHLine(x + 4, forecastY - 8, w - 8, C_LINE);

    int colW = (w - 16) / 3;

    for (int i = 0; i < 3; i++)
    {
        int cx = x + 8 + i * colW;

        if (i > 0)
        {
            tft.drawFastVLine(cx - 5, forecastY - 2, 52, C_LINE);
        }

        tft.setTextColor(C_TEXT, C_BG);
        tft.drawString(weatherDate[i], cx + 6, forecastY, 2);

        tft.setTextColor(C_RED, C_BG);

        if (!isnan(weatherHigh[i]))
        {
            tft.drawString("H " + String(weatherHigh[i], 0) + "C", cx + 6, forecastY + 20, 2);
        }
        else
        {
            tft.drawString("H N/A", cx + 6, forecastY + 20, 2);
        }

        tft.setTextColor(C_BLUE, C_BG);

        if (!isnan(weatherLow[i]))
        {
            tft.drawString("L " + String(weatherLow[i], 0) + "C", cx + 6, forecastY + 38, 2);
        }
        else
        {
            tft.drawString("L N/A", cx + 6, forecastY + 38, 2);
        }
    }

    // Centered and smaller back text
    tft.setTextDatum(TC_DATUM);
    tft.setTextColor(C_MUTED, C_BG);
    tft.drawString("Press * to go back", W / 2, H - 18, 1);

    tft.setTextDatum(TL_DATUM);
}

// ======================================================
// ROOM SELECTION GUI
// ======================================================
void drawPrefixMenu()
{
    int W = tft.width();
    int H = tft.height();

    tft.fillScreen(C_BG);

    int x = 10;
    int y = 10;
    int w = W - 20;
    int h = H - 20;

    tft.drawRoundRect(x, y, w, h, 8, C_BLACK);
    tft.fillRect(x + 4, y + 4, w - 8, 38, C_HEADER);

    tft.setTextDatum(TC_DATUM);
    tft.setTextColor(C_WHITE, C_HEADER);
    tft.drawString("SELECT SALA PREFIX", W / 2, y + 12, 2);

    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(C_TEXT, C_BG);

    tft.drawString("1 = EC", x + 24, y + 58, 4);
    tft.drawString("2 = ED", x + 150, y + 58, 4);

    tft.drawString("3 = EF", x + 24, y + 100, 4);
    tft.drawString("4 = EG", x + 150, y + 100, 4);

    tft.drawString("5 = PR", x + 24, y + 142, 4);

    tft.setTextColor(C_MUTED, C_BG);
    tft.drawString("Press # to confirm", x + 24, H - 52, 2);
    tft.drawString("Press * to Go back", x + 24, H - 30, 2);

    tft.setTextDatum(TL_DATUM);
}

void drawRoomNumberMenu()
{
    int W = tft.width();
    int H = tft.height();

    tft.fillScreen(C_BG);

    int x = 10;
    int y = 10;
    int w = W - 20;
    int h = H - 20;

    tft.drawRoundRect(x, y, w, h, 8, C_BLACK);
    tft.fillRect(x + 4, y + 4, w - 8, 38, C_HEADER);

    tft.setTextDatum(TC_DATUM);
    tft.setTextColor(C_WHITE, C_HEADER);
    tft.drawString("ENTER ROOM NUMBER", W / 2, y + 12, 2);

    tft.setTextDatum(TC_DATUM);
    tft.setTextColor(C_TEXT, C_BG);
    tft.drawString("Prefix: " + selectedPrefix, W / 2, y + 58, 4);

    String preview = selectedPrefix + normalizeRoomNumber(roomNumberBuffer);

    if (roomNumberBuffer.length() == 0)
    {
        preview = selectedPrefix + "---";
    }

    tft.setTextColor(C_BLACK, C_BG);
    tft.drawString(preview, W / 2, y + 105, 6);

    tft.setTextColor(C_MUTED, C_BG);
    tft.drawString("Press # to confirm", W / 2, H - 52, 2);
    tft.drawString("Press * to Go back", W / 2, H - 30, 2);

    tft.setTextDatum(TL_DATUM);
}

// ======================================================
// REFRESH
// ======================================================
void refreshScreen()
{
    flashStatusLed();

    wifiOK = connectWiFi();

    if (wifiOK && !timeOK)
    {
        setupInternetTime();
    }

    currentTimeText = getCurrentTimeText();

    if (wifiOK)
    {
        fetchScheduleFromDashboard();
        fetchWeather();
    }

    lastInteriorTemp = readInteriorTemperature();

    drawDashboard(lastInteriorTemp, lastExteriorTemp);

    uiMode = UI_MAIN;

    Serial.println("Screen refreshed.");
}

void updateTimeOnly()
{
    if (uiMode != UI_MAIN)
    {
        return;
    }

    currentTimeText = getCurrentTimeText();
    drawFooterOnly();
}

// ======================================================
// PIR
// ======================================================
void handlePIR()
{
    bool pirState = digitalRead(PIR_PIN);
    unsigned long now = millis();

    if (pirState == HIGH && lastPirState == LOW)
    {
        if (now - lastPirTrigger >= PIR_COOLDOWN_MS)
        {
            pirCounter++;
            lastPirTrigger = now;

            Serial.print("PIR motion detected. Counter = ");
            Serial.println(pirCounter);

            if (uiMode == UI_MAIN)
            {
                drawCounterBox();
            }
        }
    }

    lastPirState = pirState;
}

// ======================================================
// KEYPAD HANDLING
// ======================================================
void handleKeypad()
{
    char key = keypad.getKey();

    if (!key)
    {
        return;
    }

    Serial.print("Key pressed: ");
    Serial.println(key);

    if (uiMode == UI_MAIN)
    {
        if (key == '*')
        {
            uiMode = UI_SELECT_PREFIX;
            selectedPrefix = "";
            roomNumberBuffer = "";
            drawPrefixMenu();
            return;
        }

        if (key == '#')
        {
            uiMode = UI_WEATHER;

            if (wifiOK)
            {
                fetchWeather();
            }

            drawWeatherPage();
            return;
        }

        return;
    }

    if (uiMode == UI_WEATHER)
    {
        if (key == '*')
        {
            uiMode = UI_MAIN;
            drawDashboard(lastInteriorTemp, lastExteriorTemp);
        }

        return;
    }

    if (uiMode == UI_SELECT_PREFIX)
    {
        if (key == '*')
        {
            uiMode = UI_MAIN;
            selectedPrefix = "";
            roomNumberBuffer = "";
            drawDashboard(lastInteriorTemp, lastExteriorTemp);
            return;
        }

        if (key == '1') selectedPrefix = "EC";
        else if (key == '2') selectedPrefix = "ED";
        else if (key == '3') selectedPrefix = "EF";
        else if (key == '4') selectedPrefix = "EG";
        else if (key == '5') selectedPrefix = "PR";
        else return;

        uiMode = UI_ENTER_ROOM_NUMBER;
        roomNumberBuffer = "";
        drawRoomNumberMenu();
        return;
    }

    if (uiMode == UI_ENTER_ROOM_NUMBER)
    {
        if (key == '*')
        {
            uiMode = UI_SELECT_PREFIX;
            roomNumberBuffer = "";
            drawPrefixMenu();
            return;
        }

        if (key >= '0' && key <= '9')
        {
            if (roomNumberBuffer.length() < 4)
            {
                roomNumberBuffer += key;
                drawRoomNumberMenu();
            }

            return;
        }

        if (key == '#')
        {
            if (selectedPrefix.length() == 0 || roomNumberBuffer.length() == 0)
            {
                drawRoomNumberMenu();
                return;
            }

            selectedRoom = selectedPrefix + normalizeRoomNumber(roomNumberBuffer);

            Serial.print("Selected room changed to: ");
            Serial.println(selectedRoom);

            uiMode = UI_MAIN;
            roomNumberBuffer = "";

            refreshScreen();

            lastFullRefresh = millis();
            lastTimeRefresh = millis();
            return;
        }
    }
}

// ======================================================
// SETUP
// ======================================================
void setup()
{
    pinMode(STATUS_LED, OUTPUT);
    digitalWrite(STATUS_LED, LOW);

    pinMode(PIR_PIN, INPUT);

    Serial.begin(115200);

    // Gives you time to open Serial Monitor after upload/reset.
    delay(7000);

    Serial.println();
    Serial.println("ESP32 Smart Classroom Display");
    Serial.println("-----------------------------");
    Serial.println("Starting WiFi scan in 3 seconds...");
    Serial.println("Open Serial Monitor now if you have not already.");

    delay(3000);

    scanAvailableWiFi();

    setupBMP280();

    Serial.println("Initializing TFT...");
    tft.init();

    // Your working orientation
    tft.setRotation(3);

    Serial.print("TFT logical width = ");
    Serial.print(tft.width());
    Serial.print(" height = ");
    Serial.println(tft.height());

    refreshScreen();

    lastFullRefresh = millis();
    lastTimeRefresh = millis();

    Serial.println("TFT initialized.");
}

// ======================================================
// LOOP
// ======================================================
void loop()
{
    handleSerialCommands();

    handlePIR();
    handleKeypad();

    unsigned long now = millis();

    if (uiMode == UI_MAIN && now - lastTimeRefresh >= TIME_REFRESH_INTERVAL_MS)
    {
        lastTimeRefresh = now;
        updateTimeOnly();
    }

    if (uiMode == UI_MAIN && now - lastFullRefresh >= FULL_REFRESH_INTERVAL_MS)
    {
        lastFullRefresh = now;
        refreshScreen();
    }

    delay(20);
}