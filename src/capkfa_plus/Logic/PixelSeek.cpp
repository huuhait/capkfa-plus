#include "PixelSeek.h"
#include <opencv2/opencv.hpp>
#include <opencv2/core/ocl.hpp>
#include <iostream>
#include <chrono>

PixelSeek::PixelSeek(spdlog::logger& logger, std::shared_ptr<FrameSlot> frameSlot, std::shared_ptr<KeyWatcher> keyWatcher, std::shared_ptr<Km> km)
    : logger_(logger), frameSlot_(frameSlot), keyWatcher_(keyWatcher), km_(km), isRunning_(false), lastFrameVersion_(0) {}

PixelSeek::~PixelSeek() {
    Stop();
}

void PixelSeek::Start() {
    if (!isRunning_) {
        if (hsvFrame_.empty() || mask_.empty()) {
            logger_.error("PixelSeek not configured. Call SetConfig to enable processing.");
            return;
        }
        isRunning_ = true;
        handlerThread_ = std::thread(&PixelSeek::ProcessLoop, this);
    }
}

void PixelSeek::Stop() {
    if (isRunning_) {
        isRunning_ = false;
        if (handlerThread_.joinable()) {
            handlerThread_.join();
        }
        // Clear buffers and destroy only the instance-specific window
        hsvFrame_ = cv::UMat();
        mask_ = cv::UMat();
        cv::destroyAllWindows();
    }
}

void PixelSeek::SetConfig(const ::capkfa::RemoteConfig& config) {
    int width = config.aim().fov();
    int height = config.aim().fov();

    if (width <= 0 || height <= 0) {
        throw std::runtime_error("Invalid frame size: " + std::to_string(width) + "x" + std::to_string(height));
    }

    Stop(); // Stop processing to safely initialize buffers

    // Initialize UMat buffers
    remoteConfig_ = config;
    hsvFrame_ = cv::UMat(height, width, CV_8UC3);
    mask_ = cv::UMat(height, width, CV_8UC1);
    recoil_active_ = false;

    logger_.info("PixelSeek config set: size {}x{}", width, height);

    Start();
}

void PixelSeek::ConvertToHSV(const cv::UMat& frame) {
    if (!frame.empty() && !hsvFrame_.empty()) {
        cv::cvtColor(frame, hsvFrame_, cv::COLOR_RGB2HSV);
        cv::ocl::finish();
    } else {
        std::cerr << "frame or hsvFrame_ is empty" << std::endl;
        hsvFrame_ = cv::UMat();
    }
}

void PixelSeek::FilterInRange() {
    if (!hsvFrame_.empty() && !mask_.empty()) {
        cv::Scalar lowerb2(140, 60, 240);
        cv::Scalar upperb2(160, 255, 255);
        cv::inRange(hsvFrame_, lowerb2, upperb2, mask_);
        cv::ocl::finish();
    } else {
        logger_.error("FilterInRange called with empty hsvFrame");
        mask_ = cv::UMat();
    }
}

void PixelSeek::DisplayFrame(const cv::UMat& frame, const std::string& windowName) {
    if (!frame.empty()) {
        cv::Mat mat;
        frame.copyTo(mat);
        if (!mat.empty()) {
            cv::imshow(windowName, mat);
            cv::waitKey(1);
        } else {
            logger_.error("Failed to convert {} to Mat", windowName);
        }
    } else {
        logger_.error("DisplayFrame called with empty frame");
    }
}

