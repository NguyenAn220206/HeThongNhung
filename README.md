# Hệ thống giám sát lớp học thông minh

Hệ thống IoT theo dõi môi trường lớp học bằng ESP32 và hiển thị dữ liệu trên dashboard web. Camera được mở trực tiếp trên trình duyệt bằng Web API `getUserMedia`, chỉ dùng để xem hình ảnh và không thực hiện AI, nhận diện người hay lưu video.

## Chức năng chính

- Đo nhiệt độ, độ ẩm bằng DHT11.
- Đo khói/khí gas bằng MQ-2.
- Tự động điều chỉnh tốc độ quạt theo nhiệt độ.
- Bật relay quạt thông gió khi khói hoặc khí gas vượt ngưỡng.
- Bật còi buzzer khi có cảnh báo nguy hiểm.
- Hiển thị dữ liệu cảm biến theo thời gian thực trên dashboard.
- Mở camera trực tiếp trên máy đang truy cập dashboard để xem tại chỗ.
- Nhập ngưỡng nhiệt độ/khí gas và điều khiển còi, relay từ dashboard.
- Gửi cảnh báo cảm biến qua Telegram.

## Kiến trúc hệ thống

```text
DHT11 + MQ-2 + ESP32 ──HTTP/JSON──► Node.js + Express + Socket.IO
       ▲                                      │
       │ lệnh cấu hình/điều khiển             │ dữ liệu thời gian thực
       └──────────────────────────────────────┴──► Dashboard Web
                                                    │
                                                    └── Camera trực tiếp bằng getUserMedia
```

Camera chạy cục bộ trong trình duyệt, không truyền qua backend và không được lưu trữ. Trình duyệt sẽ yêu cầu người dùng cấp quyền camera.

## Cấu trúc thư mục

```text
.
├── backend/                 # Node.js API và Socket.IO server
│   ├── server.js
│   └── package.json
├── frontend/                # Dashboard web và camera trực tiếp
│   ├── index.html
│   ├── notifications.html
│   └── Assets/
├── esp32_code/              # Chương trình Arduino cho ESP32
│   └── esp32_code.ino
└── project.pdsprj           # Mô phỏng Proteus
```

## Phần cứng và chân kết nối ESP32

| Thiết bị | GPIO |
|---|---:|
| DHT11 | GPIO 4 |
| MQ-2 (ADC) | GPIO 34 |
| Buzzer | GPIO 18 |
| Relay quạt thông gió | GPIO 19 |
| L298 ENA / PWM quạt | GPIO 25 |
| L298 IN1 | GPIO 26 |
| L298 IN2 | GPIO 27 |

Relay quạt thông gió được cấu hình active-LOW. Ngưỡng mặc định là 28°C cho nhiệt độ tiện nghi và 700 cho giá trị ADC của MQ-2; các ngưỡng này có thể thay đổi từ dashboard.

## Yêu cầu môi trường

- Node.js 18 trở lên và npm.
- ESP32, Arduino IDE hoặc PlatformIO.
- Trình duyệt hỗ trợ `getUserMedia` như Chrome, Edge hoặc Firefox.
- Camera USB hoặc camera tích hợp.

## Cài đặt và chạy

### 1. Khởi động backend

```bash
cd backend
npm install
node server.js
```

Server mặc định chạy tại `http://localhost:3000`.

### 2. Chạy dashboard

Mở thư mục `frontend` bằng một static server, ví dụ VS Code Live Server, sau đó mở `index.html` trên trình duyệt.

Nhấn **BẬT CAMERA** và cấp quyền camera khi được hỏi. Camera thường chỉ hoạt động trên `localhost` hoặc kết nối HTTPS; mở file HTML trực tiếp bằng `file://` có thể bị trình duyệt chặn.

Nếu backend chạy trên máy khác, cập nhật biến `BASE_URL` trong `frontend/Assets/js/app.js` và các URL API tương ứng.

### 3. Nạp chương trình cho ESP32

1. Mở `esp32_code/esp32_code.ino` bằng Arduino IDE.
2. Cài board ESP32 và các thư viện `WiFi`, `HTTPClient`, `DHT`, `ArduinoJson`.
3. Cập nhật SSID, mật khẩu Wi-Fi và địa chỉ IP của backend trong file `.ino`.
4. Kiểm tra sơ đồ nối dây, chọn đúng board/COM port rồi nạp chương trình.

ESP32 phải truy cập được máy chạy backend trong cùng mạng LAN.

## API chính

| Phương thức | Endpoint | Mục đích |
|---|---|---|
| `GET` | `/api/data` | Lấy dữ liệu cảm biến hiện tại |
| `POST` | `/api/data` | ESP32 gửi dữ liệu cảm biến |
| `GET` | `/api/config` | Lấy cấu hình ngưỡng |
| `POST` | `/api/config` | Cập nhật nhiệt độ tiện nghi và ngưỡng gas |
| `POST` | `/api/buzzer/trigger` | Kích hoạt còi từ dashboard |
| `POST` | `/api/relay/toggle` | Chuyển relay giữa chế độ ép bật/tự động |
| `GET` | `/api/relay/status` | Đọc trạng thái ép bật/tự động của relay |
| `POST` | `/api/clear-all` | Xóa lịch sử thông báo trên các dashboard |

Luồng camera không sử dụng API backend; video được lấy trực tiếp từ thiết bị camera bằng `navigator.mediaDevices.getUserMedia()`.

## Luồng cảnh báo

1. ESP32 đọc DHT11 và MQ-2 theo chu kỳ.
2. ESP32 điều khiển quạt, relay và buzzer theo các ngưỡng.
3. Dữ liệu được gửi tới backend bằng JSON.
4. Backend phát dữ liệu tới dashboard qua Socket.IO và gửi Telegram khi có cảnh báo.
5. Người dùng xem camera trực tiếp tại dashboard nếu đã cấp quyền.

## Lưu ý bảo mật

Không đưa Wi-Fi password, Telegram bot token/chat ID hoặc địa chỉ IP nội bộ lên repository công khai. Các thông tin này hiện cần được chuyển sang biến môi trường hoặc file cấu hình không commit; Telegram token đã từng xuất hiện trong mã nguồn nên cần thu hồi và tạo token mới.

Các API điều khiển relay, buzzer và xóa lịch sử hiện chưa có xác thực, chỉ nên sử dụng trong mạng tin cậy.

## Hướng phát triển

- Đưa toàn bộ cấu hình và thông tin nhạy cảm ra biến môi trường.
- Thêm xác thực cho API điều khiển thiết bị.
- Lưu dữ liệu cảm biến vào cơ sở dữ liệu để vẽ biểu đồ lịch sử.
- Thêm Docker Compose và kiểm thử API.

## Link Github Sản phẩm

NguyenAn220206 — [repository HeThongNhung](https://github.com/NguyenAn220206/HeThongNhung)
