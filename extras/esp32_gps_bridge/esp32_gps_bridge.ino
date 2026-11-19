/*
 * wM-buster+ GPS transport bridge, ORIGINAL ESP32 (WROOM/WROVER) ONLY.
 *
 * Set BRIDGE_MODE to MODE_BT_SPP for battery Bluetooth GPS receivers
 * such as older Pentagram Pathfinder units (exact P3303 profile unverified).
 * Set MODE_PHONE_WIFI for coordinates provided by an Android automation.
 *
 * ESP32 TX2 GPIO17 -> Flipper USART RX GPIO pin 14 (3.3V)
 * ESP32 GND -> Flipper GND pin 11. Separate USB power is OK.
 * Never connect ESP32's 5V to a 3.3V UART input.
 *
 * UART format: NMEA 0183, 9600 baud, 8N1; built-in checksum parser on Flipper.
 * Needs Arduino-ESP32 2.x/3.x on original ESP32 with Bluetooth Classic.
 */
#include <Arduino.h>
#include <math.h>

#define MODE_BT_SPP   1
#define MODE_PHONE_WIFI 2
#define BRIDGE_MODE MODE_BT_SPP

static constexpr int FLIPPER_TX_PIN = 17;
static constexpr unsigned long FLIPPER_BAUD = 9600;

#if BRIDGE_MODE == MODE_BT_SPP
#include <BluetoothSerial.h>
BluetoothSerial gpsBT;
// Change to Bluetooth MAC of your GPS, visible when paired with Android/PC.
// Replace all zeroes before uploading.
uint8_t gpsMac[6] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
static const char* gpsPin = "0000"; // common on old receivers; check manual
unsigned long retryAt = 0;
char nmeaLine[128];
size_t nmeaUsed = 0;

static void incomingBtChar(char ch) {
    if(ch == '$') {
        nmeaUsed = 0;
        nmeaLine[nmeaUsed++] = ch;
    } else if(nmeaUsed > 0) {
        if(ch == '\n') {
            while(nmeaUsed > 0 && nmeaLine[nmeaUsed - 1] == '\r') --nmeaUsed;
            nmeaLine[nmeaUsed] = 0;
            if(strchr(nmeaLine, '*')) Serial2.println(nmeaLine);
            nmeaUsed = 0;
        } else if(nmeaUsed < sizeof(nmeaLine) - 1) {
            nmeaLine[nmeaUsed++] = ch;
        } else {
            nmeaUsed = 0;
        }
    }
}
#elif BRIDGE_MODE == MODE_PHONE_WIFI
#include <WiFi.h>
#include <WebServer.h>
static const char* apName = "wmGPS-Bridge";
static const char* apPassword = "change-this-password"; // >= 8 chars
WebServer http(80);
unsigned long lastFix = 0;

static String nmeaCoord(double degrees, bool latitude, char& hemisphere) {
    hemisphere = degrees < 0 ? (latitude ? 'S' : 'W') : (latitude ? 'N' : 'E');
    double positive = fabs(degrees);
    int whole = (int)positive;
    double minutes = (positive - whole) * 60.0;
    char buf[24];
    if(latitude) snprintf(buf, sizeof(buf), "%02d%07.4f", whole, minutes);
    else snprintf(buf, sizeof(buf), "%03d%07.4f", whole, minutes);
    return String(buf);
}

static void forwardPhoneFix() {
    if(!http.hasArg("lat") || !http.hasArg("lon")) {
        http.send(400, "text/plain", "Required: lat, lon");
        return;
    }
    String latArg = http.arg("lat"), lonArg = http.arg("lon");
    char* endLat = nullptr;
    char* endLon = nullptr;
    double lat = strtod(latArg.c_str(), &endLat);
    double lon = strtod(lonArg.c_str(), &endLon);
    if(!endLat || !endLon || *endLat != 0 || *endLon != 0 ||
       !isfinite(lat) || !isfinite(lon) ||
       lat < -90 || lat > 90 || lon < -180 || lon > 180) {
        http.send(422, "text/plain", "Invalid coordinates");
        return;
    }
    // Only explicit fresh updates are emitted: if the phone stops sending,
    // Flipper's position expires. No fabricated satellites, accuracy or UTC.
    char ns, ew;
    String la = nmeaCoord(lat, true, ns);
    String lo = nmeaCoord(lon, false, ew);
    String body = "GPGGA,000000.00," + la + "," + ns + "," + lo + "," + ew +
                  ",1,00,,,,,,";
    uint8_t checksum = 0;
    for(size_t i = 0; i < body.length(); i++) checksum ^= (uint8_t)body[i];
    char hex[4];
    snprintf(hex, sizeof(hex), "%02X", checksum);
    Serial2.print("$");
    Serial2.print(body);
    Serial2.print("*");
    Serial2.println(hex);
    lastFix = millis();
    http.send(200, "text/plain", "GPS forwarded");
}

static void setupPhoneServer() {
    WiFi.mode(WIFI_AP);
    WiFi.softAP(apName, apPassword);
    http.on("/fix", HTTP_GET, forwardPhoneFix);
    http.on("/status", HTTP_GET, []() {
        http.send(200, "text/plain",
            String("GPS bridge ready; last update age ms: ") +
            (lastFix ? String(millis() - lastFix) : "never"));
    });
    http.begin();
    Serial.print("Connect phone to WiFi: ");
    Serial.println(apName);
    Serial.println("GPS HTTP endpoint: http://192.168.4.1/fix?lat=51.75&lon=19.45");
    Serial.println("Sample coordinates are for testing ONLY.");
}
#else
#error "Choose MODE_BT_SPP or MODE_PHONE_WIFI"
#endif

void setup() {
    Serial.begin(115200);
    Serial2.begin(FLIPPER_BAUD, SERIAL_8N1, -1, FLIPPER_TX_PIN);
#if BRIDGE_MODE == MODE_BT_SPP
    if(!gpsBT.begin("wmGPSBridge", true)) {
        Serial.println("ERROR: Bluetooth Classic SPP not supported/enabled");
        return;
    }
    gpsBT.setPin(gpsPin, strlen(gpsPin));
    Serial.println("BT GPS bridge; edit gpsMac[] and gpsPin before use.");
#else
    setupPhoneServer();
#endif
}

void loop() {
#if BRIDGE_MODE == MODE_BT_SPP
    if(!gpsBT.connected(0)) {
        if(millis() - retryAt >= 10000 || retryAt == 0) {
            retryAt = millis();
            Serial.println("Connecting to BT GPS SPP...");
            if(gpsBT.connect(gpsMac)) Serial.println("BT GPS connected");
            else Serial.println("BT SPP connection failed");
        }
        delay(20);
        return;
    }
    size_t budget = 256;
    while(gpsBT.available() && budget--) {
        incomingBtChar((char)gpsBT.read());
    }
#else
    http.handleClient();
#endif
    delay(2);
}
