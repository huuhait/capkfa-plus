#include "FrameHandler.h"
#include <opencv2/opencv.hpp>
#include <opencv2/core/ocl.hpp>
#include <iostream>
#include <chrono>

FrameHandler::FrameHandler(std::shared_ptr<FrameSlot> frameSlot, UINT outputIndex)
    : frameSlot_(frameSlot), outputIndex_(outputIndex), isRunning_(false), lastFrameVersion_(0) {
    if (!cv::ocl::haveOpenCL()) {
        std::cerr << "OpenCL not available, falling back to CPU" << std::endl;
    } else {
        cv::ocl::setUseOpenCL(true);
        cv::ocl::Device device = cv::ocl::Device::getDefault();
        std::cout << "Using OpenCL device: " << device.name() << std::endl;
    }
    // Pre-allocate UMat buffers
    bgrFrame_ = cv::UMat(500, 500, CV_8UC3);
    hsvFrame_ = cv::UMat(500, 500, CV_8UC3);
    mask_ = cv::UMat(500, 500, CV_8UC1);
}

FrameHandler::~FrameHandler() {
    Stop();
}

void FrameHandler::Start() {
    if (!isRunning_) {
        isRunning_ = true;
        handlerThread_ = std::thread(&FrameHandler::ProcessLoop, this);
    }
}

void FrameHandler::Stop() {
    if (isRunning_) {
        isRunning_ = false;
        if (handlerThread_.joinable()) {
            handlerThread_.join();
        }
    }
}

void FrameHandler::ConvertToBGR(const cv::UMat& frame) {
    if (!frame.empty()) {
        cv::cvtColor(frame, bgrFrame_, cv::COLOR_BGRA2BGR);
        cv::ocl::finish();
    } else {
        std::cerr << "Input frame is empty" << std::endl;
        bgrFrame_ = cv::UMat();
    }
}

void FrameHandler::ConvertToHSV() {
    if (!bgrFrame_.empty()) {
        cv::cvtColor(bgrFrame_, hsvFrame_, cv::COLOR_BGR2HSV);
        cv::ocl::finish();
    } else {
        std::cerr << "BGR frame is empty" << std::endl;
        hsvFrame_ = cv::UMat();
    }
}

void FrameHandler::FilterInRange() {
    if (!hsvFrame_.empty()) {
        cv::Scalar lowerb2(140, 60, 240);
        cv::Scalar upperb2(160, 255, 255);
        cv::inRange(hsvFrame_, lowerb2, upperb2, mask_);
        cv::ocl::finish();
    } else {
        std::cerr << "HSV frame is empty" << std::endl;
        mask_ = cv::UMat();
    }
}

void FrameHandler::DisplayFrame(const cv::UMat& frame, const std::string& windowName) {
    if (!frame.empty()) {
        cv::Mat mat;
        frame.copyTo(mat);
        if (!mat.empty()) {
            cv::imshow(windowName, mat);
            cv::waitKey(1);
        } else {
            std::cerr << "Failed to convert " << windowName << " to Mat" << std::endl;
        }
    } else {
        std::cerr << windowName << " is empty" << std::endl;
    }
}

void FrameHandler::ProcessLoop() {
    int frameCount = 0;
    auto lastTime = std::chrono::steady_clock::now();

    while (isRunning_) {
        auto currentTime = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(currentTime - lastTime).count();

        if (duration >= 1000) {
            float fps = (float)frameCount * 1000.0f / duration;
            std::cout << "Handler Output " << outputIndex_ << " FPS: " << fps << std::endl;
            frameCount = 0;
            lastTime = currentTime;
        }

        auto [frame, newVersion] = frameSlot_->GetFrame(lastFrameVersion_);
        if (!frame.empty()) {
            DisplayFrame(frame, "Output " + std::to_string(outputIndex_) + " Mask");
            frameCount++;
            lastFrameVersion_ = newVersion;
            ConvertToBGR(frame);
            ConvertToHSV();
            FilterInRange();
        } else {
            // std::cerr << "Cached frame is empty" << std::endl;
        }

        if (GetAsyncKeyState('Q')) {
            isRunning_ = false;
        }
    }
}