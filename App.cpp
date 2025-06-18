#include "App.h"
#include <iostream>

#include "Logic/LogicManager.h"
#include "Frame/FrameCapturer.h"
#include "Movement/CommanderClient.h"

App::App(std::unique_ptr<LicenseClient> client,
         std::shared_ptr<CommanderClient> commanderClient,
         std::shared_ptr<KeyWatcher> keyWatcher,
         std::unique_ptr<FrameCapturer> capturer,
         std::shared_ptr<LogicManager> logicManager)
    : licenseClient_(std::move(client)),
      commanderClient_(commanderClient),
      keyWatcher_(keyWatcher),
      capturer_(std::move(capturer)),
      logicManager_(logicManager) {}

bool App::Start(const std::string& userId) {
    key_ = "MIKU-BC76F17DC89C8F8881EA83822C2FCA54";
    hwid_ = "490F6EA652DF94701F034CFAD4C5565F384F08A43257F9778BB2BDF35876ECC9";

    try {
        if (!CheckServerStatus()) {
            std::cerr << "Server not available" << std::endl;
            return false;
        }

        auto create_session_response = CreateSession(key_, hwid_);
        if (!create_session_response.valid()) {
            std::cerr << "Session creation failed" << std::endl;
            return false;
        }

        std::cout << "Session ID: " << create_session_response.session_id() << std::endl;

        StartConfigStream();
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
        if (logicManager_) {
            logicManager_->Stop();
        }
        StopConfigStream();
    } catch (const std::exception& e) {
        std::cerr << "Stop failed: " << e.what() << std::endl;
    }
}

bool App::CheckServerStatus() {
    try {
        capkfa::GetStatusResponse status_response = licenseClient_->GetStatus();

        return status_response.online();
    } catch (const std::exception& e) {
        std::cerr << "Server status check failed: " << e.what() << std::endl;
    }
    return false;
}

::capkfa::CreateSessionResponse App::CreateSession(const std::string& key, const std::string& hwid) {
    ::capkfa::CreateSessionResponse response;
    try {
        ::capkfa::CreateSessionRequest request;
        request.set_key(key);
        request.set_hwid(hwid);
        request.set_version("1.0.0");
        return licenseClient_->CreateSession(request);
    } catch (const std::exception& e) {
        std::cerr << "Session creation failed: " << e.what() << std::endl;
    }
}

void App::StartConfigStream() {
    if (!isStreamingConfig_) {
        isStreamingConfig_ = true;
        streamConfigThread_ = std::thread(&App::ProcessConfigStreaming, this);
    }
}

void App::StopConfigStream() {
    if (isStreamingConfig_) {
        isStreamingConfig_ = false;
        if (streamConfigThread_.joinable()) {
            streamConfigThread_.join();
        }
    }
}

void App::ProcessConfigStreaming() {
    ::capkfa::GetConfigRequest request;
    request.set_key(key_);
    LicenseClient::StreamConfigReader reader = licenseClient_->StreamConfig(request);
    ::capkfa::GetConfigResponse response;

    while (reader.Read(response) && isStreamingConfig_) {
        capturer_->SetConfig(response.remote_config());
        logicManager_->SetConfig(response.remote_config());
        keyWatcher_->SetConfig(response.remote_config());
        commanderClient_->SetConfig(response.remote_config());
    }

    reader.Finish();
}
