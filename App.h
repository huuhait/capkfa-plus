#ifndef APP_H
#define APP_H

#include "License/LicenseClient.h"
#include "Frame/FrameCapturer.h"
#include "Frame/FrameHandler.h"
#include "Frame/FrameSlot.h"
#include "Frame/DeviceManager.h"
#include <memory>
#include <string>

class App {
public:
    App(std::unique_ptr<LicenseClient> client,
        std::shared_ptr<FrameSlot> frameSlot,
        std::unique_ptr<DeviceManager> deviceManager,
        std::shared_ptr<FrameCapturer> capturer,
        std::shared_ptr<FrameHandler> handler);
    ~App() = default;

    bool Start(const std::string& userId);
    void Stop();

private:
    bool CheckServerStatus();
    ::capkfa::GetConfigResponse GetConfiguration(const std::string& configKey);

    std::unique_ptr<LicenseClient> client_;
    std::shared_ptr<FrameSlot> frameSlot_;
    std::shared_ptr<FrameCapturer> capturer_;
    std::shared_ptr<FrameHandler> handler_;
    std::unique_ptr<DeviceManager> deviceManager_;
};

#endif // APP_H