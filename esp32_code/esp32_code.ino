#include <WiFi.h>
#include <HTTPClient.h>
#include <DHT.h>
#include <ArduinoJson.h>

// ======================================================
// WIFI / BACKEND
// ======================================================
const char* ssid = "F87 Phong Lanh 2.4hz";
const char* password = "68686868";

const char* serverName = "http://192.168.1.20:3000/api/data";
const char* configUrl = "http://192.168.1.20:3000/api/config";
const char* buzzerStatusUrl = "http://192.168.1.20:3000/api/buzzer/status";
const char* buzzerResetUrl = "http://192.168.1.20:3000/api/buzzer/reset";

// ======================================================
// GPIO - KHỚP VỚI SƠ ĐỒ PROTEUS
// ======================================================
#define DHT_PIN             4
#define MQ2_PIN             34       // ADC input-only
#define BUZZER_PIN          18
#define RELAY_THONG_GIO    19       // Relay RL2, active LOW

// L298: ENA dùng PWM, IN1/IN2 chọn chiều quay
#define FAN_PWM_PIN         25       // ENA
#define FAN_IN1_PIN         26       // IN1
#define FAN_IN2_PIN         27       // IN2

#define DHT_TYPE DHT11
DHT dht(DHT_PIN, DHT_TYPE);

// ======================================================
// PWM L298
// ======================================================
const int FAN_PWM_CHANNEL = 0;
const int FAN_PWM_FREQUENCY = 5000;
const int FAN_PWM_RESOLUTION = 8;    // 0..255

const uint8_t FAN_SPEED_OFF = 0;
const uint8_t FAN_SPEED_SLOW = 90;
const uint8_t FAN_SPEED_NORMAL = 170;
const uint8_t FAN_SPEED_FAST = 255;

// ======================================================
// NGƯỠNG MẶC ĐỊNH
// Có thể thay đổi sau khi bổ sung API nhận cấu hình từ frontend.
// ======================================================
float comfortTemperature = 28.0;     // Nhiệt độ người dùng cảm thấy dễ chịu
const float comfortBand = 2.0;       // Tự tạo khoảng T-2 đến T+2
int gasThreshold = 700;              // Giá trị ADC MQ-2, cần hiệu chỉnh thực tế

// ======================================================
// TIMER / TRẠNG THÁI
// ======================================================
const unsigned long sensorInterval = 2000;
const unsigned long commandInterval = 3000;
const unsigned long configInterval = 5000;
unsigned long lastSensorSend = 0;
unsigned long lastCommandCheck = 0;
unsigned long lastConfigFetch = 0;

int webRelayForced = 0;
int currentFanSpeed = FAN_SPEED_OFF;

// Relay active LOW
const int RELAY_ON = LOW;
const int RELAY_OFF = HIGH;

// ======================================================
// WIFI
// ======================================================
void connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;

  WiFi.begin(ssid, password);
  Serial.print("Connecting WiFi");

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 20000) {
    delay(500);
    Serial.print(".");
  }

  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("WiFi Connected");
    Serial.print("ESP32 IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("WiFi connection timeout");
  }
}

// ======================================================
// ĐIỀU KHIỂN QUẠT QUA L298
// ======================================================
void setFanSpeed(uint8_t speed) {
  // Quạt chỉ quay một chiều.
  digitalWrite(FAN_IN1_PIN, HIGH);
  digitalWrite(FAN_IN2_PIN, LOW);

  #if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
    ledcWrite(FAN_PWM_PIN, speed);
  #else
    ledcWrite(FAN_PWM_CHANNEL, speed);
  #endif
  currentFanSpeed = speed;
}

String fanLevelName(uint8_t speed) {
  if (speed == FAN_SPEED_OFF) return "TAT";
  if (speed <= FAN_SPEED_SLOW) return "CHAM";
  if (speed <= FAN_SPEED_NORMAL) return "BINH_THUONG";
  return "NHANH";
}

// ======================================================
// BUZZER
// ======================================================
void setBuzzer(bool enabled) {
  digitalWrite(BUZZER_PIN, enabled ? HIGH : LOW);
}

