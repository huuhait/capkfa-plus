#ifndef FRAME_SLOT_H
#define FRAME_SLOT_H

#include <memory>
#include <atomic>
#include "Frame.h"

class FrameSlot {
public:
    FrameSlot() = default;
    void StoreFrame(std::shared_ptr<Frame> frame);
    std::pair<std::shared_ptr<Frame>, uint64_t> GetFrame(uint64_t lastVersion) const;

private:
    std::atomic<std::shared_ptr<Frame>> frame_{nullptr};
    std::atomic<uint64_t> frame_version_{0};
};

#endif // FRAME_SLOT_H