# Hệ thống giám sát lớp học thông minh

Hệ thống IoT theo dõi điều kiện môi trường và an ninh trong lớp học. Dữ liệu từ ESP32 được gửi về server theo thời gian thực, hiển thị trên dashboard web và kết hợp với camera + YOLO để phát hiện người xâm nhập vùng giám sát.

## Chức năng chính

- Đo nhiệt độ, độ ẩm bằng cảm biến DHT11.
- Đo khói/khí gas bằng cảm biến MQ-2.
- Tự động điều chỉnh tốc độ quạt theo nhiệt độ:
  - Thấp hơn vùng tiện nghi: quạt chạy chậm.
  - Trong vùng tiện nghi: quạt chạy bình thường.
  - Cao hơn ngưỡng: quạt chạy nhanh.
- Bật relay quạt thông gió khi phát hiện khói hoặc khí gas vượt ngưỡng.
- Bật còi buzzer khi có cảnh báo nguy hiểm.
- Dashboard web hiển thị nhiệt độ, độ ẩm, khí gas, trạng thái kết nối và luồng camera.
- Nhập ngưỡng nhiệt độ/khí gas trực tiếp từ dashboard.
- Điều khiển còi và relay từ xa trên giao diện web.
- Phát hiện người bằng YOLOv8, ghi nhận thời gian ra/vào vùng giám sát và lưu ảnh sự kiện.
- Gửi thông báo cảnh báo qua Telegram.

## Kiến trúc hệ thống

```text
DHT11 + MQ-2 + ESP32
          │ HTTP/JSON
          ▼
Node.js + Express + Socket.IO ─────► Dashboard HTML/CSS/JS
          ▲                                  │
          │ HTTP/Socket.IO                   │ lệnh điều khiển
          │                                  ▼
Python + OpenCV + YOLOv8              ESP32 / thiết bị chấp hành
          │
          └──── ảnh, sự kiện xâm nhập, Telegram
```

## Cấu trúc thư mục

```text
.
├── backend/                 # Node.js API và Socket.IO server
│   ├── server.js
│   └── package.json
├── frontend/                # Dashboard web
│   ├── index.html
│   ├── notifications.html
│   └── Assets/
├── esp32_code/              # Chương trình Arduino cho ESP32
│   └── esp32_code.ino
├── python/                  # Camera, YOLOv8, GUI và ghi log
│   ├── main.py
│   ├── ui.py
│   ├── detector.py
│   ├── camera.py
│   ├── telegram.py
│   ├── requirements.txt
│   └── models/yolov8n.pt
└── project.pdsprj          # Mô phỏng Proteus
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

Relay quạt thông gió được cấu hình active-LOW. Ngưỡng mặc định trong chương trình là 28°C cho nhiệt độ tiện nghi và 700 cho giá trị ADC của MQ-2; các ngưỡng này có thể thay đổi từ dashboard.

## Yêu cầu môi trường

- Node.js 18 trở lên và npm.
- Python 3.10+ khuyến nghị.
- ESP32, Arduino IDE hoặc PlatformIO.
- Webcam/camera USB.
- Các thư viện ESP32: `WiFi`, `HTTPClient`, `DHT`, `ArduinoJson`.

## Cài đặt và chạy

### 1. Khởi động backend

```bash
cd backend
npm install
node server.js
```

Server mặc định chạy tại `http://localhost:3000`.

### 2. Chạy dashboard

Mở thư mục `frontend` bằng một static server, ví dụ VS Code Live Server. Dashboard mặc định kết nối tới `http://localhost:3000`.

Nếu backend chạy trên máy khác, cập nhật biến `BASE_URL` trong `frontend/Assets/js/app.js` và các URL camera trong `frontend/index.html`/`frontend/notifications.html`.

### 3. Chạy ứng dụng Python AI

```bash
cd python
python -m venv .venv

# Windows
.venv\Scripts\activate

# macOS/Linux
# source .venv/bin/activate

pip install -r requirements.txt
pip install requests
python main.py
```

Ứng dụng Python mở camera, chạy YOLOv8, lưu ảnh sự kiện vào `python/images` và gửi sự kiện về backend tại `http://localhost:3000`.

### 4. Nạp chương trình cho ESP32

1. Mở `esp32_code/esp32_code.ino` bằng Arduino IDE.
2. Cài board ESP32 và các thư viện cần thiết.
3. Cập nhật SSID, mật khẩu Wi-Fi và địa chỉ IP của backend trong file `.ino`.
4. Kiểm tra sơ đồ nối dây, chọn đúng board/COM port rồi nạp chương trình.

ESP32 hiện gửi dữ liệu tới endpoint `/api/data` và lấy cấu hình từ `/api/config`. Máy chạy backend phải có địa chỉ IP mà ESP32 truy cập được trong cùng mạng LAN.

## API chính

| Phương thức | Endpoint | Mục đích |
|---|---|---|
| `GET` | `/api/data` | Lấy dữ liệu cảm biến hiện tại |
| `POST` | `/api/data` | ESP32 gửi dữ liệu cảm biến |
| `GET` | `/api/config` | Lấy cấu hình ngưỡng |
| `POST` | `/api/config` | Cập nhật nhiệt độ tiện nghi và ngưỡng gas |
| `GET` | `/api/video-feed` | Luồng MJPEG từ Python |
| `GET` | `/api/video-snapshot` | Lấy frame camera hiện tại |
| `POST` | `/api/video-stream` | Python gửi frame JPEG lên server |
| `POST` | `/api/alert` | Python gửi sự kiện phát hiện người |
| `POST` | `/api/buzzer/trigger` | Kích hoạt còi từ dashboard |
| `POST` | `/api/relay/toggle` | Chuyển relay giữa chế độ ép ngắt/tự động |
| `POST` | `/api/clear-all` | Xóa lịch sử, log và ảnh đã lưu |

## Luồng xử lý cảnh báo

1. ESP32 đọc DHT11 và MQ-2 theo chu kỳ.
2. ESP32 xác định mức nhiệt độ, tốc độ quạt, relay thông gió và buzzer.
3. Dữ liệu được gửi tới backend bằng JSON.
4. Backend phát dữ liệu tới dashboard qua Socket.IO và gửi Telegram khi có cảnh báo mới.
5. Python xử lý camera bằng YOLOv8, lưu ảnh và gửi sự kiện xâm nhập tới backend.

## Lưu ý bảo mật

Không đưa thông tin thật lên repository công khai. Trước khi push mã nguồn, cần thay Wi-Fi password, Telegram bot token/chat ID và các địa chỉ IP hard-code bằng biến môi trường hoặc file cấu hình không commit. Token Telegram đang xuất hiện trong mã nguồn hiện tại nên cần thu hồi và tạo token mới nếu repository được chia sẻ công khai.

## Hướng phát triển

- Đưa toàn bộ cấu hình và thông tin nhạy cảm ra biến môi trường.
- Thêm xác thực cho API điều khiển relay/buzzer.
- Lưu dữ liệu cảm biến vào cơ sở dữ liệu để vẽ biểu đồ lịch sử.
- Thêm Docker Compose cho backend, Python AI và frontend.
- Bổ sung kiểm thử API và cơ chế tự khởi động lại khi mất kết nối.

## Tác giả

NguyenAn220206 — [repository HeThongNhung](https://github.com/NguyenAn220206/HeThongNhung)
