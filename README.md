# Hệ thống giám sát lớp học thông minh

Project IoT theo dõi môi trường lớp học bằng ESP32, cảm biến DHT11/MQ-2 và dashboard web. ESP32 đọc dữ liệu cảm biến, tự điều khiển quạt thông qua L298, relay thông gió và buzzer; backend Node.js tiếp nhận dữ liệu, phát trực tiếp tới dashboard bằng Socket.IO và gửi cảnh báo Telegram.

Camera chỉ được mở trực tiếp trên trình duyệt bằng `navigator.mediaDevices.getUserMedia()`. Video không đi qua backend và không được lưu trữ.

> Trạng thái hiện tại: project có firmware ESP32, backend Express/Socket.IO, dashboard web và file mô phỏng Proteus. Chưa có bộ kiểm thử tự động hoặc cơ chế lưu dữ liệu cảm biến lâu dài.

## 1. Chức năng

- Đo nhiệt độ và độ ẩm bằng DHT11.
- Đo giá trị khói/khí gas dạng analog bằng MQ-2.
- Tự động điều chỉnh tốc độ quạt theo nhiệt độ và trạng thái cảnh báo.
- Bật relay quạt thông gió khi phát hiện gas/khói hoặc độ ẩm cao.
- Phát buzzer ngắt quãng khi có cảnh báo cảm biến.
- Điều khiển relay và buzzer từ dashboard web.
- Có ba nút vật lý trên ESP32 tạo thành **Safety Mode**: vẫn điều khiển được relay, quạt và buzzer khi dashboard, backend hoặc kết nối online không tương tác được.
- Hiển thị dữ liệu gần thời gian thực trên dashboard qua Socket.IO.
- Ghi lịch sử cảnh báo ở `localStorage` của trình duyệt.
- Gửi cảnh báo gas, quá nhiệt và cảnh báo từ ESP32 qua Telegram.
- Mở/tắt camera trực tiếp trên máy đang truy cập dashboard.
- Hỗ trợ giao diện sáng/tối.

## 2. Kiến trúc

```text
 DHT11 + MQ-2 + nút vật lý
             │
             ▼
          ESP32
   ┌─────────┼──────────┐
   │         │          │
   │ HTTP    │ HTTP     │ điều khiển
   ▼         ▼          │
 POST data  GET config  │
   └─────────┬──────────┘
             ▼
   Node.js + Express + Socket.IO ─────► Telegram
             │
             ├── REST API
             └── Socket.IO ───────────► Dashboard web
                                           │
                                           └── Camera local bằng getUserMedia
```

ESP32 gửi dữ liệu cảm biến khoảng mỗi 2 giây, đọc lệnh điều khiển khoảng mỗi 3 giây và lấy cấu hình mới khoảng mỗi 5 giây. Backend lưu trạng thái trong bộ nhớ; khởi động lại server sẽ đưa trạng thái về mặc định.

## 3. Cấu trúc thư mục

```text
.
├── backend/
│   ├── server.js             # Express API, Socket.IO và Telegram
│   ├── package.json
│   └── package-lock.json
├── frontend/
│   ├── index.html            # Dashboard chính
│   ├── notifications.html    # Lịch sử cảnh báo
│   └── Assets/
│       ├── css/
│       └── js/app.js
├── esp32_code/
│   └── esp32_code.ino        # Firmware ESP32 chính
├── backupcode/
│   └── esp32_code.ino        # Bản firmware dự phòng
├── project.pdsprj           # Mô phỏng Proteus
├── Project Backups/          # Các bản autosave/backup Proteus
└── Mô tả.txt                 # Mô tả ý tưởng ban đầu
```

## 4. Phần cứng và sơ đồ chân

| Thiết bị/chức năng | GPIO ESP32 | Ghi chú |
|---|---:|---|
| DHT11 | 4 | Dữ liệu nhiệt độ/độ ẩm |
| MQ-2 | 34 | ADC, chân input-only |
| Buzzer | 18 | Xuất tín hiệu buzzer |
| Relay thông gió | 19 | Firmware hiện tại: `HIGH` là bật |
| Nút relay | 23 | `INPUT_PULLUP`, nhấn để đổi trạng thái |
| Nút quạt MAX | 22 | `INPUT_PULLUP`, nhấn để ép quạt chạy nhanh |
| Nút buzzer | 21 | `INPUT_PULLUP`, nhấn để bật/tắt buzzer |
| L298 ENA/PWM | 25 | PWM tốc độ quạt |
| L298 IN1 | 26 | Chiều quay |
| L298 IN2 | 27 | Chiều quay |

### Mức quạt

| Mức | PWM |
|---|---:|
| Tắt | 0 |
| Chậm | 90 |
| Bình thường | 170 |
| Nhanh/MAX | 255 |

## 5. Logic điều khiển

