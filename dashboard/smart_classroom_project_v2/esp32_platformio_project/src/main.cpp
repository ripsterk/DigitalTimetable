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

// -------------------- WIFI --------------------
const char *WIFI_SSID = "Informatica";
const char *WIFI_PASSWORD = "Info2026";

// Change this if your Ubuntu IP changes. Test in browser first:
// http://YOUR_UBUNTU_IP:5000/api/current?sala=EC002
String dashboardBaseUrl = "http://192.168.10.38:5000/api/current";

// Current room shown by ESP32
String selectedRoom = "EC002";

// Open-Meteo Bucharest: temperature + weather condition + wind
const char *WEATHER_URL =
    "http://api.open-meteo.com/v1/forecast?latitude=44.4268&longitude=26.1025&current=temperature_2m,weather_code,wind_speed_10m&timezone=Europe%2FBucharest";

// -------------------- BMP280 SENSOR --------------------
#define BMP_SDA 32
#define BMP_SCL 33
#define BMP280_ADDRESS 0x77
#define TEMP_OFFSET -4.0

// -------------------- PIR SENSOR --------------------
#define PIR_PIN 27
#define PIR_COOLDOWN_MS 8000UL

volatile unsigned long pirCounter = 0;
bool lastPirState = LOW;
unsigned long lastPirTrigger = 0;

// -------------------- ESP32 LED --------------------
#define STATUS_LED 2

// -------------------- TIMING --------------------
#define FULL_REFRESH_INTERVAL_MS 60000UL
#define TIME_REFRESH_INTERVAL_MS 10000UL

// -------------------- KEYPAD --------------------
const byte ROWS = 4;
const byte COLS = 3;

char keys[ROWS][COLS] = {
    {'1', '2', '3'},
    {'4', '5', '6'},
    {'7', '8', '9'},
    {'*', '0', '#'}};

byte rowPins[ROWS] = {13, 14, 21, 22};
byte colPins[COLS] = {25, 26, 4};

Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

// -------------------- UI MODE --------------------
enum UiMode
{
    MODE_DASHBOARD,
    MODE_SELECT_PREFIX,
    MODE_ENTER_ROOM_NUMBER
};

UiMode uiMode = MODE_DASHBOARD;

String tempPrefix = "";
String tempRoomNumber = "";

// -------------------- OBJECTS --------------------
TFT_eSPI tft = TFT_eSPI();
Adafruit_BMP280 bmp;

bool bmpOK = false;
bool wifiOK = false;
bool timeOK = false;

unsigned long lastFullRefresh = 0;
unsigned long lastTimeRefresh = 0;

float lastExteriorTemp = NAN;
float lastInteriorTemp = NAN;
int lastWeatherCode = -1;
float lastWindSpeed = NAN;

// -------------------- COLORS --------------------
#define C_BLACK TFT_BLACK
#define C_WHITE TFT_WHITE
#define C_TEXT TFT_BLACK
#define C_HEADER 0x18E3
#define C_BG 0xEF5D
#define C_LINE 0xC618
#define C_MUTED 0x7BEF
#define C_GREEN TFT_GREEN
#define C_RED TFT_RED
#define C_YELLOW TFT_YELLOW
#define C_BLUE TFT_BLUE
#define C_ORANGE 0xFD20
#define C_DARK TFT_BLACK

// -------------------- DISPLAY DATA --------------------
String sala = "N/A";
String curs = "N/A";
String cursShort = "N/A";
String profesor = "N/A";
String ora = "N/A";
String currentTimeText = "N/A";

// -------------------- ICONS --------------------
void drawClockIcon(int x, int y, uint16_t color)
{
    tft.drawCircle(x, y, 9, color);
    tft.drawLine(x, y, x, y - 6, color);
    tft.drawLine(x, y, x + 6, y, color);
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
    tft.fillCircle(x, y, 1, color);
    tft.drawArc(x, y - 1, 6, 5, 220, 320, color, C_BG);
    tft.drawArc(x, y - 1, 10, 8, 220, 320, color, C_BG);
}

void drawCloudIcon(int x, int y, uint16_t color)
{
    tft.fillCircle(x, y, 7, color);
    tft.fillCircle(x + 9, y - 4, 9, color);
    tft.fillCircle(x + 19, y, 7, color);
    tft.fillRect(x - 2, y, 24, 7, color);
}

