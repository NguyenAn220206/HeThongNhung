#include <WiFi.h>
#include <HTTPClient.h>
#include <DHT.h>
#include <ArduinoJson.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "LocalControls.h"

// ======================================================
// WIFI / BACKEND
// ======================================================
const char* ssid = "ai phôn nèee nhaaaa";
const char* password = "nguyenan2202";

const char* serverName = "http://172.20.10.2:3000/api/data";
const char* configUrl = "http://172.20.10.2:3000/api/config";
const char* buzzerStatusUrl = "http://172.20.10.2:3000/api/buzzer/status";
const char* buzzerResetUrl = "http://172.20.10.2:3000/api/buzzer/reset";

// ======================================================
// GPIO - KHỚP VỚI SƠ ĐỒ PROTEUS
// ======================================================
#define DHT_PIN             4
#define MQ2_PIN             34       // ADC input-only
#define BUZZER_PIN          18
#define RELAY_THONG_GIO    19       // Relay RL2, active HIGH 
#define BUTTON_RELAY_PIN   23       // Nút bên phải: bật/tắt relay thông gió
#define BUTTON_FAN_MAX_PIN 22       // Nút ở giữa: ép quạt chạy tốc độ MAX
#define BUTTON_BUZZER_PIN  21       // Nút bên trái: bật/tắt còi cảnh báo

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

// ======================================================
// NGƯỠNG MẶC ĐỊNH
// Có thể thay đổi sau khi bổ sung API nhận cấu hình từ frontend.
// ======================================================
float comfortTemperature = 30.0;     // Nhiệt độ người dùng cảm thấy dễ chịu
const float comfortBand = 3.0;       // Tự tạo khoảng T-3 đến T+3
int gasThreshold = 100;              // Giá trị ADC MQ-2, cần hiệu chỉnh thực tế
const float humidityThreshold = 80.0; // Ngưỡng độ ẩm cao (%)

// ======================================================
// TIMER / TRẠNG THÁI
// ======================================================
const unsigned long sensorInterval = 2000;
const unsigned long commandInterval = 3000;
const unsigned long configInterval = 5000;
unsigned long lastSensorSend = 0;
unsigned long lastCommandCheck = 0;
unsigned long lastConfigFetch = 0;

// HTTP chạy trong loop; task riêng xử lý nút và đầu ra, không chờ mạng.
portMUX_TYPE controlMux = portMUX_INITIALIZER_UNLOCKED;
ControlInputs controlInputs;
ControlOutputs controlOutputs;
TaskHandle_t controlTaskHandle = nullptr;
uint32_t sensorVersion = 0;
uint32_t appliedSensorVersion = 0;
enum RemoteBuzzerCommand { REMOTE_IDLE, REMOTE_REQUESTED, REMOTE_ACTIVE, REMOTE_FINISHED };
RemoteBuzzerCommand remoteBuzzerCommand = REMOTE_IDLE;

ControlOutputs readControlOutputs(uint32_t sampleVersion) {
  const unsigned long startedAt = millis();
  for (;;) {
    portENTER_CRITICAL(&controlMux);
    const ControlOutputs snapshot = controlOutputs;
    const bool applied = appliedSensorVersion == sampleVersion;
    portEXIT_CRITICAL(&controlMux);
    if (applied) return snapshot;
    if (millis() - startedAt >= 50) {
      Serial.println("[WARN] Control task chua ap dung mau cam bien moi");
      return snapshot;
    }
    vTaskDelay(1);
  }
}

// Relay active high
const int RELAY_ON = HIGH;
const int RELAY_OFF = LOW;

// ======================================================
// WIFI
// ======================================================
const unsigned long wifiRetryInterval = 5000;
const unsigned long wifiAttemptTimeout = 20000;
unsigned long lastWifiAttempt = 0;
unsigned long wifiAttemptStartedAt = 0;
bool wifiAttemptInProgress = false;

