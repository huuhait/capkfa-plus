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
        if (remoteConfig_.mode() != ::capkfa::RemoteConfig_Mode_ObjectDetection && (hsvFrame_.empty() || mask_.empty())) {
            logger_.error("HSV mode not configured. Call SetConfig to enable processing.");
            return;
        }
        if (remoteConfig_.mode() == ::capkfa::RemoteConfig_Mode_ObjectDetection && (!yoloModel_ || !frameSlot_ || !keyWatcher_ || !km_)) {
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

    if (remoteConfig_.mode() != ::capkfa::RemoteConfig_Mode_ObjectDetection) {
        int captureSize = remoteConfig_.capture().size();
        hsvFrame_ = cv::UMat(captureSize, captureSize, CV_8UC3);
        mask_ = cv::UMat(captureSize, captureSize, CV_8UC1);
        if (hsvFrame_.empty() || mask_.empty()) {
            logger_.error("Failed to initialize hsvFrame_ or mask_");
            throw std::runtime_error("Resource initialization failed");
        }
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

            if (remoteConfig_.mode() == ::capkfa::RemoteConfig_Mode_PixelSeek)
            {
                if (hsvFrame_.empty() || mask_.empty()) continue;
                cv::UMat umatFrame = frame->ToUMat();
                ConvertToHSV(umatFrame);
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

                auto point = GetObjectDetectionAimPoint(detections);
                if (point.has_value()) {
                    targetPoint = point.value();
                    hasTarget = true;
                }
            }

            if (hasTarget) {
                capkfa::RemoteConfigGame_Base aimBase = keyWatcher_->IsFlickKeyDown() ? remoteConfig_.game().flick().base() : remoteConfig_.game().aim().base();
                AimPoint aimPoint = CalculateCoordinates(targetPoint, aimBase);

                if (keyWatcher_->IsFlickKeyDown())
                {
                    HandleFlick(aimPoint);
                } else {
                    aimPoint = CalculateRecoil(aimPoint);
                    Move(aimPoint);
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
    if (!hsvFrame_.empty()) {
        cv::Scalar lowerb2(140, 60, 240);
        cv::Scalar upperb2(160, 255, 255);

        try {
            cv::inRange(hsvFrame_, lowerb2, upperb2, mask_);

            if (mask_.channels() != 1 || mask_.dims != 2) {
                logger_.error("FilterInRange: mask_ invalid shape! channels = {}, dims = {}", mask_.channels(), mask_.dims);
                mask_.release(); // Prevent further use
                return;
            }

            cv::ocl::finish();
        } catch (const cv::Exception& e) {
            logger_.error("FilterInRange OpenCV error: {}", e.what());
            mask_.release();
        }
    } else {
        logger_.error("FilterInRange: hsvFrame_ is empty");
        mask_ = cv::UMat(); // Clear
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

    const int captureSize = remoteConfig_.capture().size(); // e.g. 256
    const int fov = Fov();                                  // e.g. 80
    const int offset = (captureSize - fov) / 2;

    cv::Point min(-1, INT_MAX);

    for (const auto& pt : points) {
        if (pt.x < offset || pt.x >= (captureSize - offset) ||
            pt.y < offset || pt.y >= (captureSize - offset)) {
            continue; // outside FOV
            }

        if (pt.y < min.y) {
            min = pt;
        }
    }

    if (min.y == INT_MAX)
        return std::nullopt;

    return min;
}

std::optional<cv::Point> LogicManager::GetObjectDetectionAimPoint(const std::vector<Detection>& detections) {
    if (detections.empty()) return std::nullopt;

    const int captureSize = remoteConfig_.capture().size();
    const int fov = Fov();
    const int cx = captureSize / 2;
    const int cy = captureSize / 2;
    const float maxDistPx = fov / 2.0f;
    const float maxDistSq = maxDistPx * maxDistPx;
    const int fovOffset = (captureSize - fov) / 2;

    struct Target {
        const Detection* body = nullptr;
        const Detection* head = nullptr;
    };

    std::vector<Target> targets;

    // Step 1: Group detections by size & geometry (no class id)
    for (const auto& body : detections) {
        if (body.confidence < 0.27f) continue;

        float bodyWidth = body.x2 - body.x1;
        float bodyHeight = body.y2 - body.y1;
        float bodyArea = bodyWidth * bodyHeight;
        if (bodyArea <= 0) continue;

        Target target;
        target.body = &body;

        for (const auto& head : detections) {
            if (&head == &body || head.confidence < 0.27f) continue;

            float headWidth = head.x2 - head.x1;
            float headHeight = head.y2 - head.y1;
            float headArea = headWidth * headHeight;
            if (headArea <= 0 || headArea >= bodyArea * 0.9f) continue; // must be smaller

            // Head aspect ratio must be tall-ish
            float aspectRatio = headHeight / headWidth;
            if (aspectRatio < 1.2f) continue;

            float headCenterY = (head.y1 + head.y2) * 0.5f;
            float bodyCenterY = (body.y1 + body.y2) * 0.5f;
            bool aboveBodyCenter = headCenterY < bodyCenterY;

            // Head must be inside upper part of body box
            bool inside = head.x1 >= body.x1 &&
                          head.x2 <= body.x2 &&
                          head.y1 >= body.y1 &&
                          head.y2 <= (body.y1 + bodyHeight * 0.4f);

            if (aboveBodyCenter && inside) {
                target.head = &head;
                break;
            }
        }

        targets.push_back(target);
    }

    // Step 2: Score targets
    const Target* bestTarget = nullptr;
    float bestScore = -1.0f;

    for (const auto& target : targets) {
        const Detection* det = target.body;
        if (!det) continue;

        float centerX = (det->x1 + det->x2) * 0.5f;
        float centerY = (det->y1 + det->y2) * 0.5f;
        float distSq = (centerX - cx) * (centerX - cx) + (centerY - cy) * (centerY - cy);

        if (centerX < fovOffset || centerX > (captureSize - fovOffset) ||
            centerY < fovOffset || centerY > (captureSize - fovOffset)) {
            continue;
        }

        float area = (det->x2 - det->x1) * (det->y2 - det->y1);
        float score = det->confidence + (area / (fov * fov)) * 0.5f - (distSq / maxDistSq) * 0.3f;

        if (score > bestScore) {
            bestScore = score;
            bestTarget = &target;
        }
    }

    if (!bestTarget || !bestTarget->body) return std::nullopt;

    // Step 3: Aim point logic
    float centerX = (bestTarget->body->x1 + bestTarget->body->x2) * 0.5f;
    float height = bestTarget->body->y2 - bestTarget->body->y1;
    float width  = bestTarget->body->x2 - bestTarget->body->x1;
    float aimY;

    bool preferHead = keyWatcher_->IsFlickKeyDown();

    // Body looks like a standalone head (used only if no grouped head)
    bool looksLikeHeadAlone = !bestTarget->head &&
                              width > 0 && height > 0 &&
                              (height / width) >= 0.8f &&
                              (height / width) <= 1.8f;

    if (preferHead && bestTarget->head) {
        // Flick mode + valid head
        aimY = (bestTarget->head->y1 + bestTarget->head->y2) * 0.5f;
    } else if (looksLikeHeadAlone) {
        // No grouped head, but body looks like a head
        aimY = (bestTarget->body->y1 + bestTarget->body->y2) * 0.5f;
    } else {
        // Default: aim near body top
        aimY = bestTarget->body->y1 + height * 0.10f;
    }

    return cv::Point(static_cast<int>(centerX), static_cast<int>(aimY));
}

AimPoint LogicManager::CalculateCoordinates(cv::Point target, const capkfa::RemoteConfigGame_Base& aimBase) {
    /* ---------- Basic deltas ---------- */
    const int captureSize = remoteConfig_.capture().size();      // e.g. 256
    const int fov         = Fov();
    const int cx          = captureSize / 2;
    const int cy          = captureSize / 2;

    const int offsetX = aimBase.offset_x();
    const int offsetY = aimBase.offset_y();

    const int dx = target.x - cx + offsetX;
    const int dy = target.y - cy + offsetY;

    const bool flickKey = keyWatcher_->IsFlickKeyDown();
    const int pixelThreshold = 6;
    const int cooldownMs = remoteConfig_.game().flick().delay();

    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastFlickTime_).count();

    if (flickKey && elapsed >= cooldownMs && std::abs(dx) <= pixelThreshold && std::abs(dy) <= pixelThreshold) {
        return AimPoint{
            static_cast<short>(dx),
            static_cast<short>(dy),
            false  // no smoothing — instant
        };
    }

    /* ---------- Dead-zone ---------- */
    const float distSq          = static_cast<float>(dx * dx + dy * dy);
    constexpr float deadZoneRad = 2.0f;
    if (distSq < deadZoneRad * deadZoneRad)
        return {0, 0, true};

    /* ---------- Smooth factor based on FOV weighting ---------- */
    const double distance     = std::sqrt(distSq);
    const double maxDistance  = fov / 2.0;
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

    return AimPoint{
        moveX, moveY, true
    };
}

AimPoint LogicManager::CalculateRecoil(AimPoint point)
{
    if (!keyWatcher_->IsFlickKeyDown() && remoteConfig_.game().recoil().enabled()) {
        const int duration = remoteConfig_.game().recoil().duration(); // ms
        const int factor   = remoteConfig_.game().recoil().factor();   // %
        const int delay    = remoteConfig_.game().recoil().delay();    // ms

        if (keyWatcher_->IsShotKeyDown()) {
            const auto now = std::chrono::steady_clock::now();

            if (!recoil_active_) {
                recoil_active_     = true;
                recoil_start_time_ = now;
            }

            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - recoil_start_time_).count();

            if (elapsed >= delay && elapsed <= duration && !recoil_pattern_.empty()) {
                const float t     = (elapsed - delay) / static_cast<float>(duration - delay);
                const size_t n    = recoil_pattern_.size();
                const float fIdx  = t * (n - 1);
                const size_t idx  = static_cast<size_t>(fIdx);
                const float frac  = fIdx - idx;

                const float baseOffset = (idx < n - 1)
                    ? recoil_pattern_[idx] + frac * (recoil_pattern_[idx + 1] - recoil_pattern_[idx])
                    : recoil_pattern_.back();

                const float recoilOffset = baseOffset * (factor / 100.0f);
                point.y += static_cast<short>(recoilOffset);
            }

            if (elapsed > duration) {
                recoil_active_ = false; // finished pattern
            }
        } else {
            recoil_active_ = false; // trigger released
        }
    }

    return point;
}

void LogicManager::FlickMove(AimPoint point, bool instant)
{
    // Sensitivity
    float sensitivity = remoteConfig_.game().flick().sensitivity();

    if (!point.smooth && instant)
    {
        // Convert pixel coordinates to mouse units
        point.x = point.x / sensitivity;
        point.y = point.y / sensitivity;
    }

    Move(point);
}

void LogicManager::HandleFlick(AimPoint point) {
    if (!point.smooth) {
        lastFlickTime_ = std::chrono::steady_clock::now();
        FlickMove(point, true);
        km_->Click();
        std::this_thread::sleep_for(std::chrono::milliseconds(250)); // debounce
    } else {
        FlickMove(point, false);
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

void LogicManager::Move(AimPoint point)
{
    if (point.x == 0 && point.y == 0) {
        return; // No movement needed
    }

    km_->Move(point.x, point.y);
    int32_t inputDelay = remoteConfig_.game().input_delay();
    if (inputDelay > 0) {
        std::this_thread::sleep_for(std::chrono::microseconds(inputDelay));
    }
}

int LogicManager::Fov() {
    return keyWatcher_->IsFlickKeyDown() ? remoteConfig_.game().flick().base().fov() : remoteConfig_.game().aim().base().fov();
}
