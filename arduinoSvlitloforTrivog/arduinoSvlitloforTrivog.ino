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
// RGB LED
// =========================
const int RED_PIN = 3;
const int GREEN_PIN = 4;
const int BLUE_PIN = 2;

// true  = спільний катод
// false = спільний анод
const bool COMMON_CATHODE = false;

// Перевірка кожні 5 секунд
unsigned long lastCheck = 0;
const unsigned long checkInterval = 5000;

// ========================================
// Керування RGB-світлодіодом
// ========================================
void setColor(bool red, bool green, bool blue) {
  digitalWrite(RED_PIN, (red != COMMON_CATHODE) ? HIGH : LOW);
  digitalWrite(GREEN_PIN, (green != COMMON_CATHODE) ? HIGH : LOW);
  digitalWrite(BLUE_PIN, (blue != COMMON_CATHODE) ? HIGH : LOW);
}

void setGreen() {
  setColor(false, true, false);
}

void setYellow() {
  setColor(true, true, false);
}

void setRed() {
  setColor(true, false, false);
}

void setOff() {
  setColor(false, false, false);
}

// ========================================
// Читаємо chunked-відповідь
// ========================================
String readChunkedResponse(WiFiClientSecure &client) {
  String body = "";
  while (client.connected() || client.available()) {
    String sizeLine = client.readStringUntil('\n');
    sizeLine.trim();
    if (sizeLine.length() == 0) {
      if (!client.connected() && !client.available()) {
        break;
      }
      continue;
    }
    
    // Розмір chunk у hex. Ігноруємо додаткові параметри після ;
    int semicolon = sizeLine.indexOf(';');
    if (semicolon >= 0) {
      sizeLine = sizeLine.substring(0, semicolon);
    }
    
    long chunkSize = strtol(sizeLine.c_str(), NULL, 16);
    if (chunkSize <= 0) {
      // Завершальний chunk; читаємо можливі trailer-заголовки
      while (client.connected() || client.available()) {
        String trailer = client.readStringUntil('\n');
        if (trailer == "\r" || trailer.length() == 0) {
          break;
        }
      }
      break;
    }
    
    long remaining = chunkSize;
    while (remaining > 0) {
      if (!client.connected() && !client.available()) {
        break;
      }
      if (!client.available()) {
        delay(1);
        continue;
      }
      int toRead = client.available();
      if (toRead > remaining) {
        toRead = remaining;
      }
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
    
    if (remaining > 0) {
      break;
    }
    
    // CRLF після chunk
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
  
  USBSerial.println("Отримання даних з NEPTUN...");
  if (!client.connect(host, port)) {
    USBSerial.println("Помилка підключення до NEPTUN");
    return "";
  }
  
  client.println("GET /api/v1/alerts HTTP/1.1");
  client.println("Host: neptun.in.ua");
  client.println("User-Agent: ESP32-C3");
  client.println("Accept: application/json");
  client.println("Connection: close");
  client.println();
  
  // Очікуємо початок відповіді
  unsigned long start = millis();
  while (!client.available()) {
    if (!client.connected() || millis() - start > 5000) {
      USBSerial.println("Timeout очікування відповіді");
      client.stop();
      return "";
    }
    delay(10);
  }
  
  // Читаємо HTTP-статус
  String statusLine = client.readStringUntil('\n');
  statusLine.trim();
  USBSerial.print("HTTP: ");
  USBSerial.println(statusLine);
  
  if (statusLine.indexOf("200") < 0) {
    USBSerial.println("Помилка HTTP-відповіді");
    client.stop();
    return "";
  }
  
  // Читаємо HTTP-заголовки
  bool chunked = false;
  int contentLength = -1;
  while (client.connected() || client.available()) {
    String line = client.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) {
      break;
    }
    String lowerLine = line;
    lowerLine.toLowerCase();
    if (lowerLine.startsWith("transfer-encoding:") && lowerLine.indexOf("chunked") >= 0) {
      chunked = true;
    }
    if (lowerLine.startsWith("content-length:")) {
      contentLength = lowerLine.substring(15).toInt();
    }
  }
  
  String body = "";
  if (chunked) {
    body = readChunkedResponse(client);
  } else if (contentLength >= 0) {
    body.reserve(contentLength);
    unsigned long bodyStart = millis();
    while ((int)body.length() < contentLength) {
      if (client.available()) {
        body += (char)client.read();
        bodyStart = millis();
      } else if (!client.connected() || millis() - bodyStart > 5000) {
        break;
      } else {
        delay(1);
      }
    }
  } else {
    unsigned long bodyStart = millis();
    while (client.connected() || client.available()) {
      if (client.available()) {
        body += (char)client.read();
        bodyStart = millis();
      } else if (millis() - bodyStart > 5000) {
        break;
      } else {
        delay(1);
      }
    }
  }
  
  client.stop();
  USBSerial.print("Отримано байтів JSON: ");
  USBSerial.println(body.length());
  return body;
}

// ========================================
// Перевірка районів Київської області
// ========================================
void checkAlert() {
  String json = getAlerts();
  if (json.length() == 0) {
    USBSerial.println("Дані не отримано. Колір не змінено.");
    return;
  }
  
  bool yellowAlert = false;
  bool redAlert = false;
  int searchPos = 0;
  
  while (true) {
    int oblastPos = json.indexOf("\"oblast\":\"Київська область\"", searchPos);
    if (oblastPos < 0) {
      break;
    }
    
    int objectStart = json.lastIndexOf('{', oblastPos);
    int objectEnd = json.indexOf('}', oblastPos);
    if (objectStart < 0 || objectEnd < 0) {
      searchPos = oblastPos + 1;
      continue;
    }
    
    String object = json.substring(objectStart, objectEnd + 1);
    
    int namePos = object.indexOf("\"name\":\"");
    if (namePos >= 0) {
      int nameStart = namePos + 8;
      int nameEnd = object.indexOf('"', nameStart);
      if (nameEnd > nameStart) {
        String name = object.substring(nameStart, nameEnd);
        USBSerial.print("Район: ");
        USBSerial.println(name);
      }
    }
    
    int levelPos = object.indexOf("\"level\":\"");
    if (levelPos >= 0) {
      int levelStart = levelPos + 9;
      int levelEnd = object.indexOf('"', levelStart);
      if (levelEnd > levelStart) {
        String level = object.substring(levelStart, levelEnd);
        USBSerial.print("Рівень: ");
        USBSerial.println(level);
        if (level == "red") {
          redAlert = true;
        } else if (level == "yellow") {
          yellowAlert = true;
        }
      }
    }
    
    searchPos = objectEnd + 1;
  }
  
  if (redAlert) {
    setRed();
    USBSerial.println("ЧЕРВОНА ТРИВОГА");
    USBSerial.println("RGB: ЧЕРВОНИЙ");
  } else if (yellowAlert) {
    setYellow();
    USBSerial.println("ЖОВТА ТРИВОГА");
    USBSerial.println("RGB: ЖОВТИЙ");
  } else {
    setGreen();
    USBSerial.println("ТРИВОГИ НЕМАЄ / ВІДБІЙ");
    USBSerial.println("RGB: ЗЕЛЕНИЙ");
  }
  USBSerial.println("-----------------------------");
}

// ========================================
// SETUP
// ========================================
void setup() {
  USBSerial.begin(115200);
  delay(2000);
  
  pinMode(RED_PIN, OUTPUT);
  pinMode(GREEN_PIN, OUTPUT);
  pinMode(BLUE_PIN, OUTPUT);
  
  setGreen();
  
  WiFi.setSleep(false);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  
  USBSerial.println();
  USBSerial.println("=============================");
  USBSerial.println(" ESP32-C3 NEPTUN RGB ALERT");
  USBSerial.println("=============================");
  USBSerial.print("Підключення до Wi-Fi");
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(250);
    USBSerial.print(".");
  }
  
  USBSerial.println();
  USBSerial.println("Wi-Fi підключено!");
  USBSerial.print("IP: ");
  USBSerial.println(WiFi.localIP());
  USBSerial.println();
  
  checkAlert();
  lastCheck = millis();
}

// ========================================
// LOOP
// ========================================
void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    USBSerial.println("Wi-Fi відключено. Перепідключення...");
    WiFi.disconnect();
    WiFi.begin(ssid, password);
    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 10000) {
      delay(250);
    }
    lastCheck = millis();
    return;
  }
  
  if (millis() - lastCheck >= checkInterval) {
    lastCheck = millis();
    checkAlert();
  }
}