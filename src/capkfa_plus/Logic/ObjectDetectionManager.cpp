#include "ObjectDetectionManager.h"
#include <cuda_runtime.h>
#include <opencv2/opencv.hpp>
#include <opencv2/core/ocl.hpp>

ObjectDetectionManager::ObjectDetectionManager(
    spdlog::logger& logger, std::shared_ptr<FrameSlot> frameSlot, std::shared_ptr<KeyWatcher> keyWatcher,
    std::shared_ptr<Km> km, std::unique_ptr<YoloModel> yoloModel, std::unique_ptr<CudaModel> cudaModel)
    : logger_(logger), frameSlot_(frameSlot), keyWatcher_(keyWatcher), km_(km),
      yoloModel_(std::move(yoloModel)), cudaModel_(std::move(cudaModel)), isRunning_(false), lastFrameVersion_(0) {}

ObjectDetectionManager::~ObjectDetectionManager()
{
    Stop();
}

void ObjectDetectionManager::Start()
{
    if (!isRunning_) {
        isRunning_ = true;
        handlerThread_ = std::thread(&ObjectDetectionManager::ProcessLoop, this);
    }
}

void ObjectDetectionManager::Stop()
{
    if (isRunning_) {
        isRunning_ = false;
        if (handlerThread_.joinable()) {
            handlerThread_.join();
        }
    }
}

void ObjectDetectionManager::ProcessLoop() {
    try {
        if (!frameSlot_ || !keyWatcher_ || !yoloModel_ || !km_) {
            logger_.error("ObjectDetectionManager initialization failed: frameSlot, keyWatcher, yoloModel or km is null");
            isRunning_ = false;
            return;
        }

        int frameCount = 0;
        auto lastTime = std::chrono::steady_clock::now();

        while (isRunning_) {
            auto currentTime = std::chrono::steady_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(currentTime - lastTime).count();

            if (duration >= 1000) {
                float fps = (float)frameCount * 1000.0f / duration;
                logger_.info("Handler Object Detection FPS: {}", fps);
                frameCount = 0;
                lastTime = currentTime;
            }

            if (keyWatcher_->IsHandlerKeyDown()) {
                auto [frame, newVersion] = frameSlot_->GetFrame(lastFrameVersion_);
                if (!frame || !frame->IsValid()) {
                    continue;
                }

                std::vector<Detection> detections = yoloModel_->Predict(frame);

                Detection* head = nullptr;
                float bestDistSq = std::numeric_limits<float>::max();
                float largestArea = 0.0f;
                constexpr int cx = 128;
                constexpr int cy = 128;
                constexpr float maxDistancePx = 40.0f;
                constexpr float maxDistSq = maxDistancePx * maxDistancePx;

                for (size_t i = 0; i < detections.size(); ++i) {
                    Detection& det = detections[i];
                    float area = (det.x2 - det.x1) * (det.y2 - det.y1);
                    float centerX = (det.x1 + det.x2) / 2.0f;
                    float headY = det.y2; // Will be adjusted in CalculateCoordinates
                    float distSq = (centerX - cx) * (centerX - cx) + (headY - cy) * (headY - cy);

                    if (distSq < maxDistSq && area > largestArea) {
                        largestArea = area;
                        bestDistSq = distSq;
                        head = &det;
                    }
                }

                if (head) {
                    cv::Point headPoint((head->x1 + head->x2) / 2, head->y2); // Initial point, adjusted in CalculateCoordinates
                    capkfa::RemoteConfigAimType aimType = remoteConfig_.aim().aim();
                    auto [moveX, moveY] = CalculateCoordinates(headPoint, aimType, head->y1, head->y2);

                    HandleFlick(moveX, moveY);

                    if (moveX != 0 || moveY != 0) {
                        km_->Move(moveX, moveY);

                        int32_t inputDelay = remoteConfig_.aim().aim().input_delay();
                        if (inputDelay > 0)
                        {
                            std::this_thread::sleep_for(std::chrono::microseconds(inputDelay));
                        }
                    }
                }

                // cv::Mat mat = frame->GetMat();
                // DrawDetections(mat, detections);
                // DisplayFrame(mat, "Detection");

                frameCount++;
                lastFrameVersion_ = newVersion;
            }
        }

        cv::destroyAllWindows();
    } catch (const std::exception& e) {
        logger_.error("ObjectDetectionManager crashed: {}", e.what());
        isRunning_ = false;
    } catch (...) {
        logger_.error("ObjectDetectionManager crashed with unknown error");
        isRunning_ = false;
    }
}

