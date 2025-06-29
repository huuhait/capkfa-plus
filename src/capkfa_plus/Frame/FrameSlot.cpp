#include "FrameSlot.h"

void FrameSlot::StoreFrame(std::shared_ptr<Frame> frame) {
    std::lock_guard<std::mutex> lock(mutex_);
    frame_ = frame;
    frameVersion_++;
}

std::pair<std::shared_ptr<Frame>, uint64_t> FrameSlot::GetFrame(uint64_t lastVersion) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (lastVersion < frameVersion_ && frame_ && frame_->IsValid()) {
        return {frame_, frameVersion_};
    }
    return {nullptr, frameVersion_};
}
