#include "LogicManager.h"

LogicManager::LogicManager(std::unique_ptr<PixelSeek> pixelSeek,
                         std::unique_ptr<ObjectDetectionManager> objectDetectionManager)
    : pixelSeek_(std::move(pixelSeek)),
      objectDetectionManager_(std::move(objectDetectionManager)) {}

LogicManager::~LogicManager() {
    Stop();
}

void LogicManager::Start() {
    // pixelSeek_->Start();
    objectDetectionManager_->Start();
}

void LogicManager::Stop() {
    pixelSeek_->Stop();
    objectDetectionManager_->Stop();
}

void LogicManager::SetConfig(const ::capkfa::RemoteConfig& config) {
    if (true)
    {
        objectDetectionManager_->SetConfig(config);
    } else
    {
        pixelSeek_->SetConfig(config);
    }
}
