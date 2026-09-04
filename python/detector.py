from ultralytics import YOLO

model = YOLO("models/yolov8n.pt")

def detect_all(frame):
    results = model(frame, verbose=False)

    people = []

    for r in results:
        for box in r.boxes:
            if int(box.cls[0]) == 0:  # person
                x1, y1, x2, y2 = map(int, box.xyxy[0])
                cx = (x1 + x2) // 2
                cy = (y1 + y2) // 2
                people.append((x1, y1, x2, y2, cx, cy))

    return frame, people