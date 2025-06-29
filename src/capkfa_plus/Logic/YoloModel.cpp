#define NOMINMAX
#include "YoloModel.h"
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/dnn.hpp>
#include <onnxruntime_cxx_api.h>
#include <dml_provider_factory.h>
#include <string>
#include <chrono>
#include <thread>
#include <algorithm>
#include <numeric>
#include <filesystem>
#include <vector>
#include <iostream>

using namespace std;

YoloModel::YoloModel(spdlog::logger& logger): logger_(logger) {
    memory_info_ = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    run_options_ = Ort::RunOptions();
    input_buffer_.resize(1 * 3 * 256 * 256);
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
            throw std::runtime_error("Model file missing");
        }
        session_ = Ort::Session(env_, model_path, session_options);
    } catch (const Ort::Exception& e) {
        throw std::runtime_error("YoloModel initialization failed: " + std::string(e.what()));
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
    // Convert detections to OpenCV format for GPU-accelerated NMS
    std::vector<cv::Rect> boxes;
    std::vector<float> scores;
    boxes.reserve(dets.size());
    scores.reserve(dets.size());
    for (const auto& d : dets) {
        boxes.emplace_back(cv::Rect(static_cast<int>(d.x1), static_cast<int>(d.y1),
                                   static_cast<int>(d.x2 - d.x1), static_cast<int>(d.y2 - d.y1)));
        scores.push_back(d.confidence);
    }

    std::vector<int> indices;
    cv::dnn::NMSBoxes(boxes, scores, 0.1f, iou_threshold, indices);
    return indices;
}

cv::Mat YoloModel::preprocessFrame(std::shared_ptr<Frame>& frame) {
    cv::Mat rgb = frame->GetMat(); // CPU-based, as in original
    if (rgb.empty()) {
        return cv::Mat();
    }
    cv::Mat blob;
    // Use CV_32F for compatibility, optimized with parallel backend
    cv::dnn::blobFromImage(rgb, blob, 1.0 / 255.0, cv::Size(256, 256), cv::Scalar(), false, false, CV_32F);
    cv::Mat blob_fp16;
    blob.convertTo(blob_fp16, CV_16F); // Convert to FP16 for model
    return blob_fp16;
}

Ort::Value YoloModel::createInputTensor(const cv::Mat& blob_fp16) {
    if (blob_fp16.total() != 1 * 3 * 256 * 256) {
        return Ort::Value(nullptr);
    }
    // Copy directly to input_buffer_
    std::memcpy(input_buffer_.data(), blob_fp16.data, 1 * 3 * 256 * 256 * sizeof(Ort::Float16_t));
    return Ort::Value::CreateTensor<Ort::Float16_t>(
        memory_info_,
        input_buffer_.data(),
        input_buffer_.size(),
        std::array<int64_t, 4>{1, 3, 256, 256}.data(),
        4
    );
}

std::vector<float> YoloModel::processOutputTensor(std::vector<Ort::Value>& output_tensors) {
    auto tensor_info = output_tensors[0].GetTensorTypeAndShapeInfo();
    auto shape = tensor_info.GetShape();
    if (shape != std::vector<int64_t>{1, 5, 1344}) {
        return {};
    }
    std::vector<float> raw_float(5 * 1344);
    if (tensor_info.GetElementType() == ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT16) {
        Ort::Float16_t* raw = output_tensors[0].GetTensorMutableData<Ort::Float16_t>();
        #pragma omp parallel for
        for (size_t i = 0; i < 5 * 1344; ++i) {
            raw_float[i] = static_cast<float>(raw[i]);
        }
    } else {
        float* raw = output_tensors[0].GetTensorMutableData<float>();
        std::copy(raw, raw + 5 * 1344, raw_float.begin());
    }
    return raw_float;
}

