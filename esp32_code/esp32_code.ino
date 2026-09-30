#include <WiFi.h>
#include <HTTPClient.h>
#include <DHT.h>
#include <ArduinoJson.h>

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
#define RELAY_THONG_GIO    19       // Relay RL2, active HIGH theo firmware hiện tại
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
int gasThreshold = 70;              // Giá trị ADC MQ-2, cần hiệu chỉnh thực tế
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

int webRelayForced = 0;  // 0: tự động theo gas, 1: ép bật relay từ Web
bool localRelayForced = false; // Trạng thái relay do nút vật lý điều khiển
bool localFanMaxForced = false; // Trạng thái ép quạt MAX do nút vật lý điều khiển
bool localBuzzerEnabled = false; // Trạng thái còi do nút vật lý điều khiển
bool sensorDangerActive = false; // Cảnh báo cảm biến được giữ giữa các vòng lặp
int currentFanSpeed = FAN_SPEED_OFF;

// Cờ sự kiện nút được ghi nhận bằng interrupt để không bị mất khi mạng chậm.
volatile bool relayButtonPressed = false;
volatile bool fanButtonPressed = false;
volatile bool buzzerButtonPressed = false;
volatile unsigned long lastRelayInterruptUs = 0;
volatile unsigned long lastFanInterruptUs = 0;
volatile unsigned long lastBuzzerInterruptUs = 0;
const unsigned long buttonDebounceUs = 200000; // 200 ms

// Chu kỳ còi ngắt quãng: kêu 250 ms, nghỉ 750 ms.
const unsigned long buzzerOnDuration = 250;
const unsigned long buzzerOffDuration = 750;
unsigned long lastBuzzerToggle = 0;
bool buzzerOutputState = false;

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

void IRAM_ATTR onRelayButtonPressed() {
  const unsigned long nowUs = micros();
  if (nowUs - lastRelayInterruptUs >= buttonDebounceUs) {
    relayButtonPressed = true;
    lastRelayInterruptUs = nowUs;
  }
}

void IRAM_ATTR onFanButtonPressed() {
  const unsigned long nowUs = micros();
  if (nowUs - lastFanInterruptUs >= buttonDebounceUs) {
    fanButtonPressed = true;
    lastFanInterruptUs = nowUs;
  }
}

void IRAM_ATTR onBuzzerButtonPressed() {
  const unsigned long nowUs = micros();
  if (nowUs - lastBuzzerInterruptUs >= buttonDebounceUs) {
    buzzerButtonPressed = true;
    lastBuzzerInterruptUs = nowUs;
  }
}

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

void logButtonAction(const char* name, bool enabled) {
  Serial.printf("[BUTTON] %s: %s\n", name, enabled ? "ON" : "OFF/AUTO");
}

void setBuzzer(bool enabled);

