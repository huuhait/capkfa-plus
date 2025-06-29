#ifndef FRAME_H
#define FRAME_H

#include <stdexcept>
#include <opencv2/core.hpp>

class Frame {
public:
    Frame() : width_(0), height_(0) {}

    Frame(const cv::Mat& mat, int width, int height)
        : mat_(mat), width_(width), height_(height) {
        if (width <= 0 || height <= 0 || mat.empty()) {
            throw std::runtime_error("Invalid frame parameters");
        }
    }

    Frame(const cv::UMat& umat, int width, int height)
    : width_(width), height_(height) {
        umat.copyTo(mat_);  // convert to cv::Mat
    }

    int GetWidth() const { return width_; }
    int GetHeight() const { return height_; }

    bool IsValid() const {
        return !mat_.empty() && width_ > 0 && height_ > 0;
    }

    cv::Mat GetMat() const {
        return mat_;
    }

    cv::UMat ToUMat() const {
        cv::UMat umat;
        mat_.copyTo(umat);  // Upload to GPU on demand
        return umat;
    }
private:
    cv::Mat mat_;  // Store in CPU memory
    int width_, height_;
};

#endif