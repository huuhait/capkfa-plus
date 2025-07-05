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
                logger_.debug("LogicManager FPS: {:.2f}, Avg Prediction Time: {:.2f} ms", fps, avgPredictionTimeMs);
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

                auto point = GetBiggestAimPoint(detections, keyWatcher_->IsFlickKeyDown());
                if (point.has_value()) {
                    targetPoint = point.value();
                    hasTarget = true;
                }
            }

            if (hasTarget) {
                capkfa::RemoteConfigAim_Base aimBase = keyWatcher_->IsFlickKeyDown() ? remoteConfig_.aim().flick() : remoteConfig_.aim().aim();
                auto [moveX, moveY] = CalculateCoordinates(targetPoint, aimBase);

                // if (remoteConfig_.mode() == ::capkfa::ObjectDetection) {
                //     float boxCenterX = targetPoint.x;
                //     float boxCenterY = targetPoint.y;
                //     int cx = remoteConfig_.capture().size() / 2;
                //     int cy = remoteConfig_.capture().size() / 2;
                //     float distToCenter = std::sqrt((cx - boxCenterX) * (cx - boxCenterX) + (cy - boxCenterY) * (cy - boxCenterY));
                //     if (distToCenter <= 4.0f) {
                //         moveX = 0;
                //         moveY = 0;
                //     }
                // }

                if (keyWatcher_->IsFlickKeyDown())
                {
                    HandleFlick(moveX, moveY);
                } else if (moveX != 0 || moveY != 0) {
                    km_->Move(moveX, moveY);
                    int32_t inputDelay = remoteConfig_.aim().input_delay();
                    if (inputDelay > 0) {
                        std::this_thread::sleep_for(std::chrono::microseconds(inputDelay));
                    }
                }
            }

            // if (remoteConfig_.mode() == ::capkfa::ObjectDetection) {
            //     cv::Mat displayFrame = frame->GetMat();
            //     DrawDetections(displayFrame, PredictYolo(frame), 0.27f);
            //     DisplayFrame(displayFrame, "Object Detection");
            // }
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

            cv::Scalar color = det.class_id == 0 ? cv::Scalar(0, 255, 0) : cv::Scalar(0, 0, 255);
            cv::rectangle(image, cv::Point2f(x1, y1), cv::Point2f(x2, y2), color, 2, cv::LINE_AA);

            std::string label = cv::format("%d | conf=%.2f | obj=%.2f | cls=%.2f",
                                          det.class_id, det.confidence, det.obj_score, det.cls_score);
            float text_y = y1 - 7 < 10 ? 10 : y1 - 7;
            cv::putText(image, label, cv::Point2f(x1, text_y), cv::FONT_HERSHEY_SIMPLEX,
                        0.4, color, 1, cv::LINE_AA);
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

std::optional<cv::Point> LogicManager::GetBiggestAimPoint(const std::vector<Detection>& detections, bool flick) {
    if (detections.empty()) return std::nullopt;

    const int captureSize = remoteConfig_.capture().size();
    const int fov = remoteConfig_.aim().fov();
    const int cx = captureSize / 2;
    const int cy = captureSize / 2;
    const float maxDistPx = fov / 2.0f;
    const float maxDistSq = maxDistPx * maxDistPx;
    const int fovOffset = (captureSize - fov) / 2;

    const Detection* best = nullptr;
    float bestScore = -1.0f;

    for (const auto& det : detections) {
        if (det.confidence < 0.27f) continue;

        float centerX = (det.x1 + det.x2) * 0.5f;
        float centerY = (det.y1 + det.y2) * 0.5f;
        float distSq = (centerX - cx) * (centerX - cx) + (centerY - cy) * (centerY - cy);

        if (centerX < fovOffset || centerX > (captureSize - fovOffset) ||
            centerY < fovOffset || centerY > (captureSize - fovOffset)) {
            continue;
            }

        float score = det.confidence;
        float area = (det.x2 - det.x1) * (det.y2 - det.y1);
        score += area / (fov * fov) * 0.5f;
        score -= distSq / maxDistSq * 0.3f;

        if (score > bestScore) {
            best = &det;
            bestScore = score;
        }
    }

    if (!best) return std::nullopt;

    // Use raw detection center coordinates
    float centerX = (best->x1 + best->x2) * 0.5f;
    float boxHeight = best->y2 - best->y1;
    float aimY = best->class_id == 1 ? (best->y1 + best->y2) * 0.5f : best->y1 + boxHeight * 0.07f;

    cv::Point aimPoint(static_cast<int>(centerX), static_cast<int>(aimY));
    logger_.info("Selected detection: class={}, conf={:.2f}, aimPoint=({}, {}), score={:.2f}",
                best->class_id, best->confidence, aimPoint.x, aimPoint.y, bestScore);
    return aimPoint;
}

