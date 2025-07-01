#ifndef LOGICMANAGER_H
#define LOGICMANAGER_H
#define _WINSOCKAPI_

#include <winsock2.h>
#include <Windows.h>
#include <memory>
#include <thread>
#include <atomic>
#include <chrono>
#include <opencv2/opencv.hpp>
#include <opencv2/core/ocl.hpp>
#include <cuda_runtime.h>
#include <license.grpc.pb.h>
#include "../Frame/FrameSlot.h"
#include "../Movement/KeyWatcher.h"
#include "../Movement/Km.h"
#include "YoloModel.h"
#include "CudaModel.h"

class LogicManager {
public:
    LogicManager(
        spdlog::logger& logger,
        std::shared_ptr<FrameSlot> frameSlot,
        std::shared_ptr<KeyWatcher> keyWatcher,
        std::shared_ptr<Km> km,
        std::unique_ptr<YoloModel> yoloModel = nullptr,
        std::unique_ptr<CudaModel> cudaModel = nullptr);
    ~LogicManager();

    void Start();
    void Stop();
    void SetConfig(const ::capkfa::RemoteConfig& config);

private:
    void ProcessLoop();
    void ConvertToHSV(const cv::UMat& frame);
    void FilterInRange();
    void DisplayFrame(const cv::Mat& frame, const std::string& windowName);
    void DrawDetections(cv::Mat& image, const std::vector<Detection>& detections, float confThreshold = 0.3f);
    std::optional<cv::Point> GetHighestMaskPoint();
    std::optional<cv::Point> GetBiggestAimPoint(const std::vector<Detection>& detections);
    std::tuple<short, short> CalculateCoordinates(cv::Point target, const capkfa::RemoteConfigAim_Base& aimBase, float y1 = 0, float y2 = 0);
    void HandleFlick(short moveX, short moveY);
    std::vector<Detection> PredictYolo(std::shared_ptr<Frame>& frame);
    bool HasNvidiaGPU();

    spdlog::logger& logger_;
    std::shared_ptr<FrameSlot> frameSlot_;
    std::shared_ptr<KeyWatcher> keyWatcher_;
    std::shared_ptr<Km> km_;
    std::unique_ptr<YoloModel> yoloModel_;
    std::unique_ptr<CudaModel> cudaModel_;
    ::capkfa::RemoteConfig remoteConfig_;

    cv::Rect roi_;
    std::thread handlerThread_;
    std::atomic<bool> isRunning_;
    uint64_t lastFrameVersion_;
    cv::UMat hsvFrame_;
    cv::UMat mask_;

    std::vector<float> recoil_pattern_ = {2.0f, 2.7f, 3.2f, 3.7f, 4.5f};
    bool recoil_active_;
    std::chrono::steady_clock::time_point recoil_start_time_;
};

#endif