- Nhiệt độ dễ chịu mặc định: `28.0°C`.
- Dải nhiệt độ bình thường: `comfortTemperature - 2` đến `comfortTemperature + 2`.
- Ngưỡng MQ-2 mặc định: `70` theo thang ADC của firmware hiện tại; cần hiệu chỉnh theo cảm biến thực tế.
- Ngưỡng độ ẩm cao: `80%`.
- Khi nhiệt độ thấp hơn dải bình thường: quạt tắt.
- Khi nhiệt độ nằm trong dải bình thường: quạt chạy mức bình thường.
- Khi nhiệt độ, gas hoặc độ ẩm vượt ngưỡng: quạt chạy nhanh và buzzer cảnh báo.
- Relay thông gió bật khi gas/độ ẩm vượt ngưỡng hoặc relay bị ép bật từ nút vật lý/dashboard.
- Nút quạt MAX có ưu tiên cao hơn logic tốc độ tự động.
- Lệnh buzzer từ dashboard tạo một chuỗi buzzer từ xa, sau đó ESP32 báo backend reset cờ lệnh.

### Safety Mode – điều khiển dự phòng tại chỗ

Safety Mode là nhóm nút chức năng vật lý trên ESP32, giúp người dùng vẫn xử lý được tình huống khẩn cấp khi tương tác online không hoạt động, chẳng hạn dashboard mất kết nối, backend dừng hoặc mạng LAN gặp sự cố. Các nút được xử lý trực tiếp trên ESP32 và không phụ thuộc vào frontend hay API:

- **Nút relay (GPIO 23):** nhấn để bật/tắt yêu cầu relay thông gió tại chỗ. Khi có cảnh báo gas, logic an toàn tự động vẫn ưu tiên bật relay.
- **Nút quạt MAX (GPIO 22):** nhấn để ép quạt chạy tốc độ tối đa; nhấn lần nữa để quay lại chế độ tự động.
- **Nút buzzer (GPIO 21):** nhấn để bật/tắt buzzer thủ công tại chỗ.

Vì các nút này hoạt động độc lập với đường truyền online, đây là phương án dự phòng để duy trì khả năng can thiệp trực tiếp vào thiết bị khi dashboard hoặc backend không phản hồi.

## 6. Yêu cầu môi trường

- Node.js 18+ và npm.
- Arduino IDE hoặc PlatformIO.
- Board ESP32 tương thích.
- Các thư viện Arduino: `WiFi`, `HTTPClient`, `DHT`, `ArduinoJson`.
- Trình duyệt hiện đại hỗ trợ Fetch, WebSocket và `getUserMedia`.
- Camera USB/tích hợp nếu cần dùng tính năng camera.
- ESP32 và máy chạy backend phải cùng mạng LAN.

## 7. Cài đặt và chạy

### 7.1. Chạy backend

```bash
cd backend
npm install
node server.js
```

Backend lắng nghe tại `http://localhost:3000`. Có thể mở URL này trên trình duyệt để kiểm tra; server sẽ trả về `IoT Backend Running`.

### 7.2. Chạy frontend

Không nên mở HTML bằng `file://`, đặc biệt khi dùng camera. Hãy chạy static server trong thư mục `frontend`, ví dụ:

```bash
cd frontend
npx serve .
```

Sau đó mở URL do lệnh trả về. Nếu static server dùng cùng cổng `3000` với backend, hãy đổi một trong hai cổng để tránh xung đột.

Frontend mặc định kết nối tới `http://localhost:3000`. Nếu backend chạy ở máy hoặc cổng khác, cập nhật `BASE_URL` trong:

- `frontend/Assets/js/app.js`
- `frontend/notifications.html`

Camera thường chỉ hoạt động trên `localhost` hoặc HTTPS. Khi được hỏi, cấp quyền camera rồi bấm **BẬT CAMERA**.

### 7.3. Nạp firmware ESP32

1. Mở `esp32_code/esp32_code.ino` bằng Arduino IDE.
2. Cài board ESP32 và các thư viện cần thiết.
3. Cập nhật `ssid`, `password` và các URL backend ở đầu file `.ino`.
4. Đảm bảo địa chỉ backend là địa chỉ LAN mà ESP32 truy cập được; không dùng `localhost` trên ESP32.
5. Kiểm tra dây nối theo bảng GPIO.
6. Chọn đúng board và cổng COM, sau đó nạp chương trình.
7. Mở Serial Monitor ở baud rate `115200` để theo dõi Wi-Fi, cảm biến và mã HTTP.

## 8. API backend

### Dữ liệu cảm biến

| Phương thức | Endpoint | Mục đích |
|---|---|---|
| `GET` | `/api/data` | Lấy dữ liệu cảm biến mới nhất |
| `POST` | `/api/data` | ESP32 gửi dữ liệu cảm biến |

Payload chính do ESP32 gửi gồm `temperature`, `humidity`, `gas`, `gasThreshold`, `humidityThreshold`, `comfortTemperature`, `tempLow`, `tempHigh`, các cờ cảnh báo, `alertReason`, `fanSpeed`, `fanLevel` và `thongGio`.

### Cấu hình

| Phương thức | Endpoint | Mục đích |
|---|---|---|
| `GET` | `/api/config` | Đọc `comfortTemperature` và `gasThreshold` |
| `POST` | `/api/config` | Cập nhật ngưỡng |