std::tuple<short, short> LogicManager::CalculateCoordinates(cv::Point target, const capkfa::RemoteConfigAim_Base& aimBase) {
    /* ---------- Basic deltas ---------- */
    const int captureSize = remoteConfig_.capture().size();      // e.g. 256
    const int cx          = captureSize / 2;
    const int cy          = captureSize / 2;

    const int offsetX = remoteConfig_.aim().offset_x();
    const int offsetY = remoteConfig_.aim().offset_y();

    const int dx = target.x - cx + offsetX;
    const int dy = target.y - cy + offsetY;

    /* ---------- Dead-zone ---------- */
    const float distSq          = static_cast<float>(dx * dx + dy * dy);
    constexpr float deadZoneRad = 2.0f;
    if (distSq < deadZoneRad * deadZoneRad)
        return {0, 0};

    /* ---------- Smooth factor based on FOV weighting ---------- */
    const double distance     = std::sqrt(distSq);
    const double maxDistance  = remoteConfig_.aim().fov() / 2.0;
    const double weight       = std::clamp(distance / maxDistance, 0.1, 1.0);

    const double smoothX =
        aimBase.smooth_x().min() +
        (aimBase.smooth_x().max() - aimBase.smooth_x().min()) * weight;

    const double smoothY =
        aimBase.smooth_y().min() +
        (aimBase.smooth_y().max() - aimBase.smooth_y().min()) * weight;

    /* ---------- Sub-pixel accumulator ---------- */
    static double accX = 0.0, accY = 0.0;
    accX += dx / smoothX;
    accY += dy / smoothY;

    short moveX = 0, moveY = 0;

    if (std::abs(accX) >= 0.6 || std::abs(accY) >= 0.6) {
        moveX = static_cast<short>(std::round(accX));
        moveY = static_cast<short>(std::round(accY));
        accX -= moveX;
        accY -= moveY;

        moveX = std::clamp<int>(moveX, -15, 30);
        moveY = std::clamp<int>(moveY, -15, 30);
    }

    /* ---------- Recoil compensation (applied after smoothing) ---------- */
    if (!keyWatcher_->IsFlickKeyDown() && remoteConfig_.aim().recoil().enabled()) {
        const int duration = remoteConfig_.aim().recoil().duration(); // ms
        const int factor   = remoteConfig_.aim().recoil().factor();   // %
        constexpr int clampValue = 30;                                // safety

        if (keyWatcher_->IsShotKeyDown()) {
            if (!recoil_active_) {          // first shot
                recoil_active_    = true;
                recoil_start_time_ = std::chrono::steady_clock::now();
            }

            const auto now      = std::chrono::steady_clock::now();
            const auto elapsed  = std::chrono::duration_cast<std::chrono::milliseconds>(
                                     now - recoil_start_time_)
                                     .count();

            if (elapsed <= duration && !recoil_pattern_.empty()) {
                const float t          = elapsed / static_cast<float>(duration);
                const size_t n         = recoil_pattern_.size();
                const float  fIdx      = t * (n - 1);
                const size_t idx       = static_cast<size_t>(fIdx);
                const float  frac      = fIdx - idx;

                /* linear-interpolated pattern value */
                const float baseOffset = (idx < n - 1)
                                             ? recoil_pattern_[idx] +
                                                   frac * (recoil_pattern_[idx + 1] -
                                                           recoil_pattern_[idx])
                                             : recoil_pattern_.back();

                const float recoilOffset = baseOffset * (factor / 100.0f);
                moveY += static_cast<short>(recoilOffset);
                moveY  = std::clamp(moveY,
                                   static_cast<short>(-clampValue),
                                   static_cast<short>(clampValue));
            } else {
                recoil_active_ = false;     // finished pattern
            }
        } else {
            recoil_active_ = false;         // trigger released
        }
    }

    return {moveX, moveY};
}

void LogicManager::HandleFlick(short moveX, short moveY) {
    if (!keyWatcher_->IsFlickKeyDown()) return;

    static auto lastClick = std::chrono::steady_clock::now();
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastClick).count();

    if (moveX > 5 && moveY > 5)
    {
        moveX = moveX/0.7;
        moveY = moveY/0.7;
    }

    logger_.info("Flick moveX: {}, moveY: {}", moveX, moveY);

    if (moveX != 0 || moveY != 0) {
        km_->Move(moveX, moveY);
    }

    if (elapsed > 300 && moveX == 0 && moveY == 0) {
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