void drawRainIcon(int x, int y, uint16_t color)
{
    drawCloudIcon(x, y, C_MUTED);
    tft.drawLine(x + 3, y + 13, x, y + 20, color);
    tft.drawLine(x + 12, y + 13, x + 9, y + 20, color);
    tft.drawLine(x + 21, y + 13, x + 18, y + 20, color);
}

void drawSnowIcon(int x, int y, uint16_t color)
{
    drawCloudIcon(x, y, C_MUTED);
    tft.setTextColor(color, C_BG);
    tft.drawString("*", x + 1, y + 10, 2);
    tft.drawString("*", x + 11, y + 14, 2);
    tft.drawString("*", x + 21, y + 10, 2);
}

void drawWindIcon(int x, int y, uint16_t color)
{
    tft.drawLine(x, y, x + 28, y, color);
    tft.drawLine(x + 4, y + 8, x + 32, y + 8, color);
    tft.drawLine(x, y + 16, x + 24, y + 16, color);
}

void drawWeatherIconByCode(int x, int y, int weatherCode, float windSpeed)
{
    if (!isnan(windSpeed) && windSpeed >= 35.0)
    {
        drawWindIcon(x - 10, y - 8, C_TEXT);
        return;
    }

    if (weatherCode == 0)
    {
        drawSunIcon(x, y, C_TEXT);
        return;
    }

    if (weatherCode >= 1 && weatherCode <= 3)
    {
        drawCloudIcon(x - 10, y - 4, C_TEXT);
        return;
    }

    if (weatherCode == 45 || weatherCode == 48)
    {
        drawWindIcon(x - 10, y - 8, C_MUTED);
        return;
    }

    if ((weatherCode >= 51 && weatherCode <= 67) ||
        (weatherCode >= 80 && weatherCode <= 82))
    {
        drawRainIcon(x - 10, y - 8, C_BLUE);
        return;
    }

    if ((weatherCode >= 71 && weatherCode <= 77) ||
        (weatherCode >= 85 && weatherCode <= 86))
    {
        drawSnowIcon(x - 10, y - 8, C_TEXT);
        return;
    }

    if (weatherCode >= 95 && weatherCode <= 99)
    {
        drawRainIcon(x - 10, y - 8, C_RED);
        return;
    }

    drawSunIcon(x, y, C_TEXT);
}

uint16_t getTemperatureColor(float temp)
{
    if (isnan(temp))
        return C_MUTED;
    if (temp < 22.0)
        return C_BLUE;
    if (temp < 25.0)
        return C_GREEN;
    if (temp < 28.0)
        return C_ORANGE;
    return C_RED;
}

int getTemperatureFillHeight(float temp)
{
    if (isnan(temp))
        return 0;

    float minTemp = 15.0;
    float maxTemp = 35.0;

    if (temp < minTemp)
        temp = minTemp;
    if (temp > maxTemp)
        temp = maxTemp;

    float ratio = (temp - minTemp) / (maxTemp - minTemp);
    return (int)(ratio * 25.0);
}

void drawThermometerIconFilled(int x, int y, float temp)
{
    uint16_t color = getTemperatureColor(temp);

    tft.drawRoundRect(x + 1, y, 9, 30, 4, C_TEXT);
    tft.drawCircle(x + 5, y + 25, 5, C_TEXT);

    tft.fillCircle(x + 5, y + 25, 4, color);

    int fillH = getTemperatureFillHeight(temp);
    int tubeBottom = y + 25;
    int tubeTop = tubeBottom - fillH;

    if (fillH > 0)
    {
        tft.fillRoundRect(x + 3, tubeTop, 5, fillH, 3, color);
    }
}

// -------------------- UTILS --------------------
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

String normalizeRoomNumber(String number)
{
    number.trim();
    while (number.length() < 3)
        number = "0" + number;
    return number;
}

String makeSelectedRoom(String prefix, String number)
{
    return prefix + normalizeRoomNumber(number);
}

String urlEncodeRoom(String room)
{
    room.replace(" ", "");
    return room;
}

bool isBadValue(String value)
{
    value.trim();
    return value.length() == 0 || value == "N/A" || value == "null" || value == "NULL";
}

