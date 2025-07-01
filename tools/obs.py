import cv2
import numpy as np
import subprocess
import threading
import time
import signal
import sys

# ------------------- CONFIG -------------------
W, H = 256, 256  # image resolution (must match OBS)
URI = 'tcp://127.0.0.1:3100'  # Changed to TCP
FFMPEG_BIN = 'ffmpeg'
# ----------------------------------------------

cmd = [
    FFMPEG_BIN,
    '-loglevel', 'quiet',
    '-i', URI,
    '-f', 'rawvideo',
    '-pix_fmt', 'bgr24',  # Keep using BGR for OpenCV
    '-'
]

pipe = subprocess.Popen(cmd, stdout=subprocess.PIPE, bufsize=W * H * 3 * 8)

# Shared state
last_frame = None
lock = threading.Lock()
running = True

def stop_everything(*_):
    global running
    running = False

signal.signal(signal.SIGINT, stop_everything)
signal.signal(signal.SIGTERM, stop_everything)

def imshow_thread():
    cv2.namedWindow("Preview", cv2.WINDOW_NORMAL)
    cv2.resizeWindow("Preview", W, H)

    shown = 0
    start = time.time()

    while running:
        with lock:
            frame = last_frame.copy() if last_frame is not None else None

        if frame is not None:
            cv2.imshow("Preview", frame)
            shown += 1

        if shown and shown % 100 == 0:
            elapsed = time.time() - start
            print(f"[Preview] FPS: {shown / elapsed:.1f}")

        if cv2.waitKey(1) & 0xFF == 27:
            stop_everything()
            break

        time.sleep(0.001)

    cv2.destroyAllWindows()

threading.Thread(target=imshow_thread, daemon=True).start()

# Main frame loop
frame_bytes = W * H * 3
frame_count = 0
start_time = time.time()

while running:
    buf = pipe.stdout.read(frame_bytes)
    if len(buf) != frame_bytes:
        print("⚠️ Incomplete frame or stream ended.")
        break

    frame = np.frombuffer(buf, dtype=np.uint8).reshape((H, W, 3))

    if frame.shape != (H, W, 3) or frame.dtype != np.uint8:
        print(f"⚠️ Unexpected frame shape: {frame.shape}, dtype: {frame.dtype}")
    else:
        b, g, r = frame[0, 0]
        # print(f"🟢 BGR sample at (0,0): B={b}, G={g}, R={r}")

    with lock:
        last_frame = frame

    frame_count += 1
    if frame_count % 100 == 0:
        elapsed = time.time() - start_time
        print(f"[MainLoop] FPS: {frame_count / elapsed:.1f}")

pipe.terminate()
pipe.stdout.close()