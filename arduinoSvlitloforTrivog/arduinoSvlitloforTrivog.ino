#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

HWCDC USBSerial;

// =========================
// Wi-Fi
// =========================
const char* ssid = "Home Protsak";
const char* password = "Wednesday";

// =========================
// NEPTUN
// =========================
const char* host = "neptun.in.ua";
const int port = 443;

// =========================
// LED
// =========================
const int LED_PIN = 8;

// Перевірка кожні 5 секунд
unsigned long lastCheck = 0;
const unsigned long checkInterval = 5000;


// ========================================
// Читаємо chunked-відповідь
// ========================================
String readChunkedResponse(WiFiClientSecure &client) {

  String body = "";

  while (true) {

    // Читаємо розмір наступного chunk
    String sizeLine = client.readStringUntil('\n');
    sizeLine.trim();

    if (sizeLine.length() == 0) {
      continue;
    }

    // Розмір у hex
    int chunkSize = strtol(sizeLine.c_str(), NULL, 16);

    // Кінець відповіді
    if (chunkSize == 0) {
      break;
    }

    // Читаємо chunk
    int remaining = chunkSize;

    while (remaining > 0) {

      while (!client.available()) {
        delay(1);
      }

      int availableBytes = client.available();

      int toRead = min(availableBytes, remaining);

      char buffer[256];

      if (toRead > 255) {
        toRead = 255;
      }

      int readBytes = client.readBytes(buffer, toRead);

      if (readBytes > 0) {
        body.concat(buffer, readBytes);
        remaining -= readBytes;
      }
    }

    // Після кожного chunk є CRLF
    client.readStringUntil('\n');
  }

  return body;
}


// ========================================
// Отримання JSON з NEPTUN
// ========================================
String getAlerts() {

  WiFiClientSecure client;

  client.setInsecure();
  client.setTimeout(5000);

  USBSerial.println("Отримання даних...");

  if (!client.connect(host, port)) {

    USBSerial.println("❌ Помилка підключення до NEPTUN");

    return "";
  }

  client.println("GET /api/v1/alerts HTTP/1.1");
  client.println("Host: neptun.in.ua");
  client.println("User-Agent: ESP32-C3");
  client.println("Accept: application/json");
  client.println("Connection: close");
  client.println();


  // Чекаємо відповідь
  unsigned long start = millis();

  while (!client.available()) {

    if (millis() - start > 5000) {

      USBSerial.println("❌ Timeout");

      client.stop();

      return "";
    }

    delay(10);
  }


  // ========================================
  // Читаємо HTTP-заголовки
  // ========================================

  bool chunked = false;

  while (true) {

    String line = client.readStringUntil('\n');

    line.trim();

    if (line.length() == 0) {
      break;
    }

    if (line.indexOf("Transfer-Encoding: chunked") >= 0 ||
        line.indexOf("transfer-encoding: chunked") >= 0) {

      chunked = true;
    }
  }


  String body = "";


  // ========================================
  // Якщо chunked
  // ========================================

  if (chunked) {

    body = readChunkedResponse(client);

  }

  // ========================================
  // Якщо звичайна відповідь
  // ========================================

  else {

    while (client.available()) {
      body += client.readString();
    }
  }


  client.stop();

  return body;
}


// ========================================
// Перевірка Києва
// ========================================
bool checkKyivAlert(String json) {

  const char* targetKey = "\"key\":\"м. київ\"";

  int keyPos = json.indexOf(targetKey);


  // Київ не знайдено
  if (keyPos < 0) {

    USBSerial.println("⚠️ Київ у JSON не знайдено!");

    return false;
  }


  USBSerial.println("✅ Київ знайдено в JSON");


  // Знаходимо об'єкт Києва
  int objectStart = json.lastIndexOf('{', keyPos);
  int objectEnd = json.indexOf('}', keyPos);


  if (objectStart < 0 || objectEnd < 0) {

    USBSerial.println("❌ Помилка читання об'єкта Києва");

    return false;
  }


  String kyivObject = json.substring(
    objectStart,
    objectEnd + 1
  );


  // Шукаємо level
  int levelPos = kyivObject.indexOf("\"level\":\"");


  if (levelPos < 0) {

    USBSerial.println("⚠️ Level Києва не знайдено");

    return false;
  }


  int valueStart = levelPos + 9;

  int valueEnd = kyivObject.indexOf(
    '"',
    valueStart
  );


  if (valueEnd < 0) {

    USBSerial.println("❌ Помилка читання level");

    return false;
  }


  String level = kyivObject.substring(
    valueStart,
    valueEnd
  );


  USBSerial.print("Рівень Києва: ");
  USBSerial.println(level);


  // yellow або red = тривога
  if (level == "yellow" || level == "red") {

    return true;
  }


  return false;
}


