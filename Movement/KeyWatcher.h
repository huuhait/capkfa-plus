#ifndef KEYWATCHER_H
#define KEYWATCHER_H

#include <memory>
#include "CommanderClient.h"
#include "proto/license.pb.h"

class KeyWatcher {
public:
    KeyWatcher(std::shared_ptr<CommanderClient> commanderClient);

    void SetConfig(const ::capkfa::RemoteConfig& config);
    bool IsCaptureKeyDown();
    bool IsHandlerKeyDown();
    bool IsAimKeuDown();
    bool IsFlickKeyDown();
    bool IsShotKeyDown();
private:
    bool IsKeyDown(uint8_t key);
    uint8_t GetKey(const std::string& key);

    std::shared_ptr<CommanderClient> commanderClient_;
    ::capkfa::RemoteConfig config_;
};

#endif
