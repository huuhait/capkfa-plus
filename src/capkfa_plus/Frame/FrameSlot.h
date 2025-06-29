#ifndef FRAME_SLOT_H
#define FRAME_SLOT_H

#include "Frame.h"
#include <mutex>
#include <cstdint>
#include <memory>

class FrameSlot {
public:
    FrameSlot() : frameVersion_(0) {}
    void StoreFrame(std::shared_ptr<Frame> frame);
    std::pair<std::shared_ptr<Frame>, uint64_t> GetFrame(uint64_t lastVersion) const;

private:
    std::shared_ptr<Frame> frame_;
    uint64_t frameVersion_;
    mutable std::mutex mutex_;
};

#endif