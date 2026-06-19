from picamera2 import Picamera2
import cv2
import time
from pathlib import Path

try:
    import serial
except ImportError:
    serial = None


SERIAL_PORT = "/dev/ttyUSB0"
BAUDRATE = 9600

CAMERA_WIDTH = 320
CAMERA_HEIGHT = 240

FACE_SCALE_FACTOR = 1.15
FACE_MIN_NEIGHBORS = 7
FACE_MIN_SIZE = (50, 50)

FACE_CLEAR_TIME = 2.5
FACE_CONFIRM_FRAMES = 3
STOP_REPEAT_INTERVAL = 0.4

SHOW_CAMERA = True


class ArduinoSerial:
    def __init__(self, port, baudrate):
        self.ser = None
        self.last_command = None
        self.last_send_time = 0

        if serial is None:
            print("pyserial yok, Arduino komutu gonderilmeyecek.")
            return

        try:
            self.ser = serial.Serial(port, baudrate, timeout=0.1)
            time.sleep(2)
            print("Arduino baglandi:", port)
        except Exception as e:
            print("Arduino baglanamadi:", e)

    def send(self, command, force=False):
        now = time.time()

        if not force:
            if command == self.last_command and now - self.last_send_time < STOP_REPEAT_INTERVAL:
                return

        self.last_command = command
        self.last_send_time = now

        print("ARDUINO ->", command)

        if self.ser is not None:
            self.ser.write((command + "\n").encode("ascii"))
            self.ser.flush()

    def close(self):
        if self.ser is not None:
            self.ser.close()


class FaceDetector:
    def __init__(self):
        cascade_path = self.find_cascade_path()
        self.detector = cv2.CascadeClassifier(cascade_path)

        if self.detector.empty():
            raise RuntimeError("Yuz cascade dosyasi yuklenemedi.")

        print("Cascade:", cascade_path)
        print("OpenCV yuz detector yuklendi.")

    def find_cascade_path(self):
        candidates = []

        if hasattr(cv2, "data") and hasattr(cv2.data, "haarcascades"):
            candidates.append(
                Path(cv2.data.haarcascades) / "haarcascade_frontalface_default.xml"
            )

        candidates.extend([
            Path("/usr/share/opencv4/haarcascades/haarcascade_frontalface_default.xml"),
            Path("/usr/share/opencv/haarcascades/haarcascade_frontalface_default.xml"),
            Path("/usr/local/share/opencv4/haarcascades/haarcascade_frontalface_default.xml"),
            Path("/usr/local/share/opencv/haarcascades/haarcascade_frontalface_default.xml"),
        ])

        for path in candidates:
            if path.exists():
                return str(path)

        raise RuntimeError("haarcascade_frontalface_default.xml bulunamadi.")

    def detect_largest_face(self, frame_rgb):
        gray = cv2.cvtColor(frame_rgb, cv2.COLOR_RGB2GRAY)
        gray = cv2.equalizeHist(gray)

        faces = self.detector.detectMultiScale(
            gray,
            scaleFactor=FACE_SCALE_FACTOR,
            minNeighbors=FACE_MIN_NEIGHBORS,
            minSize=FACE_MIN_SIZE,
        )

        if len(faces) == 0:
            return None

        x, y, w, h = max(faces, key=lambda face: face[2] * face[3])
        return (int(x), int(y), int(x + w), int(y + h))


def draw_face(frame_bgr, face_box, fps, face_active, face_confirm_count):
    status = "FACE: YES" if face_active else "FACE: NO"
    color = (0, 0, 255) if face_active else (0, 255, 0)

    cv2.putText(
        frame_bgr,
        f"{status} C:{face_confirm_count}/{FACE_CONFIRM_FRAMES} FPS:{fps:.1f}",
        (10, 24),
        cv2.FONT_HERSHEY_SIMPLEX,
        0.6,
        color,
        2,
    )

    if face_box is None:
        return

    x1, y1, x2, y2 = face_box
    box_color = (0, 0, 255) if face_active else (0, 255, 255)
    cv2.rectangle(frame_bgr, (x1, y1), (x2, y2), box_color, 2)
    cv2.putText(
        frame_bgr,
        "face" if face_active else "candidate",
        (x1, max(20, y1 - 8)),
        cv2.FONT_HERSHEY_SIMPLEX,
        0.6,
        box_color,
        2,
    )


def main():
    detector = FaceDetector()
    arduino = ArduinoSerial(SERIAL_PORT, BAUDRATE)

    picam2 = Picamera2()
    config = picam2.create_preview_configuration(
        main={"size": (CAMERA_WIDTH, CAMERA_HEIGHT), "format": "RGB888"}
    )
    picam2.configure(config)
    picam2.start()

    face_active = False
    last_face_seen = 0
    face_confirm_count = 0
    last_frame_time = time.time()
    fps = 0.0

    print("Kamera basladi. Yuz varsa durur, yuz yoksa Arduino serit takibe devam eder.")
    print("Cikmak icin q.")

    try:
        while True:
            frame_rgb = picam2.capture_array()
            now = time.time()

            dt = now - last_frame_time
            last_frame_time = now
            if dt > 0:
                fps = 0.9 * fps + 0.1 * (1.0 / dt)

            face_box = detector.detect_largest_face(frame_rgb)
            face_seen = face_box is not None

            if face_seen:
                if face_confirm_count < FACE_CONFIRM_FRAMES:
                    face_confirm_count += 1
                last_face_seen = now

                if face_confirm_count >= FACE_CONFIRM_FRAMES:
                    face_active = True
                    arduino.send("STOP_HUMAN")

            elif face_active and now - last_face_seen > FACE_CLEAR_TIME:
                face_active = False
                face_confirm_count = 0
                arduino.send("FOLLOW", force=True)
            elif not face_active:
                face_confirm_count = 0

            if SHOW_CAMERA:
                frame_bgr = cv2.cvtColor(frame_rgb, cv2.COLOR_RGB2BGR)
                draw_face(frame_bgr, face_box, fps, face_active, face_confirm_count)
                cv2.imshow("Camera", frame_bgr)

                if cv2.waitKey(1) == ord("q"):
                    break

    finally:
        arduino.send("FOLLOW", force=True)
        arduino.close()
        picam2.stop()
        cv2.destroyAllWindows()


if __name__ == "__main__":
    main()
