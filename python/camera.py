import cv2
import sys

def open_camera():
    """
    Tự động dò tìm hệ điều hành và quét tìm cổng camera khả dụng 
    để đảm bảo không bị lỗi giao diện trong mọi trường hợp.
    """
    # 1. Tự động xác định driver phù hợp theo hệ điều hành (OS)
    if sys.platform == "darwin":  # Hệ điều hành macOS
        backend = cv2.CAP_AVFOUNDATION
        search_ports = [1, 0, 2] # Mac thường ưu tiên webcam rời ở cổng 1 hoặc 0
    elif sys.platform == "win32":  # Hệ điều hành Windows
        backend = cv2.CAP_DSHOW        # DirectShow giúp mở camera trên Windows nhanh và ổn định
        search_ports = [0, 1, 2]
    else:                          # Hệ điều hành Linux (như Raspberry Pi, Ubuntu...)
        backend = cv2.CAP_V4L2
        search_ports = [0, 1, 2]

    # 2. Vòng lặp tự động quét các cổng camera khả dụng
    for port in search_ports:
        print(f"🔍 [Camera] Đang thử kết nối cổng {port} với backend phù hợp...")
        cap = cv2.VideoCapture(port, backend)
        
        # Kiểm tra xem cổng này có mở thành công và đọc được hình ảnh không
        if cap.isOpened():
            ret, frame = cap.read()
            if ret and frame is not None:
                print(f"✅ [Camera] Kết nối THÀNH CÔNG tại cổng: {port}")
                return cap
            cap.release() 

    # 3. Phương án dự phòng cuối cùng (Fallback) nếu thất bại
    print("⚠️ [Camera] Không tìm thấy camera với driver tối ưu. Thử mở mặc định...")
    for port in [0, 1, 2]:
        cap = cv2.VideoCapture(port)
        if cap.isOpened():
            print(f"✅ [Camera] Kết nối thành công bằng chế độ mặc định tại cổng: {port}")
            return cap
            
    print("❌ [Camera] LỖI: Không tìm thấy bất kỳ Camera nào!")
    return cv2.VideoCapture(0)


def open_video(path):
    """Mở file video theo đường dẫn đã chọn từ giao diện."""
    return cv2.VideoCapture(path)