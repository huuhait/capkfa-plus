//
// Created by huuhait on 6/18/2025.
//

#include "KeyWatcher.h"

KeyWatcher::KeyWatcher(std::shared_ptr<CommanderClient> commanderClient): commanderClient_(commanderClient) {
    if (!commanderClient_) {
        throw std::runtime_error("Failed to create Km");
    }
}

void KeyWatcher::SetConfig(const ::capkfa::RemoteConfig& config) {
    config_ = config;
}

bool KeyWatcher::IsKeyDown(uint8_t key) {
    std::map<uint8_t, bool> buttonStates = commanderClient_->ButtonStates();
    return buttonStates[key];
}

uint8_t KeyWatcher::GetKey(const std::string& key) {
    if (key == "x1") {
        return 3;
    } else if (key == "x2") {
        return 4;
    }

    return 0;
}

bool KeyWatcher::IsAimKeyDown() {
    uint8_t key = GetKey(config_.game().aim().base().key());

    if (key == 0) {
        return false;
    }

    return IsKeyDown(key);
}

bool KeyWatcher::IsFlickKeyDown() {
    uint8_t key = GetKey(config_.game().flick().base().key());

    if (key == 0) {
        return false;
    }

    return IsKeyDown(key);
}

bool KeyWatcher::IsShotKeyDown() {
    return IsKeyDown(0);
}

bool KeyWatcher::IsCaptureKeyDown() {
    return IsAimKeyDown() || IsFlickKeyDown();
}

bool KeyWatcher::IsHandlerKeyDown() {
    return IsAimKeyDown() || IsFlickKeyDown();
}
