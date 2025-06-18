#include "App.h"
#include <iostream>

#include "Frame/FrameCapturer.h"
#include "Frame/FrameHandler.h"

App::App(std::unique_ptr<LicenseClient> client,
         std::shared_ptr<FrameSlot> frameSlot,
         std::unique_ptr<DeviceManager> deviceManager,
         std::shared_ptr<FrameCapturer> capturer,
         std::shared_ptr<FrameHandler> handler)
    : client_(std::move(client)),
      frameSlot_(frameSlot),
      deviceManager_(std::move(deviceManager)),
      capturer_(capturer),
      handler_(handler) {}

bool App::Start(const std::string& userId) {
    try {
        if (!CheckServerStatus()) {
            std::cerr << "Server not available" << std::endl;
            return false;
        }

        std::cout << "Starting capture..." << std::endl;
        capturer_->StartCapture();
        handler_->Start();
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Start failed: " << e.what() << std::endl;
        return false;
    }
}

void App::Stop() {
    try {
        if (capturer_) {
            capturer_->StopCapture();
        }
        if (handler_) {
            handler_->Stop();
        }
        cv::destroyAllWindows();
    } catch (const std::exception& e) {
        std::cerr << "Stop failed: " << e.what() << std::endl;
    }
}

bool App::CheckServerStatus() {
    try {
        capkfa::GetStatusResponse status_response = client_->GetStatus();

        return status_response.online();
    } catch (const std::exception& e) {
        std::cerr << "Server status check failed: " << e.what() << std::endl;
    }
    return false;
}

::capkfa::GetConfigResponse App::GetConfiguration(const std::string& configKey) {
    ::capkfa::GetConfigResponse response;
    try {
        ::capkfa::GetConfigRequest request;
        response = client_->GetRemoteConfig(request);
    } catch (const std::exception& e) {
        std::cerr << "Configuration retrieval failed: " << e.what() << std::endl;
    }
    return response;
}