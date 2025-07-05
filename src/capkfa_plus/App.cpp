#include <iostream>
#include "App.h"

#include <opencv2/core/ocl.hpp>

#include "Config.h"
#include "Obfuscate.h"
#include "HWIDTool.h"
#include "Logic/LogicManager.h"
#include "Frame/FrameCapturer.h"
#include "Frame/NDICapturer.h"
#include "Movement/CommanderClient.h"

App::App(spdlog::logger& logger,
        std::unique_ptr<LicenseClient> client,
        std::shared_ptr<CommanderClient> commanderClient,
        std::shared_ptr<KeyWatcher> keyWatcher,
        std::unique_ptr<FrameCapturer> frameCapturer,
        std::unique_ptr<NDICapturer> ndiCapturer,
        std::shared_ptr<LogicManager> logicManager)
    : logger_(logger),
      licenseClient_(std::move(client)),
      commanderClient_(commanderClient),
      keyWatcher_(keyWatcher),
      frameCapturer_(std::move(frameCapturer)),
      ndiCapturer_(std::move(ndiCapturer)),
      logicManager_(logicManager) {}

bool App::Start() {
    cv::ocl::setUseOpenCL(true);

    constexpr auto obfDevKey = $o("MIKU-BC76F17DC89C8F8881EA83822C2FCA54");
    auto obfGetHWID = $of(HWIDTool::GetHWID);
    std:: string computerHWID = obfGetHWID();

    hwid_ = $d_inline(LOCKED_HWID);

    if (computerHWID != hwid_) {
       logger_.error("HWID mismatch {} != {}", computerHWID, hwid_);
       return false;
    }

    std::cout << "Enter your listener port ]k y ]e: ";
    std::getline(std::cin, key_);

    // DEV BLOCK
    if (key_.empty()) {
        key_ = $d_inline(obfDevKey);
    }
    // DEV BLOCK

    try {
        auto obfCheckStatusFunc = $om(CheckServerStatus, App, bool);
        if (!$call(this, obfCheckStatusFunc)) {
            logger_.error("Server not available");
            return false;
        }

        auto obfCreateSessionFunc = $om(CreateSession, App, ::capkfa::CreateSessionResponse);
        auto create_session_response = $call(this, obfCreateSessionFunc, key_, hwid_);
        if (!create_session_response.valid()) {
            logger_.error("Session creation failed");
            return false;
        }

        session_id_ = create_session_response.session_id();

        logger_.info("Session ID: {}", session_id_);

        auto obfStartConfigStreamFunc = $om(StartConfigStream, App, void);
        $call(this, obfStartConfigStreamFunc);
        auto obfStartPingLoopFunc = $om(StartPingLoop, App, void);
        $call(this, obfStartPingLoopFunc);

        return true;
    } catch (const std::exception& e) {
        logger_.error("Start failed {}", e.what());
        return false;
    }
}

void App::Stop() {
    try {
        constexpr uint8_t bytecode[] = {1, 2, 3, 4};
        OBF_VM_FUNCTION(bytecode, [this](uint8_t instr) {
            switch (instr) {
                VM_CASE(1) {
                    frameCapturer_->StopCapture();
                    ndiCapturer_->Stop();
                }
                VM_CASE(2) {
                    if (logicManager_) {
                        logicManager_->Stop();
                    }
                }
                VM_CASE(3)
                {
                    StopPingLoop();
                }
                VM_CASE(4) {
                    StopConfigStream();
                }
            }
        });
    } catch (const std::exception& e) {
        logger_.error("Stop failed {}", e.what());
    }
}

bool App::CheckServerStatus() {
    try {
        capkfa::GetStatusResponse status_response = licenseClient_->GetStatus();

        if (status_response.version() != version_) {
            logger_.error("Version mismatch: {}", status_response.version());
            return false;
        }

        return status_response.online();
    } catch (const std::exception& e) {
        logger_.error("Server status check failed: {}", e.what());
    }
    return false;
}

::capkfa::CreateSessionResponse App::CreateSession(const std::string& key, const std::string& hwid) {
    ::capkfa::CreateSessionResponse response;
    try {
        ::capkfa::CreateSessionRequest request;
        request.set_key(key);
        request.set_hwid(hwid);
        request.set_version(version_);
        return licenseClient_->CreateSession(request);
    } catch (const std::exception& e) {
        logger_.error("Session creation failed: {}", e.what());
        return response;
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

void App::StartPingLoop() {
    if (!isPingingLicense_) {
        isPingingLicense_ = true;
        licensePingThread_ = std::thread(&App::ProcessPingLoop, this);
    }
}

void App::StopPingLoop() {
    if (isPingingLicense_) {
        isPingingLicense_ = false;
        if (licensePingThread_.joinable()) {
            licensePingThread_.join();
        }
    }
}

void App::ProcessConfigStreaming() {
    ::capkfa::GetConfigRequest request;
    request.set_key(key_);
    LicenseClient::StreamConfigReader reader = licenseClient_->StreamConfig(request);
    ::capkfa::GetConfigResponse response;

    bool started = false;

    while (reader.Read(response) && isStreamingConfig_) {
        constexpr uint8_t bytecode[] = {1, 2, 3};
        auto vm_block = [this, &response, &started](uint8_t instr) {
            switch (instr) {
                VM_CASE(1)
                {
                    ndiCapturer_->Stop();
                    frameCapturer_->StopCapture();
                    if (response.remote_config().capture().mode().type() == ::capkfa::RemoteConfigCaptureMode_CaptureModeType_NDI) {
                        ndiCapturer_->SetConfig(response.remote_config());
                    } else {
                        frameCapturer_->SetConfig(response.remote_config());
                    }
                }
                VM_CASE(2) {
                    if (keyWatcher_)
                    {
                        keyWatcher_->SetConfig(response.remote_config());
                    }
                }
                VM_CASE(3) {
                    if (!started && commanderClient_) {
                        commanderClient_->SetConfig(response.remote_config());
                        started = true;
                    }
                }
            }
        };
        OBF_VM_FUNCTION(bytecode, vm_block);

        logicManager_->SetConfig(response.remote_config());
    }

    reader.Finish();
}

void App::ProcessPingLoop()
{
    return;

    while (isPingingLicense_)
    {
        try
        {
            ::capkfa::PingRequest request;
            request.set_key(key_);
            request.set_hwid(hwid_);
            request.set_session_id(session_id_);
            request.set_version(version_);
            auto response = licenseClient_->Ping(request);
            logger_.info("Ping license response: {}", response.valid());
            if (!response.valid())
            {
                throw std::runtime_error("Ping response failed (gonna session killed)");
            }
        } catch (const std::exception& e)
        {
            logger_.error("Ping failed: {}", e.what());
            Stop();
        }
    }
}