// ========================================
// Перевірка тривоги
// ========================================
void checkAlert() {

  String json = getAlerts();

  if (json.length() == 0) {
    return;
  }

  bool alert = false;

  // ========================================
  // Перевіряємо райони Київської області
  // ========================================

  int searchPos = 0;

  while (true) {

    // Шукаємо "oblast":"Київська область"
    int oblastPos = json.indexOf(
      "\"oblast\":\"Київська область\"",
      searchPos
    );

    if (oblastPos < 0) {
      break;
    }

    // Знаходимо початок об'єкта району
    int objectStart = json.lastIndexOf('{', oblastPos);

    // Знаходимо кінець об'єкта
    int objectEnd = json.indexOf('}', oblastPos);

    if (objectStart >= 0 && objectEnd > oblastPos) {

      String object = json.substring(
        objectStart,
        objectEnd + 1
      );

      // Назва району
      int namePos = object.indexOf("\"name\":\"");

      if (namePos >= 0) {

        int nameStart = namePos + 8;
        int nameEnd = object.indexOf('"', nameStart);

        if (nameEnd > nameStart) {

          String name = object.substring(
            nameStart,
            nameEnd
          );

          USBSerial.print("Район Київської області: ");
          USBSerial.println(name);
        }
      }

      // Рівень
      int levelPos = object.indexOf("\"level\":\"");

      if (levelPos >= 0) {

        int levelStart = levelPos + 9;
        int levelEnd = object.indexOf('"', levelStart);

        if (levelEnd > levelStart) {

          String level = object.substring(
            levelStart,
            levelEnd
          );

          USBSerial.print("Рівень: ");
          USBSerial.println(level);

          // yellow або red = активна тривога
          if (level == "yellow" || level == "red") {

            alert = true;
          }
        }
      }
    }

    searchPos = objectEnd + 1;
  }


  // ========================================
  // LED
  // ========================================

  if (alert) {

    // LOW = LED світиться
    digitalWrite(LED_PIN, LOW);

    USBSerial.println("🔴 КИЇВСЬКА ОБЛАСТЬ — ТРИВОГА Є");
    USBSerial.println("LED: ON");

  } else {

    // HIGH = LED вимкнений
    digitalWrite(LED_PIN, HIGH);

    USBSerial.println("🟢 КИЇВСЬКА ОБЛАСТЬ — ТРИВОГИ НЕМАЄ");
    USBSerial.println("LED: OFF");
  }

  USBSerial.println("-----------------------------");
}
// ========================================
// SETUP
// ========================================
void setup() {

  USBSerial.begin(115200);

  delay(2000);


  // LED
  pinMode(LED_PIN, OUTPUT);

  // Вимкнути LED
  digitalWrite(LED_PIN, HIGH);


  // Wi-Fi
  WiFi.setSleep(false);

  WiFi.mode(WIFI_STA);

  WiFi.begin(ssid, password);


  USBSerial.println();
  USBSerial.println("=============================");
  USBSerial.println(" ESP32-C3 NEPTUN ALERT");
  USBSerial.println("=============================");


  USBSerial.print("Підключення до Wi-Fi");


  while (WiFi.status() != WL_CONNECTED) {

    delay(250);

    USBSerial.print(".");
  }


  USBSerial.println();

  USBSerial.println("✅ Wi-Fi підключено!");

  USBSerial.print("IP: ");
  USBSerial.println(WiFi.localIP());

  USBSerial.println();


  // Перша перевірка
  checkAlert();

  lastCheck = millis();
}


// ========================================
// LOOP 
// ========================================
void loop() {

  if (millis() - lastCheck >= checkInterval) {

    lastCheck = millis();

    checkAlert();
  }
}