const express = require("express");
const cors = require("cors");
const http = require("http");
const { Server } = require("socket.io");
const path = require("path");
const fs = require("fs"); 
const fetch = require("node-fetch"); // Đảm bảo đã chạy npm install node-fetch@2

const app = express();
const server = http.createServer(app);

const io = new Server(server, {
    cors: { origin: "*" }
});

app.use(cors());
app.use(express.json());

// ===== STATIC IMAGE =====
app.use("/images", express.static(path.join(__dirname, "../python/images")));

// =========================================================
// CẤU HÌNH VÀ HÀM GỬI TELEGRAM
// =========================================================
const TELEGRAM_BOT_TOKEN = "8966373592:AAF4IwUTWFC0Ln9ElfOoa8xfz9ca45EsO2Y";
const TELEGRAM_CHAT_ID = "-5036187167";

async function sendTelegramAlert(message) {
    if (!TELEGRAM_BOT_TOKEN || !TELEGRAM_CHAT_ID) {
        console.log("[WARNING] [Telegram] Chưa cấu hình đầy đủ Token hoặc Chat ID!");
        return false;
    }

    const url = `https://api.telegram.org/bot${TELEGRAM_BOT_TOKEN}/sendMessage`;
    try {
        const response = await fetch(url, {
            method: "POST",
            headers: { "Content-Type": "application/json" },
            body: JSON.stringify({
                chat_id: TELEGRAM_CHAT_ID,
                text: message,
                parse_mode: "HTML" // Sử dụng HTML để tránh lỗi bẻ gãy cú pháp bởi ký tự đặc biệt
            })
        });
        
        if (response.ok) {
            console.log("[LOG] [Telegram] Đã gửi thông báo thành công!");
            return true;
        } else {
            const errorData = await response.json();
            console.log("--------------------------------------------------");
            console.log(`[WARNING] [Telegram] Gửi thất bại! Mã HTTP: ${response.status}`);
            console.log(`[LỖI TỪ TELEGRAM]:`, JSON.stringify(errorData, null, 2));
            console.log("--------------------------------------------------");
            return false;
        }
    } catch (error) {
        console.error("[ERROR] [Telegram] Lỗi kết nối mạng khi gửi cảnh báo:", error);
        return false;
    }
}

// ===== BIẾN TOÀN CỤC LƯU FRAME HÌNH ẢNH AI TỪ PYTHON =====
let latestPythonFrame = null;

// ===== SENSOR DATA =====
let sensorData = {
    temperature: 0,
    humidity: 0,
    gas: 0,
    motion: 0,
    distance: 0,
    alertReason: "An toàn",
    updatedAt: null
};

// Biến bộ đệm (cache) để chống spam tin nhắn liên tục lên Telegram
let lastTelegramReason = "An toàn";
let isGasAlertSent = false;
let isTempAlertSent = false;

// ===== CHỨC NĂNG MỚI KHÁC (GIỮ NGUYÊN) =====
let shouldBuzzerSound = 0; // 0: Tắt còi, 1: Bật còi kêu khẩn cấp
let manualRelayState = 0;  // THÊM: 0: Chạy tự động/bình thường, 1: Ép ngắt Relay từ Web

// ===== ALERT LIST =====
let alerts = [];

// Biến toàn cục lưu giữ trạng thái sống của luồng Python AI
let isPythonConnected = false;

// ===== SOCKET MANAGEMENT =====
io.on("connection", (socket) => {
    const userAgent = socket.handshake.headers["user-agent"] || "";
    let clientType = "Frontend Web";

    if (userAgent.includes("Python") || userAgent.includes("python")) {
        clientType = "Python AI Edge";
        isPythonConnected = true; 
        console.log(`[LOG] ỨNG DỤNG PYTHON AI ĐÃ KẾT NỐI! (ID: ${socket.id})`);
        io.emit("python-status", true); 
    } else {
        console.log(`[LOG] Frontend đã kết nối. (ID: ${socket.id})`);
        socket.emit("python-status", isPythonConnected); 
    }

    socket.on("check-python-status", () => {
        socket.emit("python-status", isPythonConnected);
    });

    socket.emit("sensor-data", sensorData);
    socket.emit("alert-list", alerts);

    socket.on("disconnect", (reason) => {
        if (clientType === "Python AI Edge") {
            isPythonConnected = false; 
            console.log(`[WARNING][Socket] PYTHON ĐÃ MẤT KẾT NỐI! (Lý do: ${reason})`);
            io.emit("python-status", false); 
        } else {
            console.log(`[WARNING] Frontend đã ngắt kết nối. (Lý do: ${reason})`);
        }
    });
});