void ObjectDetectionManager::DisplayFrame(const cv::Mat& frame, const std::string& windowName) {
    if (frame.empty()) {
        std::cerr << windowName << " is empty" << std::endl;
        return;
    }

    try {
        cv::Mat mat;
        frame.copyTo(mat); // Ensure copy to CPU memory
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

void ObjectDetectionManager::DrawDetections(cv::Mat& image, const std::vector<Detection>& detections, float confThreshold) {
    if (image.empty()) {
        logger_.error("DrawDetections: Image is empty");
        return;
    }

    const cv::Scalar boxColor(0, 255, 0); // Green
    const int imgWidth = image.cols;
    const int imgHeight = image.rows;

    try {
        for (const auto& det : detections) {
            if (det.confidence < confThreshold) continue;

            // Validate coordinates
            float x1 = std::max(0.0f, std::min(det.x1, static_cast<float>(imgWidth - 1)));
            float y1 = std::max(0.0f, std::min(det.y1, static_cast<float>(imgHeight - 1)));
            float x2 = std::max(0.0f, std::min(det.x2, static_cast<float>(imgWidth - 1)));
            float y2 = std::max(0.0f, std::min(det.y2, static_cast<float>(imgHeight - 1)));

            if (x2 <= x1 || y2 <= y1) {
                logger_.warn("Invalid detection box: ({}, {}, {}, {})", x1, y1, x2, y2);
                continue;
            }

            // Draw box
            cv::rectangle(
                image,
                cv::Point2f(x1, y1),
                cv::Point2f(x2, y2),
                boxColor,
                2,
                cv::LINE_AA
            );

            // Format confidence as string
            std::string label = cv::format("%.2f", det.confidence);

            // Draw label background
            int baseLine = 0;
            cv::Size labelSize = cv::getTextSize(label, cv::FONT_HERSHEY_SIMPLEX, 0.5, 1, &baseLine);
            float labelHeight = static_cast<float>(labelSize.height);
            float top = y1 < labelHeight ? labelHeight : y1;
            cv::rectangle(
                image,
                cv::Point2f(x1, top - labelSize.height - baseLine),
                cv::Point2f(x1 + labelSize.width, top),
                boxColor,
                cv::FILLED
            );

            // Draw label text
            cv::putText(
                image,
                label,
                cv::Point2f(x1, top - 2),
                cv::FONT_HERSHEY_SIMPLEX,
                0.5,
                cv::Scalar(0, 0, 0),
                1,
                cv::LINE_AA
            );
        }
    } catch (const cv::Exception& e) {
        logger_.error("DrawDetections error: {}", e.what());
    }
}

std::tuple<short, short> ObjectDetectionManager::CalculateCoordinates(cv::Point target, const capkfa::RemoteConfigAimType& aimType, float y1, float y2) {
    constexpr int screenCenterX = 128;
    constexpr int screenCenterY = 128;

    auto adjustedY = static_cast<int>(y2) - 3; // always = bottom - 3

    // Offset from center
    int dx = target.x - screenCenterX;
    int dy = adjustedY - screenCenterY;

    // Apply offset config
    dx += remoteConfig_.aim().offset_x();
    dy += remoteConfig_.aim().offset_y();

    // Distance to center
    double distance = std::sqrt(dx * dx + dy * dy);
    double maxDistance = std::sqrt(2.0) * 128.0;

    // Smooth interpolation curve (nonlinear, stable)
    double weight = std::clamp(1.0 - std::pow(distance / maxDistance, 2.0), 0.05, 1.0);
    double smoothX = aimType.smooth_x().min() + (aimType.smooth_x().max() - aimType.smooth_x().min()) * weight;
    double smoothY = aimType.smooth_y().min() + (aimType.smooth_y().max() - aimType.smooth_y().min()) * weight;

    // Final movement (rounded)
    short moveX = static_cast<short>(std::round(dx / smoothX));
    short moveY = static_cast<short>(std::round(dy / smoothY));

    // Random clamp between 15 and 30
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(15, 30);
    short clampValue = static_cast<short>(dis(gen));

    // Clamp moveX and moveY
    moveX = std::clamp(moveX, static_cast<short>(-clampValue), static_cast<short>(clampValue));
    moveY = std::clamp(moveY, static_cast<short>(-clampValue), static_cast<short>(clampValue));

    // Apply recoil compensation if enabled
    if (remoteConfig_.aim().recoil()) {
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
                moveY += static_cast<short>(recoil_offset);
                // Re-clamp moveY after recoil
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

void ObjectDetectionManager::HandleFlick(short moveX, short moveY) {
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

void ObjectDetectionManager::SetConfig(const ::capkfa::RemoteConfig& config)
{
    Stop();
    remoteConfig_ = config;
    Start();
}

std::vector<Detection> ObjectDetectionManager::Predict(std::shared_ptr<Frame>& frame)
{
    if (hasNvidiaGPU()) {
        // return cudaModel_->Predict(input);
        return yoloModel_->Predict(frame);
    } else {
        return yoloModel_->Predict(frame);
    }
}

bool ObjectDetectionManager::hasNvidiaGPU()
{
    int device_count;
    cudaError_t err = cudaGetDeviceCount(&device_count);
    return err == cudaSuccess && device_count > 0;
}
