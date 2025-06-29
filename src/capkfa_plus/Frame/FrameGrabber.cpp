#include "FrameGrabber.h"
#include "FrameSlot.h"
#include <iostream>
#include <memory>
#include <iomanip>

extern "C" {
#include <libavformat/avformat.h>
}

FrameGrabber::FrameGrabber(spdlog::logger& logger, std::shared_ptr<FrameSlot> frameSlot)
    : logger_(logger), format_ctx_(nullptr), packet_(nullptr), running_(true),
      frame_count_(0), frame_slot_(frameSlot),
      start_time_(std::chrono::steady_clock::now()),
      last_fps_time_(start_time_) {}

FrameGrabber::~FrameGrabber() {
    Stop();
}

void FrameGrabber::Start() {
    avformat_network_init();
    tj_handle_ = tjInitDecompress();
    if (!tj_handle_) throw std::runtime_error("Failed to initialize TurboJPEG");

    packet_ = av_packet_alloc();
    if (!packet_) throw std::runtime_error("Failed to allocate AVPacket");

    logger_.info("FrameGrabber configured with size {}x{}", remote_config_.aim().capture_size(), remote_config_.aim().capture_size());
    io_thread_ = std::thread([this]() { ReadFrames(); });
}

void FrameGrabber::Stop() {
    running_ = false;
    if (format_ctx_) avformat_close_input(&format_ctx_);
    if (io_thread_.joinable()) io_thread_.join();
    if (packet_) av_packet_free(&packet_);
    if (format_ctx_) avformat_close_input(&format_ctx_);
    if (tj_handle_) tjDestroy(tj_handle_);
    avformat_network_deinit(); // Added cleanup
}

void FrameGrabber::SetConfig(const ::capkfa::RemoteConfig& config) {
    Stop();
    remote_config_ = config;
    Start();
}

void FrameGrabber::ReadFrames() {
    std::vector<uint8_t> buffer(0 * 0 * 3);

    while (running_) {
        if (!format_ctx_) {
            const char* uri = "0.0.0.0:4500";
            AVDictionary* opts = nullptr;
            av_dict_set(&opts, "fifo_size", "5000000", 0);
            av_dict_set(&opts, "overrun_nonfatal", "1", 0);
            av_dict_set(&opts, "timeout", "1000000", 0);
            av_dict_set(&opts, "probesize", "32", 0);
            av_dict_set(&opts, "analyzeduration", "0", 0);
            int ret = avformat_open_input(&format_ctx_, uri, nullptr, &opts);
            av_dict_free(&opts); // Moved after checking ret
            if (ret < 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }

            logger_.info("Opened input stream: {}", uri);

            if (avformat_find_stream_info(format_ctx_, nullptr) < 0) {
                avformat_close_input(&format_ctx_);
                format_ctx_ = nullptr;
                continue;
            }
        }

        if (av_read_frame(format_ctx_, packet_) < 0) {
            avformat_close_input(&format_ctx_);
            format_ctx_ = nullptr;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }

        // Check JPEG SOI marker
        if (packet_->size < 2 || packet_->data[0] != 0xFF || packet_->data[1] != 0xD8) {
            logger_.error("Skipping non-JPEG packet (starts with 0x{:02x} 0x{:02x})",
                          packet_->data[0], packet_->data[1]);
            av_packet_unref(packet_);
            continue;
        }

        // Validate dimensions
        int width = remote_config_.aim().capture_size(), height = remote_config_.aim().capture_size();
        int subsamp;
        if (tjDecompressHeader2(tj_handle_, packet_->data, packet_->size, &width, &height, &subsamp) != 0) {
            logger_.error("TurboJPEG header decode failed for packet size {}: {}",
                          packet_->size, tjGetErrorStr());
            av_packet_unref(packet_);
            continue;
        }
        if (width != remote_config_.aim().capture_size() || height != remote_config_.aim().capture_size()) {
            logger_.error("Invalid dimensions: {}x{}", width, height);
            av_packet_unref(packet_);
            continue;
        }

        int pixel_format = TJPF_BGR;
        if (tjDecompress2(tj_handle_, packet_->data, packet_->size,
                          buffer.data(), width, 0, height, pixel_format, 0) != 0) {
            logger_.error("TurboJPEG decode failed: {}", tjGetErrorStr());
            av_packet_unref(packet_);
            continue;
        }

        av_packet_unref(packet_);

        cv::Mat decoded(remote_config_.aim().capture_size(), remote_config_.aim().capture_size(), CV_8UC3, buffer.data());
        auto frame = std::make_shared<Frame>(decoded.clone(), remote_config_.aim().capture_size(), remote_config_.aim().capture_size());
        if (frame->IsValid())
            frame_slot_->StoreFrame(frame);

        frame_count_++;
        auto now = std::chrono::steady_clock::now();
        auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_fps_time_).count();
        if (elapsed_ms >= 1000) {
            double fps = frame_count_ * 1000.0 / elapsed_ms;
            logger_.info("Grabber FPS: {}", frame_count_);
            frame_count_ = 0;
            last_fps_time_ = now;
        }
    }
}