// ===== CÁC API PHỤC VỤ STREAM HÌNH ẢNH ĐỒNG BỘ =====
app.post("/api/video-stream", express.raw({ type: 'image/jpeg', limit: '10mb' }), (req, res) => {
    latestPythonFrame = req.body;
    res.end();
});

app.get("/api/video-feed", (req, res) => {
    res.writeHead(200, {
        'Content-Type': 'multipart/x-mixed-replace; boundary=frame',
        'Cache-Control': 'no-cache',
        'Connection': 'close',
        'Pragma': 'no-cache'
    });

    const streamInterval = setInterval(() => {
        if (latestPythonFrame) {
            res.write(`--frame\r\n`);
            res.write(`Content-Type: image/jpeg\r\n`);
            res.write(`Content-Length: ${latestPythonFrame.length}\r\n\r\n`);
            res.write(latestPythonFrame);
            res.write(`\r\n`);
        }
    }, 40);

    req.on('close', () => {
        clearInterval(streamInterval);
    });
});

app.get("/api/video-snapshot", (req, res) => {
    if (latestPythonFrame) {
        res.writeHead(200, {
            'Content-Type': 'image/jpeg',
            'Cache-Control': 'no-cache, no-store, must-revalidate',
            'Pragma': 'no-cache',
            'Expires': '0'
        });
        res.end(latestPythonFrame); 
    } else {
        res.status(404).send("Chưa có frame ảnh nào từ Python AI");
    }
});

// ===== ESP32 DATA =====
app.post("/api/data", (req, res) => {
    sensorData = {
        ...req.body,
        updatedAt: new Date()
    };

    console.log("-----------------------------------------");
    console.log(`[LOG] [ESP32] Nhận chuỗi JSON gửi lên lúc: ${sensorData.updatedAt.toLocaleTimeString()}`);
    console.log(`[LÝ DO]: ${sensorData.alertReason}`);
    console.log(JSON.stringify(req.body, null, 2)); 
    console.log("-----------------------------------------");

    const timeString = sensorData.updatedAt.toLocaleTimeString();

    // 1. Gửi cảnh báo Khí Gas vượt ngưỡng (> 2000)
    if (sensorData.gas > 2000) {
        console.log("[ALERT] CANH BAO GAS! Đã vượt ngưỡng an toàn.");
        if (!isGasAlertSent) {
            sendTelegramAlert(`⚠️ <b>[CẢNH BÁO RÒ RỈ KHÍ GAS]</b>\n🔥 Giá trị đo được: ${sensorData.gas}\n🕒 Thời gian: ${timeString}`);
            isGasAlertSent = true;
        }
    } else {
        isGasAlertSent = false;
    }

    // 2. Gửi cảnh báo Quá Nhiệt độ (> 50°C)
    if (sensorData.temperature > 50) {
        console.log("[ALERT] CANH BAO NHIET DO! Thiết bị quá nóng.");
        if (!isTempAlertSent) {
            sendTelegramAlert(`🥵 <b>[CẢNH BÁO QUÁ NHIỆT ĐỘ]</b>\n🌡 Nhiệt độ hiện tại: ${sensorData.temperature}°C\n🕒 Thời gian: ${timeString}`);
            isTempAlertSent = true;
        }
    } else {
        isTempAlertSent = false;
    }

    // 3. Gửi cảnh báo từ hệ thống phần cứng (Lý do cảnh báo thay đổi khác "An toàn")
    if (sensorData.alertReason && sensorData.alertReason !== "An toàn") {
        if (sensorData.alertReason !== lastTelegramReason) {
            
            // Chuẩn hóa HTML: Đổi dấu '<' sang '&lt;' để bảo vệ cú pháp thẻ của Telegram
            let formattedReason = sensorData.alertReason;
            if (formattedReason.includes("<")) {
                formattedReason = formattedReason.split("<").join("&lt;");
            }

            sendTelegramAlert(`🚨 <b>[HỆ THỐNG ESP32 BÁO ĐỘNG]</b>\n• Nội dung: ${formattedReason}\n🕒 Thời gian: ${timeString}`);
            lastTelegramReason = sensorData.alertReason;
        }
    } else {
        lastTelegramReason = "An toàn";
    }

    io.emit("sensor-data", sensorData);

    res.json({ 
        success: true,
        message: "Data received" 
    });
});

