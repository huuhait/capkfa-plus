#ifndef APP_H
#define APP_H

#include "../../License/LicenseClient.h"
#include "../../Frame/FrameCapturer.h"
#include "../../Logic/LogicManager.h"
#include "../../Movement/CommanderClient.h"
#include <memory>
#include <string>

class App {
public:
    App(std::unique_ptr<LicenseClient> licenseClient,
        std::shared_ptr<CommanderClient> commanderClient,
        std::shared_ptr<KeyWatcher> keyWatcher,
        std::unique_ptr<FrameCapturer> capturer,
        std::shared_ptr<LogicManager> logicManager);
    ~App() = default;

    bool Start();
    void Stop();

private:
    bool CheckServerStatus();
    ::capkfa::CreateSessionResponse CreateSession(const std::string& key, const std::string& hwid);
    void StartConfigStream();
    void StopConfigStream();
    void ProcessConfigStreaming();

    std::unique_ptr<LicenseClient> licenseClient_;
    std::shared_ptr<CommanderClient> commanderClient_;
    std::shared_ptr<KeyWatcher> keyWatcher_;
    std::unique_ptr<FrameCapturer> capturer_;
    std::shared_ptr<LogicManager> logicManager_;

    std::string key_;
    std::string hwid_;

    std::thread streamConfigThread_;
    std::atomic<bool> isStreamingConfig_;
};

#endif // APP_H