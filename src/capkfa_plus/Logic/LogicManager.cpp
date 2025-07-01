#include "LogicManager.h"
#include <random>
#include <iostream>

LogicManager::LogicManager(
    spdlog::logger& logger, std::shared_ptr<FrameSlot> frameSlot, std::shared_ptr<KeyWatcher> keyWatcher,
    std::shared_ptr<Km> km, std::unique_ptr<YoloModel> yoloModel, std::unique_ptr<CudaModel> cudaModel)
    : logger_(logger), frameSlot_(frameSlot), keyWatcher_(keyWatcher), km_(km),
      yoloModel_(std::move(yoloModel)), cudaModel_(std::move(cudaModel)), isRunning_(false),
      lastFrameVersion_(0), recoil_active_(false) {}

LogicManager::~LogicManager() {
    Stop();
}

void LogicManager::Start() {
    if (!isRunning_) {
        if (remoteConfig_.mode() != ::capkfa::ObjectDetection && (hsvFrame_.empty() || mask_.empty())) {
            logger_.error("HSV mode not configured. Call SetConfig to enable processing.");
            return;
        }
        if (remoteConfig_.mode() == ::capkfa::ObjectDetection && (!yoloModel_ || !frameSlot_ || !keyWatcher_ || !km_)) {
            logger_.error("YOLO mode initialization failed: missing dependencies.");
            return;
        }
        isRunning_ = true;
        handlerThread_ = std::thread(&LogicManager::ProcessLoop, this);
    }
}

void LogicManager::Stop() {
    if (isRunning_) {
        isRunning_ = false;
        if (handlerThread_.joinable()) {
            handlerThread_.join();
        }
        handlerThread_ = std::thread(); // Reset thread to avoid reuse
        hsvFrame_.release(); // Explicitly release UMat resources
        mask_.release();
        recoil_active_ = false;
        //cv::destroyWindow("LogicManager");
    }
}

void LogicManager::SetConfig(const ::capkfa::RemoteConfig& config) {
    Stop();

    // Ensure thread is fully stopped
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    remoteConfig_ = config;

    if (remoteConfig_.mode() != ::capkfa::ObjectDetection) {
        int fov = config.aim().fov();
        if (fov <= 0 || fov > remoteConfig_.capture().size()) {
            throw std::runtime_error("Invalid FOV: " + std::to_string(fov));
        }
        int offset = (remoteConfig_.capture().size() - fov) / 2;
        roi_ = cv::Rect(offset, offset, fov, fov);
        hsvFrame_ = cv::UMat(fov, fov, CV_8UC3);
        mask_ = cv::UMat(fov, fov, CV_8UC1);
        if (hsvFrame_.empty() || mask_.empty()) {
            logger_.error("Failed to initialize hsvFrame_ or mask_");
            throw std::runtime_error("Resource initialization failed");
        }
        logger_.info("HSV config set: FOV {}x{}, ROI offset {}", fov, fov, offset);
    }
    Start();
}

void LogicManager::ProcessLoop() {
    try {
        int frameCount = 0;
        double totalPredictionTimeMs = 0.0;
        auto lastTime = std::chrono::steady_clock::now();

        while (isRunning_) {
            auto currentTime = std::chrono::steady_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(currentTime - lastTime).count();

            if (duration >= 1000) {
                float fps = frameCount > 0 ? (float)frameCount * 1000.0f / duration : 0.0f;
                float avgPredictionTimeMs = frameCount > 0 ? (float)(totalPredictionTimeMs / frameCount) : 0.0f;
                logger_.info("LogicManager FPS: {:.2f}, Avg Prediction Time: {:.2f} ms", fps, avgPredictionTimeMs);
                frameCount = 0;
                totalPredictionTimeMs = 0.0;
                lastTime = currentTime;
            }

            if (!keyWatcher_->IsHandlerKeyDown()) {
                std::this_thread::yield();
                continue;
            }

            auto [frame, newVersion] = frameSlot_->GetFrame(lastFrameVersion_);
            if (!frame || !frame->IsValid()) {
                continue;
            }

            frameCount++;
            lastFrameVersion_ = newVersion;
            cv::Point targetPoint;
            bool hasTarget = false;

            if (remoteConfig_.mode() == ::capkfa::PixelSeek) {
                if (hsvFrame_.empty() || mask_.empty()) continue;
                cv::UMat croppedFrame = frame->ToUMat()(roi_);
                ConvertToHSV(croppedFrame);
                FilterInRange();
                auto point = GetHighestMaskPoint();
                if (point.has_value()) {
                    targetPoint = point.value();
                    hasTarget = true;
                }
            } else {
                auto startPredict = std::chrono::steady_clock::now();
                std::vector<Detection> detections = PredictYolo(frame);
                auto endPredict = std::chrono::steady_clock::now();
                totalPredictionTimeMs += std::chrono::duration_cast<std::chrono::microseconds>(endPredict - startPredict).count() / 1000.0;

                auto point = GetBiggestAimPoint(detections);
                if (point.has_value()) {
                    targetPoint = point.value();
                    hasTarget = true;
                }
            }

            if (hasTarget) {
                capkfa::RemoteConfigAim_Base aimBase = keyWatcher_->IsFlickKeyDown() ? remoteConfig_.aim().flick() : remoteConfig_.aim().aim();
                auto [moveX, moveY] = CalculateCoordinates(targetPoint, aimBase, 0, targetPoint.y);

                HandleFlick(moveX, moveY);

                if (remoteConfig_.mode() == ::capkfa::ObjectDetection) {
                    float boxCenterX = targetPoint.x;
                    float boxCenterY = targetPoint.y;
                    int cx = remoteConfig_.capture().size() / 2;
                    int cy = remoteConfig_.capture().size() / 2;
                    float distToCenter = std::sqrt((cx - boxCenterX) * (cx - boxCenterX) + (cy - boxCenterY) * (cy - boxCenterY));
                    if (distToCenter <= 4.0f) {
                        moveX = 0;
                        moveY = 0;
                    }
                }

                if (moveX != 0 || moveY != 0) {
                    km_->Move(moveX, moveY);
                    int32_t inputDelay = remoteConfig_.aim().input_delay();
                    if (inputDelay > 0) {
                        std::this_thread::sleep_for(std::chrono::microseconds(inputDelay));
                    }
                }
            }
        }
    } catch (const std::exception& e) {
        logger_.error("LogicManager crashed: {}", e.what());
        isRunning_ = false;
    } catch (...) {
        logger_.error("LogicManager crashed with unknown error");
        isRunning_ = false;
    }
}

