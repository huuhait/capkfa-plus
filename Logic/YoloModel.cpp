#include "YoloModel.h"
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/dnn.hpp>
#include <onnxruntime_cxx_api.h>
#include <dml_provider_factory.h>
#include <iostream>
#include <filesystem>
#include <algorithm>
#include <cmath>
#include <numeric>
#include <absl/time/internal/cctz/include/cctz/time_zone.h>
#include <boost/date_time/posix_time/posix_time_duration.hpp>
#include <opencv2/imgcodecs.hpp>

inline float sigmoid(float x) {
    return 1.0f / (1.0f + std::exp(-x));
}

inline float clamp(float x, float lower, float upper) {
    return std::max(lower, std::min(x, upper));
}

void bgra2rgb_scalar(const uint8_t* src, uint8_t* dst, int width, int height) {
    for (int i = 0; i < width * height; i++) {
        dst[i * 3]     = src[i * 4 + 2]; // R
        dst[i * 3 + 1] = src[i * 4 + 1]; // G
        dst[i * 3 + 2] = src[i * 4 + 0]; // B
    }
}

YoloModel::YoloModel() {
    memory_info_ = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    run_options_ = Ort::RunOptions();
    rgb_buffer_ = cv::Mat(256, 256, CV_8UC3);
    input_buffer_.resize(3 * 256 * 256); // Preallocate in constructor
    initializeSession();
}

YoloModel::~YoloModel() {}

void YoloModel::initializeSession() {
    Ort::SessionOptions session_options;
    session_options.SetIntraOpNumThreads(std::thread::hardware_concurrency());
    session_options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);

    try {
        Ort::ThrowOnError(OrtSessionOptionsAppendExecutionProvider_DML(session_options, 0));
        const wchar_t* model_path = L"models/best.onnx";
        if (!std::filesystem::exists("models/best.onnx")) {
            std::cerr << "Model file not found: models/best.onnx" << std::endl;
            throw std::runtime_error("Model file missing");
        }
        session_ = Ort::Session(env_, model_path, session_options);
        std::cout << "Model loaded successfully" << std::endl;
    } catch (const Ort::Exception& e) {
        std::cerr << "ONNX initialization failed: " << e.what() << std::endl;
        throw std::runtime_error("YoloModel initialization failed");
    }
}

float IoU(const Detection& a, const Detection& b) {
    float xx1 = std::max(a.x1, b.x1);
    float yy1 = std::max(a.y1, b.y1);
    float xx2 = std::min(a.x2, b.x2);
    float yy2 = std::min(a.y2, b.y2);
    float w = std::max(0.0f, xx2 - xx1 + 1);
    float h = std::max(0.0f, yy2 - yy1 + 1);
    float inter = w * h;
    float area_a = (a.x2 - a.x1 + 1) * (a.y2 - a.y1 + 1);
    float area_b = (b.x2 - b.x1 + 1) * (b.y2 - b.y1 + 1);
    return inter / (area_a + area_b - inter);
}

std::vector<int> nms(const std::vector<Detection>& dets, float iou_threshold) {
    std::vector<int> indices(dets.size());
    std::iota(indices.begin(), indices.end(), 0);
    std::sort(indices.begin(), indices.end(), [&](int i, int j) {
        return dets[i].confidence > dets[j].confidence;
    });
    std::vector<int> keep;
    std::vector<bool> suppressed(dets.size(), false);
    for (size_t _i = 0; _i < indices.size(); ++_i) {
        int i = indices[_i];
        if (suppressed[i]) continue;
        keep.push_back(i);
        for (size_t _j = _i + 1; _j < indices.size(); ++_j) {
            int j = indices[_j];
            if (suppressed[j]) continue;
            if (IoU(dets[i], dets[j]) > iou_threshold)
                suppressed[j] = true;
        }
    }
    return keep;
}

std::vector<Detection> YoloModel::Predict(const Frame& frame) {
    // Step 1: Convert BGRA to RGB
    cv::Mat bgra = frame.ToMat();  // CV_8UC4
    cv::Mat rgb;
    cv::cvtColor(bgra, rgb, cv::COLOR_BGRA2RGB);  // CV_8UC3

    // Step 2: Create input blob
    cv::Mat blob;
    cv::dnn::blobFromImage(rgb, blob, 1.0 / 255.0, cv::Size(256, 256), cv::Scalar(), false, false);

    float* blob_data = blob.ptr<float>();
    Ort::Value input_tensor = Ort::Value::CreateTensor<float>(
        memory_info_,
        blob_data,
        blob.total(),
        std::array<int64_t, 4>{1, 3, 256, 256}.data(),
        4
    );

    // Step 3: Run inference
    const char* input_names[] = {"images"};
    const char* output_names[] = {"output0"};
    std::vector<Ort::Value> output_tensors;

    try {
        output_tensors = session_.Run(run_options_, input_names, &input_tensor, 1, output_names, 1);
    } catch (const Ort::Exception& e) {
        std::cerr << "Inference failed: " << e.what() << std::endl;
        return {};
    }

    float* raw = output_tensors[0].GetTensorMutableData<float>();
    auto shape = output_tensors[0].GetTensorTypeAndShapeInfo().GetShape();

    std::vector<Detection> detections;
    if (shape != std::vector<int64_t>{1, 5, 1344}) {
        std::cerr << "Unexpected output shape: ";
        for (auto s : shape) std::cerr << s << " ";
        std::cerr << std::endl;
        return {};
    }

    // Step 4: Process detections with strides and grid sizes
    const int anchor_counts[] = {384, 768, 192};
    int anchor_offset = 0;
    const float img_size = 256.0f;

    for (size_t s = 0; s < 3; ++s) {
        int num_anchors = anchor_counts[s];
        // Extract detections for current stride: [5, num_anchors]
        for (int i = 0; i < num_anchors; ++i) {
            int idx = anchor_offset + i;
            float obj_score = raw[4 * 1344 + idx]; // Confidence
            if (obj_score > 0.1f) {
                float x_center = raw[0 * 1344 + idx];
                float y_center = raw[1 * 1344 + idx];
                float w = raw[2 * 1344 + idx];
                float h = raw[3 * 1344 + idx];

                float x1 = std::max(0.0f, x_center - w / 2);
                float y1 = std::max(0.0f, y_center - h / 2);
                float x2 = std::min(img_size, x_center + w / 2);
                float y2 = std::min(img_size, y_center + h / 2);

                detections.push_back({x1, y1, x2, y2, obj_score});
            }
        }
        anchor_offset += num_anchors;
    }

    // Step 5: Apply NMS
    std::vector<int> keep = nms(detections, 0.6f);
    std::vector<Detection> final;
    for (int idx : keep) {
        final.push_back(detections[idx]);
    }

    return final;
}
