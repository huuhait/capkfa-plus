#include <iostream>
#include "App.h"

#include <opencv2/core/ocl.hpp>

#include "Obfuscate.h"
#include "HWIDTool.h"
#include "Logic/LogicManager.h"
#include "Frame/FrameCapturer.h"
#include "Frame/FrameGrabber.h"
#include "Movement/CommanderClient.h"

App::App(spdlog::logger& logger,
        std::unique_ptr<LicenseClient> client,
        std::shared_ptr<CommanderClient> commanderClient,
        std::shared_ptr<KeyWatcher> keyWatcher,
        // std::unique_ptr<FrameCapturer> frameCapturer,
        std::unique_ptr<FrameGrabber> frameGrabber,
        std::shared_ptr<LogicManager> logicManager)
    : logger_(logger),
      licenseClient_(std::move(client)),
      commanderClient_(commanderClient),
      keyWatcher_(keyWatcher),
      // frameCapturer_(std::move(frameCapturer)),
      frameGrabber_(std::move(frameGrabber)),
      logicManager_(logicManager) {}

bool App::Start() {
    cv::ocl::setUseOpenCL(true);

    constexpr auto obfLockedHwid = $o("16F24F4AE3D7102990E9CF9AFA65E3B33952A6CD8D04E29221B73EE519A99109");
    constexpr auto obfDevKey = $o("MIKU-BC76F17DC89C8F8881EA83822C2FCA54");
    auto obfGetHWID = $of(HWIDTool::GetHWID);
    std:: string computerHWID = obfGetHWID();

    hwid_ = $d_inline(obfLockedHwid);

    // if (computerHWID != hwid_) {
    //     std::cerr << "HWID is not to the generated locked HWID Loader" << std::endl;
    //     return false;
    // }

    // std::cout << "Enter your key: ";
    // std::getline(std::cin, key_);

    // DEV BLOCK
    // if (key_.empty()) {
        key_ = $d_inline(obfDevKey);
    // }
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

        logger_.info("Session ID: {}", create_session_response.session_id());

        frameGrabber_->Start();

        auto obfStartConfigStreamFunc = $om(StartConfigStream, App, void);
        $call(this, obfStartConfigStreamFunc);
        return true;
    } catch (const std::exception& e) {
        logger_.error("Start failed {}", e.what());
        return false;
    }
}

void App::Stop() {
    try {
        constexpr uint8_t bytecode[] = {1, 2, 3};
        OBF_VM_FUNCTION(bytecode, [this](uint8_t instr) {
            switch (instr) {
                VM_CASE(1) {
                    // if (frameCapturer_) {
                    //     frameCapturer_->StopCapture();
                    // }
                    if (frameGrabber_)
                    {
                        frameGrabber_->Stop();
                    }
                }
                VM_CASE(2) {
                    if (logicManager_) {
                        logicManager_->Stop();
                    }
                }
                VM_CASE(3) {
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
        request.set_version("1.0.0");
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
                    if (frameGrabber_)
                    {
                        frameGrabber_->SetConfig(response.remote_config());
                    }
                }
                VM_CASE(2) {
                    if (logicManager_) logicManager_->SetConfig(response.remote_config());
                }
                VM_CASE(3) {
                    if (keyWatcher_) keyWatcher_->SetConfig(response.remote_config());
                }
                VM_CASE(4) {
                    if (!started && commanderClient_) {
                        commanderClient_->SetConfig(response.remote_config());
                        started = true;
                    }
                }
            }
        };
        OBF_VM_FUNCTION(bytecode, vm_block);
    }

    reader.Finish();
}
