#include "ObjectDetectionManager.h"
#include <cuda_runtime.h>
#include <opencv2/opencv.hpp>
#include <opencv2/core/ocl.hpp>

ObjectDetectionManager::ObjectDetectionManager(
    std::shared_ptr<FrameSlot> frameSlot, std::shared_ptr<KeyWatcher> keyWatcher,
    std::shared_ptr<Km> km, std::unique_ptr<YoloModel> yoloModel, std::unique_ptr<CudaModel> cudaModel)
    : frameSlot_(frameSlot), keyWatcher_(keyWatcher), km_(km),
      yoloModel_(std::move(yoloModel)), cudaModel_(std::move(cudaModel)), isRunning_(false), lastFrameVersion_(0) {
    std::cout << "ObjectDetectionManager initialized: "
              << "frameSlot=" << (frameSlot_ ? "valid" : "null") << ", "
              << "keyWatcher=" << (keyWatcher_ ? "valid" : "null") << ", "
              << "yoloModel=" << (yoloModel_ ? "valid" : "null") << std::endl;
}

ObjectDetectionManager::~ObjectDetectionManager()
{
    Stop();
}

void ObjectDetectionManager::Start()
{
    std::cout << "Starting ObjectDetectionManager" << std::endl;
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

void ObjectDetectionManager::DisplayFrame(const cv::UMat& frame, const std::string& windowName) {
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

void ObjectDetectionManager::DrawDetections(cv::UMat& image, const std::vector<Detection>& detections, float confThreshold)
{
    const cv::Scalar boxColor(0, 255, 0); // Green
    for (const auto& det : detections) {
        if (det.confidence < confThreshold) continue;

        // Draw box using float coordinates for accuracy
        cv::rectangle(
            image,
            cv::Point2f(det.x1, det.y1),
            cv::Point2f(det.x2, det.y2),
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
        float top = det.y1 < labelHeight ? labelHeight : det.y1;
        cv::rectangle(
            image,
            cv::Point2f(det.x1, top - labelSize.height - baseLine),
            cv::Point2f(det.x1 + labelSize.width, top),
            boxColor,
            cv::FILLED
        );

        // Draw label text (confidence)
        cv::putText(
            image,
            label,
            cv::Point2f(det.x1, top - 2),
            cv::FONT_HERSHEY_SIMPLEX,
            0.5,
            cv::Scalar(0, 0, 0), // Black text for contrast
            1,
            cv::LINE_AA
        );
    }
}

void ObjectDetectionManager::ProcessLoop() {
    try {
        if (!frameSlot_ || !keyWatcher_ || !yoloModel_ || !km_) {
            std::cerr << "Invalid initialization" << std::endl;
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
                std::cout << "Handler OD FPS: " << fps << std::endl;
                frameCount = 0;
                lastTime = currentTime;
            }

            auto [frame, newVersion] = frameSlot_->GetFrame(lastFrameVersion_);
            if (!frame.IsValid()) {
                continue;
            }

            if (keyWatcher_->IsHandlerKeyDown())
            {
                std::vector<Detection> detections = yoloModel_->Predict(frame);

                // Find head detection with highest confidence
                Detection* head = nullptr;
                float bestDistSq = std::numeric_limits<float>::max();

                constexpr int cx = 128;
                constexpr int cy = 128;
                constexpr float maxDistancePx = 40.0f;
                constexpr float maxDistSq = maxDistancePx * maxDistancePx;

                // Iterate over pairs of detections to find head-body pairs
                for (size_t i = 0; i < detections.size(); ++i) {
                    const auto& small = detections[i]; // Potential head
                    int h1 = small.y2 - small.y1;

                    for (size_t j = 0; j < detections.size(); ++j) {
                        if (i == j) continue; // Skip same detection
                        const auto& big = detections[j]; // Potential body
                        int h2 = big.y2 - big.y1;

                        // Check if small is a head relative to big (body)
                        if (h1 >= h2) continue; // Head should be smaller than body
                        if (small.confidence < 0.3f || big.confidence < 0.3f) continue;

                        // Check if small is contained within big horizontally
                        if (small.x1 < big.x1 || small.x2 > big.x2) continue;

                        // Check vertical containment (head in upper part of body)
                        int verticalLimit = big.y1 + h2 * 2 / 3; // Allow head in top 2/3 of body
                        if (small.y1 < big.y1 || small.y2 > verticalLimit) continue;

                        // Compute center & distance from crosshair
                        int centerX = (small.x1 + small.x2) / 2;
                        int centerY = (small.y1 + small.y2) / 2;
                        float dx = static_cast<float>(centerX - cx);
                        float dy = static_cast<float>(centerY - cy);
                        float distSq = dx * dx + dy * dy;

                        if (distSq > maxDistSq) continue; // Reject if too far

                        // Valid head-body pair and within distance → select
                        if (distSq < bestDistSq) {
                            bestDistSq = distSq;
                            head = const_cast<Detection*>(&small); // Note: Ensure thread safety
                        }
                    }
                }

                if (head) {
                    int centerX = (head->x1 + head->x2) / 2;
                    int centerY = (head->y1 + head->y2) / 2;
                    cv::Point headCenter(centerX, centerY);

                    capkfa::RemoteConfigAimType aimType = remoteConfig_.aim().aim();
                    auto [moveX, moveY] = CalculateCoordinates(headCenter, aimType);

                    HandleFlick(moveX, moveY);

                    if (moveX != 0 || moveY != 0) {
                        km_->Move(moveX, moveY);
                    }
                }

                // Visualize detections for debugging
                // cv::UMat mat = frame.ToUMat();
                // DrawDetections(mat, detections);
                // DisplayFrame(mat, "Object Detection Output");
                frameCount++;
                lastFrameVersion_ = newVersion;
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "ObjectDetectionManager crashed: " << e.what() << std::endl;
        isRunning_ = false;
    } catch (...) {
        std::cerr << "ObjectDetectionManager crashed: Unknown error" << std::endl;
        isRunning_ = false;
    }
}

std::tuple<short, short> ObjectDetectionManager::CalculateCoordinates(cv::Point target, const capkfa::RemoteConfigAimType& aimType) {
    constexpr int screenCenterX = 208;
    constexpr int screenCenterY = 208;

    // Offset from center
    int dx = target.x - screenCenterX;
    int dy = target.y - screenCenterY;

    // Apply offset config
    dx += remoteConfig_.aim().offset_x();
    dy += remoteConfig_.aim().offset_y();

    // Distance to center
    double distance = std::sqrt(dx * dx + dy * dy);
    double maxDistance = std::sqrt(2.0) * 208.0;

    // Smooth interpolation curve (nonlinear, stable)
    double weight = std::clamp(1.0 - std::pow(distance / maxDistance, 2.0), 0.05, 1.0);
    double smoothX = aimType.smooth_x().min() + (aimType.smooth_x().max() - aimType.smooth_x().min()) * weight;
    double smoothY = aimType.smooth_y().min() + (aimType.smooth_y().max() - aimType.smooth_y().min()) * weight;

    // Final movement (rounded)
    short moveX = static_cast<short>(std::round(dx / smoothX));
    short moveY = static_cast<short>(std::round(dy / smoothY));

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

std::vector<Detection> ObjectDetectionManager::Predict(const Frame& frame)
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
