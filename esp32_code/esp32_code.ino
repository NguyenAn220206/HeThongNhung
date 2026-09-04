#include <WiFi.h>
#include <HTTPClient.h>
#include <DHT.h>
#include <ArduinoJson.h>

// =========================
// WIFI
// =========================
const char* ssid = "Aruba";
const char* password = "123456789";

// =========================
// SERVER
// =========================
const char* serverName = "http://172.20.10.6:3000/api/data";

const char* buzzerStatusUrl =
    "http://172.20.10.6:3000/api/buzzer/status";

const char* buzzerResetUrl =
    "http://172.20.10.6:3000/api/buzzer/reset";

// =========================
// GPIO
// =========================
#define DHT_PIN             4
#define RELAY_FAN_TRANN     5
#define RELAY_THONG_GIO     19
#define BUZZER_PIN          18
#define MQ2_PIN             34

#define DHT_TYPE DHT11

DHT dht(DHT_PIN, DHT_TYPE);

// =========================
// TIMER
// =========================
unsigned long lastSend = 0;
const unsigned long sendInterval = 2000;

// =========================
// NGƯỠNG
// =========================
// Người dùng có thể thay đổi các giá trị này
float tempThreshold = 45.0;
int gasThreshold = 700;

// =========================
// WEB CONTROL
// =========================
int webRelayForced = 0;


// ======================================================
// KẾT NỐI WIFI
// ======================================================
void connectWiFi() {

  WiFi.begin(ssid, password);

  Serial.print("Connecting WiFi");

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println();
  Serial.println("WiFi Connected");

  Serial.print("ESP32 IP: ");
  Serial.println(WiFi.localIP());
}


// ======================================================
// SETUP
// ======================================================
void setup() {

  Serial.begin(115200);

  pinMode(MQ2_PIN, INPUT);

  pinMode(RELAY_FAN_TRANN, OUTPUT);
  pinMode(RELAY_THONG_GIO, OUTPUT);

  pinMode(BUZZER_PIN, OUTPUT);

  dht.begin();

  // Relay active LOW
  // HIGH = OFF
  // LOW  = ON
  digitalWrite(RELAY_FAN_TRANN, HIGH);
  digitalWrite(RELAY_THONG_GIO, HIGH);

  // Test buzzer
  digitalWrite(BUZZER_PIN, HIGH);
  delay(500);
  digitalWrite(BUZZER_PIN, LOW);

  connectWiFi();
}


