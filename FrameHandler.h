#ifndef FRAME_HANDLER_H
#define FRAME_HANDLER_H

#include <memory>
#include <thread>
#include <atomic>
#include <chrono>
#include <windows.h>
#include <cstdint>
#include <opencv2/opencv.hpp>
#include "FrameSlot.h"

class FrameHandler {
public:
    FrameHandler(std::shared_ptr<FrameSlot> frameSlot, UINT outputIndex);
    ~FrameHandler();
    void Start();
    void Stop();

private:
    void ProcessLoop();
    void ConvertToBGR(const cv::UMat& frame);
    void ConvertToHSV();
    void FilterInRange();
    void DisplayFrame(const cv::UMat& frame, const std::string& windowName);

    std::shared_ptr<FrameSlot> frameSlot_;
    UINT outputIndex_;
    std::thread handlerThread_;
    std::atomic<bool> isRunning_;
    uint64_t lastFrameVersion_;
    cv::UMat bgrFrame_;
    cv::UMat hsvFrame_;
    cv::UMat mask1_;
    cv::UMat mask2_;
    cv::UMat mask_;
};

#endif