void LogicManager::ConvertToHSV(const cv::UMat& frame) {
    if (!frame.empty() && !hsvFrame_.empty()) {
        cv::cvtColor(frame, hsvFrame_, cv::COLOR_BGR2HSV);
        cv::ocl::finish();
    } else {
        hsvFrame_ = cv::UMat();
    }
}

void LogicManager::FilterInRange() {
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

void LogicManager::DisplayFrame(const cv::Mat& frame, const std::string& windowName) {
    if (frame.empty()) {
        logger_.error("{} is empty", windowName);
        return;
    }
    try {
        cv::Mat mat;
        frame.copyTo(mat);
        if (mat.empty()) {
            logger_.error("Failed to convert to Mat");
            return;
        }
        cv::imshow(windowName, mat);
        cv::waitKey(1);
    } catch (const cv::Exception& e) {
        logger_.error("DisplayFrame error: {}", e.what());
    }
}

void LogicManager::DrawDetections(cv::Mat& image, const std::vector<Detection>& detections, float confThreshold) {
    if (image.empty()) {
        logger_.error("DrawDetections: Image is empty");
        return;
    }
    const cv::Scalar boxColor(0, 255, 0);
    const int imgWidth = image.cols;
    const int imgHeight = image.rows;

    try {
        for (const auto& det : detections) {
            if (det.confidence < confThreshold) continue;
            float x1 = std::max(0.0f, std::min(det.x1, static_cast<float>(imgWidth - 1)));
            float y1 = std::max(0.0f, std::min(det.y1, static_cast<float>(imgHeight - 1)));
            float x2 = std::max(0.0f, std::min(det.x2, static_cast<float>(imgWidth - 1)));
            float y2 = std::max(0.0f, std::min(det.y2, static_cast<float>(imgHeight - 1)));

            if (x2 <= x1 || y2 <= y1) {
                logger_.warn("Invalid detection box: ({}, {}, {}, {})", x1, y1, x2, y2);
                continue;
            }

            cv::rectangle(image, cv::Point2f(x1, y1), cv::Point2f(x2, y2), boxColor, 2, cv::LINE_AA);
            std::string label = cv::format("%.2f", det.confidence);
            int baseLine = 0;
            cv::Size labelSize = cv::getTextSize(label, cv::FONT_HERSHEY_SIMPLEX, 0.5, 1, &baseLine);
            float labelHeight = static_cast<float>(labelSize.height);
            float top = y1 < labelHeight ? labelHeight : y1;
            cv::rectangle(image, cv::Point2f(x1, top - labelSize.height - baseLine),
                          cv::Point2f(x1 + labelSize.width, top), boxColor, cv::FILLED);
            cv::putText(image, label, cv::Point2f(x1, top - 2), cv::FONT_HERSHEY_SIMPLEX, 0.5,
                        cv::Scalar(0, 0, 0), 1, cv::LINE_AA);
        }
    } catch (const cv::Exception& e) {
        logger_.error("DrawDetections error: {}", e.what());
    }
}

std::optional<cv::Point> LogicManager::GetHighestMaskPoint() {
    std::vector<cv::Point> points;
    cv::findNonZero(mask_, points);
    if (points.empty()) return std::nullopt;

    cv::Point min = points[0];
    for (size_t i = 1; i < points.size(); ++i)
        if (points[i].y < min.y) min = points[i];
    return min;
}

std::optional<cv::Point> LogicManager::GetBiggestAimPoint(const std::vector<Detection>& detections) {
    if (detections.empty()) return std::nullopt;

    int cx = remoteConfig_.capture().size() / 2;
    int cy = remoteConfig_.capture().size() / 2;
    float maxDistancePx = remoteConfig_.aim().fov() / 2;
    float maxDistSq = maxDistancePx * maxDistancePx;

    const Detection* biggest = nullptr;
    float maxArea = 0.0f;

    for (const auto& det : detections) {
        float centerX = (det.x1 + det.x2) / 2.0f;
        float centerY = (det.y1 + det.y2) / 2.0f;
        float distSq = (centerX - cx) * (centerX - cx) + (centerY - cy) * (centerY - cy);

        if (distSq < maxDistSq) {
            float area = (det.x2 - det.x1) * (det.y2 - det.y1);
            if (area > maxArea) {
                maxArea = area;
                biggest = &det;
            }
        }
    }

    if (biggest) {
        return cv::Point((biggest->x1 + biggest->x2) / 2, biggest->y2);
    }
    return std::nullopt;
}

std::tuple<short, short> LogicManager::CalculateCoordinates(cv::Point target, const capkfa::RemoteConfigAim_Base& aimBase, float y1, float y2) {
    int zoneX = (remoteConfig_.mode() == capkfa::ObjectDetection ? remoteConfig_.capture().size() : remoteConfig_.aim().fov()) / 2;
    int zoneY = (remoteConfig_.mode() == capkfa::ObjectDetection ? remoteConfig_.capture().size() : remoteConfig_.aim().fov()) / 2;
    int adjustedY = remoteConfig_.mode() == ::capkfa::ObjectDetection ? static_cast<int>(y2) - 3 : target.y;

    int dx = target.x - zoneX;
    int dy = adjustedY - zoneY;
    dx += remoteConfig_.aim().offset_x();
    dy += remoteConfig_.aim().offset_y();

    double distance = std::sqrt(dx * dx + dy * dy);
    double maxDistance = std::sqrt(2.0) * (remoteConfig_.aim().fov() / 2.0);

    double weight = std::clamp(1.0 - std::pow(distance / maxDistance, 2.0), 0.05, 1.0);
    double smoothX = aimBase.smooth_x().min() + (aimBase.smooth_x().max() - aimBase.smooth_x().min()) * weight;
    double smoothY = aimBase.smooth_y().min() + (aimBase.smooth_y().max() - aimBase.smooth_y().min()) * weight;

    short moveX = static_cast<short>(std::round(dx / smoothX));
    short moveY = static_cast<short>(std::round(dy / smoothY));

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(15, 30);
    short clampValue = static_cast<short>(dis(gen));

    moveX = std::clamp(moveX, static_cast<short>(-clampValue), static_cast<short>(clampValue));
    moveY = std::clamp(moveY, static_cast<short>(-clampValue), static_cast<short>(clampValue));

    if (remoteConfig_.aim().recoil().enabled()) {
        int duration = remoteConfig_.aim().recoil().duration();
        int factor = remoteConfig_.aim().recoil().factor();
        if (keyWatcher_->IsShotKeyDown()) {
            if (!recoil_active_) {
                recoil_active_ = true;
                recoil_start_time_ = std::chrono::steady_clock::now();
            }
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - recoil_start_time_).count();
            if (elapsed <= duration && !recoil_pattern_.empty()) {
                float t = elapsed / static_cast<float>(duration);
                size_t pattern_size = recoil_pattern_.size();
                float index = t * (pattern_size - 1);
                size_t idx = static_cast<size_t>(index);
                float frac = index - idx;

                float base_offset = (idx < pattern_size - 1)
                    ? recoil_pattern_[idx] + frac * (recoil_pattern_[idx + 1] - recoil_pattern_[idx])
                    : recoil_pattern_.back();

                float recoil_offset = base_offset * (factor / 100.0f);
                moveY += static_cast<short>(recoil_offset);
                moveY = std::clamp(moveY, static_cast<short>(-clampValue), static_cast<short>(clampValue));
            } else {
                recoil_active_ = false;
            }
        } else {
            recoil_active_ = false;
        }
    }

    return {moveX, moveY};
}

void LogicManager::HandleFlick(short moveX, short moveY) {
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

std::vector<Detection> LogicManager::PredictYolo(std::shared_ptr<Frame>& frame) {
    if (HasNvidiaGPU()) {
        return yoloModel_->Predict(frame);
    }
    return yoloModel_->Predict(frame);
}

bool LogicManager::HasNvidiaGPU() {
    int device_count;
    cudaError_t err = cudaGetDeviceCount(&device_count);
    return err == cudaSuccess && device_count > 0;
}