String firstGoodValue(const char *a, const char *b, const char *c, const char *d, const char *e)
{
    String values[5] = {
        a == nullptr ? "" : String(a),
        b == nullptr ? "" : String(b),
        c == nullptr ? "" : String(c),
        d == nullptr ? "" : String(d),
        e == nullptr ? "" : String(e)};

    for (int i = 0; i < 5; i++)
    {
        values[i].trim();
        if (!isBadValue(values[i]))
            return values[i];
    }
    return "N/A";
}

bool isStopWord(String word)
{
    word.toLowerCase();
    return word == "si" || word == "și" || word == "in" || word == "în" || word == "de" ||
           word == "la" || word == "cu" || word == "pe" || word == "the" || word == "and" ||
           word == "of" || word == "for" || word == "to" || word == "a" || word == "an";
}

String cleanToken(String token)
{
    token.trim();
    String cleaned = "";
    for (unsigned int i = 0; i < token.length(); i++)
    {
        char c = token.charAt(i);
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))
            cleaned += c;
    }
    return cleaned;
}

String makeCourseAcronym(String name)
{
    name.trim();
    if (name.length() == 0 || name == "N/A")
        return "N/A";
    if (name.length() <= 8)
        return name;

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
            source += c;
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
                    first -= 32;
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
        return name.substring(0, min((int)name.length(), 8));
    if (acronym.length() > 8)
        acronym = acronym.substring(0, 8);
    return acronym;
}

// -------------------- WIFI --------------------
bool connectWiFi()
{
    if (WiFi.status() == WL_CONNECTED)
        return true;

    Serial.println();
    Serial.print("Connecting to WiFi: ");
    Serial.println(WIFI_SSID);

    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
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
        Serial.print("RSSI: ");
        Serial.println(WiFi.RSSI());
        return true;
    }

    Serial.println("WiFi connection FAILED.");
    return false;
}

// -------------------- TIME --------------------
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
        return "N/A";
    char buffer[6];
    strftime(buffer, sizeof(buffer), "%H:%M", &timeinfo);
    return String(buffer);
}

// -------------------- WEATHER --------------------
float fetchExteriorTemperature()
{
    if (WiFi.status() != WL_CONNECTED)
    {
        Serial.println("Cannot fetch weather: WiFi not connected.");
        return NAN;
    }

    Serial.println("Fetching exterior temperature for Bucharest...");
    WiFiClient client;
    HTTPClient http;
    http.setTimeout(20000);
    http.useHTTP10(true);

    if (!http.begin(client, WEATHER_URL))
    {
        Serial.println("HTTP begin failed.");
        return NAN;
    }

    http.addHeader("User-Agent", "ESP32-Classroom-Display");
    int httpCode = http.GET();
    if (httpCode != HTTP_CODE_OK)
    {
        Serial.print("Weather HTTP error: ");
        Serial.println(httpCode);
        http.end();
        return NAN;
    }

    String payload = http.getString();
    http.end();

    StaticJsonDocument<1536> doc;
    DeserializationError error = deserializeJson(doc, payload);
    if (error)
    {
        Serial.print("Weather JSON parse failed: ");
        Serial.println(error.c_str());
        return NAN;
    }

    float temperature = doc["current"]["temperature_2m"] | NAN;
    lastWeatherCode = doc["current"]["weather_code"] | -1;
    lastWindSpeed = doc["current"]["wind_speed_10m"] | NAN;

    if (isnan(temperature))
    {
        Serial.println("Weather temperature missing from JSON.");
        return NAN;
    }

    Serial.print("Exterior temperature Bucharest: ");
    Serial.print(temperature, 1);
    Serial.println(" C");
    Serial.print("Weather code: ");
    Serial.println(lastWeatherCode);
    Serial.print("Wind speed: ");
    Serial.println(lastWindSpeed);

    return temperature;
}

