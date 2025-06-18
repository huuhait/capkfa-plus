#include "FrameSlot.h"

void FrameSlot::StoreFrame(const cv::UMat& frame) {
    std::lock_guard<std::mutex> lock(mutex_);
    frame_ = frame.clone();
    frameVersion_++;
}

std::pair<cv::UMat, uint64_t> FrameSlot::GetFrame(uint64_t lastVersion) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (lastVersion < frameVersion_ && !frame_.empty()) {
        return {frame_.clone(), frameVersion_};
    }
    return {cv::UMat(), frameVersion_};
}