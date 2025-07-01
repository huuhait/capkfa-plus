#ifndef FRAME_GRABBER_H
#define FRAME_GRABBER_H

#include <Processing.NDI.Lib.h>
#include <opencv2/opencv.hpp>
#include <spdlog/spdlog.h>
#include <thread>
#include <chrono>
#include <license.pb.h>
#include <memory>
#include "Frame.h"
#include "FrameSlot.h" // Assumed to define FrameSlot class

class FrameGrabber {
public:
    FrameGrabber(spdlog::logger& logger, std::shared_ptr<FrameSlot> frameSlot);
    ~FrameGrabber();

    void Start();
    void Stop();
    void SetConfig(const ::capkfa::RemoteConfig& config);

private:
    void ReadFrames();
    void DisplayFrames();
    bool FindSources(std::vector<std::pair<NDIlib_source_t, std::pair<int, int>>>& matching_sources);

    spdlog::logger& logger_;
    NDIlib_recv_instance_t receiver_;
    NDIlib_find_instance_t finder_;
    bool running_;
    std::thread io_thread_;
    std::thread display_thread_;
    uint64_t frame_count_;
    uint64_t failed_frame_count_;
    uint64_t total_decode_time_;
    std::shared_ptr<FrameSlot> frame_slot_;
    ::capkfa::RemoteConfig remote_config_;
    std::chrono::steady_clock::time_point start_time_;
    std::chrono::steady_clock::time_point last_fps_time_;
    std::chrono::steady_clock::time_point last_frame_time_;
};

#endif // FRAME_GRABBER_H