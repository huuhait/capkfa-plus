#ifndef FRAME_SLOT_H
#define FRAME_SLOT_H

#include "Frame.h"
#include <mutex>
#include <cstdint>

class FrameSlot {
public:
    FrameSlot() : frameVersion_(0) {}
    void StoreFrame(const Frame& frame);
    std::pair<Frame, uint64_t> GetFrame(uint64_t lastVersion) const;

private:
    Frame frame_;
    uint64_t frameVersion_;
    mutable std::mutex mutex_;
};

#endif