void PixelSeek::ProcessLoop() {
    try {
        int frameCount = 0;
        auto lastTime = std::chrono::steady_clock::now();

        while (isRunning_) {
            if (hsvFrame_.empty() || mask_.empty()) {
                continue; // Skip if buffers not initialized
            }

            auto currentTime = std::chrono::steady_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(currentTime - lastTime).count();

            if (duration >= 1000) {
                float fps = (float)frameCount * 1000.0f / duration;
                logger_.info("Handler PixelSeek FPS: {}", fps);
                frameCount = 0;
                lastTime = currentTime;
            }

            auto [frame, newVersion] = frameSlot_->GetFrame(lastFrameVersion_);
            if (!frame->IsValid()) {
                continue;
            }

            if (!keyWatcher_->IsHandlerKeyDown()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }

            frameCount++;
            lastFrameVersion_ = newVersion;
            // DisplayFrame(frame, "Output Mask");
            ConvertToHSV(frame->ToUMat());
            FilterInRange();

            std::optional<cv::Point> point = GetHighestMaskPoint();
            if (!point.has_value()) {
                continue;
            }

            capkfa::RemoteConfigAimType aimType = keyWatcher_->IsFlickKeyDown() ? remoteConfig_.aim().flick() : remoteConfig_.aim().aim();
            auto [moveX, moveY] = CalculateCoordinates(point.value(), aimType);

            HandleFlick(moveX, moveY);

            if (moveX != 0 || moveY != 0) {
                km_->Move(moveX, moveY);
            }
        }
    } catch (const std::exception& e) {
        logger_.error("PixelSeek exception caught: {}", e.what());
        isRunning_ = false;
    } catch (...) {
        logger_.error("PixelSeek crashed with unknown error");
        isRunning_ = false;
    }
}

std::optional<cv::Point> PixelSeek::GetHighestMaskPoint() {
    std::vector<cv::Point> points;
    cv::findNonZero(mask_, points);
    if (points.empty()) return std::nullopt;

    cv::Point min = points[0];
    for (size_t i = 1; i < points.size(); ++i)
        if (points[i].y < min.y) min = points[i];

    return min;
}

std::tuple<short, short> PixelSeek::CalculateCoordinates(cv::Point p, capkfa::RemoteConfigAimType aimType)
{
    double m = remoteConfig_.aim().fov() / 2.0;
    double dx = p.x - m, dy = p.y - m;
    double distance = std::sqrt(dx * dx + dy * dy);
    double maxDistance = m * std::sqrt(2.0);

    double smoothX = aimType.smooth_x().min() + (aimType.smooth_x().max() - aimType.smooth_x().min()) * (1.0 - distance / maxDistance);
    double smoothY = aimType.smooth_y().min() + (aimType.smooth_y().max() - aimType.smooth_y().min()) * (1.0 - distance / maxDistance);

    short adjustedX = static_cast<short>(std::round((p.x - m + remoteConfig_.aim().offset_x()) / smoothX));
    short adjustedY = static_cast<short>(std::round((p.y - m + remoteConfig_.aim().offset_y()) / smoothY));

    if (remoteConfig_.aim().recoil()) {
        // Apply recoil pattern when firing (shot key held)
        if (keyWatcher_->IsShotKeyDown()) {
            if (!recoil_active_) {
                recoil_active_ = true;
                recoil_start_time_ = std::chrono::steady_clock::now();
            }
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - recoil_start_time_).count();
            if (elapsed <= recoil_duration_ms_ && !recoil_pattern_.empty()) {
                float t = elapsed / static_cast<float>(recoil_duration_ms_);
                size_t pattern_size = recoil_pattern_.size();
                float index = t * (pattern_size - 1);
                size_t idx = static_cast<size_t>(index);
                float frac = index - idx;
                float recoil_offset = (idx < pattern_size - 1)
                    ? recoil_pattern_[idx] + frac * (recoil_pattern_[idx + 1] - recoil_pattern_[idx])
                    : recoil_pattern_.back();
                adjustedY += static_cast<short>(recoil_offset);
            } else {
                recoil_active_ = false;
            }
        } else {
            recoil_active_ = false;
        }
    }

    return {adjustedX, adjustedY};
}

void PixelSeek::HandleFlick(short moveX, short moveY) {
    if (!keyWatcher_->IsFlickKeyDown()) return;

    static auto lastClick = std::chrono::steady_clock::now();
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastClick).count();

    bool isIdealToFlick = std::abs(moveX) <= 1 && std::abs(moveY) <= 1;

    if (isIdealToFlick && elapsed > 300) {
        km_->Click();
        lastClick = now;
    }
}
