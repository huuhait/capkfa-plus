import numpy as np
import onnxruntime as ort
import cv2
from mss import mss
import time
import keyboard
import threading
import queue

# Non-maximum suppression
def nms(boxes, scores, iou_threshold=0.6):
    if len(boxes) == 0:
        return []
    x1, y1, x2, y2 = boxes[:, 0], boxes[:, 1], boxes[:, 2], boxes[:, 3]
    areas = (x2 - x1 + 1) * (y2 - y1 + 1)
    order = scores.argsort()[::-1]
    keep = []
    while order.size > 0:
        i = order[0]
        keep.append(i)
        xx1 = np.maximum(x1[i], x1[order[1:]])
        yy1 = np.maximum(y1[i], y1[order[1:]])
        xx2 = np.minimum(x2[i], x2[order[1:]])
        yy2 = np.minimum(y2[i], y2[order[1:]])
        w = np.maximum(0, xx2 - xx1 + 1)
        h = np.maximum(0, yy2 - yy1 + 1)
        iou = w * h / (areas[i] + areas[order[1:]] - w * h)
        order = order[1:][iou < iou_threshold]
    return keep

# Capture thread function
def capture_thread(capture_queue):
    sct = mss()
    monitor = sct.monitors[2]
    width, height = monitor['width'], monitor['height']
    left = monitor['left'] + (width - 256) // 2
    top = monitor['top'] + (height - 256) // 2
    region = {'left': left, 'top': top, 'width': 256, 'height': 256}
    capture_time = 0
    frame_count = 0
    start_time = time.time()

    while not keyboard.is_pressed('q'):
        capture_start = time.time()
        screenshot = sct.grab(region)
        img = np.frombuffer(screenshot.rgb, dtype=np.uint8).reshape(256, 256, 3)
        capture_time += time.time() - capture_start

        try:
            capture_queue.put_nowait(img)
        except queue.Full:
            pass

        frame_count += 1
        elapsed_time = time.time() - start_time
        if elapsed_time > 1:
            fps = frame_count / elapsed_time
            avg_capture = (capture_time / frame_count) * 1000
            print(f"Capture FPS: {fps:.2f}, Capture: {avg_capture:.2f}ms")
            start_time = time.time()
            frame_count = 0
            capture_time = 0

# Display thread function
def display_thread(display_queue):
    display_time = 0
    frame_count = 0
    start_time = time.time()
    while not keyboard.is_pressed('q'):
        try:
            img_display, boxes, scores = display_queue.get(timeout=0.1)
            display_start = time.time()

            for box, score in zip(boxes, scores):
                x1, y1, x2, y2 = map(int, box)
                if 0 <= x1 < 256 and 0 <= y1 < 256 and 0 <= x2 < 256 and 0 <= y2 < 256:
                    cv2.rectangle(img_display, (x1, y1), (x2, y2), (0, 255, 0), 1)
                    cv2.putText(img_display, f'{score:.2f}', (x1, y1-10),
                                cv2.FONT_HERSHEY_SIMPLEX, 0.4, (0, 255, 0), 1)

            display_time += time.time() - display_start
            frame_count += 1

            elapsed_time = time.time() - start_time
            if elapsed_time > 1:
                fps = frame_count / elapsed_time
                avg_display = (display_time / frame_count) * 1000
                print(f"Display FPS: {fps:.2f}, Display: {avg_display:.2f}ms")
                start_time = time.time()
                frame_count = 0
                display_time = 0
        except queue.Empty:
            pass

        cv2.imshow('Prediction', img_display)
        cv2.waitKey(1)

    cv2.destroyAllWindows()

# ONNX-Runtime Initialization
providers = ['DmlExecutionProvider']
try:
    session_options = ort.SessionOptions()
    session_options.graph_optimization_level = ort.GraphOptimizationLevel.ORT_ENABLE_ALL
    session = ort.InferenceSession('best.onnx', providers=providers, sess_options=session_options)
    print("Active providers:", session.get_providers())
    print("Model output shape:", session.get_outputs()[0].shape)
except Exception as e:
    print("Session error:", e)
    exit(1)

# Initialize queues and threads
capture_queue = queue.Queue(maxsize=5)
display_queue = queue.Queue(maxsize=10)
capture_thread = threading.Thread(target=capture_thread, args=(capture_queue,), daemon=True)
display_thread = threading.Thread(target=display_thread, args=(display_queue,), daemon=True)
capture_thread.start()
display_thread.start()

# FPS and timing
start_time = time.time()
frame_count = 0
preprocess_time = inference_time = postprocess_time = 0

# Main loop
while not keyboard.is_pressed('q'):
    try:
        img = capture_queue.get(timeout=0.1)
    except queue.Empty:
        continue

    # Preprocess
    preprocess_start = time.time()
    img_array = img.transpose(2, 0, 1)
    img_array = np.expand_dims(img_array, axis=0).astype(np.float16) / 255.0
    preprocess_time += time.time() - preprocess_start

    # Inference
    inference_start = time.time()
    input_name = session.get_inputs()[0].name
    output_name = session.get_outputs()[0].name
    outputs = session.run([output_name], {input_name: img_array})[0]  # [1, 5, 1344]
    inference_time += time.time() - inference_start

    # Post-process
    postprocess_start = time.time()
    boxes, scores = [], []
    img_size = 256
    strides = [8, 16, 32]
    grid_sizes = [32, 16, 8]
    anchor_counts = [384, 768, 192]
    anchor_offset = 0

    for stride, grid_size, num_anchors in zip(strides, grid_sizes, anchor_counts):
        detections = outputs[0, :, anchor_offset:anchor_offset + num_anchors]  # [5, num_anchors]
        anchor_offset += num_anchors

        for i in range(num_anchors):
            obj_score = detections[4, i]
            if obj_score > 0.1:
                x_center = detections[0, i]
                y_center = detections[1, i]
                w = detections[2, i]
                h = detections[3, i]
                score = obj_score
                x1 = max(0, x_center - w / 2)
                y1 = max(0, y_center - h / 2)
                x2 = min(img_size, x_center + w / 2)
                y2 = min(img_size, y_center + h / 2)
                boxes.append([x1, y1, x2, y2])
                scores.append(score)

    if boxes:
        boxes = np.array(boxes)
        scores = np.array(scores)
        keep = nms(boxes, scores, iou_threshold=0.6)
        boxes = boxes[keep].tolist()
        scores = scores[keep].tolist()

    postprocess_time += time.time() - postprocess_start

    # Queue frame for display
    try:
        display_queue.put_nowait((img.copy(), boxes, scores))
    except queue.Full:
        pass

    # Calculate FPS
    frame_count += 1
    elapsed_time = time.time() - start_time
    if elapsed_time > 1:
        fps = frame_count / elapsed_time
        avg_preprocess = (preprocess_time / frame_count) * 1000
        avg_inference = (inference_time / frame_count) * 1000
        avg_postprocess = (postprocess_time / frame_count) * 1000
        print(f"Main FPS: {fps:.2f}, Preprocess: {avg_preprocess:.2f}ms, "
              f"Inference: {avg_inference:.2f}ms, Postprocess: {avg_postprocess:.2f}ms, "
              f"Detections: {len(boxes)}")
        start_time = time.time()
        frame_count = 0
        preprocess_time = inference_time = postprocess_time = 0

# Cleanup
print("Exiting...")
cv2.destroyAllWindows()