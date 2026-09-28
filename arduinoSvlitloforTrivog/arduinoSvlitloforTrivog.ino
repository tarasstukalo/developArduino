#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

HWCDC USBSerial;

// ================= WI-FI =================

const char* ssid = "Home Protsak";
const char* password = "Wednesday";

const char* host = "neptun.in.ua";
const int port = 443;


// ================= LED =================

// Вбудований LED на ESP32-C3 Super Mini
const int LED_PIN = 8;


// ================= СТАТУСИ =================

enum AlertStatus {
  STATUS_GREEN,    // Відбій
  STATUS_UAV,      // БПЛА
  STATUS_MISSILE   // Ракета
};

AlertStatus currentStatus = STATUS_GREEN;


// ================= ТАЙМЕРИ =================

unsigned long lastCheck = 0;
const unsigned long checkInterval = 5000;

unsigned long lastBlink = 0;
bool ledState = false;


// ================= LED =================

void updateLed() {

  // 🔴 РАКЕТНА ЗАГРОЗА
  if (currentStatus == STATUS_MISSILE) {

    digitalWrite(LED_PIN, HIGH);
    return;
  }


  // 🟢 ВІДБІЙ
  if (currentStatus == STATUS_GREEN) {

    digitalWrite(LED_PIN, LOW);
    ledState = false;
    return;
  }


  // 🟠 БПЛА — блимає кожну секунду
  if (currentStatus == STATUS_UAV) {

    if (millis() - lastBlink >= 1000) {

      lastBlink = millis();

      ledState = !ledState;

      digitalWrite(LED_PIN, ledState);
    }
  }
}


// ================= HTTP ЗАПИТ =================

String makeGetRequest(const char* path) {

  WiFiClientSecure client;

  client.setInsecure();

  USBSerial.print("Запит: ");
  USBSerial.println(path);

  if (!client.connect(host, port)) {

    USBSerial.println("Помилка підключення до NEPTUN!");

    return "";
  }


  client.printf("GET %s HTTP/1.1\r\n", path);
  client.printf("Host: %s\r\n", host);
  client.printf("User-Agent: ESP32-C3-AlertMonitor\r\n");
  client.printf("Accept: application/json\r\n");
  client.printf("Connection: close\r\n\r\n");


  unsigned long timeout = millis();

  while (client.connected() && !client.available()) {

    if (millis() - timeout > 5000) {

      USBSerial.println("Timeout!");

      client.stop();

      return "";
    }

    delay(10);
  }


  String response = "";

  while (client.available()) {

    response += client.readString();
  }

  client.stop();


  return response;
}


// ================= ПЕРЕВІРКА ТРИВОГ =================

void checkAlerts() {

  if (WiFi.status() != WL_CONNECTED) {

    USBSerial.println("Wi-Fi відключено!");

    return;
  }


  USBSerial.println();
  USBSerial.println("=================================");
  USBSerial.println("Перевірка загроз...");


  // Отримуємо активні загрози

  String threatsJson = makeGetRequest("/api/v1/threats");


  if (threatsJson.length() == 0) {

    USBSerial.println("Не отримано дані!");

    return;
  }


  // Переводимо все в нижній регістр

  threatsJson.toLowerCase();


  // ================= РАКЕТИ =================

  bool missileThreat =

      threatsJson.indexOf("\"missile\"") != -1 ||

      threatsJson.indexOf("\"ballistic\"") != -1;


  // ================= БПЛА =================

  bool uavThreat =

      threatsJson.indexOf("\"uav\"") != -1 ||

      threatsJson.indexOf("\"drone\"") != -1;


  // ================= ВИЗНАЧЕННЯ СТАТУСУ =================

  if (missileThreat) {

    currentStatus = STATUS_MISSILE;

  }

  else if (uavThreat) {

    currentStatus = STATUS_UAV;

  }

  else {

    currentStatus = STATUS_GREEN;
  }


  // ================= SERIAL MONITOR =================

  if (currentStatus == STATUS_MISSILE) {

    USBSerial.println("🔴 СТАТУС: РАКЕТНА ЗАГРОЗА");

  }

  else if (currentStatus == STATUS_UAV) {

    USBSerial.println("🟠 СТАТУС: ЗАГРОЗА БПЛА");

  }

  else {

    USBSerial.println("🟢 СТАТУС: ВІДБІЙ / НЕМАЄ ТРИВОГИ");
  }


  USBSerial.println("=================================");
}


// ================= SETUP =================

void setup() {

  USBSerial.begin(115200);

  delay(2000);


  // LED

  pinMode(LED_PIN, OUTPUT);

  digitalWrite(LED_PIN, LOW);


  // ================= WI-FI =================

  WiFi.setSleep(false);

  WiFi.mode(WIFI_STA);

  WiFi.begin(ssid, password);


  USBSerial.print("Підключення до Wi-Fi");


  while (WiFi.status() != WL_CONNECTED) {

    delay(250);

    USBSerial.print(".");
  }


  USBSerial.println();

  USBSerial.println("Wi-Fi успішно підключено!");

  USBSerial.print("IP адреса: ");

  USBSerial.println(WiFi.localIP());


  // Початковий стан — відбій

  currentStatus = STATUS_GREEN;

  updateLed();


  // Перша перевірка

  checkAlerts();
}


// ================= LOOP =================

void loop() {

  // Оновлюємо LED постійно

  updateLed();


  // Перевіряємо API кожні 5 секунд

  if (millis() - lastCheck >= checkInterval) {

    lastCheck = millis();

    checkAlerts();
  }
}