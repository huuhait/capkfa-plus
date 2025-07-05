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

YoloModel::YoloModel(spdlog::logger& logger) : logger_(logger) {
    memory_info_ = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    run_options_ = Ort::RunOptions();
    input_buffer_.resize(1 * 3 * 256 * 256);
    xyxy_buffer_.resize(YoloConfig::TOP_K * 4);
    scores_buffer_.resize(YoloConfig::TOP_K);
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

float YoloModel::sigmoid(float x) const {
    return 1.0f / (1.0f + std::exp(-x));
}

float IoU(const Detection& a, const Detection& b) {
    float xx1 = std::max(a.x1, b.x1);
    float yy1 = std::max(a.y1, b.y1);
    float xx2 = std::min(a.x2, b.x2);
    float yy2 = std::min(a.y2, b.y2);
    float w = std::max(0.0f, xx2 - xx1);
    float h = std::max(0.0f, yy2 - yy1);
    float inter = w * h;
    float area_a = (a.x2 - a.x1) * (a.y2 - a.y1);
    float area_b = (b.x2 - b.x1) * (b.y2 - b.y1);
    return inter / (area_a + area_b - inter + 1e-16f);
}

std::vector<int> nms(const std::vector<Detection>& dets, float iou_threshold) {
    std::vector<int> indices(dets.size());
    std::iota(indices.begin(), indices.end(), 0);
    std::sort(indices.begin(), indices.end(), [&dets](int a, int b) {
        return dets[a].confidence > dets[b].confidence;
    });

    std::vector<int> keep;
    while (!indices.empty()) {
        int i = indices[0];
        keep.push_back(i);
        std::vector<int> remaining;
        for (size_t j = 1; j < indices.size(); ++j) {
            if (IoU(dets[i], dets[indices[j]]) < iou_threshold) {
                remaining.push_back(indices[j]);
            }
        }
        indices = remaining;
    }
    return keep;
}

cv::Mat YoloModel::preprocessFrame(std::shared_ptr<Frame>& frame) {
    cv::Mat rgb = frame->GetMat();
    if (rgb.empty()) {
        return cv::Mat();
    }
    cv::Mat blob;
    cv::cvtColor(rgb, rgb_buffer_, cv::COLOR_BGR2RGB);
    cv::dnn::blobFromImage(rgb_buffer_, blob, 1.0 / 255.0, cv::Size(YoloConfig::INPUT_SIZE, YoloConfig::INPUT_SIZE), cv::Scalar(), false, false, CV_32F);
    cv::Mat blob_fp16;
    blob.convertTo(blob_fp16, CV_16F);
    return blob_fp16;
}

Ort::Value YoloModel::createInputTensor(const cv::Mat& blob_fp16) {
    if (blob_fp16.total() != 1 * 3 * 256 * 256) {
        return Ort::Value(nullptr);
    }
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
    if (shape != std::vector<int64_t>{1, 6, 1344}) {
        return {};
    }
    std::vector<float> raw_float(6 * 1344);
    if (tensor_info.GetElementType() == ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT16) {
        Ort::Float16_t* raw = output_tensors[0].GetTensorMutableData<Ort::Float16_t>();
        #pragma omp parallel for
        for (size_t i = 0; i < 6 * 1344; ++i) {
            raw_float[i] = static_cast<float>(raw[i]);
        }
    } else {
        float* raw = output_tensors[0].GetTensorMutableData<float>();
        std::copy(raw, raw + 6 * 1344, raw_float.begin());
    }
    return raw_float;
}

std::vector<Detection> YoloModel::extractDetections(const std::vector<float>& raw_float) {
    std::vector<Detection> detections;
    detections.reserve(1344);
    const float img_size = YoloConfig::INPUT_SIZE;

    for (size_t i = 0; i < 1344; ++i) {
        float w = raw_float[2 * 1344 + i];
        float h = raw_float[3 * 1344 + i];
        float obj_score = sigmoid(raw_float[4 * 1344 + i]);
        float cls_score = sigmoid(raw_float[5 * 1344 + i]);
        float confidence = obj_score * cls_score;

        // Size and aspect ratio filtering
        if (w <= 4 || h <= 4 || w >= img_size * 0.9 || h >= img_size * 0.9) continue;
        float aspect_ratio = w / (h + 1e-6f);
        if (aspect_ratio <= 0.2f || aspect_ratio >= 5.0f) continue;

        // Confidence filtering
        if (confidence <= YoloConfig::CONF_THRESHOLD || obj_score <= YoloConfig::MIN_OBJ) continue;

        float x_center = raw_float[0 * 1344 + i];
        float y_center = raw_float[1 * 1344 + i];
        float x1 = x_center - w / 2;
        float y1 = y_center - h / 2;
        float x2 = x_center + w / 2;
        float y2 = y_center + h / 2;

        x1 = std::max(0.0f, std::min(x1, img_size - 1));
        y1 = std::max(0.0f, std::min(y1, img_size - 1));
        x2 = std::max(0.0f, std::min(x2, img_size - 1));
        y2 = std::max(0.0f, std::min(y2, img_size - 1));

        int class_id = cls_score > 0.5f ? 1 : 0;
        detections.push_back({x1, y1, x2, y2, confidence, obj_score, cls_score, class_id});
    }

    // Top-K filtering
    if (detections.size() > YoloConfig::TOP_K) {
        std::vector<size_t> indices(detections.size());
        std::iota(indices.begin(), indices.end(), 0);
        std::partial_sort(indices.begin(), indices.begin() + YoloConfig::TOP_K, indices.end(),
            [&detections](size_t a, size_t b) { return detections[a].confidence > detections[b].confidence; });

        std::vector<Detection> top_k_detections;
        top_k_detections.reserve(YoloConfig::TOP_K);
        for (size_t i = 0; i < YoloConfig::TOP_K; ++i) {
            top_k_detections.push_back(detections[indices[i]]);
        }
        detections = std::move(top_k_detections);
    }

    return detections;
}

std::vector<Detection> YoloModel::Predict(std::shared_ptr<Frame>& frame) {
    using namespace std::chrono;
    auto t_start = high_resolution_clock::now();
    std::vector<double> durations;

    auto record_time = [&](const std::string& step, const auto& start) {
        auto end = high_resolution_clock::now();
        durations.push_back(duration_cast<microseconds>(end - start).count() / 1000.0);
        return end;
    };

    cv::Mat blob_fp16 = preprocessFrame(frame);
    auto t1 = record_time("Preprocess Frame", t_start);
    if (blob_fp16.empty()) return {};

    Ort::Value input_tensor = createInputTensor(blob_fp16);
    auto t2 = record_time("Create Input Tensor", t1);
    if (!input_tensor) return {};

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
    if (raw_float.empty()) return {};

    std::vector<Detection> detections = extractDetections(raw_float);
    auto t5 = record_time("Extract Detections", t4);
    if (detections.empty()) return {};

    std::vector<int> keep = nms(detections, YoloConfig::NMS_IOU_THRESHOLD);
    auto t6 = record_time("Apply NMS", t5);

    std::vector<Detection> final;
    final.reserve(keep.size());
    for (int idx : keep) final.push_back(detections[idx]);

    auto total_duration = duration_cast<microseconds>(high_resolution_clock::now() - t_start).count() / 1000.0;
    // logger_.debug("YoloModel Predict: Preprocess: {:.2f}ms, Input Tensor: {:.2f}ms, Inference: {:.2f}ms, Process Output: {:.2f}ms, Extract Detections: {:.2f}ms, NMS: {:.2f}ms, Total: {:.2f}ms",
    //               durations[0], durations[1], durations[2], durations[3], durations[4], durations[5], total_duration);

    return final;
}
