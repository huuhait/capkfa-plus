#ifndef LOGICMANAGER_H
#define LOGICMANAGER_H

#include <memory>
#include "PixelSeek.h"
#include "ObjectDetectionManager.h"

class LogicManager {
public:
    LogicManager(std::unique_ptr<PixelSeek> pixelSeek, std::unique_ptr<ObjectDetectionManager> objectDetectionManager);
    ~LogicManager();
    void Start();
    void Stop();
    void SetConfig(const ::capkfa::RemoteConfig& config);

private:
    std::unique_ptr<PixelSeek> pixelSeek_;
    std::unique_ptr<ObjectDetectionManager> objectDetectionManager_;
};

#endif