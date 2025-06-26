#include "LogicManager.h"

LogicManager::LogicManager(std::unique_ptr<Colorbot> colorbot,
                         std::unique_ptr<ObjectDetectionManager> objectDetectionManager)
    : colorbot_(std::move(colorbot)),
      objectDetectionManager_(std::move(objectDetectionManager)) {}

LogicManager::~LogicManager() {
    Stop();
}

void LogicManager::Start() {
    // colorbot_->Start();
    objectDetectionManager_->Start();
}

void LogicManager::Stop() {
    colorbot_->Stop();
    objectDetectionManager_->Stop();
}

void LogicManager::SetConfig(const ::capkfa::RemoteConfig& config) {
    if (true)
    {
        objectDetectionManager_->SetConfig(config);
    } else
    {
        colorbot_->SetConfig(config);
    }
}
