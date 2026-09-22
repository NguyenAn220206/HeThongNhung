const express = require("express");
const cors = require("cors");
const http = require("http");
const { Server } = require("socket.io");
const fetch = require("node-fetch"); // Đảm bảo đã chạy npm install node-fetch@2

const app = express();
const server = http.createServer(app);

const io = new Server(server, {
    cors: { origin: "*" }
});

app.use(cors());
app.use(express.json());

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

// ===== CẤU HÌNH NGƯỠNG DO NGƯỜI DÙNG NHẬP =====
let systemConfig = {
    comfortTemperature: 28.0,
    gasThreshold: 700
};

// Biến bộ đệm (cache) để chống spam tin nhắn liên tục lên Telegram
let lastTelegramReason = "An toàn";
let isGasAlertSent = false;
let isTempAlertSent = false;

// ===== CHỨC NĂNG MỚI KHÁC (GIỮ NGUYÊN) =====
let shouldBuzzerSound = 0; // 0: Tắt còi, 1: Bật còi kêu khẩn cấp
let manualRelayState = 0;  // THÊM: 0: Chạy tự động/bình thường, 1: Ép ngắt Relay từ Web

// ===== SOCKET MANAGEMENT =====
io.on("connection", (socket) => {
    console.log(`[LOG] Frontend đã kết nối. (ID: ${socket.id})`);

    socket.emit("sensor-data", sensorData);

    socket.on("disconnect", (reason) => {
        console.log(`[WARNING] Frontend đã ngắt kết nối. (Lý do: ${reason})`);
    });
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

    const gasLimit = Number(sensorData.gasThreshold ?? systemConfig.gasThreshold);
    const temperatureLimit = Number(sensorData.tempHigh ?? (systemConfig.comfortTemperature + 2));

    // 1. Gửi cảnh báo Khí Gas vượt ngưỡng cấu hình
    if (sensorData.gas > gasLimit) {
        console.log("[ALERT] CANH BAO GAS! Đã vượt ngưỡng an toàn.");
        if (!isGasAlertSent) {
            sendTelegramAlert(`⚠️ <b>[CẢNH BÁO RÒ RỈ KHÍ GAS]</b>\n🔥 Giá trị đo được: ${sensorData.gas}\n🕒 Thời gian: ${timeString}`);
            isGasAlertSent = true;
        }
    } else {
        isGasAlertSent = false;
    }

    // 2. Gửi cảnh báo Nhiệt độ vượt T dễ chịu + 2°C
    if (sensorData.temperature > temperatureLimit) {
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
        io.emit("history-cleared");
        console.log("[SYNC] Đã phát tín hiệu 'history-cleared' tới tất cả Client đang kết nối.");

        res.json({
            success: true,
            message: "Đã xóa lịch sử thông báo trên các giao diện đang kết nối."
        });

    } catch (error) {
        console.error("[ERROR] Lỗi đồng bộ xóa lịch sử:", error);
        res.status(500).json({ success: false, message: "Lỗi xóa lịch sử thông báo." });
    }
});

app.get("/api/data", (req, res) => {
    res.json(sensorData);
});

// ===== API CẤU HÌNH NHIỆT ĐỘ DỄ CHỊU / NGƯỠNG GAS =====
app.get("/api/config", (req, res) => {
    res.json(systemConfig);
});

app.post("/api/config", (req, res) => {
    const comfortTemperature = Number(req.body.comfortTemperature);
    const gasThreshold = req.body.gasThreshold === undefined
        ? systemConfig.gasThreshold
        : Number(req.body.gasThreshold);

    if (!Number.isFinite(comfortTemperature) || comfortTemperature < 10 || comfortTemperature > 45) {
        return res.status(400).json({
            success: false,
            message: "Nhiệt độ dễ chịu phải nằm trong khoảng 10 đến 45°C."
        });
    }

    if (!Number.isFinite(gasThreshold) || gasThreshold < 1 || gasThreshold > 4095) {
        return res.status(400).json({
            success: false,
            message: "Ngưỡng gas phải nằm trong khoảng 1 đến 4095."
        });
    }

    systemConfig = {
        comfortTemperature: Math.round(comfortTemperature * 10) / 10,
        gasThreshold: Math.round(gasThreshold)
    };

    io.emit("system-config", systemConfig);
    console.log("[CONFIG] Đã cập nhật cấu hình:", systemConfig);

    res.json({ success: true, config: systemConfig });
});

app.get("/", (req, res) => {
    res.send("IoT Backend Running");
});

// ===== START =====
server.listen(3000, () => {
    console.log("IoT Backend Running at http://localhost:3000");
});
