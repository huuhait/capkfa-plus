#include <Processing.NDI.Lib.h>
#include <opencv2/opencv.hpp>
#include <iostream>
#include <csignal>
#include <chrono>
#include <thread>
#include <mutex>
#ifdef _WIN32
#pragma comment(lib, "Processing.NDI.Lib.x64.lib")
#endif

static volatile std::sig_atomic_t g_exit = 0;
static std::mutex g_frame_mtx;
static cv::Mat g_latest_bgra;
static cv::Size g_frame_size;

void signal_handler(int) { g_exit = 1; }

void display_loop()
{
    cv::namedWindow("NDI Preview", cv::WINDOW_NORMAL);
    while (!g_exit) {
        cv::Mat local_frame;
        cv::Size current_size;
        {
            std::scoped_lock lk(g_frame_mtx);
            if (!g_latest_bgra.empty()) {
                g_latest_bgra.copyTo(local_frame);
                current_size = g_frame_size;
            }
        }

        if (!local_frame.empty()) {
            cv::resizeWindow("NDI Preview", current_size.width, current_size.height);
            cv::imshow("NDI Preview", local_frame);
        }

        int key = cv::waitKey(1) & 0xFF;
        if (key == 27 || key == 'q') g_exit = 1;
    }
    cv::destroyAllWindows();
}

int main()
{
    if (!NDIlib_initialize()) {
        std::cerr << "ERROR: This CPU is not supported by NDI." << std::endl;
        return EXIT_FAILURE;
    }
    std::signal(SIGINT, signal_handler);

    NDIlib_find_instance_t finder = NDIlib_find_create_v2(nullptr);
    if (!finder) {
        std::cerr << "ERROR: Cannot create NDI finder." << std::endl;
        return EXIT_FAILURE;
    }

    uint32_t source_count = 0;
    const NDIlib_source_t* sources = nullptr;
    std::cout << "Searching for NDI sources ..." << std::endl;
    while (!g_exit && source_count == 0) {
        NDIlib_find_wait_for_sources(finder, 1000);
        sources = NDIlib_find_get_current_sources(finder, &source_count);
    }
    if (g_exit || source_count == 0) {
        std::cerr << "No NDI sources found." << std::endl;
        NDIlib_find_destroy(finder);
        NDIlib_destroy();
        return EXIT_FAILURE;
    }

    std::cout << "Available NDI Sources:\n";
    for (uint32_t i = 0; i < source_count; ++i)
        std::cout << "  [" << i << "] " << sources[i].p_ndi_name << "\n";

    // Automatically select source with IP 192.168.44.121
    int sel = -1;
    std::string target_ip = "192.168.44.121";
    for (uint32_t i = 0; i < source_count; ++i) {
        std::string source_name(sources[i].p_ndi_name);
        std::string source_ip(sources[i].p_url_address ? sources[i].p_url_address : "");
        if (source_name.find(target_ip) != std::string::npos || source_ip.find(target_ip) != std::string::npos) {
            sel = i;
            break;
        }
    }

    if (sel == -1) {
        std::cerr << "ERROR: Source with IP 192.168.44.121 not found." << std::endl;
        NDIlib_find_destroy(finder);
        NDIlib_destroy();
        return EXIT_FAILURE;
    }

    const NDIlib_source_t* chosen = &sources[sel];
    std::cout << "Connecting to: " << chosen->p_ndi_name << " (" << (chosen->p_url_address ? chosen->p_url_address : "no IP") << ")" << std::endl;

    NDIlib_recv_create_v3_t recv_desc{};
    recv_desc.source_to_connect_to = *chosen;
    recv_desc.color_format = NDIlib_recv_color_format_BGRX_BGRA; // Native raw BGRA
    recv_desc.bandwidth = NDIlib_recv_bandwidth_highest;         // Uncompressed
    recv_desc.allow_video_fields = false;

    NDIlib_recv_instance_t receiver = NDIlib_recv_create_v3(&recv_desc);
    if (!receiver) {
        std::cerr << "ERROR: Unable to create NDI receiver." << std::endl;
        NDIlib_find_destroy(finder);
        NDIlib_destroy();
        return EXIT_FAILURE;
    }

    std::thread gui_thread(display_loop);

    using clock_t = std::chrono::steady_clock;
    auto last_stats = clock_t::now();
    int frames = 0;

    while (!g_exit) {
        NDIlib_video_frame_v2_t vframe{};
        if (NDIlib_recv_capture_v3(receiver, &vframe, nullptr, nullptr, 100) == NDIlib_frame_type_video) {
            {
                std::scoped_lock lk(g_frame_mtx);
                cv::Mat frame(vframe.yres, vframe.xres, CV_8UC4, vframe.p_data, vframe.line_stride_in_bytes);
                // Calculate center crop (256x256)
                int crop_size = 256;
                int x = (vframe.xres - crop_size) / 2;
                int y = (vframe.yres - crop_size) / 2;
                if (x >= 0 && y >= 0 && x + crop_size <= vframe.xres && y + crop_size <= vframe.yres) {
                    cv::Rect roi(x, y, crop_size, crop_size);
                    g_latest_bgra = frame(roi).clone();
                    g_frame_size = cv::Size(crop_size, crop_size);
                } else {
                    g_latest_bgra = frame.clone(); // Fallback if crop invalid
                    g_frame_size = cv::Size(vframe.xres, vframe.yres);
                }
            }
            NDIlib_recv_free_video_v2(receiver, &vframe);
            frames++;
        }

        auto now = clock_t::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_stats).count();
        if (ms >= 1000) {
            float fps = frames * 1000.0f / ms;
            std::cout << "FPS: " << fps << std::endl;
            frames = 0;
            last_stats = now;
        }
    }

    if (gui_thread.joinable()) gui_thread.join();
    NDIlib_recv_destroy(receiver);
    NDIlib_find_destroy(finder);
    NDIlib_destroy();
    return 0;
}