void connectWiFi() {
  const unsigned long now = millis();

  if (WiFi.status() == WL_CONNECTED) {
    if (wifiAttemptInProgress) {
      Serial.println("WiFi Connected");
      Serial.print("ESP32 IP: ");
      Serial.println(WiFi.localIP());
    }
    wifiAttemptInProgress = false;
    return;
  }

  // Không chờ đồng bộ ở đây: các nút vật lý vẫn được xử lý trong lúc Wi-Fi kết nối.
  if (wifiAttemptInProgress) {
    if (now - wifiAttemptStartedAt >= wifiAttemptTimeout) {
      Serial.println("WiFi connection timeout; retrying later");
      WiFi.disconnect();
      wifiAttemptInProgress = false;
    }
    return;
  }

  if (lastWifiAttempt != 0 && now - lastWifiAttempt < wifiRetryInterval) {
    return;
  }

  lastWifiAttempt = now;
  wifiAttemptStartedAt = now;
  wifiAttemptInProgress = true;
  WiFi.begin(ssid, password);
  Serial.println("Connecting WiFi in background...");
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

void localControlTask(void*) {
  LocalControls controls;
  ControlOutputs previous;
  for (;;) {
    portENTER_CRITICAL(&controlMux);
    ControlInputs input = controlInputs;
    const uint32_t sampleVersion = sensorVersion;
    const bool startRemote = remoteBuzzerCommand == REMOTE_REQUESTED;
    if (startRemote) remoteBuzzerCommand = REMOTE_ACTIVE;
    portEXIT_CRITICAL(&controlMux);

    const ControlOutputs output = controls.update(
      millis(), digitalRead(BUTTON_RELAY_PIN) == LOW,
      digitalRead(BUTTON_FAN_MAX_PIN) == LOW,
      digitalRead(BUTTON_BUZZER_PIN) == LOW, input, startRemote);

    if (output.fanSpeed != previous.fanSpeed) setFanSpeed(output.fanSpeed);
    if (output.ventilationOn != previous.ventilationOn) {
      digitalWrite(RELAY_THONG_GIO, output.ventilationOn ? RELAY_ON : RELAY_OFF);
    }
    if (output.buzzerOn != previous.buzzerOn) setBuzzer(output.buzzerOn);

    portENTER_CRITICAL(&controlMux);
    controlOutputs = output;
    appliedSensorVersion = sampleVersion;
    if (remoteBuzzerCommand == REMOTE_ACTIVE && !output.remoteBuzzerActive) {
      remoteBuzzerCommand = REMOTE_FINISHED;
    }
    portEXIT_CRITICAL(&controlMux);
    previous = output;
    // Chỉ task này ghi đầu ra sau setup; không gọi HTTP/Serial trong task.
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(5) > 0 ? pdMS_TO_TICKS(5) : 1);
  }
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
  bool humidityDanger,
  const ControlOutputs& output,
  const String& alertReason
) {
  if (WiFi.status() != WL_CONNECTED) return;

  HTTPClient http;
  http.begin(serverName);
  http.setConnectTimeout(1000);
  http.setTimeout(1500);
  http.addHeader("Content-Type", "application/json");

  StaticJsonDocument<768> doc;
  doc["temperature"] = temperature;
  doc["humidity"] = humidity;
  doc["gas"] = gas;
  doc["gasThreshold"] = gasThreshold;
  doc["humidityThreshold"] = humidityThreshold;
  doc["comfortTemperature"] = comfortTemperature;
  doc["tempLow"] = comfortTemperature - comfortBand;
  doc["tempHigh"] = comfortTemperature + comfortBand;
  doc["temperatureDanger"] = tempDanger;
  doc["gasDanger"] = gasDanger;
  doc["humidityDanger"] = humidityDanger;
  doc["alertReason"] = alertReason;

  // Các trường này giúp frontend cũ vẫn hiển thị được trạng thái cơ bản.
  doc["fanSpeed"] = output.fanSpeed;
  doc["fanLevel"] = fanLevelName(output.fanSpeed);
  doc["fanTran"] = output.fanSpeed > 0 ? 1 : 0;
  doc["thongGio"] = output.ventilationOn ? 1 : 0;
  doc["localFanMaxForced"] = output.localFanMaxForced;
  doc["localRelayForced"] = output.localRelayForced;
  doc["localBuzzerEnabled"] = output.localBuzzerEnabled;
  doc["buzzerEnabled"] = output.buzzerEnabled;
  doc["remoteBuzzerActive"] = output.remoteBuzzerActive;

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
bool resetBuzzerOnServer() {
  if (WiFi.status() != WL_CONNECTED) return false;

  HTTPClient http;
  http.begin(buzzerResetUrl);
  http.setConnectTimeout(1000);
  http.setTimeout(1500);
  http.addHeader("Content-Type", "application/json");
  const int responseCode = http.POST("{}");
  http.end();
  return responseCode >= 200 && responseCode < 300;
}

void fetchSystemConfig() {
  if (WiFi.status() != WL_CONNECTED) return;

  HTTPClient http;
  http.begin(configUrl);
  http.setConnectTimeout(1000);
  http.setTimeout(1500);
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

void checkWebCommands() {
  if (WiFi.status() != WL_CONNECTED) return;

  HTTPClient http;
  http.begin(buzzerStatusUrl);
  http.setConnectTimeout(1000);
  http.setTimeout(1500);
  int responseCode = http.GET();

  if (responseCode > 0) {
    String payload = http.getString();
    StaticJsonDocument<512> doc;
    DeserializationError error = deserializeJson(doc, payload);

    if (!error) {
      if (doc.containsKey("relayManualState")) {
        const bool forced = doc["relayManualState"].as<int>() == 1;
        portENTER_CRITICAL(&controlMux);
        controlInputs.webRelayForced = forced;
        portEXIT_CRITICAL(&controlMux);
        xTaskNotifyGive(controlTaskHandle);
      }

      if (doc["buzzerAlert"].as<int>() == 1) {
        portENTER_CRITICAL(&controlMux);
        if (remoteBuzzerCommand == REMOTE_IDLE) remoteBuzzerCommand = REMOTE_REQUESTED;
        portEXIT_CRITICAL(&controlMux);
        xTaskNotifyGive(controlTaskHandle);
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

  WiFi.mode(WIFI_STA);

  pinMode(MQ2_PIN, INPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(RELAY_THONG_GIO, OUTPUT);
  pinMode(BUTTON_RELAY_PIN, INPUT_PULLUP);
  pinMode(BUTTON_FAN_MAX_PIN, INPUT_PULLUP);
  pinMode(BUTTON_BUZZER_PIN, INPUT_PULLUP);
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

  if (xTaskCreate(localControlTask, "local-controls", 4096, nullptr, 2, &controlTaskHandle) != pdPASS) {
    Serial.println("[ERROR] Khong tao duoc task dieu khien nut; dung khoi dong");
    for (;;) delay(1000);
  }

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
    portENTER_CRITICAL(&controlMux);
    const bool remoteFinished = remoteBuzzerCommand == REMOTE_FINISHED;
    portEXIT_CRITICAL(&controlMux);
    if (remoteFinished && resetBuzzerOnServer()) {
      portENTER_CRITICAL(&controlMux);
      remoteBuzzerCommand = REMOTE_IDLE;
      portEXIT_CRITICAL(&controlMux);
    }
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
  bool humidityDanger = dhtValid && humidity > humidityThreshold;
  portENTER_CRITICAL(&controlMux);
  controlInputs.sensorReady = true;
  controlInputs.tooCold = temperature < lowTemperature;
  controlInputs.temperatureDanger = tempDanger;
  controlInputs.gasDanger = gasDanger;
  controlInputs.humidityDanger = humidityDanger;
  const uint32_t sampleVersion = ++sensorVersion;
  portEXIT_CRITICAL(&controlMux);
  xTaskNotifyGive(controlTaskHandle);
  const ControlOutputs output = readControlOutputs(sampleVersion);

  String alertReason = "An toan";
  if (tempDanger && gasDanger) {
    alertReason = "Nguy hiem: Nhiet do cao va khi gas/khoi vuot nguong";
  } else if (humidityDanger) {
    alertReason = "Canh bao: Do am vuot nguong 80%";
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
                output.fanSpeed,
                fanLevelName(output.fanSpeed).c_str(),
                output.ventilationOn ? "ON" : "OFF",
                output.buzzerEnabled ? "ON" : "OFF");
  Serial.printf("[SOURCE] Local fan=%d relay=%d buzzer=%d | Web relay=%d buzzer=%d | Sensor temp=%d gas=%d humidity=%d\n",
                output.localFanMaxForced, output.localRelayForced, output.localBuzzerEnabled,
                output.webRelayForced, output.remoteBuzzerActive,
                output.temperatureDanger, output.gasDanger, output.humidityDanger);
  Serial.printf("[BUTTON COUNT] Relay=%lu Fan=%lu Buzzer=%lu\n",
                (unsigned long)output.relayPresses, (unsigned long)output.fanPresses,
                (unsigned long)output.buzzerPresses);

  sendSensorData(
    temperature,
    humidity,
    gas,
    tempDanger,
    gasDanger,
    humidityDanger,
    output,
    alertReason
  );
}