// Các nút được xử lý nhanh ở local, không phụ thuộc backend/Wi-Fi.
void handleButtons() {
  bool relayPressed = false;
  bool fanPressed = false;
  bool buzzerPressed = false;

  noInterrupts();
  relayPressed = relayButtonPressed;
  fanPressed = fanButtonPressed;
  buzzerPressed = buzzerButtonPressed;
  relayButtonPressed = false;
  fanButtonPressed = false;
  buzzerButtonPressed = false;
  interrupts();

  if (relayPressed) {
    localRelayForced = !localRelayForced;
    logButtonAction("Relay thong gio", localRelayForced);
  }

  if (fanPressed) {
    localFanMaxForced = !localFanMaxForced;
    logButtonAction("Quat MAX", localFanMaxForced);
  }

  if (buzzerPressed) {
    localBuzzerEnabled = !localBuzzerEnabled;
    if (localBuzzerEnabled) {
      buzzerOutputState = true;
      setBuzzer(true);
      lastBuzzerToggle = millis();
    } else {
      buzzerOutputState = false;
      setBuzzer(false);
      lastBuzzerToggle = millis();
    }
    logButtonAction("Buzzer", localBuzzerEnabled);
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

void updateIntermittentBuzzer(bool enabled) {
  if (!enabled) {
    buzzerOutputState = false;
    setBuzzer(false);
    lastBuzzerToggle = millis();
    return;
  }

  unsigned long duration = buzzerOutputState
                             ? buzzerOnDuration
                             : buzzerOffDuration;

  if (millis() - lastBuzzerToggle >= duration) {
    buzzerOutputState = !buzzerOutputState;
    lastBuzzerToggle = millis();
    setBuzzer(buzzerOutputState);
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
  bool ventilationOn,
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
  http.setConnectTimeout(1000);
  http.setTimeout(1500);
  http.addHeader("Content-Type", "application/json");
  http.POST("{}");
  http.end();
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
  http.setConnectTimeout(1000);
  http.setTimeout(1500);
  int responseCode = http.GET();

  if (responseCode > 0) {
    String payload = http.getString();
    StaticJsonDocument<512> doc;
    DeserializationError error = deserializeJson(doc, payload);

    if (!error) {
      if (doc.containsKey("relayManualState")) {
        webRelayForced = doc["relayManualState"].as<int>();
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

  WiFi.mode(WIFI_STA);

  pinMode(MQ2_PIN, INPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(RELAY_THONG_GIO, OUTPUT);
  pinMode(BUTTON_RELAY_PIN, INPUT_PULLUP);
  pinMode(BUTTON_FAN_MAX_PIN, INPUT_PULLUP);
  pinMode(BUTTON_BUZZER_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(BUTTON_RELAY_PIN), onRelayButtonPressed, FALLING);
  attachInterrupt(digitalPinToInterrupt(BUTTON_FAN_MAX_PIN), onFanButtonPressed, FALLING);
  attachInterrupt(digitalPinToInterrupt(BUTTON_BUZZER_PIN), onBuzzerButtonPressed, FALLING);
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

  // Đọc nút trước các đoạn return theo chu kỳ cảm biến.
  handleButtons();
  // Cập nhật còi thường xuyên để tạo nhịp kêu ngắt quãng mượt.
  // Cảnh báo cảm biến sẽ được OR thêm sau khi đọc cảm biến.
  updateIntermittentBuzzer(localBuzzerEnabled || sensorDangerActive);

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
  bool humidityDanger = dhtValid && humidity > humidityThreshold;
  bool danger = tempDanger || gasDanger || humidityDanger;
  sensorDangerActive = danger;

  // Nút giữa đổi trạng thái ép quạt chạy mức MAX.
  // Nhấn lần nữa để trở lại chế độ tự động.
  if (localFanMaxForced) {
    setFanSpeed(FAN_SPEED_FAST);
  } else if (gasDanger || tempDanger || humidityDanger) {
    setFanSpeed(FAN_SPEED_FAST);
  } else if (temperature < lowTemperature) {
    // Trời lạnh: tắt quạt. Có thể đổi thành FAN_SPEED_SLOW nếu cần.
    setFanSpeed(FAN_SPEED_OFF);
  } else {
    setFanSpeed(FAN_SPEED_NORMAL);
  }

  // Relay bật khi bị ép từ Web hoặc khi khí gas vượt ngưỡng.
  // webRelayForced = 1 có ưu tiên bật relay, không vô hiệu hóa logic an toàn tự động.
  // Trạng thái từ nút vật lý và dashboard đều có thể yêu cầu bật relay.
  // Khi gas nguy hiểm, relay vẫn luôn được bật để đảm bảo an toàn.
  bool ventilationOn = localRelayForced || webRelayForced == 1 || gasDanger || humidityDanger;
  digitalWrite(RELAY_THONG_GIO, ventilationOn ? RELAY_ON : RELAY_OFF);

  // Còi kêu ngắt quãng khi được bật bằng nút hoặc khi có cảnh báo cảm biến.
  updateIntermittentBuzzer(localBuzzerEnabled || danger);

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
    humidityDanger,
    ventilationOn,
    alertReason
  );
}
