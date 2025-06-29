from ultralytics import YOLO

model = YOLO("best.pt")

model.export(
    format="onnx",
    imgsz=[256, 256],
    dynamic=False,
    simplify=True,
    verbose=False,
    half=True,
    opset=15,
    device=0
)