// -------------------- DASHBOARD API --------------------
bool fetchScheduleFromDashboard()
{
    if (WiFi.status() != WL_CONNECTED)
    {
        Serial.println("Cannot fetch schedule: WiFi not connected.");
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
        return false;
    }

    http.addHeader("User-Agent", "ESP32-Classroom-Display");
    int httpCode = http.GET();
    if (httpCode != HTTP_CODE_OK)
    {
        Serial.print("Schedule HTTP error: ");
        Serial.println(httpCode);
        http.end();
        return false;
    }

    String payload = http.getString();
    http.end();
    Serial.print("Schedule payload: ");
    Serial.println(payload);

    StaticJsonDocument<2304> doc;
    DeserializationError error = deserializeJson(doc, payload);
    if (error)
    {
        Serial.print("Schedule JSON parse failed: ");
        Serial.println(error.c_str());
        return false;
    }

    bool found = doc["found"] | false;
    JsonObject source;

    if (found)
    {
        source = doc.as<JsonObject>();
    }
    else if (doc["next"].is<JsonObject>())
    {
        source = doc["next"].as<JsonObject>();
        Serial.println("No active course; showing NEXT course.");
    }
    else
    {
        sala = selectedRoom;
        curs = "N/A";
        cursShort = "N/A";
        profesor = "N/A";
        ora = "N/A";
        return true;
    }

    sala = doc["sala"] | selectedRoom;
    curs = source["curs"] | "N/A";
    ora = source["ora"] | "N/A";

    profesor = firstGoodValue(
        source["profesor"] | nullptr,
        source["professor"] | nullptr,
        source["prof"] | nullptr,
        source["teacher"] | nullptr,
        source["cadru"] | nullptr);

    const char *apiShort1 = source["curs_scurt"] | nullptr;
    const char *apiShort2 = source["course_short"] | nullptr;
    const char *apiShort3 = source["acronym"] | nullptr;

    if (apiShort1 != nullptr && String(apiShort1).length() > 0)
        cursShort = String(apiShort1);
    else if (apiShort2 != nullptr && String(apiShort2).length() > 0)
        cursShort = String(apiShort2);
    else if (apiShort3 != nullptr && String(apiShort3).length() > 0)
        cursShort = String(apiShort3);
    else
        cursShort = makeCourseAcronym(curs);

    Serial.print("Selected room: ");
    Serial.println(selectedRoom);
    Serial.print("Course full: ");
    Serial.println(curs);
    Serial.print("Course short: ");
    Serial.println(cursShort);
    Serial.print("Professor: ");
    Serial.println(profesor);

    return true;
}

// -------------------- BMP280 --------------------
void setupBMP280()
{
    Serial.println("Starting BMP280...");
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
        Adafruit_BMP280::STANDBY_MS_500);
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

// -------------------- KEYPAD UI SCREENS --------------------
void drawPrefixSelectScreen()
{
    tft.fillScreen(C_BLACK);
    int W = tft.width();
    int H = tft.height();
    tft.drawRoundRect(6, 6, W - 12, H - 12, 6, C_WHITE);
    tft.setTextDatum(TC_DATUM);
    tft.setTextColor(C_WHITE, C_BLACK);
    tft.drawString("SELECT ROOM PREFIX", W / 2, 14, 2);
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(C_MUTED, C_BLACK);
    tft.drawString("* cancel    # confirm", 20, H - 22, 1);

    int boxW = 88;
    int boxH = 44;
    int x1 = 22, x2 = 116, x3 = 210;
    int y1 = 48, y2 = 108;

    tft.setTextColor(C_WHITE, C_BLACK);

    tft.drawRoundRect(x1, y1, boxW, boxH, 5, C_WHITE);
    tft.drawString("1", x1 + 8, y1 + 8, 2);
    tft.drawString("EG", x1 + 36, y1 + 10, 4);

    tft.drawRoundRect(x2, y1, boxW, boxH, 5, C_WHITE);
    tft.drawString("2", x2 + 8, y1 + 8, 2);
    tft.drawString("EF", x2 + 36, y1 + 10, 4);

    tft.drawRoundRect(x3, y1, boxW, boxH, 5, C_WHITE);
    tft.drawString("3", x3 + 8, y1 + 8, 2);
    tft.drawString("ED", x3 + 36, y1 + 10, 4);

    tft.drawRoundRect(x1, y2, boxW, boxH, 5, C_WHITE);
    tft.drawString("4", x1 + 8, y2 + 8, 2);
    tft.drawString("EC", x1 + 36, y2 + 10, 4);

    tft.drawRoundRect(x2, y2, boxW, boxH, 5, C_WHITE);
    tft.drawString("5", x2 + 8, y2 + 8, 2);
    tft.drawString("PR", x2 + 36, y2 + 10, 4);

    if (tempPrefix.length() > 0)
    {
        tft.setTextDatum(TC_DATUM);
        tft.setTextColor(C_GREEN, C_BLACK);
        tft.drawString("Selected: " + tempPrefix, W / 2, 172, 2);
        tft.setTextDatum(TL_DATUM);
    }
}

