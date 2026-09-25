const BASE_URL = "http://localhost:3000";
const API_URL = `${BASE_URL}/api/data`;
const CONFIG_URL = `${BASE_URL}/api/config`;
const RELAY_STATUS_URL = `${BASE_URL}/api/relay/status`;

const socket = io(BASE_URL, {
    transports: ["websocket"]
});

let notifications = JSON.parse(localStorage.getItem("notifications")) || [];
let lastGasAlert = null;
let lastEsp32ActiveTime = null;
let lastSystemReason = "An toàn";
let currentGasThreshold = 700;

const dotBackend = document.getElementById("statusBackend");
const dotEsp32 = document.getElementById("statusEsp32");

socket.on("connect", () => {
    console.log("✅ SYSTEM SOCKET CONNECTED:", socket.id);
    if (dotBackend) {
        dotBackend.className = "dot dot-green";
    }
});

socket.on("disconnect", () => {
    console.log("❌ System socket disconnected");
    if (dotBackend) dotBackend.className = "dot dot-red";
});

socket.on("sensor-data", (data) => {
    updateSensorUI(data);
    lastEsp32ActiveTime = Date.now(); 
    if (dotEsp32) dotEsp32.className = "dot dot-green";
});

socket.on("system-config", (config) => {
    applyConfigToForm(config);
});

// THÊM TẠI ĐÂY: Lắng nghe tín hiệu từ Backend khi bất kỳ thiết bị nào (App hoặc chính Web) yêu cầu xóa sạch
socket.on("history-cleared", () => {
    console.log("🔄 [SYNC] Nhận lệnh đồng bộ: Xóa sạch lịch sử thông báo hệ thống.");
    notifications = []; // Làm rỗng dữ liệu trên RAM Web
    localStorage.removeItem("notifications"); // Xóa sạch bộ nhớ cục bộ trên Web trình duyệt
    updateNotificationUI(); // Làm mới lại giao diện hiển thị danh sách thông báo trống ngay lập tức
});

setInterval(() => {
    if (lastEsp32ActiveTime) {
        if (Date.now() - lastEsp32ActiveTime > 5000) {
            if (dotEsp32) dotEsp32.className = "dot dot-red";
        }
    } else {
        if (dotEsp32) dotEsp32.className = "dot dot-red";
    }
}, 2000);

async function getData() {
    try {
        const response = await fetch(API_URL);
        const data = await response.json();
        updateSensorUI(data);
        if (data.updatedAt) {
            lastEsp32ActiveTime = Date.now();
            if (dotEsp32) dotEsp32.className = "dot dot-green";
        }
    } catch (error) {
        console.log("Polling error:", error);
    }
}
setInterval(getData, 2000);
getData();

function updateSensorUI(data) {
    const tempEl = document.getElementById("temperature");
    if (tempEl) tempEl.innerHTML = data.temperature + " °C";

    const humEl = document.getElementById("humidity");
    if (humEl) humEl.innerHTML = data.humidity + " %";

    const reasonEl = document.getElementById("alertReason");
    if (reasonEl && data.alertReason) {
        reasonEl.innerHTML = data.alertReason;
        if (data.alertReason.includes("Nguy hiểm")) {
            reasonEl.style.color = "#ff4d4d";
        } else if (data.alertReason.includes("Cảnh báo")) {
            reasonEl.style.color = "#ffa500";
        } else {
            reasonEl.style.color = "#2ecc71";
        }

        if (data.alertReason !== "An toàn" && data.alertReason !== lastSystemReason) {
            const systemAlertObj = {
                text: `<strong>[ESP32 ALERT]</strong><br>${data.alertReason}<br>Thời gian: ${new Date().toLocaleTimeString()}`
            };
            pushNotification(systemAlertObj);
            lastSystemReason = data.alertReason;
        } else if (data.alertReason === "An toàn") {
            lastSystemReason = "An toàn";
        }
    }

    const gasEl = document.getElementById("gas");
    if (gasEl) {
        gasEl.innerHTML = data.gas;
        if (data.gas > Number(data.gasThreshold ?? 700)) {
            gasEl.className = "value warning";
            const msgObj = { text: `⚠ Gas vượt ngưỡng: ${data.gas}` };
            if (msgObj.text !== lastGasAlert) {
                pushNotification(msgObj);
                lastGasAlert = msgObj.text;
            }
        } else {
            gasEl.className = "value safe";
        }
    }

    if (data.comfortTemperature !== undefined) {
        applyConfigToForm({
            comfortTemperature: data.comfortTemperature,
            gasThreshold: data.gasThreshold
        });
    }

    const updatedEl = document.getElementById("updatedAt");
    if (updatedEl) updatedEl.innerHTML = new Date(data.updatedAt).toLocaleTimeString();
}

