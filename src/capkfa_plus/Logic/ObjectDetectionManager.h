#ifndef OBJECTDETECTIONMANAGER_H
#define OBJECTDETECTIONMANAGER_H
#define _WINSOCKAPI_

#include <winsock2.h>
#include <Windows.h>
#include <memory>
#include "../Frame/FrameSlot.h"
#include "../Movement/KeyWatcher.h"
#include "../Movement/Km.h"
#include "CudaModel.h"
#include "YoloModel.h"

class ObjectDetectionManager {
public:
    ObjectDetectionManager(
        spdlog::logger& logger,
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
    std::vector<Detection> Predict(std::shared_ptr<Frame>& frame);
    bool hasNvidiaGPU();
    void DrawDetections(cv::Mat& image, const std::vector<Detection>& detections, float confThreshold = 0.3f);
    void DisplayFrame(const cv::Mat& mat, const std::string& windowName);
    std::tuple<short, short> CalculateCoordinates(cv::Point target, const capkfa::RemoteConfigAimType& aimType, float y1, float y2);
    void HandleFlick(short moveX, short moveY);

    spdlog::logger& logger_;
    std::shared_ptr<FrameSlot> frameSlot_;
    std::shared_ptr<KeyWatcher> keyWatcher_;
    std::shared_ptr<Km> km_;
    std::unique_ptr<YoloModel> yoloModel_;
    std::unique_ptr<CudaModel> cudaModel_;
    ::capkfa::RemoteConfig remoteConfig_;

    std::thread handlerThread_;
    std::atomic<bool> isRunning_;
    uint64_t lastFrameVersion_;

    static constexpr int recoil_duration_ms_ = 90;
    std::vector<float> recoil_pattern_ = {1.2f, 1.5f, 1.7f, 2.2f, 2.8f}; // Downward pixel adjustments
    bool recoil_active_;
    std::chrono::steady_clock::time_point recoil_start_time_;
};

#endif //OBJECTDETECTIONMANAGER_H
