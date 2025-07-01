#include "FrameSlot.h"

void FrameSlot::StoreFrame(std::shared_ptr<Frame> frame) {
    frame_.store(frame, std::memory_order_release);
    frame_version_.fetch_add(1, std::memory_order_release);
}

std::pair<std::shared_ptr<Frame>, uint64_t> FrameSlot::GetFrame(uint64_t lastVersion) const {
    auto current_frame = frame_.load(std::memory_order_acquire);
    auto current_version = frame_version_.load(std::memory_order_acquire);
    if (lastVersion < current_version && current_frame && current_frame->IsValid()) {
        return {current_frame, current_version};
    }
    return {nullptr, current_version};
}