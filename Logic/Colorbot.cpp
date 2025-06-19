#include "Colorbot.h"
#include <opencv2/opencv.hpp>
#include <opencv2/core/ocl.hpp>
#include <iostream>
#include <chrono>

Colorbot::Colorbot(std::shared_ptr<FrameSlot> frameSlot, std::shared_ptr<KeyWatcher> keyWatcher, std::shared_ptr<Km> km)
    : frameSlot_(frameSlot), keyWatcher_(keyWatcher), km_(km), isRunning_(false), lastFrameVersion_(0) {
    if (!cv::ocl::haveOpenCL()) {
        std::cerr << "OpenCL not available, falling back to CPU" << std::endl;
    } else {
        cv::ocl::setUseOpenCL(true);
        cv::ocl::Device device = cv::ocl::Device::getDefault();
        std::cout << "Using OpenCL device: " << device.name() << std::endl;
    }
}

Colorbot::~Colorbot() {
    Stop();
}

void Colorbot::Start() {
    if (!isRunning_) {
        if (bgrFrame_.empty() || hsvFrame_.empty() || mask_.empty()) {
            std::cerr << "Warning: Colorbot not configured. Call SetConfig to enable processing." << std::endl;
        }
        isRunning_ = true;
        handlerThread_ = std::thread(&Colorbot::ProcessLoop, this);
    }
}

void Colorbot::Stop() {
    if (isRunning_) {
        isRunning_ = false;
        if (handlerThread_.joinable()) {
            handlerThread_.join();
        }
        // Clear buffers and destroy only the instance-specific window
        bgrFrame_ = cv::UMat();
        hsvFrame_ = cv::UMat();
        mask_ = cv::UMat();
        cv::destroyAllWindows();
    }
}

void Colorbot::SetConfig(const ::capkfa::RemoteConfig& config) {
    int width = config.aim().fov();
    int height = config.aim().fov();

    if (width <= 0 || height <= 0) {
        throw std::runtime_error("Invalid frame size: " + std::to_string(width) + "x" + std::to_string(height));
    }

    Stop(); // Stop processing to safely initialize buffers

    // Initialize UMat buffers
    remoteConfig_ = config;
    bgrFrame_ = cv::UMat(height, width, CV_8UC3);
    hsvFrame_ = cv::UMat(height, width, CV_8UC3);
    mask_ = cv::UMat(height, width, CV_8UC1);

    std::cout << "Colorbot config set: size " << width << "x" << height << std::endl;

    Start();
}

void Colorbot::ConvertToBGR(const cv::UMat& frame) {
    if (!frame.empty() && !bgrFrame_.empty()) {
        cv::cvtColor(frame, bgrFrame_, cv::COLOR_BGRA2BGR);
        cv::ocl::finish();
    } else {
        std::cerr << "Input frame or bgrFrame_ is empty" << std::endl;
        bgrFrame_ = cv::UMat();
    }
}

void Colorbot::ConvertToHSV() {
    if (!bgrFrame_.empty() && !hsvFrame_.empty()) {
        cv::cvtColor(bgrFrame_, hsvFrame_, cv::COLOR_BGR2HSV);
        cv::ocl::finish();
    } else {
        std::cerr << "bgrFrame_ or hsvFrame_ is empty" << std::endl;
        hsvFrame_ = cv::UMat();
    }
}

void Colorbot::FilterInRange() {
    if (!hsvFrame_.empty() && !mask_.empty()) {
        cv::Scalar lowerb2(140, 60, 240);
        cv::Scalar upperb2(160, 255, 255);
        cv::inRange(hsvFrame_, lowerb2, upperb2, mask_);
        cv::ocl::finish();
    } else {
        std::cerr << "hsvFrame_ or mask_ is empty" << std::endl;
        mask_ = cv::UMat();
    }
}

void Colorbot::DisplayFrame(const cv::UMat& frame, const std::string& windowName) {
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

void Colorbot::ProcessLoop() {
    int frameCount = 0;
    auto lastTime = std::chrono::steady_clock::now();

    while (isRunning_) {
        if (bgrFrame_.empty() || hsvFrame_.empty() || mask_.empty()) {
            continue; // Skip if buffers not initialized
        }

        auto currentTime = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(currentTime - lastTime).count();

        if (duration >= 1000) {
            float fps = (float)frameCount * 1000.0f / duration;
            std::cout << "Handler FPS: " << fps << std::endl;
            frameCount = 0;
            lastTime = currentTime;
        }

        auto [frame, newVersion] = frameSlot_->GetFrame(lastFrameVersion_);
        if (!frame.empty()) {
            if (!keyWatcher_->IsHandlerKeyDown()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }

            frameCount++;
            lastFrameVersion_ = newVersion;
            // DisplayFrame(frame, "Output Mask");
            ConvertToBGR(frame);
            ConvertToHSV();
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

        if (GetAsyncKeyState('Q')) {
            isRunning_ = false;
        }
    }
}

std::optional<cv::Point> Colorbot::GetHighestMaskPoint() {
    std::vector<cv::Point> points;
    cv::findNonZero(mask_, points);
    if (points.empty()) return std::nullopt;

    cv::Point min = points[0];
    for (size_t i = 1; i < points.size(); ++i)
        if (points[i].y < min.y) min = points[i];

    return min;
}

std::tuple<short, short> Colorbot::CalculateCoordinates(cv::Point p, capkfa::RemoteConfigAimType aimType)
{
    double m = remoteConfig_.aim().fov() / 2.0;
    double dx = p.x - m, dy = p.y - m;
    double distance = std::sqrt(dx * dx + dy * dy);
    double maxDistance = m * std::sqrt(2.0);

    double smoothX = aimType.smooth_x().min() + (aimType.smooth_x().max() - aimType.smooth_x().min()) * (1.0 - distance / maxDistance);
    double smoothY = aimType.smooth_y().min() + (aimType.smooth_y().max() - aimType.smooth_y().min()) * (1.0 - distance / maxDistance);

    short adjustedX = static_cast<short>(std::round((p.x - m + remoteConfig_.aim().offset_x()) / smoothX));
    short adjustedY = static_cast<short>(std::round((p.y - m + remoteConfig_.aim().offset_y()) / smoothY));

    return {adjustedX, adjustedY};
}

void Colorbot::HandleFlick(short moveX, short moveY) {
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