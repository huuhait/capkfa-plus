#ifndef YOLOMODEL_H
#define YOLOMODEL_H

#include <onnxruntime_cxx_api.h>
#include "../Frame/Frame.h"
#include <vector>

struct Detection {
    float  x1, y1, x2, y2;
    float confidence;
};

class YoloModel {
public:
    YoloModel();
    ~YoloModel();

    std::vector<Detection> Predict(const Frame& input);

private:
    void initializeSession();

    Ort::Env env_{ORT_LOGGING_LEVEL_WARNING, "Yolo"};
    Ort::Session session_{nullptr};
    Ort::RunOptions run_options_;
    Ort::MemoryInfo memory_info_{nullptr};

    cv::Mat rgb_buffer_;
    std::vector<float> input_buffer_; // Added for input tensor
};

#endif