function applyConfigToForm(config) {
    const comfortInput = document.getElementById("comfortTemperature");
    const gasInput = document.getElementById("gasThreshold");
    const range = document.getElementById("temperatureRange");

    if (comfortInput && document.activeElement !== comfortInput && config.comfortTemperature !== undefined) {
        comfortInput.value = Number(config.comfortTemperature).toFixed(1);
    }
    if (gasInput && document.activeElement !== gasInput && config.gasThreshold !== undefined) {
        gasInput.value = config.gasThreshold;
    }
    if (config.gasThreshold !== undefined) {
        currentGasThreshold = Number(config.gasThreshold);
    }
    if (range && config.comfortTemperature !== undefined) {
        const comfort = Number(config.comfortTemperature);
        range.textContent = `Khoảng quạt: ${(comfort - 2).toFixed(1)}–${(comfort + 2).toFixed(1)}°C`;
    }
}

async function saveComfortTemperature() {
    const comfortInput = document.getElementById("comfortTemperature");
    const status = document.getElementById("configStatus");
    const comfortTemperature = Number(comfortInput?.value);

    if (!Number.isFinite(comfortTemperature) || comfortTemperature < 10 || comfortTemperature > 45) {
        if (status) status.textContent = "Nhập từ 10 đến 45°C";
        return;
    }

    try {
        const response = await fetch(CONFIG_URL, {
            method: "POST",
            headers: { "Content-Type": "application/json" },
            body: JSON.stringify({
                comfortTemperature,
                gasThreshold: currentGasThreshold
            })
        });
        const result = await response.json();
        if (!response.ok) throw new Error(result.message || "Không thể lưu cấu hình");

        applyConfigToForm(result.config);
        if (status) {
            status.textContent = "Đã gửi ESP32";
            status.style.color = "#2ecc71";
        }
    } catch (error) {
        if (status) {
            status.textContent = error.message;
            status.style.color = "#e74c3c";
        }
    }
}

const comfortTemperatureInput = document.getElementById("comfortTemperature");
if (comfortTemperatureInput) {
    comfortTemperatureInput.addEventListener("keydown", (event) => {
        if (event.key === "Enter") {
            event.preventDefault();
            saveComfortTemperature();
            comfortTemperatureInput.blur();
        }
    });
}

async function loadConfig() {
    try {
        const response = await fetch(CONFIG_URL);
        if (response.ok) applyConfigToForm(await response.json());
    } catch (error) {
        console.log("Config loading error:", error);
    }
}

loadConfig();

function pushNotification(alertObj) {
    const last = notifications[notifications.length - 1];
    if (!last || last.text !== alertObj.text) {
        notifications.push(alertObj);
        localStorage.setItem("notifications", JSON.stringify(notifications));
        updateNotificationUI();
    }
}

function updateNotificationUI() {
    const countEl = document.getElementById("notificationCount");
    const list = document.getElementById("notificationList");

    if (!countEl || !list) return;

    countEl.innerText = notifications.length;
    list.innerHTML = "";

    if (notifications.length === 0) {
        const emptyItem = document.createElement("div");
        emptyItem.className = "notification-empty";
        emptyItem.innerText = "Không có thông báo nào !";
        list.appendChild(emptyItem);
        return; 
    }

    notifications.slice(-5).reverse().forEach(itemData => {
        const item = document.createElement("div");
        item.className = "notification-item";
        
        item.innerHTML = `
            <div class="notification-icon">🔔</div>
            <div class="notification-text">${itemData.text}</div>
        `;

        list.appendChild(item);
    });
}

document.addEventListener("DOMContentLoaded", () => {
    const bell = document.getElementById("notificationBell");
    const dropdown = document.getElementById("notificationDropdown");
    if (!bell || !dropdown) return;

    bell.addEventListener("click", (e) => {
        e.stopPropagation();
        dropdown.style.display = dropdown.style.display === "block" ? "none" : "block";
    });
    document.addEventListener("click", () => dropdown.style.display = "none");
});

const viewMoreBtn = document.querySelector(".view-more");
if (viewMoreBtn) {
    viewMoreBtn.addEventListener("click", () => {
        localStorage.setItem("notifications", JSON.stringify(notifications));
        window.location.href = "notifications.html";
    });
}