void drawRoomNumberScreen()
{
    tft.fillScreen(C_BLACK);
    int W = tft.width();
    int H = tft.height();
    tft.drawRoundRect(6, 6, W - 12, H - 12, 6, C_WHITE);
    tft.setTextDatum(TC_DATUM);
    tft.setTextColor(C_WHITE, C_BLACK);
    tft.drawString("ENTER ROOM NUMBER", W / 2, 16, 2);
    tft.setTextColor(C_GREEN, C_BLACK);
    tft.drawString("Prefix: " + tempPrefix, W / 2, 48, 4);
    tft.setTextColor(C_WHITE, C_BLACK);
    tft.drawString("Room:", W / 2, 88, 2);

    String shown = tempRoomNumber;
    if (shown.length() == 0)
        shown = "_";

    tft.setTextColor(C_YELLOW, C_BLACK);
    tft.drawString(tempPrefix + shown, W / 2, 112, 6);
    tft.setTextColor(C_MUTED, C_BLACK);
    tft.drawString("Digits = number", W / 2, 168, 1);
    tft.drawString("* clear/back    # confirm", W / 2, H - 24, 1);
    tft.setTextDatum(TL_DATUM);
}

// -------------------- DRAWING --------------------
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

void drawTemperatureBox(int x, int y, String label, float value, bool exterior)
{
    int boxW = 138;
    int boxH = 60;

    tft.fillRoundRect(x, y, boxW, boxH, 5, C_BG);
    tft.drawRoundRect(x, y, boxW, boxH, 5, C_LINE);

    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(C_TEXT, C_BG);
    tft.drawString(label, x + 8, y + 5, 2);

    if (exterior)
    {
        drawWeatherIconByCode(x + 22, y + 40, lastWeatherCode, lastWindSpeed);
    }
    else
    {
        drawThermometerIconFilled(x + 13, y + 24, value);
    }

    if (isnan(value))
    {
        tft.setTextColor(C_TEXT, C_BG);
        tft.drawString("N/A", x + 50, y + 30, 4);
    }
    else
    {
        uint16_t tempColor = exterior ? C_TEXT : getTemperatureColor(value);
        tft.setTextColor(tempColor, C_BG);
        String tempText = String(value, 1) + " C";
        tft.drawString(tempText, x + 50, y + 30, 4);
    }
}

void drawFooterOnly()
{
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
    tft.drawString(sala, x + 16, y + 29, 4);

    tft.setTextColor(C_MUTED, C_HEADER);
    tft.drawString("* room  # ok", x + 16, y + 51, 1);

    tft.setTextColor(C_WHITE, C_HEADER);
    tft.drawString("Curs curent", x + 115, y + 10, 2);
    tft.drawString(cursShort, x + 115, y + 30, 4);

    drawCounterBox();

    int bodyY = y + 4 + headerH;
    int tempY = y + 132;

    tft.setTextColor(C_TEXT, C_BG);
    tft.drawFastHLine(x + 4, bodyY, w - 8, C_LINE);
    tft.drawFastHLine(x + 4, tempY, w - 8, C_LINE);

    tft.drawString("PROFESOR", x + 16, bodyY + 17, 2);
    if (profesor.length() > 16)
        tft.drawString(profesor, x + 130, bodyY + 18, 2);
    else
        tft.drawString(profesor, x + 130, bodyY + 12, 4);

    tft.drawString("ORA", x + 16, bodyY + 49, 2);
    drawClockIcon(x + 132, bodyY + 57, C_TEXT);
    tft.drawString(ora, x + 154, bodyY + 45, 4);

    drawTemperatureBox(x + 12, tempY + 8, "INTERIOR", interiorTemp, false);
    drawTemperatureBox(W / 2 + 10, tempY + 8, "EXTERIOR", exteriorTemp, true);

    drawFooterOnly();
}

