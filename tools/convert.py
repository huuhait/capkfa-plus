from ultralytics import YOLO

# Load the model
model = YOLO("best.pt")

# Export to ONNX with optimized settings
model.export(
    format="onnx",
    opset=12,        # Lower opset for broader compatibility
    simplify=True,   # Simplify model for reduced complexity
    dynamic=False,   # Fixed shapes for better runtime performance
    half=True,       # FP16 for reduced memory and faster inference
    batch=1,         # Fixed batch size for single-image inference
    imgsz=[256, 256] # Fixed input size based on metadata
)
