import os
import requests

# ===== CẤU HÌNH TELEGRAM =====
TELEGRAM_BOT_TOKEN = "8966373592:AAF4IwUTWFC0Ln9ElfOoa8xfz9ca45EsO2Y"
TELEGRAM_CHAT_ID = "-5036187167"

def send_telegram_alert(message, image_path=None):
    """
    Gửi cảnh báo đến Telegram (Hỗ trợ gửi tin nhắn thuần hoặc kèm hình ảnh).
    Chạy độc lập trong Thread để không gây ảnh hưởng đến luồng xử lý chính.
    """
    # Kiểm tra cấu hình hợp lệ chống chạy lỗi
    if not TELEGRAM_BOT_TOKEN or not TELEGRAM_CHAT_ID or "YOUR_BOT_TOKEN" in TELEGRAM_BOT_TOKEN:
        print("[WARNING] [Telegram] Chưa cấu hình đầy đủ BOT_TOKEN hoặc CHAT_ID trong file telegram.py!")
        return False

    try:
        # Trường hợp 1: Có đính kèm hình ảnh và file ảnh tồn tại trên ổ cứng
        if image_path and os.path.exists(image_path):
            url = f"https://api.telegram.org/bot{TELEGRAM_BOT_TOKEN}/sendPhoto"
            payload = {
                "chat_id": TELEGRAM_CHAT_ID, 
                "caption": message,
                "parse_mode": "Markdown" 
            }
            with open(image_path, "rb") as image_file:
                files = {"photo": image_file}
                response = requests.post(url, data=payload, files=files, timeout=10)
        
        # Trường hợp 2: Chỉ gửi nội dung tin nhắn dạng văn bản thuần
        else:
            url = f"https://api.telegram.org/bot{TELEGRAM_BOT_TOKEN}/sendMessage"
            payload = {
                "chat_id": TELEGRAM_CHAT_ID, 
                "text": message,
                "parse_mode": "Markdown"
            }
            response = requests.post(url, json=payload, timeout=10)

        # Kiểm tra phản hồi từ Telegram Server
        if response.status_code == 200:
            print("[LOG] [Telegram] Đã gửi cảnh báo thành công!")
            return True
        else:
            print(f"[WARNING] [Telegram] Gửi thất bại. Telegram phản hồi mã lỗi: {response.status_code}")
            return False
            
    except requests.exceptions.RequestException as e:
        print("[ERROR] [Telegram] Lỗi kết nối mạng khi gửi cảnh báo:", e)
        return False