// -------------------- REFRESH --------------------
void refreshScreen()
{
    flashStatusLed();
    wifiOK = connectWiFi();

    if (wifiOK && !timeOK)
        setupInternetTime();

    currentTimeText = getCurrentTimeText();

    if (wifiOK)
    {
        fetchScheduleFromDashboard();
        float fetchedExteriorTemp = fetchExteriorTemperature();
        if (!isnan(fetchedExteriorTemp))
            lastExteriorTemp = fetchedExteriorTemp;
    }

    lastInteriorTemp = readInteriorTemperature();
    drawDashboard(lastInteriorTemp, lastExteriorTemp);
    Serial.println("Screen refreshed.");
}

void updateTimeOnly()
{
    if (uiMode != MODE_DASHBOARD)
        return;
    currentTimeText = getCurrentTimeText();
    drawFooterOnly();
}

// -------------------- PIR --------------------
void handlePIR()
{
    if (uiMode != MODE_DASHBOARD)
        return;

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
            drawCounterBox();
        }
    }

    lastPirState = pirState;
}

// -------------------- KEYPAD HANDLING --------------------
void handleKeypad()
{
    char key = keypad.getKey();
    if (!key)
        return;

    Serial.print("Key pressed: ");
    Serial.println(key);

    if (uiMode == MODE_DASHBOARD)
    {
        if (key == '*')
        {
            tempPrefix = "";
            tempRoomNumber = "";
            uiMode = MODE_SELECT_PREFIX;
            drawPrefixSelectScreen();
        }
        return;
    }

    if (uiMode == MODE_SELECT_PREFIX)
    {
        if (key == '*')
        {
            uiMode = MODE_DASHBOARD;
            drawDashboard(lastInteriorTemp, lastExteriorTemp);
            return;
        }

        if (key == '1') tempPrefix = "EG";
        if (key == '2') tempPrefix = "EF";
        if (key == '3') tempPrefix = "ED";
        if (key == '4') tempPrefix = "EC";
        if (key == '5') tempPrefix = "PR";

        if (key >= '1' && key <= '5')
        {
            drawPrefixSelectScreen();
            return;
        }

        if (key == '#')
        {
            if (tempPrefix.length() > 0)
            {
                tempRoomNumber = "";
                uiMode = MODE_ENTER_ROOM_NUMBER;
                drawRoomNumberScreen();
            }
            return;
        }
        return;
    }

    if (uiMode == MODE_ENTER_ROOM_NUMBER)
    {
        if (key == '*')
        {
            if (tempRoomNumber.length() > 0)
            {
                tempRoomNumber.remove(tempRoomNumber.length() - 1);
                drawRoomNumberScreen();
            }
            else
            {
                uiMode = MODE_SELECT_PREFIX;
                drawPrefixSelectScreen();
            }
            return;
        }

        if (key >= '0' && key <= '9')
        {
            if (tempRoomNumber.length() < 3)
            {
                tempRoomNumber += key;
                drawRoomNumberScreen();
            }
            return;
        }

        if (key == '#')
        {
            if (tempPrefix.length() > 0 && tempRoomNumber.length() > 0)
            {
                selectedRoom = makeSelectedRoom(tempPrefix, tempRoomNumber);
                Serial.print("New selected room: ");
                Serial.println(selectedRoom);
                uiMode = MODE_DASHBOARD;
                refreshScreen();
                lastFullRefresh = millis();
                lastTimeRefresh = millis();
            }
            return;
        }
    }
}

// -------------------- SETUP --------------------
void setup()
{
    pinMode(STATUS_LED, OUTPUT);
    digitalWrite(STATUS_LED, LOW);
    pinMode(PIR_PIN, INPUT);

    Serial.begin(115200);
    delay(1500);

    Serial.println();
    Serial.println("ESP32 Smart Classroom Display");
    Serial.println("-----------------------------");

    setupBMP280();

    Serial.println("Initializing TFT...");
    tft.init();
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

// -------------------- LOOP --------------------
void loop()
{
    handleKeypad();
    handlePIR();

    unsigned long now = millis();
    if (uiMode == MODE_DASHBOARD)
    {
        if (now - lastTimeRefresh >= TIME_REFRESH_INTERVAL_MS)
        {
            lastTimeRefresh = now;
            updateTimeOnly();
        }

        if (now - lastFullRefresh >= FULL_REFRESH_INTERVAL_MS)
        {
            lastFullRefresh = now;
            refreshScreen();
        }
    }

    delay(20);
}
