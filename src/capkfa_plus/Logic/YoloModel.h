#ifndef YOLOMODEL_H
#define YOLOMODEL_H

#include <onnxruntime_cxx_api.h>
#include "../Frame/Frame.h"
#include <vector>
#include <mutex>
#include <spdlog/spdlog.h>

struct YoloConfig {
    static constexpr int INPUT_SIZE = 256;
    static constexpr float CONF_THRESHOLD = 0.1f;
    static constexpr float NMS_IOU_THRESHOLD = 0.6f;
};

struct Detection {
    float x1, y1, x2, y2;
    float confidence;
};

class YoloModel {
public:
    YoloModel(spdlog::logger& logger);
    ~YoloModel();

    std::vector<Detection> Predict(std::shared_ptr<Frame>& input);
private:
    void initializeSession();
    std::vector<Detection> extractDetections(const std::vector<float>& raw_float);
    std::vector<float> processOutputTensor(std::vector<Ort::Value>& output_tensors);
    Ort::Value createInputTensor(const cv::Mat& blob_fp16);
    cv::Mat preprocessFrame(std::shared_ptr<Frame>& frame);

    spdlog::logger& logger_;
    Ort::Env env_;
    Ort::Session session_{nullptr};
    Ort::MemoryInfo memory_info_{nullptr};
    Ort::RunOptions run_options_;
    cv::Mat rgb_buffer_;
    cv::Mat blob_buffer_;
    std::vector<Ort::Float16_t> input_buffer_;
    std::mutex predict_mutex_;
};

#endif
