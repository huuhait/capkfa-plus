#include "FrameSlot.h"

void FrameSlot::StoreFrame(const Frame& frame) {
    std::lock_guard<std::mutex> lock(mutex_);
    frame_ = frame;
    frameVersion_++;
}

std::pair<Frame, uint64_t> FrameSlot::GetFrame(uint64_t lastVersion) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (lastVersion < frameVersion_ && frame_.IsValid()) {
        return {frame_, frameVersion_};
    }
    return {Frame(), frameVersion_};
}