// ======================================================
// LOOP
// ======================================================
void loop() {

  // ----------------------------------------------------
  // KIỂM TRA WIFI
  // ----------------------------------------------------
  if (WiFi.status() != WL_CONNECTED) {

    Serial.println("WiFi Lost!");

    connectWiFi();
  }


  // ----------------------------------------------------
  // ĐỌC CẢM BIẾN
  // ----------------------------------------------------
  if (millis() - lastSend >= sendInterval) {

    lastSend = millis();

    float t = dht.readTemperature();
    float h = dht.readHumidity();

    int g = analogRead(MQ2_PIN);


    // Nếu DHT11 lỗi
    if (isnan(t)) {
      t = 0;
    }

    if (isnan(h)) {
      h = 0;
    }


    // --------------------------------------------------
    // HIỂN THỊ SERIAL
    // --------------------------------------------------

    Serial.println();
    Serial.println("==============================");

    Serial.print("Temperature : ");
    Serial.print(t);
    Serial.println(" C");

    Serial.print("Humidity    : ");
    Serial.print(h);
    Serial.println(" %");

    Serial.print("Gas / Smoke : ");
    Serial.println(g);

    Serial.print("Temp Limit  : ");
    Serial.print(tempThreshold);
    Serial.println(" C");

    Serial.print("Gas Limit   : ");
    Serial.println(gasThreshold);


    // --------------------------------------------------
    // SO SÁNH NGƯỠNG
    // --------------------------------------------------

    bool tempDanger = (t > tempThreshold);
    bool gasDanger = (g > gasThreshold);

    bool danger = tempDanger || gasDanger;


    // ==================================================
    // QUẠT TRẦN
    // ==================================================

    if (tempDanger) {

      digitalWrite(RELAY_FAN_TRANN, LOW);

      Serial.println(
        "[FAN] Nhiet do vuot nguong -> BAT QUAT TRAN"
      );

    }
    else {

      digitalWrite(RELAY_FAN_TRANN, HIGH);

      Serial.println(
        "[FAN] Nhiet do binh thuong -> TAT QUAT TRAN"
      );
    }


    // ==================================================
    // QUẠT THÔNG GIÓ
    // ==================================================

    if (webRelayForced == 1 || danger) {

      digitalWrite(RELAY_THONG_GIO, LOW);

      Serial.println(
        "[VENT] BAT QUAT THONG GIO"
      );

    }
    else {

      digitalWrite(RELAY_THONG_GIO, HIGH);

      Serial.println(
        "[VENT] TAT QUAT THONG GIO"
      );
    }


    // ==================================================
    // BUZZER
    // ==================================================

    String alertReason = "An toan";


    if (gasDanger && tempDanger) {

      digitalWrite(BUZZER_PIN, HIGH);

      alertReason =
        "Nguy hiem: Khi gas/khoi va nhiet do cao!";

      Serial.println(
        "[BUZZER] GAS + NHIET DO CAO"
      );

    }

    else if (gasDanger) {

      digitalWrite(BUZZER_PIN, HIGH);

      alertReason =
        "Canh bao: Khi gas/khoi vuot nguong!";

      Serial.println(
        "[BUZZER] GAS/KHOI VUOT NGUONG"
      );

    }

    else if (tempDanger) {

      digitalWrite(BUZZER_PIN, HIGH);

      alertReason =
        "Canh bao: Nhiet do vuot nguong!";

      Serial.println(
        "[BUZZER] NHIET DO VUOT NGUONG"
      );

    }

    else {

      digitalWrite(BUZZER_PIN, LOW);

      alertReason = "An toan";
    }


    // ==================================================
    // GỬI DỮ LIỆU LÊN SERVER
    // ==================================================

    if (WiFi.status() == WL_CONNECTED) {

      HTTPClient http;

      http.begin(serverName);

      http.addHeader(
        "Content-Type",
        "application/json"
      );


      String json = "{";

      json += "\"temperature\":";
      json += String(t, 1);

      json += ",";

      json += "\"humidity\":";
      json += String(h, 1);

      json += ",";

      json += "\"gas\":";
      json += String(g);

      json += ",";

      json += "\"tempThreshold\":";
      json += String(tempThreshold, 1);

      json += ",";

      json += "\"gasThreshold\":";
      json += String(gasThreshold);

      json += ",";

      json += "\"alertReason\":\"";
      json += alertReason;
      json += "\"";

      json += ",";

      json += "\"fanTran\":";
      json += String(
        digitalRead(RELAY_FAN_TRANN) == LOW ? 1 : 0
      );

      json += ",";

      json += "\"thongGio\":";
      json += String(
        digitalRead(RELAY_THONG_GIO) == LOW ? 1 : 0
      );

      json += "}";


      int httpCode = http.POST(json);

      Serial.print("HTTP Send Data: ");
      Serial.println(httpCode);

      http.end();
    }


    // ==================================================
    // KIỂM TRA LỆNH WEB
    // ==================================================

    checkBuzzerCommand();
  }
}


// ======================================================
// KIỂM TRA LỆNH BUZZER / RELAY TỪ WEB
// ======================================================
void checkBuzzerCommand() {

  if (WiFi.status() != WL_CONNECTED) {
    return;
  }

  HTTPClient http;

  http.begin(buzzerStatusUrl);

  int httpResponseCode = http.GET();


  if (httpResponseCode > 0) {

    String payload = http.getString();

    Serial.print("Server command: ");
    Serial.println(payload);


    DynamicJsonDocument doc(512);

    DeserializationError error =
      deserializeJson(doc, payload);


    if (error) {

      Serial.println(
        "[ERROR] Khong doc duoc JSON tu server"
      );

      http.end();

      return;
    }


    // --------------------------------------------------
    // RELAY THỦ CÔNG
    // --------------------------------------------------

    if (doc.containsKey("relayManualId")) {

      webRelayForced =
        doc["relayManualId"].as<int>();

      Serial.print(
        "[WEB] relayManualId = "
      );

      Serial.println(webRelayForced);
    }


    // --------------------------------------------------
    // BUZZER TỪ WEB
    // --------------------------------------------------

    int buzzerAlert = 0;

    if (doc.containsKey("buzzerAlert")) {

      buzzerAlert =
        doc["buzzerAlert"].as<int>();
    }


    if (buzzerAlert == 1) {

      Serial.println(
        "[WEB] Kich hoat buzzer tu xa!"
      );


      for (int chuKy = 0; chuKy < 5; chuKy++) {

        Serial.print("-> Chu ky coi: ");
        Serial.println(chuKy + 1);


        for (int bip = 0; bip < 4; bip++) {

          digitalWrite(BUZZER_PIN, HIGH);
          delay(150);

          digitalWrite(BUZZER_PIN, LOW);
          delay(100);
        }

        delay(800);
      }


      resetBuzzerOnServer();
    }
  }


  http.end();
}


// ======================================================
// RESET BUZZER TRÊN SERVER
// ======================================================
void resetBuzzerOnServer() {

  if (WiFi.status() != WL_CONNECTED) {
    return;
  }

  HTTPClient http;

  http.begin(buzzerResetUrl);

  http.addHeader(
    "Content-Type",
    "application/json"
  );

  int httpResponseCode =
    http.POST("{}");

  Serial.print(
    "Buzzer reset response: "
  );

  Serial.println(httpResponseCode);

  http.end();
}