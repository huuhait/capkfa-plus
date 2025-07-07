#include <iostream>
#include <atomic>
#include <thread>
#include <chrono>
#include <csignal>
#include <mutex>

#include <opencv2/opencv.hpp>
#include <Processing.NDI.Lib.h>

std::atomic<bool> running(true);
std::atomic<int> frame_count(0);

std::mutex frame_mutex;
cv::Mat shared_frame;

void SignalHandler(int sig) {
    if (sig == SIGINT) running = false;
}

// Convert NDI UYVY frame to OpenCV BGR image
cv::Mat ConvertNDIFrameToMat(const NDIlib_video_frame_v2_t& frame) {
    // Convert UYVY to BGR
    cv::Mat uyvy(frame.yres, frame.xres, CV_8UC2, frame.p_data, frame.line_stride_in_bytes);
    cv::Mat bgr;
    cv::cvtColor(uyvy, bgr, cv::COLOR_YUV2BGR_UYVY);

    // Crop the center 256x256 region
    int cx = frame.xres / 2;
    int cy = frame.yres / 2;
    int half = 128;

    // Ensure cropping doesn't go out of bounds
    int x = std::max(0, cx - half);
    int y = std::max(0, cy - half);
    int w = std::min(256, frame.xres - x);
    int h = std::min(256, frame.yres - y);

    return bgr(cv::Rect(x, y, w, h)).clone();
}

// High-speed capture thread
void FrameGrabber(NDIlib_recv_instance_t receiver) {
    NDIlib_video_frame_v2_t frame;

    while (running) {
        auto type = NDIlib_recv_capture_v2(receiver, &frame, nullptr, nullptr, 0);
        if (type == NDIlib_frame_type_video) {
            frame_count++;

            cv::Mat mat = ConvertNDIFrameToMat(frame);
            {
                std::lock_guard<std::mutex> lock(frame_mutex);
                shared_frame = mat.clone();
            }

            NDIlib_recv_free_video_v2(receiver, &frame);
        }
    }
}

// OpenCV imshow thread
void DisplayThread() {
    while (running) {
        cv::Mat frame_copy;
        {
            std::lock_guard<std::mutex> lock(frame_mutex);
            if (!shared_frame.empty()) {
                frame_copy = shared_frame.clone();
            }
        }

        if (!frame_copy.empty()) {
            cv::imshow("NDI Stream", frame_copy);
        }

        if (cv::waitKey(1) == 27) { // ESC
            running = false;
            break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

int main() {
    std::signal(SIGINT, SignalHandler);

    if (!NDIlib_initialize()) {
        std::cerr << "NDI initialization failed.\n";
        return -1;
    }

    // Create source finder
    NDIlib_find_instance_t finder = NDIlib_find_create_v2(nullptr);
    if (!finder) {
        std::cerr << "NDI finder creation failed.\n";
        NDIlib_destroy();
        return -1;
    }

    const NDIlib_source_t* sources = nullptr;
    uint32_t count = 0;

    std::cout << "Discovering NDI sources...\n";
    while (running && count == 0) {
        NDIlib_find_wait_for_sources(finder, 1000);
        sources = NDIlib_find_get_current_sources(finder, &count);
    }

    std::cout << "\nFound NDI Sources (" << count << "):\n";
    for (uint32_t i = 0; i < count; ++i) {
        std::cout << "[" << i << "] "
                  << (sources[i].p_ndi_name ? sources[i].p_ndi_name : "Unknown")
                  << " @ "
                  << (sources[i].p_url_address ? sources[i].p_url_address : "Unknown IP")
                  << "\n";
    }

    if (count == 0) {
        std::cerr << "No sources found.\n";
        NDIlib_find_destroy(finder);
        NDIlib_destroy();
        return -1;
    }

    // Use first source (or change index here)
    NDIlib_source_t source = sources[0];
    std::cout << "\nConnecting to: " << source.p_ndi_name << " @ " << source.p_url_address << "\n";

    // Setup receiver
    NDIlib_recv_create_v3_t recv_desc = {};
    recv_desc.source_to_connect_to = source;
    recv_desc.color_format = NDIlib_recv_color_format_best;
    recv_desc.bandwidth = NDIlib_recv_bandwidth_highest;
    recv_desc.allow_video_fields = false;

    NDIlib_recv_instance_t receiver = NDIlib_recv_create_v3(&recv_desc);
    if (!receiver) {
        std::cerr << "Failed to connect to source.\n";
        NDIlib_find_destroy(finder);
        NDIlib_destroy();
        return -1;
    }

    std::thread grabber(FrameGrabber, receiver);
    std::thread display(DisplayThread);

    while (running) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        int fps = frame_count.exchange(0);
        std::cout << "FPS: " << fps << "\n";
    }

    grabber.join();
    display.join();

    NDIlib_recv_destroy(receiver);
    NDIlib_find_destroy(finder);
    NDIlib_destroy();
    return 0;
}
