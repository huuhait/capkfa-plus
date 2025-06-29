#ifndef PIXELSEEK_H
#define PIXELSEEK_H

#define _WINSOCKAPI_
#include <memory>
#include <thread>
#include <atomic>
#include <winsock2.h>
#include <windows.h>
#include <license.grpc.pb.h>
#include <opencv2/opencv.hpp>

#include "../Frame/FrameSlot.h"
#include "../Movement/KeyWatcher.h"
#include "../Movement/Km.h"

class PixelSeek {
public:
    PixelSeek(spdlog::logger& logger, std::shared_ptr<FrameSlot> frameSlot, std::shared_ptr<KeyWatcher> keyWatcher, std::shared_ptr<Km> km);
    ~PixelSeek();
    void Start();
    void Stop();
    void SetConfig(const ::capkfa::RemoteConfig& config);

private:
    void ProcessLoop();
    void ConvertToHSV(const cv::UMat& frame);
    void FilterInRange();
    void DisplayFrame(const cv::UMat& frame, const std::string& windowName);
    void HandleFlick(short moveX, short moveY);
    std::optional<cv::Point> GetHighestMaskPoint();
    std::tuple<short, short> CalculateCoordinates(cv::Point p, capkfa::RemoteConfigAimType aimType);

    spdlog::logger& logger_;
    std::shared_ptr<FrameSlot> frameSlot_;
    std::shared_ptr<KeyWatcher> keyWatcher_;
    std::shared_ptr<Km> km_;
    ::capkfa::RemoteConfig remoteConfig_;

    UINT outputIndex_;
    std::thread handlerThread_;
    std::atomic<bool> isRunning_;
    uint64_t lastFrameVersion_;
    cv::UMat hsvFrame_;
    cv::UMat mask_;

    static constexpr int recoil_duration_ms_ = 90;
    std::vector<float> recoil_pattern_ = {2.0f, 2.7f, 3.2f, 3.7f, 4.5f}; // Downward pixel adjustments
    bool recoil_active_;
    std::chrono::steady_clock::time_point recoil_start_time_;
};

#endif
