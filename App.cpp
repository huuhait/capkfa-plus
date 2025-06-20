#include "App.h"
#include "Obfuscate.h"
#include "obf_fake_logic.inl"
#include <iostream>

#include "Logic/LogicManager.h"
#include "Frame/FrameCapturer.h"
#include "HWID/HWIDTool.h"
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

bool App::Start() {
    run_fake_combo_2();
    constexpr auto obfLockedHwid = $o("0AD3C0FE7085B18B82EB174904E340C9960CBB23DEB506A94F528C82F8DB5408");
    constexpr auto obfDevKey = $o("MIKU-BC76F17DC89C8F8881EA83822C2FCA54");
    auto obfGetHWID = $of("hidden_gethwid", HWIDTool::GetHWID);
    std:: string computerHWID = obfGetHWID();

    hwid_ = $d_inline(obfLockedHwid);

    if (computerHWID != hwid_) {
        std::cerr << "HWID is not to the generated locked HWID Loader" << std::endl;
        return false;
    }

    std::cout << "Enter your key: ";
    std::getline(std::cin, key_);

    // DEV BLOCK
    if (key_.empty()) {
        key_ = $d_inline(obfDevKey);
    }
    // DEV BLOCK

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
    run_fake_combo_2();
    try {
        capkfa::GetStatusResponse status_response = licenseClient_->GetStatus();

        return status_response.online();
    } catch (const std::exception& e) {
        std::cerr << "Server status check failed: " << e.what() << std::endl;
    }
    return false;
}

::capkfa::CreateSessionResponse App::CreateSession(const std::string& key, const std::string& hwid) {
    run_fake_combo_7();
    ::capkfa::CreateSessionResponse response;
    try {
        ::capkfa::CreateSessionRequest request;
        request.set_key(key);
        request.set_hwid(hwid);
        request.set_version("1.0.0");
        return licenseClient_->CreateSession(request);
    } catch (const std::exception& e) {
        std::cerr << "Session creation failed: " << e.what() << std::endl;
        return response;
    }
}

void App::StartConfigStream() {
    run_fake_combo_6();
    if (!isStreamingConfig_) {
        isStreamingConfig_ = true;
        streamConfigThread_ = std::thread(&App::ProcessConfigStreaming, this);
    }
}

void App::StopConfigStream() {
    run_fake_combo_1();
    if (isStreamingConfig_) {
        isStreamingConfig_ = false;
        if (streamConfigThread_.joinable()) {
            streamConfigThread_.join();
        }
    }
}

void App::ProcessConfigStreaming() {
    run_fake_combo_5();
    ::capkfa::GetConfigRequest request;
    request.set_key(key_);
    LicenseClient::StreamConfigReader reader = licenseClient_->StreamConfig(request);
    ::capkfa::GetConfigResponse response;

    bool started = false;

    while (reader.Read(response) && isStreamingConfig_) {
        capturer_->SetConfig(response.remote_config());
        logicManager_->SetConfig(response.remote_config());
        keyWatcher_->SetConfig(response.remote_config());
        if (!started) {
            // this used to prevent reconnect of the commander
            commanderClient_->SetConfig(response.remote_config());
            started = true;
        }
    }

    reader.Finish();
}
