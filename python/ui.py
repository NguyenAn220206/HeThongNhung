import tkinter as tk
from tkinter import filedialog, ttk
import cv2
from PIL import Image, ImageTk
from detector import detect_all
from camera import open_camera, open_video
from datetime import datetime
from logger import write_log
import time
import os
import requests
import threading
import queue
from collections import deque

# ===== TELEGRAM =====
from telegram import send_telegram_alert

# ===== SERVER =====
SERVER = "http://localhost:3000"

# ===== IMAGE FOLDER =====
IMAGE_FOLDER = "images"


# =========================================================
# GUI ALERTS BACKEND
# =========================================================
def send_alert(
    image_path,
    image_crop_path,
    zone,
    enter_time,
    exit_time,
    duration
):
    filename = os.path.basename(image_path)
    filename_crop = os.path.basename(image_crop_path) if image_crop_path else ""

    payload = {
        "image": filename,
        "imageCrop": filename_crop,
        "zone": zone,
        "enterTime": enter_time,
        "exitTime": exit_time,
        "duration": duration
    }

    try:
        url = f"{SERVER}/api/alert"
        response = requests.post(
            url,
            json=payload,
            timeout=5
        )
        if response.status_code in [200, 201]:
            print("[LOG] DA GUI ALERT LEN BACKEND")
        else:
            print("[WARNING] Backend loi:", response.status_code)
    except Exception as e:
        print("[ERROR] Khong gui duoc alert:", e)