const alertBtn = document.getElementById("alertBtn");
if (alertBtn) {
    alertBtn.addEventListener("click", async () => {
        try {
            const response = await fetch(`${BASE_URL}/api/buzzer/trigger`, {
                method: "POST"
            });
            const data = await response.json();
            if(data.success) {
                alert("🚨 Đã phát tín hiệu kích hoạt còi hú trên thiết bị ESP32 khẩn cấp!");
            }
        } catch (error) {
            console.log("Error triggering buzzer:", error);
            alert("❌ Không thể kết nối tới server để kích hoạt còi!");
        }
    });
}

// ===== ĐIỀU KHIỂN RELAY TỪ NÚT WEB =====
const relayBtn = document.getElementById("relayBtn");

function updateRelayButton(relayState) {
    if (!relayBtn) return;

    relayBtn.dataset.relayState = Number(relayState) === 1 ? "1" : "0";

    if (Number(relayState) === 1) {
        relayBtn.style.background = "#27ae60";
        relayBtn.innerText = "RELAY: ON (FORCED)";
    } else {
        relayBtn.style.background = "#7f8c8d";
        relayBtn.innerText = "RELAY: AUTO";
    }
}

async function loadRelayStatus() {
    try {
        const response = await fetch(RELAY_STATUS_URL);
        if (response.ok) {
            const data = await response.json();
            updateRelayButton(data.relayState);
        }
    } catch (error) {
        console.log("Relay status loading error:", error);
    }
}

if (relayBtn) {
    relayBtn.addEventListener("click", async () => {
        const currentState = Number(relayBtn.dataset.relayState || 0);
        const nextState = currentState === 1 ? 0 : 1;

        try {
            const response = await fetch(`${BASE_URL}/api/relay/set`, {
                method: "POST",
                headers: {
                    "Content-Type": "application/json"
                },
                body: JSON.stringify({ relayState: nextState })
            });
            const data = await response.json();

            if (!response.ok || !data.success) {
                throw new Error(data.message || "Không thể điều khiển Relay");
            }

            updateRelayButton(data.relayState);
        } catch (error) {
            console.log("Error toggling relay:", error);
            alert("❌ Không thể kết nối tới server để điều khiển Relay!");
        }
    });
}

loadRelayStatus();

// Camera chạy trực tiếp trong trình duyệt và không đi qua backend.
const cameraFeed = document.getElementById("cameraFeed");
const startCameraBtn = document.getElementById("startCameraBtn");
const cameraStatus = document.getElementById("cameraStatus");
let cameraStream = null;

async function startCamera() {
    if (!navigator.mediaDevices?.getUserMedia) {
        if (cameraStatus) cameraStatus.textContent = "Trình duyệt không hỗ trợ camera";
        return;
    }

    try {
        cameraStream = await navigator.mediaDevices.getUserMedia({
            video: { facingMode: "user" },
            audio: false
        });
        if (cameraFeed) cameraFeed.srcObject = cameraStream;
        if (cameraStatus) cameraStatus.textContent = "Camera đang hoạt động";
        if (startCameraBtn) startCameraBtn.textContent = "TẮT CAMERA";
    } catch (error) {
        console.error("Không thể mở camera:", error);
        if (cameraStatus) {
            cameraStatus.textContent = error.name === "NotAllowedError"
                ? "Bạn chưa cấp quyền camera"
                : "Không tìm thấy camera";
        }
    }
}

function stopCamera() {
    cameraStream?.getTracks().forEach(track => track.stop());
    cameraStream = null;
    if (cameraFeed) cameraFeed.srcObject = null;
    if (cameraStatus) cameraStatus.textContent = "Camera đã tắt";
    if (startCameraBtn) startCameraBtn.textContent = "BẬT CAMERA";
}

if (startCameraBtn) {
    startCameraBtn.addEventListener("click", () => {
        cameraStream ? stopCamera() : startCamera();
    });
}

window.addEventListener("beforeunload", stopCamera);

updateNotificationUI();

document.addEventListener("DOMContentLoaded", () => {
    const darkModeToggle = document.getElementById("darkModeToggle");
    const body = document.body;

    if (localStorage.getItem("darkMode") === "enabled") {
        body.classList.add("dark-mode");
        if (darkModeToggle) darkModeToggle.innerText = "☀️";
    }

    if (darkModeToggle) {
        darkModeToggle.addEventListener("click", () => {
            body.classList.toggle("dark-mode");
            if (body.classList.contains("dark-mode")) {
                localStorage.setItem("darkMode", "enabled");
                darkModeToggle.innerText = "☀️";
            } else {
                localStorage.setItem("darkMode", "disabled");
                darkModeToggle.innerText = "🌙";
            }
        });
    }
});