std::vector<Detection> YoloModel::extractDetections(const std::vector<float>& raw_float) {
    std::vector<Detection> detections;
    detections.reserve(1344);
    const int anchor_counts[] = {384, 768, 192};
    int anchor_offset = 0;
    const float img_size = 256.0f;

    for (size_t s = 0; s < 3; ++s) {
        int num_anchors = anchor_counts[s];
        #pragma omp parallel for
        for (int i = 0; i < num_anchors; ++i) {
            int idx = anchor_offset + i;
            float obj_score = raw_float[4 * 1344 + idx];
            if (std::isfinite(obj_score) && obj_score > 0.1f) {
                float x_center = raw_float[0 * 1344 + idx];
                float y_center = raw_float[1 * 1344 + idx];
                float w = raw_float[2 * 1344 + idx];
                float h = raw_float[3 * 1344 + idx];
                if (std::isfinite(x_center) && std::isfinite(y_center) && std::isfinite(w) && std::isfinite(h) && w > 0 && h > 0) {
                    float x1 = std::max(0.0f, x_center - w / 2);
                    float y1 = std::max(0.0f, y_center - h / 2);
                    float x2 = std::min(img_size - 1, x_center + w / 2);
                    float y2 = std::min(img_size - 1, y_center + h / 2);
                    if (x2 > x1 && y2 > y1 && x1 >= 0 && y1 >= 0 && x2 < img_size && y2 < img_size) {
                        #pragma omp critical
                        detections.push_back({x1, y1, x2, y2, obj_score});
                    }
                }
            }
        }
        anchor_offset += num_anchors;
    }
    return detections;
}

std::vector<Detection> YoloModel::Predict(std::shared_ptr<Frame>& frame) {
    using namespace std::chrono;
    auto t_start = high_resolution_clock::now();

    auto record_time = [&](const std::string& step, const auto& start) {
        auto end = high_resolution_clock::now();
        auto duration = duration_cast<microseconds>(end - start).count() / 1000.0;
        return end;
    };

    cv::Mat blob_fp16 = preprocessFrame(frame);
    auto t1 = record_time("Preprocess Frame", t_start);
    if (blob_fp16.empty()) {
        return {};
    }

    Ort::Value input_tensor = createInputTensor(blob_fp16);
    auto t2 = record_time("Create Input Tensor", t1);
    if (!input_tensor) {
        return {};
    }

    const char* input_names[] = {"images"};
    const char* output_names[] = {"output0"};
    std::vector<Ort::Value> output_tensors;
    try {
        output_tensors = session_.Run(run_options_, input_names, &input_tensor, 1, output_names, 1);
    } catch (const Ort::Exception&) {
        return {};
    }
    auto t3 = record_time("Run Inference", t2);

    std::vector<float> raw_float = processOutputTensor(output_tensors);
    auto t4 = record_time("Process Output Tensor", t3);
    if (raw_float.empty()) {
        return {};
    }

    std::vector<Detection> detections = extractDetections(raw_float);
    auto t5 = record_time("Extract Detections", t4);

    std::vector<int> keep = nms(detections, 0.6f);
    auto t6 = record_time("Apply NMS", t5);

    std::vector<Detection> final;
    final.reserve(keep.size());
    for (int idx : keep) {
        final.push_back(detections[idx]);
    }

    auto total_duration = duration_cast<microseconds>(high_resolution_clock::now() - t_start).count() / 1000.0;
    /*logger_.debug("YoloModel Predict: Preprocess: {:.2f}ms, Input Tensor: {:.2f}ms, Inference: {:.2f}ms, "
                  "Process Output: {:.2f}ms, Extract Detections: {:.2f}ms, NMS: {:.2f}ms, Total: {:.2f}ms",
                  t1.time_since_epoch().count() / 1000.0,
                  t2.time_since_epoch().count() / 1000.0,
                  t3.time_since_epoch().count() / 1000.0,
                  t4.time_since_epoch().count() / 1000.0,
                  t5.time_since_epoch().count() / 1000.0,
                  t6.time_since_epoch().count() / 1000.0,
                  total_duration);*/
    return final;
}