// ======================================================
// GỬI DỮ LIỆU LÊN BACKEND
// ======================================================
void sendSensorData(
  float temperature,
  float humidity,
  int gas,
  bool tempDanger,
  bool gasDanger,
  bool ventilationOn,
  const String& alertReason
) {
  if (WiFi.status() != WL_CONNECTED) return;

  HTTPClient http;
  http.begin(serverName);
  http.addHeader("Content-Type", "application/json");

  StaticJsonDocument<768> doc;
  doc["temperature"] = temperature;
  doc["humidity"] = humidity;
  doc["gas"] = gas;
  doc["gasThreshold"] = gasThreshold;
  doc["comfortTemperature"] = comfortTemperature;
  doc["tempLow"] = comfortTemperature - comfortBand;
  doc["tempHigh"] = comfortTemperature + comfortBand;
  doc["temperatureDanger"] = tempDanger;
  doc["gasDanger"] = gasDanger;
  doc["alertReason"] = alertReason;

  // Các trường này giúp frontend cũ vẫn hiển thị được trạng thái cơ bản.
  doc["fanSpeed"] = currentFanSpeed;
  doc["fanLevel"] = fanLevelName(currentFanSpeed);
  doc["fanTran"] = currentFanSpeed > 0 ? 1 : 0;
  doc["thongGio"] = ventilationOn ? 1 : 0;

  String payload;
  serializeJson(doc, payload);

  int httpCode = http.POST(payload);
  Serial.print("HTTP Send Data: ");
  Serial.println(httpCode);
  http.end();
}

// ======================================================
// ĐỌC LỆNH TỪ WEB: buzzer và relay thông gió thủ công
// ======================================================
void resetBuzzerOnServer() {
  if (WiFi.status() != WL_CONNECTED) return;

  HTTPClient http;
  http.begin(buzzerResetUrl);
  http.addHeader("Content-Type", "application/json");
  http.POST("{}");
  http.end();
}

void fetchSystemConfig() {
  if (WiFi.status() != WL_CONNECTED) return;

  HTTPClient http;
  http.begin(configUrl);
  int responseCode = http.GET();

  if (responseCode > 0) {
    String payload = http.getString();
    StaticJsonDocument<256> doc;
    DeserializationError error = deserializeJson(doc, payload);

    if (!error) {
      if (doc.containsKey("comfortTemperature")) {
        comfortTemperature = doc["comfortTemperature"].as<float>();
      }
      if (doc.containsKey("gasThreshold")) {
        gasThreshold = doc["gasThreshold"].as<int>();
      }
      Serial.printf("[CONFIG] Comfort: %.1f C | Gas: %d\n", comfortTemperature, gasThreshold);
    } else {
      Serial.println("[ERROR] Khong doc duoc cau hinh tu server");
    }
  }

  http.end();
}

void runRemoteBuzzerPattern() {
  for (int cycle = 0; cycle < 5; cycle++) {
    for (int beep = 0; beep < 4; beep++) {
      setBuzzer(true);
      delay(150);
      setBuzzer(false);
      delay(100);
    }
    delay(800);
  }
}

void checkWebCommands() {
  if (WiFi.status() != WL_CONNECTED) return;

  HTTPClient http;
  http.begin(buzzerStatusUrl);
  int responseCode = http.GET();

  if (responseCode > 0) {
    String payload = http.getString();
    StaticJsonDocument<512> doc;
    DeserializationError error = deserializeJson(doc, payload);

    if (!error) {
      if (doc.containsKey("relayManualId")) {
        webRelayForced = doc["relayManualId"].as<int>();
      }

      if (doc["buzzerAlert"].as<int>() == 1) {
        Serial.println("[WEB] Kich hoat buzzer tu xa");
        runRemoteBuzzerPattern();
        resetBuzzerOnServer();
      }
    } else {
      Serial.println("[ERROR] Khong doc duoc JSON tu server");
    }
  }

  http.end();
}

