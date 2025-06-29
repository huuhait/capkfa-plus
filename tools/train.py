from ultralytics import YOLO

def main():
    model = YOLO("dataset/yolo11n.yaml")

    model.train(
        name="yolo11n",
        data="dataset/data.yaml",
        epochs=100,
        imgsz=256,
        batch=32,
        device=0,
        workers=6,
        cache=True,
        amp=True,
        auto_augment="randaugment",
        val=True,
        lr0=0.001,
        optimizer="AdamW",
        cos_lr=True,
        patience=30            # optional: stop early if no improvement
    )

if __name__ == "__main__":
    from multiprocessing import freeze_support
    freeze_support()
    main()
