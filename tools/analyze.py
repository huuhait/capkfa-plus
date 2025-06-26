import numpy as np
import onnxruntime as ort
import cv2
from mss import mss

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

def main():
    # Load ONNX model
    providers = ['DmlExecutionProvider']  # or ['CPUExecutionProvider'] to test CPU
    session_options = ort.SessionOptions()
    session_options.graph_optimization_level = ort.GraphOptimizationLevel.ORT_ENABLE_ALL
    session = ort.InferenceSession('best.onnx', providers=providers, sess_options=session_options)
    print("Model loaded.")
    print("Input name:", session.get_inputs()[0].name)
    print("Output name:", session.get_outputs()[0].name)
    print("Output shape:", session.get_outputs()[0].shape)

    # Capture a single frame using MSS
    sct = mss()
    monitor = sct.monitors[2]  # Adjust monitor index if needed
    width, height = monitor['width'], monitor['height']
    left = monitor['left'] + (width - 256) // 2
    top = monitor['top'] + (height - 256) // 2
    region = {'left': left, 'top': top, 'width': 256, 'height': 256}

    screenshot = sct.grab(region)
    img = np.frombuffer(screenshot.rgb, dtype=np.uint8).reshape(256, 256, 3)
    print("Captured image shape:", img.shape)

    # Preprocess: transpose, normalize
    input_name = session.get_inputs()[0].name
    img_array = img.transpose(2, 0, 1).astype(np.float32) / 255.0
    input_tensor = np.expand_dims(img_array, axis=0)
    print("Input tensor shape:", input_tensor.shape)
    print("Input tensor first 10 floats:", input_tensor.flatten()[:10])

    # Run inference
    output_name = session.get_outputs()[0].name
    outputs = session.run([output_name], {input_name: input_tensor})[0]  # shape: [1,5,1344]
    print("Raw output shape:", outputs.shape)
    print("Raw output first 20 floats:", outputs.flatten()[:20])

    # Postprocess
    boxes = []
    scores = []
    img_size = 256
    strides = [8, 16, 32]
    grid_sizes = [32, 16, 8]
    anchor_counts = [384, 768, 192]
    anchor_offset = 0

    for stride, grid_size, num_anchors in zip(strides, grid_sizes, anchor_counts):
        detections = outputs[0, :, anchor_offset:anchor_offset + num_anchors]  # [5, num_anchors]
        anchor_offset += num_anchors

        for i in range(num_anchors):
            obj_score = 1.0 / (1.0 + np.exp(-detections[4, i]))  # sigmoid
            if obj_score > 0.1:
                x_center = detections[0, i]
                y_center = detections[1, i]
                w = detections[2, i]
                h = detections[3, i]
                x1 = max(0, x_center - w / 2)
                y1 = max(0, y_center - h / 2)
                x2 = min(img_size, x_center + w / 2)
                y2 = min(img_size, y_center + h / 2)
                boxes.append([x1, y1, x2, y2])
                scores.append(obj_score)

    print(f"Detections before NMS: {len(boxes)}")
    if boxes:
        boxes = np.array(boxes)
        scores = np.array(scores)
        keep = nms(boxes, scores, iou_threshold=0.6)
        boxes = boxes[keep]
        scores = scores[keep]
        print(f"Detections after NMS: {len(boxes)}")
        for i, (box, score) in enumerate(zip(boxes, scores)):
            print(f"Detection {i}: Box={box}, Confidence={score:.4f}")
    else:
        print("No detections above threshold.")

if __name__ == "__main__":
    main()