// ======================================================
// SETUP
// ======================================================
void setup() {
  Serial.begin(115200);

  pinMode(MQ2_PIN, INPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(RELAY_THONG_GIO, OUTPUT);
  pinMode(FAN_IN1_PIN, OUTPUT);
  pinMode(FAN_IN2_PIN, OUTPUT);

  // Khởi tạo PWM cho chân ENA của L298.
  #if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
    ledcAttachChannel(FAN_PWM_PIN, FAN_PWM_FREQUENCY, FAN_PWM_RESOLUTION, FAN_PWM_CHANNEL);
  #else
    ledcSetup(FAN_PWM_CHANNEL, FAN_PWM_FREQUENCY, FAN_PWM_RESOLUTION);
    ledcAttachPin(FAN_PWM_PIN, FAN_PWM_CHANNEL);
  #endif

  dht.begin();
  setFanSpeed(FAN_SPEED_OFF);
  digitalWrite(RELAY_THONG_GIO, RELAY_OFF);
  setBuzzer(false);

  // Test buzzer lúc khởi động.
  setBuzzer(true);
  delay(300);
  setBuzzer(false);

  connectWiFi();
}

// ======================================================
// LOOP
// ======================================================
void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    connectWiFi();
  }

  if (millis() - lastCommandCheck >= commandInterval) {
    lastCommandCheck = millis();
    checkWebCommands();
  }

  if (millis() - lastConfigFetch >= configInterval) {
    lastConfigFetch = millis();
    fetchSystemConfig();
  }

  if (millis() - lastSensorSend < sensorInterval) return;
  lastSensorSend = millis();

  float temperature = dht.readTemperature();
  float humidity = dht.readHumidity();
  int gas = analogRead(MQ2_PIN);

  bool dhtValid = !isnan(temperature) && !isnan(humidity);
  if (!dhtValid) {
    Serial.println("[ERROR] Khong doc duoc DHT11");
    temperature = comfortTemperature;
    humidity = 0;
  }

  float lowTemperature = comfortTemperature - comfortBand;
  float highTemperature = comfortTemperature + comfortBand;
  bool tempDanger = dhtValid && temperature > highTemperature;
  bool gasDanger = gas > gasThreshold;
  bool danger = tempDanger || gasDanger;

  // Ưu tiên an toàn: gas hoặc nhiệt độ cao thì quạt nhanh.
  if (gasDanger || tempDanger) {
    setFanSpeed(FAN_SPEED_FAST);
  } else if (temperature < lowTemperature) {
    // Trời lạnh: tắt quạt. Có thể đổi thành FAN_SPEED_SLOW nếu cần.
    setFanSpeed(FAN_SPEED_OFF);
  } else {
    setFanSpeed(FAN_SPEED_NORMAL);
  }

  // Quạt thông gió chỉ bật khi có nguy hiểm hoặc bị ép bật từ web.
  bool ventilationOn = (webRelayForced == 1) || danger;
  digitalWrite(RELAY_THONG_GIO, ventilationOn ? RELAY_ON : RELAY_OFF);

  // Còi bật khi một trong các chỉ số vượt ngưỡng.
  setBuzzer(danger);

  String alertReason = "An toan";
  if (tempDanger && gasDanger) {
    alertReason = "Nguy hiem: Nhiet do cao va khi gas/khoi vuot nguong";
  } else if (tempDanger) {
    alertReason = "Canh bao: Nhiet do vuot nguong";
  } else if (gasDanger) {
    alertReason = "Canh bao: Khi gas/khoi vuot nguong";
  }

  Serial.println("==============================");
  Serial.printf("Temperature: %.1f C | Humidity: %.1f %%\n", temperature, humidity);
  Serial.printf("Gas: %d | Gas limit: %d\n", gas, gasThreshold);
  Serial.printf("Comfort: %.1f C | Range: %.1f..%.1f C\n", comfortTemperature, lowTemperature, highTemperature);
  Serial.printf("Fan: %d/255 (%s) | Vent: %s | Buzzer: %s\n",
                currentFanSpeed,
                fanLevelName(currentFanSpeed).c_str(),
                ventilationOn ? "ON" : "OFF",
                danger ? "ON" : "OFF");

  sendSensorData(
    temperature,
    humidity,
    gas,
    tempDanger,
    gasDanger,
    ventilationOn,
    alertReason
  );
}