Giới hạn backend: `comfortTemperature` từ `10` đến `45°C`; `gasThreshold` từ `1` đến `4095`.

Ví dụ:

```json
{
  "comfortTemperature": 28,
  "gasThreshold": 70
}
```

### Điều khiển

| Phương thức | Endpoint | Mục đích |
|---|---|---|
| `POST` | `/api/buzzer/trigger` | Yêu cầu ESP32 phát buzzer từ xa và gửi Telegram |
| `GET` | `/api/buzzer/status` | ESP32 đọc cờ buzzer và trạng thái relay web |
| `POST` | `/api/buzzer/reset` | ESP32 xóa cờ buzzer sau khi xử lý |
| `POST` | `/api/relay/set` | Đặt relay web: `relayState` là `0` hoặc `1` |
| `POST` | `/api/relay/toggle` | Đảo giữa chế độ tự động và ép bật relay |
| `GET` | `/api/relay/status` | Đọc trạng thái relay web |
| `POST` | `/api/clear-all` | Phát sự kiện xóa lịch sử tới các dashboard đang kết nối |

`relayState = 0` đưa relay về chế độ tự động; `relayState = 1` yêu cầu ESP32 ép bật relay. Lịch sử cảnh báo thực tế nằm trong `localStorage` từng trình duyệt; `/api/clear-all` đồng bộ yêu cầu xóa giữa các dashboard đang mở.

## 9. Socket.IO

Frontend kết nối Socket.IO tới `BASE_URL` và sử dụng các sự kiện:

| Sự kiện | Hướng | Ý nghĩa |
|---|---|---|
| `sensor-data` | Backend → frontend | Dữ liệu cảm biến mới nhất |
| `system-config` | Backend → frontend | Cấu hình vừa được cập nhật |
| `history-cleared` | Backend → frontend | Yêu cầu xóa lịch sử cảnh báo |

## 10. Mô phỏng Proteus

Mở `project.pdsprj` bằng Proteus phiên bản tương thích để xem mạch mô phỏng. Khi chạy mô phỏng, cần kiểm tra lại model linh kiện, baud rate, kết nối UART/Wi-Fi giả lập và mức logic relay vì hành vi mô phỏng có thể khác phần cứng thật.

## 11. Xử lý lỗi thường gặp

| Hiện tượng | Kiểm tra |
|---|---|
| Dashboard không có dữ liệu | Backend đã chạy chưa, `BASE_URL` đúng chưa, Console trình duyệt có lỗi CORS/WebSocket không |
| ESP32 không gửi được dữ liệu | ESP32 cùng LAN chưa, URL đúng IP backend chưa, firewall có chặn cổng `3000` không |
| Camera không mở | Đang dùng `localhost`/HTTPS chưa, đã cấp quyền chưa, camera có bị ứng dụng khác chiếm không |
| Gas báo sai | MQ-2 cần thời gian làm nóng và cần hiệu chuẩn ngưỡng theo môi trường thực tế |
| Relay chạy ngược | Kiểm tra module relay và chỉnh `RELAY_ON`/`RELAY_OFF` trong firmware |
| DHT11 lỗi | Kiểm tra nguồn, điện trở kéo lên, dây DATA và khoảng thời gian đọc cảm biến |
| Không có Telegram | Kiểm tra Internet của backend, bot token/chat ID và log server |

## 12. Bảo mật và giới hạn hiện tại

Project hiện phù hợp cho thử nghiệm trong mạng tin cậy, chưa phù hợp để đưa trực tiếp lên Internet:

- Wi-Fi và thông tin Telegram đang khai báo trực tiếp trong mã nguồn; cần chuyển sang cấu hình riêng/biến môi trường và không commit thông tin thật.
- Nếu thông tin đã từng được đẩy lên Git hoặc chia sẻ, cần thu hồi mật khẩu Wi-Fi và tạo lại Telegram bot token.
- API điều khiển relay, buzzer và xóa lịch sử chưa có xác thực.
- CORS backend đang cho phép mọi origin (`*`).
- Dữ liệu cảm biến và cấu hình chỉ lưu trong RAM, không có database.
- Frontend sử dụng `innerHTML` cho một số nội dung; nếu mở rộng nguồn dữ liệu, cần thêm xử lý chống XSS.
- Chưa có HTTPS, rate limiting, logging tập trung hoặc kiểm thử tự động.

## 13. Hướng phát triển

1. Đưa Wi-Fi, Telegram và địa chỉ backend ra file cấu hình/biến môi trường.
2. Thêm xác thực và phân quyền cho API điều khiển.
3. Giới hạn CORS theo domain dashboard.
4. Lưu lịch sử cảm biến và cảnh báo vào database để vẽ biểu đồ.
5. Thêm Docker Compose, health check và test API.
6. Hiệu chuẩn MQ-2, kiểm thử relay active-high/active-low và kiểm thử với phần cứng thật.
7. Thêm trạng thái mất kết nối ESP32/backend rõ ràng hơn trên dashboard.

## 14. Liên kết repository

[NguyenAn220206/HeThongNhung](https://github.com/NguyenAn220206/HeThongNhung)
