#ifndef COLORBOT_H
#define COLORBOT_H

#include <memory>
#include <thread>
#include <atomic>
#include <windows.h>
#include <license.grpc.pb.h>
#include <opencv2/opencv.hpp>

#include "../Frame/FrameSlot.h"
#include "../Movement/KeyWatcher.h"
#include "../Movement/Km.h"

class Colorbot {
public:
    Colorbot(std::shared_ptr<FrameSlot> frameSlot, std::shared_ptr<KeyWatcher> keyWatcher, std::shared_ptr<Km> km);
    ~Colorbot();
    void Start();
    void Stop();
    void SetConfig(const ::capkfa::RemoteConfig& config);

private:
    void ProcessLoop();
    void ConvertToBGR(const cv::UMat& frame);
    void ConvertToHSV();
    void FilterInRange();
    void DisplayFrame(const cv::UMat& frame, const std::string& windowName);
    void HandleFlick(short moveX, short moveY);
    std::optional<cv::Point> GetHighestMaskPoint();
    std::tuple<short, short> CalculateCoordinates(cv::Point p, capkfa::RemoteConfigAimType aimType);

    ::capkfa::RemoteConfig remoteConfig_;
    std::shared_ptr<FrameSlot> frameSlot_;
    std::shared_ptr<KeyWatcher> keyWatcher_;
    std::shared_ptr<Km> km_;

    UINT outputIndex_;
    std::thread handlerThread_;
    std::atomic<bool> isRunning_;
    uint64_t lastFrameVersion_;
    cv::UMat bgrFrame_;
    cv::UMat hsvFrame_;
    cv::UMat mask_;
};

#endif
