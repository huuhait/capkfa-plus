#include "FrameGrabber.h"
#include "FrameSlot.h"
#include <future>
#include <iostream>
#include <memory>
#include <iomanip>
#ifdef _WIN32
#pragma comment(lib, "Processing.NDI.Lib.x64.lib")
#endif

NDICapture::NDICapture(spdlog::logger& logger, std::shared_ptr<FrameSlot> frameSlot)
    : logger_(logger), receiver_(nullptr), finder_(nullptr), running_(true),
      frame_count_(0), total_decode_time_(0), frame_slot_(frameSlot),
      start_time_(std::chrono::steady_clock::now()),
      last_fps_time_(start_time_), last_frame_time_(start_time_) {}

NDICapture::~NDICapture() {
    Stop();
}

void NDICapture::Stop() {
    running_ = false;

    // Clean up NDI resources
    if (receiver_) {
        NDIlib_recv_destroy(receiver_);
        receiver_ = nullptr;
    }
    if (finder_) {
        NDIlib_find_destroy(finder_);
        finder_ = nullptr;
    }
    NDIlib_destroy();

    // Join threads
    if (io_thread_.joinable()) {
        io_thread_.join();
    }
    if (display_thread_.joinable()) {
        display_thread_.join();
    }

    // Safely clean up all OpenCV windows
    cv::destroyAllWindows();

    // Reset state variables
    frame_count_ = 0;
    total_decode_time_ = 0;
    failed_frame_count_ = 0;
    start_time_ = std::chrono::steady_clock::now();
    last_fps_time_ = start_time_;
    last_frame_time_ = start_time_;
    logger_.info("FrameGrabber stopped");
}

void NDICapture::Start() {
    if (remote_config_.capture().size() <= 0) {
        logger_.error("Cannot start: Invalid capture size {}", remote_config_.capture().size());
        throw std::runtime_error("Invalid capture size");
    }
    logger_.info("Starting FrameGrabber with size {}x{}",
                 remote_config_.capture().size(), remote_config_.capture().size());

    // Initialize NDI
    if (!NDIlib_initialize()) {
        logger_.error("Failed to initialize NDI: CPU not supported");
        throw std::runtime_error("Failed to initialize NDI");
    }

    // Create finder
    finder_ = NDIlib_find_create_v2(nullptr);
    if (!finder_) {
        NDIlib_destroy();
        logger_.error("Failed to create NDI finder");
        throw std::runtime_error("Failed to create NDI finder");
    }

    running_ = true;
    try {
        // Ensure threads are not running before starting new ones
        if (io_thread_.joinable()) {
            io_thread_.join();
        }
        if (display_thread_.joinable()) {
            display_thread_.join();
        }
        io_thread_ = std::thread([this]() { ReadFrames(); });
        display_thread_ = std::thread([this]() { DisplayFrames(); });
        logger_.info("FrameGrabber threads started");
    } catch (const std::exception& e) {
        running_ = false;
        if (finder_) {
            NDIlib_find_destroy(finder_);
            finder_ = nullptr;
        }
        NDIlib_destroy();
        logger_.error("Failed to launch threads: {}", e.what());
        throw;
    }
}

void NDICapture::SetConfig(const ::capkfa::RemoteConfig& config) {
    logger_.info("Setting new config for FrameGrabber");
    Stop();
    remote_config_ = config;
    Start();
}

void NDICapture::DisplayFrames() {
    uint64_t last_version = 0;

    // Only create window if it doesn't exist
    cv::namedWindow("FrameGrabber", cv::WINDOW_AUTOSIZE);

    while (running_) {
        auto [frame, version] = frame_slot_->GetFrame(last_version);
        if (frame && version > last_version) {
            cv::imshow("FrameGrabber", frame->GetMat());
            last_version = version;
        }

        if (cv::waitKey(1) == 27) { // Exit on ESC
            running_ = false;
        }
    }

    // Clean up window safely
    cv::destroyAllWindows();
    logger_.info("DisplayFrames stopped");
}

bool NDICapture::FindSources(std::vector<std::pair<NDIlib_source_t, std::pair<int, int>>>& matching_sources) {
    matching_sources.clear();
    uint32_t source_count = 0;
    const NDIlib_source_t* sources = nullptr;
    constexpr int max_retries = 5;
    int retry_count = 0;
    constexpr int base_retry_delay_ms = 50;

    while (running_ && source_count == 0 && retry_count < max_retries) {
        NDIlib_find_wait_for_sources(finder_, 1000);
        sources = NDIlib_find_get_current_sources(finder_, &source_count);
        if (source_count == 0) {
            logger_.warn("No NDI sources found, retrying ({}/{})", retry_count + 1, max_retries);
            retry_count++;
            std::this_thread::sleep_for(std::chrono::milliseconds(base_retry_delay_ms * (1 << retry_count)));
        }
    }

    if (source_count == 0) {
        logger_.error("No NDI sources found after {} retries", max_retries);
        return false;
    }

    std::string target_ip = remote_config_.capture().mode().source();
    for (uint32_t i = 0; i < source_count; ++i) {
        std::string url = sources[i].p_url_address ? sources[i].p_url_address : "";
        std::string name = sources[i].p_ndi_name ? sources[i].p_ndi_name : "";
        if (url.find(target_ip) != std::string::npos || name.find(target_ip) != std::string::npos) {
            // Add source without checking resolution
            matching_sources.emplace_back(sources[i], std::make_pair(0, 0));
            logger_.info("Found source: {}", name);
        }
    }

    if (matching_sources.empty()) {
        logger_.error("No sources found matching IP {}", target_ip);
        return false;
    }

    return true;
}