// ===== PYTHON ALERT =====
app.post("/api/alert", (req, res) => {
    const alert = {
        id: Date.now(),
        image: req.body.image,
        imageCrop: req.body.imageCrop, 
        zone: req.body.zone,
        enterTime: req.body.enterTime,
        exitTime: req.body.exitTime,
        duration: req.body.duration,
        createdAt: new Date(),
        sensor: sensorData
    };

    alerts.unshift(alert);

    console.log("=================================");
    console.log("[ALERT] PHÁT HIỆN XÂM NHẬP");
    console.log("Zone:", alert.zone);
    console.log("Vào lúc:", alert.enterTime);
    console.log("Ra lúc:", alert.exitTime);
    console.log("Ở lại:", alert.duration, "giây");
    console.log("Ảnh:", alert.image);
    console.log("Ảnh Crop:", alert.imageCrop);
    console.log("=================================");

    io.emit("new-alert", alert);

    res.json({
        success: true
    });
});

// ===== CÁC API PHỤC VỤ ĐIỀU KHIỂN CÒI BUZZER TỪ NÚT WEB =====
// SỬA ĐỔI TẠI ĐÂY: Thêm chức năng bắn tin nhắn Telegram khi nhấn nút ALERT trên web dashboard
app.post("/api/buzzer/trigger", (req, res) => {
    shouldBuzzerSound = 1;
    console.log("[ALERT] Nút bấm trên Web yêu cầu bật còi Buzzer!");

    const timeString = new Date().toLocaleTimeString();
    sendTelegramAlert(`📢 <b>[THÔNG BÁO TỪ WEB DASHBOARD]</b>\n🚨 Người dùng đã nhấn nút kích hoạt còi báo động khẩn cấp từ xa!\n🕒 Thời gian: ${timeString}`);

    res.json({ success: true, message: "Buzzer state set to 1 and Telegram alert sent" });
});

// THÊM API: Nhận lệnh bật/tắt ép buộc từ Web Dashboard
app.post("/api/relay/toggle", (req, res) => {
    // Đảo trạng thái 0 <-> 1
    manualRelayState = manualRelayState === 0 ? 1 : 0;
    console.log(`[CONTROL] Người dùng thay đổi trạng thái ép ngắt Relay trên Web: ${manualRelayState}`);
    
    res.json({ 
        success: true, 
        relayState: manualRelayState, 
        message: manualRelayState === 1 ? "Đã ra lệnh ép ngắt Relay" : "Đã trả về chế độ tự động" 
    });
});

app.get("/api/buzzer/status", (req, res) => {
    res.json({ 
        buzzerAlert: shouldBuzzerSound,
        relayManualId: manualRelayState 
    });
});

app.post("/api/buzzer/reset", (req, res) => {
    shouldBuzzerSound = 0;
    console.log("[CLEAN] ESP32 báo cáo đã kêu xong. Reset cờ còi về 0.");
    res.json({ success: true, message: "Buzzer state reset to 0" });
});

// ===== API CLEAR TOÀN BỘ DỮ LIỆU HỆ THỐNG =====
// ===== API CLEAR TOÀN BỘ DỮ LIỆU HỆ THỐNG =====
app.post("/api/clear-all", (req, res) => {
    try {
        alerts = [];
        io.emit("alert-list", alerts);

        const logFilePath = path.join(__dirname, "../python", "log.txt");
        const imagesFolderPath = path.join(__dirname, "../python", "images");

        if (fs.existsSync(logFilePath)) {
            fs.unlinkSync(logFilePath);
            console.log("[CLEAN] Đã xóa file nhật ký log.txt.");
        }

        if (fs.existsSync(imagesFolderPath)) {
            const files = fs.readdirSync(imagesFolderPath);
            files.forEach((file) => {
                if (file.endsWith(".jpg") || file.endsWith(".jpeg") || file.endsWith(".png")) {
                    fs.unlinkSync(path.join(imagesFolderPath, file));
                }
            });
            console.log("[CLEAN] Đã dọn dẹp sạch toàn bộ kho ảnh lưu trữ trong python/images.");
        }

        // THÊM TẠI ĐÂY: Phát tín hiệu Socket báo cho cả App và Web xóa lịch sử cục bộ
        io.emit("history-cleared");
        console.log("[SYNC] Đã phát tín hiệu 'history-cleared' tới tất cả Client đang kết nối.");

        res.json({
            success: true,
            message: "Xóa dữ liệu ổ cứng server thành công."
        });

    } catch (error) {
        console.error("[ERROR] Lỗi dọn dẹp tệp tin hệ thống:", error);
        res.status(500).json({ success: false, message: "Lỗi dọn dẹp dữ liệu server." });
    }
});

app.get("/api/data", (req, res) => {
    res.json(sensorData);
});

app.get("/", (req, res) => {
    res.send("IoT Backend Running");
});

// ===== START =====
server.listen(3000, () => {
    console.log("IoT Backend Running at http://localhost:3000");
});