# =========================================================
# MAIN APP CLASS
# =========================================================
class App:

    def __init__(self, root):
        self.root = root
        self.root.title("AI Security Monitoring System")
        self.root.geometry("1100x700")
        self.root.minsize(1024, 680)
        self.root.configure(bg="#1b2733")

        self.style = ttk.Style()
        self.style.theme_use("clam")
        self.style.configure("TLabel", background="#1b2733", foreground="#ffffff")
        self.style.configure("Card.TFrame", background="#1c2a38")
        self.style.configure("Card.TLabel", background="#1c2a38", foreground="#f0f0f0", font=("Segoe UI", 10))
        self.style.configure("Value.TLabel", background="#1c2a38", foreground="#1abc9c", font=("Segoe UI", 12, "bold"))
        self.style.configure(
            "TButton",
            font=("Segoe UI", 10, "bold"),
            padding=8,
            background="#1abc9c",
            foreground="#ffffff",
            relief="flat"
        )
        self.style.map(
            "TButton",
            background=[("active", "#16a085")],
            foreground=[("active", "#ffffff")]
        )
        self.style.configure("Header.TLabel", font=("Segoe UI", 18, "bold"), background="#1b2733", foreground="#ffffff")

        # =================================================
        # THU MUC ANH
        # =================================================
        os.makedirs(IMAGE_FOLDER, exist_ok=True)

        # =================================================
        # HEADER
        # =================================================
        header_frame = tk.Frame(root, bg="#1b2733")
        header_frame.pack(fill=tk.X, padx=15, pady=(15, 0))

        header_label = ttk.Label(header_frame, text="AI Security Monitoring System", style="Header.TLabel")
        header_label.pack(side=tk.LEFT, pady=5)

        self.status_text_var = tk.StringVar(value="Sẵn sàng")
        status_label = ttk.Label(header_frame, textvariable=self.status_text_var, style="Card.TLabel")
        status_label.pack(side=tk.RIGHT, pady=5)

        # =================================================
        # MAIN CONTENT
        # =================================================
        content_frame = tk.Frame(root, bg="#1b2733")
        content_frame.pack(fill=tk.BOTH, expand=True, padx=15, pady=15)
        content_frame.columnconfigure(0, weight=3)
        content_frame.columnconfigure(1, weight=1)
        content_frame.rowconfigure(0, weight=1)

        # =================================================
        # KHUNG HIEN THI CAMERA
        # =================================================
        self.video_frame = tk.Frame(content_frame, bg="#161d27", padx=6, pady=6, bd=0)
        self.video_frame.grid(row=0, column=0, sticky="nsew", padx=(0, 10), pady=0)

        self.label = tk.Label(self.video_frame, bg="#000000", bd=0)
        self.label.pack(fill=tk.BOTH, expand=True)

        # =================================================
        # SIDEBAR DIEU KHIEN
        # =================================================
        control_frame = tk.Frame(content_frame, bg="#1c2a38", bd=0)
        control_frame.grid(row=0, column=1, sticky="nsew")
        control_frame.columnconfigure(0, weight=1)
        control_frame.rowconfigure(0, weight=1)

        sidebar_canvas = tk.Canvas(control_frame, bg="#1c2a38", highlightthickness=0)
        sidebar_canvas.grid(row=0, column=0, sticky="nsew")

        sidebar_scrollbar = ttk.Scrollbar(control_frame, orient="vertical", command=sidebar_canvas.yview)
        sidebar_scrollbar.grid(row=0, column=1, sticky="ns")
        sidebar_canvas.configure(yscrollcommand=sidebar_scrollbar.set)

        scrollable_frame = tk.Frame(sidebar_canvas, bg="#1c2a38")
        scrollable_window = sidebar_canvas.create_window((0, 0), window=scrollable_frame, anchor="nw")

        def _on_frame_configure(event):
            sidebar_canvas.configure(scrollregion=sidebar_canvas.bbox("all"))

        def _on_mousewheel(event):
            sidebar_canvas.yview_scroll(int(-1 * (event.delta / 120)), "units")

        scrollable_frame.bind("<Configure>", _on_frame_configure)
        sidebar_canvas.bind_all("<MouseWheel>", _on_mousewheel)

        source_frame = tk.LabelFrame(scrollable_frame, text="Nguồn camera", bg="#1c2a38", fg="#ffffff", font=("Segoe UI", 11, "bold"), bd=0, relief="flat", labelanchor="n")
        source_frame.pack(fill=tk.X, padx=10, pady=(10, 5))
        source_frame.configure(highlightthickness=0)

        lbl_source = ttk.Label(source_frame, text="Chọn nguồn:", style="Card.TLabel")
        lbl_source.pack(anchor="w", padx=10, pady=(10, 5))

        self.camera_port_var = tk.StringVar(value="Cổng 0 (Mặc định)")
        self.camera_select = ttk.Combobox(
            source_frame,
            textvariable=self.camera_port_var,
            values=["Cổng 0 (Mặc định)", "Cổng 1 (Webcam rời)", "Cổng 2 (Thiết bị khác)"],
            state="readonly",
            width=24
        )
        self.camera_select.pack(anchor="w", padx=10, pady=(0, 10), ipady=3)
        self.camera_select.bind("<<ComboboxSelected>>", self.on_camera_port_changed)

        button_frame = tk.Frame(scrollable_frame, bg="#1c2a38")
        button_frame.pack(fill=tk.X, padx=10, pady=5)

        btn_camera = ttk.Button(button_frame, text="Kích hoạt cam", command=self.use_camera, cursor="hand2")
        btn_camera.pack(fill=tk.X, pady=(0, 8))

        btn_video = ttk.Button(button_frame, text="Chọn file video", command=self.use_video, cursor="hand2")
        btn_video.pack(fill=tk.X, pady=(0, 8))

        self.btn_toggle_stream = ttk.Button(button_frame, text="Tiếp tục", command=self.toggle_stream, cursor="hand2", state="disabled")
        self.btn_toggle_stream.pack(fill=tk.X, pady=(0, 8))

        btn_undo = ttk.Button(button_frame, text="Hoàn tác (Z)", command=self.undo_zone, cursor="hand2")
        btn_undo.pack(fill=tk.X, pady=(0, 8))

        btn_clear = ttk.Button(button_frame, text="Xóa vùng (C)", command=self.clear_zones, cursor="hand2")
        btn_clear.pack(fill=tk.X, pady=(0, 8))

        status_frame = tk.LabelFrame(scrollable_frame, text="Trạng thái hệ thống", bg="#1c2a38", fg="#ffffff", font=("Segoe UI", 11, "bold"), bd=0, relief="flat", labelanchor="n")
        status_frame.pack(fill=tk.X, padx=10, pady=(10, 5))
        status_frame.configure(highlightthickness=0)

        self.video_status_var = tk.StringVar(value="Offline")
        self.fps_var = tk.StringVar(value="0 FPS")
        self.frame_size_var = tk.StringVar(value="0x0")

        ttk.Label(status_frame, text="Trạng thái video:", style="Card.TLabel").pack(anchor="w", padx=10, pady=(10, 2))
        ttk.Label(status_frame, textvariable=self.video_status_var, style="Value.TLabel").pack(anchor="w", padx=10)

        ttk.Label(status_frame, text="Kích thước khung:", style="Card.TLabel").pack(anchor="w", padx=10, pady=(10, 2))
        ttk.Label(status_frame, textvariable=self.frame_size_var, style="Value.TLabel").pack(anchor="w", padx=10)

        ttk.Label(status_frame, text="FPS trung bình:", style="Card.TLabel").pack(anchor="w", padx=10, pady=(10, 2))
        ttk.Label(status_frame, textvariable=self.fps_var, style="Value.TLabel").pack(anchor="w", padx=10)

        info_frame = tk.LabelFrame(scrollable_frame, text="Thông tin", bg="#1c2a38", fg="#ffffff", font=("Segoe UI", 11, "bold"), bd=0, relief="flat", labelanchor="n")
        info_frame.pack(fill=tk.BOTH, padx=10, pady=10, expand=True)
        info_frame.configure(highlightthickness=0)

        self.people_count_var = tk.StringVar(value="0")
        self.zone_count_var = tk.StringVar(value="0")
        self.alert_count_var = tk.StringVar(value="0")

        ttk.Label(info_frame, text="Số người đang phát hiện:", style="Card.TLabel").pack(anchor="w", padx=10, pady=(10, 2))
        ttk.Label(info_frame, textvariable=self.people_count_var, style="Value.TLabel").pack(anchor="w", padx=10)

        ttk.Label(info_frame, text="Số vùng giám sát:", style="Card.TLabel").pack(anchor="w", padx=10, pady=(10, 2))
        ttk.Label(info_frame, textvariable=self.zone_count_var, style="Value.TLabel").pack(anchor="w", padx=10)

        ttk.Label(info_frame, text="Trạng thái cảnh báo:", style="Card.TLabel").pack(anchor="w", padx=10, pady=(10, 2))
        ttk.Label(info_frame, textvariable=self.alert_count_var, style="Value.TLabel").pack(anchor="w", padx=10)

        guidance_label = ttk.Label(scrollable_frame, text="Kéo và thả vùng trên khung video để tạo zone giám sát.", style="Card.TLabel", wraplength=240, justify="left")
        guidance_label.pack(fill=tk.X, padx=10, pady=(5, 10))

        history_frame = tk.LabelFrame(scrollable_frame, text="Lịch sử cảnh báo", bg="#1c2a38", fg="#ffffff", font=("Segoe UI", 11, "bold"), bd=0, relief="flat", labelanchor="n")
        history_frame.pack(fill=tk.BOTH, padx=10, pady=(0, 10), expand=True)
        history_frame.configure(highlightthickness=0)

        self.history_listbox = tk.Listbox(history_frame, bg="#16212b", fg="#ffffff", bd=0, highlightthickness=0, activestyle="none", selectbackground="#0a627b", font=("Segoe UI", 10), height=6)
        self.history_listbox.pack(fill=tk.BOTH, padx=10, pady=10, expand=True)

        history_scrollbar = ttk.Scrollbar(history_frame, orient="vertical", command=self.history_listbox.yview)
        history_scrollbar.place(relx=0.97, rely=0.05, relheight=0.9)
        self.history_listbox.config(yscrollcommand=history_scrollbar.set)

        # =================================================
        # CONTROL CAMERA & THREADING
        # =================================================
        self.cap = None
        self.delay = 15              
        self.running = False         
        self.latest_frame = None     
        self.people = []             
        self.lock = threading.Lock()
        self.run_mode = "idle"
        self.last_source = None
        self.processing_thread = None
        self.recent_events = deque(maxlen=8)
        self.last_frame_time = time.time()
        self.frame_count = 0
        self.current_fps = 0.0

        # CÁC BIẾN OFFSET ĐỂ FIX TRỎ CHUỘT LỆCH
        self.offset_x = 0
        self.offset_y = 0
        self.current_w_resized = 1
        self.current_h_resized = 1

        # FIX LAG: CACHE KÍCH THƯỚC GỐC CAMERA (KHÔNG TRUY VẤN Ở MỖI FRAME)
        self.orig_w = 0
        self.orig_h = 0

        # =================================================
        # QUEUE STREAM WORKER
        # =================================================
        self.stream_queue = queue.Queue(maxsize=2) 
        self.stream_running = True
        self.stream_thread = threading.Thread(target=self.mjpeg_stream_worker, daemon=True)
        self.stream_thread.start()

        # =================================================
        # ZONE DATA
        # =================================================
        self.zones = []
        self.zone_states = []
        self.zone_times = []
        self.zone_captured = []
        self.zone_tg_reported = []

        # =================================================
        # DRAW
        # =================================================
        self.start_point = None
        self.current_point = None
        self.drawing = False

        # =================================================
        # ALERT UI
        # =================================================
        self.show_alert = False
        self.alert_frames = 0

        # =================================================
        # SOCKET
        # =================================================
        import socketio
        self.sio = socketio.Client()

        @self.sio.event
        def connect():
            print("[LOG] [Socket] Connected to backend")

        @self.sio.on("capture-image")
        def on_capture(data):
            print("[LOG] [Socket] Server yeu cau chup anh")
            self.capture_from_server()

        threading.Thread(target=self.start_socket, daemon=True).start()

        # =================================================
        # MOUSE EVENTS
        # =================================================
        self.label.bind("<ButtonPress-1>", self.mouse_down)
        self.label.bind("<B1-Motion>", self.mouse_drag)
        self.label.bind("<ButtonRelease-1>", self.mouse_up)

        # =================================================
        # HOTKEY
        # =================================================
        self.root.bind("c", self.clear_zones)
        self.root.bind("z", self.undo_zone)
        self.root.protocol("WM_DELETE_WINDOW", self.on_closing)

    def mjpeg_stream_worker(self):
        session = requests.Session() 
        while self.stream_running:
            try:
                encoded_bytes = self.stream_queue.get(timeout=1.0)
                try:
                    session.post(
                        f"{SERVER}/api/video-stream", 
                        data=encoded_bytes, 
                        headers={'Content-Type': 'image/jpeg'}, 
                        timeout=0.03
                    )
                except Exception:
                    pass 
                self.stream_queue.task_done()
            except queue.Empty:
                continue

    def on_camera_port_changed(self, event):
        if self.cap and self.running:
            print(f"[LOG] Nguoi dung chuyen nguon sang: {self.camera_port_var.get()}")
            self.use_camera()

    def start_socket(self):
        try:
            self.sio.connect(SERVER, headers={"User-Agent": "Python AI Edge"})
            self.sio.wait()
        except Exception as e:
            print("[ERROR] [Socket] Loi ket noi:", e)

    def capture_from_server(self):
        if not self.cap: return
        ret, frame = self.cap.read()
        if not ret: return

        filename = f"{IMAGE_FOLDER}/server_capture_{int(time.time())}.jpg"
        cv2.imwrite(filename, frame)
        print("[LOG] Da chup anh theo yeu cau:", filename)

        tg_msg = f"[LOG] Server yeu cau chup anh luc {datetime.now().strftime('%H:%M:%S')}"
        threading.Thread(target=send_telegram_alert, args=(tg_msg, filename), daemon=True).start()

    def add_history_entry(self, message):
        timestamp = datetime.now().strftime('%H:%M:%S')
        entry = f"[{timestamp}] {message}"
        self.recent_events.appendleft(entry)
        self.history_listbox.delete(0, tk.END)
        for item in self.recent_events:
            self.history_listbox.insert(tk.END, item)

    def update_dashboard(self, status=None):
        if status:
            self.status_text_var.set(status)
            self.add_history_entry(status)

        self.people_count_var.set(str(len(self.people)))
        self.zone_count_var.set(str(len(self.zones)))
        self.alert_count_var.set(str(sum(1 for state in self.zone_states if state)))
        self.video_status_var.set("Đang chạy" if self.running else ("Tạm dừng" if self.last_source else "Offline"))
        self.fps_var.set(f"{self.current_fps:.1f} FPS")

        if self.latest_frame is not None:
            height, width = self.latest_frame.shape[:2]
            self.frame_size_var.set(f"{width}x{height}")
        else:
            self.frame_size_var.set("0x0")

        if hasattr(self, 'btn_toggle_stream'):
            if self.running:
                self.btn_toggle_stream.configure(text="Dừng hiển thị", state="normal")
            elif self.last_source:
                self.btn_toggle_stream.configure(text="Tiếp tục", state="normal")
            else:
                self.btn_toggle_stream.configure(text="Tiếp tục", state="disabled")

    def toggle_stream(self):
        if self.running:
            self.running = False
            self.update_dashboard(status="Đã tạm dừng hiển thị")
        elif self.cap:
            self.running = True
            if not self.processing_thread or not self.processing_thread.is_alive():
                self.processing_thread = threading.Thread(target=self.video_processing_thread, daemon=True)
                self.processing_thread.start()
            self.update_dashboard(status="Tiếp tục hiển thị")
        elif self.last_source:
            if self.last_source.get("type") == "camera":
                self._open_camera_source(self.last_source.get("port", 0))
            elif self.last_source.get("type") == "video":
                self._open_video_source(self.last_source.get("path"))
            self.update_dashboard(status="Tiếp tục hiển thị")

    def on_closing(self):
        self.stream_running = False
        self.running = False
        if self.cap:
            try:
                self.cap.release()
            except Exception:
                pass
        self.root.destroy()

    def video_processing_thread(self):
        while self.running:
            if self.latest_frame is not None:
                with self.lock:
                    frame_to_detect = self.latest_frame.copy()
                
                _, detected_people = detect_all(frame_to_detect)
                
                with self.lock:
                    self.people = detected_people
            time.sleep(0.03)

    def _open_camera_source(self, port_index):
        self.running = False
        if self.cap:
            try:
                self.cap.release()
            except Exception:
                pass

        import sys
        if sys.platform == "darwin":
            backend = cv2.CAP_AVFOUNDATION
        elif sys.platform == "win32":
            backend = cv2.CAP_DSHOW
        else:
            backend = cv2.CAP_V4L2

        print(f"[LOG] Khoi chay cong {port_index}...")
        self.cap = cv2.VideoCapture(port_index, backend)
        if not self.cap.isOpened():
            self.cap = cv2.VideoCapture(port_index)

        # LẤY VÀ LƯU TRỮ KÍCH THƯỚC GỐC CAMERA NGAY TỪ ĐẦU (FIX LAG)
        self.orig_w = int(self.cap.get(cv2.CAP_PROP_FRAME_WIDTH))
        self.orig_h = int(self.cap.get(cv2.CAP_PROP_FRAME_HEIGHT))

        self.last_source = {"type": "camera", "port": port_index}
        self.reset_zones()
        self.running = True
        self.people = []
        self.latest_frame = None
        self.update_dashboard(status="Đang kích hoạt camera")
        if not self.processing_thread or not self.processing_thread.is_alive():
            self.processing_thread = threading.Thread(target=self.video_processing_thread, daemon=True)
            self.processing_thread.start()
        self.update()

    def _open_video_source(self, path):
        self.running = False
        if self.cap:
            try:
                self.cap.release()
            except Exception:
                pass

        self.cap = open_video(path)
        
        # LẤY VÀ LƯU TRỮ KÍCH THƯỚC GỐC VIDEO NGAY TỪ ĐẦU (FIX LAG)
        self.orig_w = int(self.cap.get(cv2.CAP_PROP_FRAME_WIDTH))
        self.orig_h = int(self.cap.get(cv2.CAP_PROP_FRAME_HEIGHT))

        self.last_source = {"type": "video", "path": path}
        self.reset_zones()
        self.running = True
        self.people = []
        self.latest_frame = None
        self.update_dashboard(status="Đang chạy video")
        if not self.processing_thread or not self.processing_thread.is_alive():
            self.processing_thread = threading.Thread(target=self.video_processing_thread, daemon=True)
            self.processing_thread.start()
        self.update()

    def use_camera(self):
        selected_text = self.camera_port_var.get()
        try:
            port_index = int(selected_text.split(" ")[1])
        except Exception:
            port_index = 0
        self._open_camera_source(port_index)

    def use_video(self):
        path = filedialog.askopenfilename()
        if path:
            self._open_video_source(path)

    def reset_zones(self):
        self.zones, self.zone_states, self.zone_times, self.zone_captured, self.zone_tg_reported = [], [], [], [], []

    # LẤY TOẠ ĐỘ TRỰC TIẾP TRÊN KHUNG RESIZED
    def get_local_coordinates(self, event_x, event_y):
        local_x = event_x - self.offset_x
        local_y = event_y - self.offset_y
        
        local_x = max(0, min(local_x, self.current_w_resized - 1))
        local_y = max(0, min(local_y, self.current_h_resized - 1))
        return local_x, local_y

    def mouse_down(self, event):
        lx, ly = self.get_local_coordinates(event.x, event.y)
        self.start_point = (lx, ly)
        self.current_point = (lx, ly)
        self.drawing = True

    def mouse_drag(self, event):
        if self.drawing:
            lx, ly = self.get_local_coordinates(event.x, event.y)
            self.current_point = (lx, ly)

    def mouse_up(self, event):
        if self.start_point and self.current_point:
            x1, y1 = self.start_point
            x2, y2 = self.current_point
            zone = (min(x1, x2), min(y1, y2), max(x1, x2), max(y1, y2))
            width = zone[2] - zone[0]
            height = zone[3] - zone[1]
            if width >= 30 and height >= 30:
                self.zones.append(zone)
                self.zone_states.append(False)
                self.zone_times.append(None)
                self.zone_captured.append(False)
                self.zone_tg_reported.append(False)
                self.update_dashboard(status=f"Đã thêm vùng giám sát #{len(self.zones)}")
        self.start_point, self.current_point, self.drawing = None, None, False

    def clear_zones(self, event=None):
        self.reset_zones()
        self.update_dashboard(status="Đã xóa vùng giám sát")

    def undo_zone(self, event=None):
        if self.zones:
            self.zones.pop()
            self.zone_states.pop()
            self.zone_times.pop()
            self.zone_captured.pop()
            self.zone_tg_reported.pop()
        self.update_dashboard(status="Hoàn tác vùng giám sát")

    def update(self):
        frame = None
        people = []
        if self.cap and self.running:
            ret, frame = self.cap.read()
            if ret and frame is not None:
                original_h, original_w = frame.shape[:2]
                display_w = self.label.winfo_width() or 800
                display_h = self.label.winfo_height() or 600
                
                scale = min(display_w / original_w, display_h / original_h)
                if scale <= 0: scale = 1.0
                new_w = max(1, int(original_w * scale))
                new_h = max(1, int(original_h * scale))
                
                if (new_w, new_h) != (original_w, original_h):
                    frame = cv2.resize(frame, (new_w, new_h))

                self.current_w_resized = new_w
                self.current_h_resized = new_h
                self.offset_x = (display_w - new_w) // 2
                self.offset_y = (display_h - new_h) // 2

                with self.lock:
                    self.latest_frame = frame.copy()
                    people = self.people.copy()

                now = time.time()
                self.frame_count += 1
                if now - self.last_frame_time >= 1.0:
                    self.current_fps = self.frame_count / (now - self.last_frame_time)
                    self.frame_count = 0
                    self.last_frame_time = now

                for (x1, y1, x2, y2, cx, cy) in people:
                    cv2.rectangle(frame, (x1, y1), (x2, y2), (0, 255, 0), 2)
                    cv2.circle(frame, (cx, cy), 4, (0, 255, 0), -1)

                for i, zone in enumerate(self.zones):
                    zx1, zy1, zx2, zy2 = zone
                    count = 0
                    for (_, _, _, _, cx, cy) in people:
                        if zx1 < cx < zx2 and zy1 < cy < zy2: count += 1

                    prev_state = self.zone_states[i]
                    current_state = count > 0

                    if current_state and not prev_state:
                        self.show_alert, self.alert_frames = True, 20
                        self.zone_times[i] = time.time()
                        self.zone_captured[i], self.zone_tg_reported[i] = False, False

                    if current_state and not self.zone_tg_reported[i]:
                        tg_msg = f"[WARNING] Phat hien nguoi vao Zone {i+1}"
                        threading.Thread(target=send_telegram_alert, args=(tg_msg,), daemon=True).start()
                        self.zone_tg_reported[i] = True

                    if current_state and self.zone_times[i]:
                        duration = time.time() - self.zone_times[i]
                        if duration >= 3 and not self.zone_captured[i]:
                            timestamp = int(time.time())
                            capture_frame = frame.copy()
                            cv2.rectangle(capture_frame, (zx1, zy1), (zx2, zy2), (0, 0, 255), 3)
                            cv2.putText(capture_frame, f"Zone {i+1}", (zx1, zy1 - 10), cv2.FONT_HERSHEY_SIMPLEX, 0.8, (0, 0, 255), 2)
                            full_path = f"{IMAGE_FOLDER}/zone_{i+1}_{timestamp}.jpg"
                            cv2.imwrite(full_path, capture_frame)

                            crop = frame[zy1:zy2, zx1:zx2]
                            crop_path = f"{IMAGE_FOLDER}/zone_{i+1}_{timestamp}_crop.jpg"
                            if crop.size > 0:
                                cv2.imwrite(crop_path, crop)

                            self.zone_captured[i] = True
                            tg_danger_msg = f"[ERROR] Nguoi o Zone {i+1} qua 3 giay!"
                            threading.Thread(target=send_telegram_alert, args=(tg_danger_msg, full_path), daemon=True).start()

                    if not current_state and prev_state:
                        start = self.zone_times[i]
                        if start:
                            end_time = time.time()
                            duration = round(end_time - start, 2)
                            enter_time_str = datetime.fromtimestamp(start).strftime('%Y-%m-%d %H:%M:%S')
                            exit_time_str = datetime.fromtimestamp(end_time).strftime('%Y-%m-%d %H:%M:%S')

                            write_log(i + 1, datetime.fromtimestamp(start), datetime.fromtimestamp(end_time))

                            latest_image, latest_crop = "", ""
                            if self.zone_captured[i] and os.path.exists(IMAGE_FOLDER):
                                files = sorted(
                                    [f for f in os.listdir(IMAGE_FOLDER) if (f.startswith(f"zone_{i+1}_") and f.endswith(".jpg") and "_crop" not in f)],
                                    key=lambda x: os.path.getmtime(os.path.join(IMAGE_FOLDER, x)),
                                    reverse=True
                                )
                                if files:
                                    latest_image = os.path.join(IMAGE_FOLDER, files[0])
                                    latest_crop = os.path.join(IMAGE_FOLDER, files[0].replace(".jpg", "_crop.jpg"))

                            threading.Thread(target=send_alert, args=(latest_image, latest_crop, i + 1, enter_time_str, exit_time_str, duration), daemon=True).start()

                        self.zone_times[i], self.zone_captured[i], self.zone_tg_reported[i] = None, False, False

                    self.zone_states[i] = current_state
                    color = (0, 0, 255) if current_state else (255, 0, 0)
                    cv2.rectangle(frame, (zx1, zy1), (zx2, zy2), color, 2)
                    cv2.putText(frame, f"Zone {i+1}: {count}", (zx1, zy1 - 10), cv2.FONT_HERSHEY_SIMPLEX, 0.6, (0, 255, 255), 2)

                if self.show_alert:
                    cv2.rectangle(frame, (200, 10), (600, 80), (0, 0, 0), -1)
                    cv2.putText(frame, "CO NGUOI", (220, 60), cv2.FONT_HERSHEY_SIMPLEX, 1.5, (0, 0, 255), 4)
                    self.alert_frames -= 1
                    if self.alert_frames <= 0: self.show_alert = False

                if self.drawing and self.start_point and self.current_point:
                    x1, y1 = self.start_point
                    x2, y2 = self.current_point
                    cv2.rectangle(frame, (x1, y1), (x2, y2), (0, 255, 255), 2)

                self.update_dashboard()

                ret_enc, encoded_img = cv2.imencode('.jpg', frame, [int(cv2.IMWRITE_JPEG_QUALITY), 65])
                if ret_enc:
                    if self.stream_queue.full():
                        try: self.stream_queue.get_nowait()
                        except queue.Empty: pass
                    try: self.stream_queue.put_nowait(encoded_img.tobytes())
                    except queue.Full: pass

                frame = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)
                img = Image.fromarray(frame)
                imgtk = ImageTk.PhotoImage(image=img)
                self.label.imgtk = imgtk
                self.label.configure(image=imgtk)

        elif self.latest_frame is not None:
            frame = self.latest_frame.copy()
            with self.lock:
                people = self.people.copy()

            for (x1, y1, x2, y2, cx, cy) in people:
                cv2.rectangle(frame, (x1, y1), (x2, y2), (0, 255, 0), 2)
                cv2.circle(frame, (cx, cy), 4, (0, 255, 0), -1)

            for i, zone in enumerate(self.zones):
                zx1, zy1, zx2, zy2 = zone
                count = 0
                for (_, _, _, _, cx, cy) in people:
                    if zx1 < cx < zx2 and zy1 < cy < zy2: count += 1

                prev_state = self.zone_states[i]
                current_state = count > 0

                if current_state and not prev_state:
                    self.show_alert, self.alert_frames = True, 20
                    self.zone_times[i] = time.time()
                    self.zone_captured[i], self.zone_tg_reported[i] = False, False

                if current_state and not self.zone_tg_reported[i]:
                    tg_msg = f"[WARNING] Phat hien nguoi vao Zone {i+1}"
                    threading.Thread(target=send_telegram_alert, args=(tg_msg,), daemon=True).start()
                    self.zone_tg_reported[i] = True

                if current_state and self.zone_times[i]:
                    duration = time.time() - self.zone_times[i]
                    if duration >= 3 and not self.zone_captured[i]:
                        timestamp = int(time.time())
                        capture_frame = frame.copy()
                        cv2.rectangle(capture_frame, (zx1, zy1), (zx2, zy2), (0, 0, 255), 3)
                        cv2.putText(capture_frame, f"Zone {i+1}", (zx1, zy1 - 10), cv2.FONT_HERSHEY_SIMPLEX, 0.8, (0, 0, 255), 2)
                        full_path = f"{IMAGE_FOLDER}/zone_{i+1}_{timestamp}.jpg"
                        cv2.imwrite(full_path, capture_frame)

                        crop = frame[zy1:zy2, zx1:zx2]
                        crop_path = f"{IMAGE_FOLDER}/zone_{i+1}_{timestamp}_crop.jpg"
                        if crop.size > 0:
                            cv2.imwrite(crop_path, crop)

                        self.zone_captured[i] = True
                        tg_danger_msg = f"[ERROR] Nguoi o Zone {i+1} qua 3 giay!"
                        threading.Thread(target=send_telegram_alert, args=(tg_danger_msg, full_path), daemon=True).start()

                if not current_state and prev_state:
                    start = self.zone_times[i]
                    if start:
                        end_time = time.time()
                        duration = round(end_time - start, 2)
                        enter_time_str = datetime.fromtimestamp(start).strftime('%Y-%m-%d %H:%M:%S')
                        exit_time_str = datetime.fromtimestamp(end_time).strftime('%Y-%m-%d %H:%M:%S')

                        write_log(i + 1, datetime.fromtimestamp(start), datetime.fromtimestamp(end_time))

                        latest_image, latest_crop = "", ""
                        if self.zone_captured[i] and os.path.exists(IMAGE_FOLDER):
                            files = sorted(
                                [f for f in os.listdir(IMAGE_FOLDER) if (f.startswith(f"zone_{i+1}_") and f.endswith(".jpg") and "_crop" not in f)],
                                key=lambda x: os.path.getmtime(os.path.join(IMAGE_FOLDER, x)),
                                reverse=True
                            )
                            if files:
                                latest_image = os.path.join(IMAGE_FOLDER, files[0])
                                latest_crop = os.path.join(IMAGE_FOLDER, files[0].replace(".jpg", "_crop.jpg"))

                        threading.Thread(target=send_alert, args=(latest_image, latest_crop, i + 1, enter_time_str, exit_time_str, duration), daemon=True).start()

                    self.zone_times[i], self.zone_captured[i], self.zone_tg_reported[i] = None, False, False

                self.zone_states[i] = current_state
                color = (0, 0, 255) if current_state else (255, 0, 0)
                cv2.rectangle(frame, (zx1, zy1), (zx2, zy2), color, 2)
                cv2.putText(frame, f"Zone {i+1}: {count}", (zx1, zy1 - 10), cv2.FONT_HERSHEY_SIMPLEX, 0.6, (0, 255, 255), 2)

            if self.show_alert:
                cv2.rectangle(frame, (200, 10), (600, 80), (0, 0, 0), -1)
                cv2.putText(frame, "CO NGUOI", (220, 60), cv2.FONT_HERSHEY_SIMPLEX, 1.5, (0, 0, 255), 4)
                self.alert_frames -= 1
                if self.alert_frames <= 0: self.show_alert = False

            if self.drawing and self.start_point and self.current_point:
                x1, y1 = self.start_point
                x2, y2 = self.current_point
                cv2.rectangle(frame, (x1, y1), (x2, y2), (0, 255, 255), 2)

            self.update_dashboard()

            ret_enc, encoded_img = cv2.imencode('.jpg', frame, [int(cv2.IMWRITE_JPEG_QUALITY), 65])
            if ret_enc:
                if self.stream_queue.full():
                    try: self.stream_queue.get_nowait()
                    except queue.Empty: pass
                try: self.stream_queue.put_nowait(encoded_img.tobytes())
                except queue.Full: pass

            frame = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)
            img = Image.fromarray(frame)
            imgtk = ImageTk.PhotoImage(image=img)
            self.label.imgtk = imgtk
            self.label.configure(image=imgtk)

        self.root.after(self.delay, self.update)