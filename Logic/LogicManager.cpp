#include "LogicManager.h"

LogicManager::LogicManager(std::shared_ptr<Colorbot> colorbot): colorbot_(colorbot) {}

LogicManager::~LogicManager() {
    Stop();
}

void LogicManager::Start() {
    colorbot_->Start();
}

void LogicManager::Stop() {
    colorbot_->Stop();
}

void LogicManager::SetConfig(const ::capkfa::RemoteConfig& config) {
    colorbot_->SetConfig(config);
}
