#ifndef FRAME_SLOT_H
#define FRAME_SLOT_H

#include <opencv2/opencv.hpp>
#include <mutex>
#include <cstdint>

class FrameSlot {
public:
    FrameSlot() : frameVersion_(0) {}
    void StoreFrame(const cv::UMat& frame); // Reverted to UMat
    std::pair<cv::UMat, uint64_t> GetFrame(uint64_t lastVersion) const;

private:
    cv::UMat frame_; // Reverted to UMat
    uint64_t frameVersion_;
    mutable std::mutex mutex_;
};

#endif