#ifndef LOGICMANAGER_H
#define LOGICMANAGER_H

#include <memory>
#include "Colorbot.h"

class LogicManager {
public:
    LogicManager(std::shared_ptr<Colorbot> colorbot);
    ~LogicManager();
    void Start();
    void Stop();
    void SetConfig(const ::capkfa::RemoteConfig& config);

private:
    std::shared_ptr<Colorbot> colorbot_;
};

#endif