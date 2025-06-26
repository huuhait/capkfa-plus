#ifndef LOGICMANAGER_H
#define LOGICMANAGER_H

#include <memory>
#include "Colorbot.h"
#include "ObjectDetectionManager.h"

class LogicManager {
public:
    LogicManager(std::unique_ptr<Colorbot> colorbot, std::unique_ptr<ObjectDetectionManager> objectDetectionManager);
    ~LogicManager();
    void Start();
    void Stop();
    void SetConfig(const ::capkfa::RemoteConfig& config);

private:
    std::unique_ptr<Colorbot> colorbot_;
    std::unique_ptr<ObjectDetectionManager> objectDetectionManager_;
};

#endif