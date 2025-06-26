#ifndef OBJECTDETECTIONMANAGER_H
#define OBJECTDETECTIONMANAGER_H

#include <memory>
#include "../Frame/FrameSlot.h"
#include "../Movement/KeyWatcher.h"
#include "../Movement/Km.h"
#include "CudaModel.h"
#include "YoloModel.h"

class ObjectDetectionManager {
public:
    ObjectDetectionManager(
        std::shared_ptr<FrameSlot> frameSlot,
        std::shared_ptr<KeyWatcher> keyWatcher,
        std::shared_ptr<Km> km,
        std::unique_ptr<YoloModel> yoloModel,
        std::unique_ptr<CudaModel> cudaModel);
    ~ObjectDetectionManager();

    void Start();
    void Stop();
    void SetConfig(const ::capkfa::RemoteConfig& config);
private:
    void ProcessLoop();
    std::vector<Detection> Predict(const Frame& frame);
    bool hasNvidiaGPU();
    void DrawDetections(cv::UMat& image, const std::vector<Detection>& detections, float confThreshold = 0.3f);
    void DisplayFrame(const cv::UMat& mat, const std::string& windowName);
    std::tuple<short, short> CalculateCoordinates(cv::Point p, const capkfa::RemoteConfigAimType& aimType);
    void HandleFlick(short moveX, short moveY);

    ::capkfa::RemoteConfig remoteConfig_;
    std::shared_ptr<FrameSlot> frameSlot_;
    std::shared_ptr<KeyWatcher> keyWatcher_;
    std::shared_ptr<Km> km_;
    std::unique_ptr<YoloModel> yoloModel_;
    std::unique_ptr<CudaModel> cudaModel_;

    std::thread handlerThread_;
    std::atomic<bool> isRunning_;
    uint64_t lastFrameVersion_;
};

#endif //OBJECTDETECTIONMANAGER_H