void NDICapture::ReadFrames() {
    int capture_size = remote_config_.capture().size();
    if (capture_size <= 0) {
        logger_.error("Invalid capture size in ReadFrames: {}", capture_size);
        return;
    }

    while (running_) {
        // Find sources matching the target IP
        std::vector<std::pair<NDIlib_source_t, std::pair<int, int>>> matching_sources;
        if (!FindSources(matching_sources)) {
            logger_.error("Failed to find NDI sources, retrying in 5 seconds");
            std::this_thread::sleep_for(std::chrono::seconds(5));
            continue;
        }

        // Always select source at index 0
        int selected_index = 0;
        if (selected_index >= static_cast<int>(matching_sources.size())) {
            logger_.error("No sources available at index 0, retrying in 5 seconds");
            std::this_thread::sleep_for(std::chrono::seconds(5));
            continue;
        }

        auto& [selected_source, resolution] = matching_sources[selected_index];
        logger_.info("Selected source: {}", selected_source.p_ndi_name);

        // Create receiver
        NDIlib_recv_create_v3_t recv_desc{};
        recv_desc.source_to_connect_to = selected_source;
        recv_desc.color_format = NDIlib_recv_color_format_BGRX_BGRA;
        recv_desc.bandwidth = NDIlib_recv_bandwidth_highest;
        recv_desc.allow_video_fields = false;

        receiver_ = NDIlib_recv_create_v3(&recv_desc);
        if (!receiver_) {
            logger_.error("Failed to create NDI receiver, retrying in 5 seconds");
            std::this_thread::sleep_for(std::chrono::seconds(5));
            continue;
        }

        bool source_valid = true;
        while (running_ && source_valid) {
            NDIlib_video_frame_v2_t vframe{};
            if (NDIlib_recv_capture_v3(receiver_, &vframe, nullptr, nullptr, 1000) == NDIlib_frame_type_video) {
                auto process_start = std::chrono::steady_clock::now();

                if (vframe.xres != capture_size || vframe.yres != capture_size) {
                    logger_.warn("Skipping frame with size: {}x{}, expected {}x{}",
                                vframe.xres, vframe.yres, capture_size, capture_size);
                    NDIlib_recv_free_video_v2(receiver_, &vframe);
                    failed_frame_count_++;
                    continue;
                }

                cv::Mat bgra(vframe.yres, vframe.xres, CV_8UC4, vframe.p_data, vframe.line_stride_in_bytes);
                cv::Mat bgr;
                cv::cvtColor(bgra, bgr, cv::COLOR_BGRA2BGR);
                std::shared_ptr<Frame> frame;
                try {
                    frame = std::make_shared<Frame>(bgr.clone(), capture_size, capture_size);
                } catch (const std::exception& e) {
                    logger_.warn("Frame creation failed: {}", e.what());
                    NDIlib_recv_free_video_v2(receiver_, &vframe);
                    failed_frame_count_++;
                    continue;
                }

                auto process_end = std::chrono::steady_clock::now();
                auto process_time = std::chrono::duration_cast<std::chrono::microseconds>(process_end - process_start).count();
                total_decode_time_ += process_time;

                frame_slot_->StoreFrame(frame);

                NDIlib_recv_free_video_v2(receiver_, &vframe);

                // FPS and metrics
                ++frame_count_;
                auto now = std::chrono::steady_clock::now();
                auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_fps_time_).count();
                if (elapsed_ms >= 1000 && frame_count_ > 0) {
                    double fps = frame_count_ * 1000.0 / elapsed_ms;
                    double avg_process_ms = total_decode_time_ / static_cast<double>(frame_count_ * 1000);
                    logger_.info("Grabber FPS: {:.1f}, Avg process time: {:.2f}ms, Failed frames: {}",
                                fps, avg_process_ms, failed_frame_count_);
                    frame_count_ = 0;
                    total_decode_time_ = 0;
                    failed_frame_count_ = 0;
                    last_fps_time_ = now;
                }
            } else {
                // No frame received, check if source is still available
                logger_.warn("No frame received, checking source availability");
                std::vector<std::pair<NDIlib_source_t, std::pair<int, int>>> new_sources;
                if (!FindSources(new_sources)) {
                    logger_.error("Source lost, retrying in 5 seconds");
                    source_valid = false;
                } else {
                    bool source_found = false;
                    for (const auto& [source, res] : new_sources) {
                        if (std::string(source.p_ndi_name) == std::string(selected_source.p_ndi_name)) {
                            source_found = true;
                            break;
                        }
                    }
                    if (!source_found) {
                        logger_.error("Selected source no longer available, retrying");
                        source_valid = false;
                    }
                }
            }
        }

        // Clean up receiver before retry
        if (receiver_) {
            NDIlib_recv_destroy(receiver_);
            receiver_ = nullptr;
        }
        if (running_) {
            logger_.info("Reconnecting in 5 seconds");
            std::this_thread::sleep_for(std::chrono::seconds(5));
        }
    }

    if (receiver_) {
        NDIlib_recv_destroy(receiver_);
        receiver_ = nullptr;
    }
    logger_.info("ReadFrames stopped");
}