#ifndef FRAME_GRABBER_H
#define FRAME_GRABBER_H

#include <memory>
#include <thread>
#include <atomic>
#include <license.pb.h>
#include <mutex>
#include <opencv2/opencv.hpp>
#include <spdlog/spdlog.h>

#include "Frame.h"

extern "C" {
#include <libavformat/avformat.h>
#include <turbojpeg.h>
}

class FrameSlot;

class FrameGrabber {
public:
    FrameGrabber(spdlog::logger& logger, std::shared_ptr<FrameSlot> frameSlot);
    ~FrameGrabber();
    void Start();
    void Stop();
    void SetConfig(const ::capkfa::RemoteConfig& config);
private:
    void ReadFrames();

    spdlog::logger& logger_;

    // TurboJPEG decoder
    tjhandle tj_handle_;

    // FFmpeg for UDP demuxing only
    AVFormatContext* format_ctx_;
    AVPacket* packet_;

    std::atomic<bool> running_;
    std::thread io_thread_;
    std::shared_ptr<FrameSlot> frame_slot_;
    ::capkfa::RemoteConfig remote_config_;

    uint64_t frame_count_;
    std::chrono::steady_clock::time_point start_time_;
    std::chrono::steady_clock::time_point last_fps_time_;

    static constexpr const char* URI = "udp://127.0.0.1:1234?fifo_size=5000000&overrun_nonfatal=1";
};

#endif // FRAME_GRABBER_H
